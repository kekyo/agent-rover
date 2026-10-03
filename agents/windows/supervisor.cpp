// agent-rover - Independent host supervision
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "operation_worker.h"
#include "supervisor.h"
#include "supervisor_limits.h"
#include "recovery_registry.h"
#include "file_logger.h"
#include "agent_log.h"
#include <chrono>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace agent_rover {

struct HostConfiguration {
  uint64_t max_transfer_bytes;
  HANDLE mapping, pulse;
  uint32_t generation, host_bytes, token_bytes;
  uint16_t port, auth_required;
};
static HANDLE host_pulse = nullptr;
static std::runtime_error Failure(const char* operation) {
  return std::runtime_error(std::string(operation) + " Win32=" + std::to_string(GetLastError()));
}

void PulseSupervisor(bool healthy) { if (healthy && host_pulse) SetEvent(host_pulse); }

int RunSupervisedServer() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  int result = 1;
  try {
    WorkerMessage command;
    if (!ReadWorkerCommand(&command) || command.kind != WorkerMessageKind::HostConfiguration ||
        command.payload.size() < sizeof(HostConfiguration)) return 1;
    HostConfiguration config = {};
    std::memcpy(&config, command.payload.data(), sizeof(config));
    if (config.host_bytes > 32768 || config.token_bytes > 32768 ||
        command.payload.size() != sizeof(config) + config.host_bytes + config.token_bytes ||
        !config.generation || !config.max_transfer_bytes || config.max_transfer_bytes % kBytesPerMiB) return 1;
    host_pulse = config.pulse;
    auto registry = AttachRecoveryRegistry(config.mapping, config.generation);
    SetHostRecoveryRegistry(registry);
    SetHelperBreakaway(true);
    const auto begin = command.payload.begin() + sizeof(config);
    ServerOptions options = {std::string(begin, begin + config.host_bytes), config.port,
        config.auth_required != 0, std::string(begin + config.host_bytes, command.payload.end()), config.max_transfer_bytes};
    std::string error;
    result = RunTcpServer(options, &error);
    SetHostRecoveryRegistry({});
  } catch (...) { SetHostRecoveryRegistry({}); }
  if (host_pulse) CloseHandle(host_pulse);
  host_pulse = nullptr;
  return result;
}

struct HostWatch {
  cardio::cancellation_source ended;
  bool exited = false, expired = false;
  DWORD exit_code = 1;
};

static cardio::promise<void> WatchExit(Worker host, std::shared_ptr<HostWatch> watch) {
  try {
    co_await cardio::from_win32_handle(WorkerProcess(host), watch->ended.get_cancellation());
    if (!GetExitCodeProcess(WorkerProcess(host), &watch->exit_code)) throw Failure("GetExitCodeProcess(host)");
    watch->exited = true;
  } catch (const cardio::canceled_exception&) {}
  catch (const std::exception& error) { PrintAgentLogEvent(std::string("phase=supervisor-wait-failed reason=") + error.what()); }
  watch->ended.cancel();
}

static cardio::promise<void> WatchProgress(HANDLE pulse, std::shared_ptr<HostWatch> watch) {
  uint32_t allowance = 30000; // Startup includes GUI, logger and capability initialization.
  while (!watch->ended.get_cancellation().is_cancellation_requested()) {
    auto deadline = cardio::cancellations::timeout(allowance);
    auto stop = cardio::cancellations::any(deadline.get_cancellation(), watch->ended.get_cancellation());
    try { co_await cardio::from_win32_handle(pulse, stop.get_cancellation()); }
    catch (const cardio::canceled_exception&) {
      if (!watch->exited) watch->expired = true;
      break;
    }
    allowance = 15000;
  }
}

static cardio::promise<void> Supervise(ServerOptions options, int* result, std::string* error) {
  const auto registry = CreateRecoveryRegistry();
  const auto pulse = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!pulse) throw Failure("CreateEventW(supervisor)");
  auto logger = CreateFileLogger([](const std::string&) {});
  SetAgentLogSink([logger](const AgentLogRecord& record) { EnqueueFileLog(logger, record); });
  cardio::cancellation_source recovery_stop;
  auto recovering = RecoverRetiredRoots(registry, recovery_stop.get_cancellation());
  using Clock = std::chrono::steady_clock;
  std::deque<uint64_t> starts;
  uint32_t generation = 0;
  Worker host;
  try {
    for (;;) {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
      const auto delay = ReserveSupervisorStart(&starts, now);
      if (delay) {
        PrintAgentLogEvent("phase=supervisor-backoff delayMs=" + std::to_string(delay));
        co_await cardio::promises::delay(delay);
        continue;
      }
      if (++generation == 0) throw std::runtime_error("Supervisor generation exhausted.");
      ResetEvent(pulse);
      std::string failure;
      auto watch = std::make_shared<HostWatch>();
      try {
        host = StartOperationWorker(HelperRole::Server);
        HostConfiguration config = {};
        config.generation = generation;
        config.host_bytes = options.host.size(); config.token_bytes = options.auth_token.size();
        config.port = options.port; config.auth_required = options.auth_required;
        config.max_transfer_bytes = options.max_transfer_bytes;
        if (!DuplicateHandle(GetCurrentProcess(), RecoveryMapping(registry), WorkerProcess(host),
            &config.mapping, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
            !DuplicateHandle(GetCurrentProcess(), pulse, WorkerProcess(host),
            &config.pulse, EVENT_MODIFY_STATE, FALSE, 0)) throw Failure("DuplicateHandle(supervisor configuration)");
        WorkerMessage command = {WorkerMessageKind::HostConfiguration, std::vector<unsigned char>(sizeof(config))};
        std::memcpy(command.payload.data(), &config, sizeof(config));
        command.payload.insert(command.payload.end(), options.host.begin(), options.host.end());
        command.payload.insert(command.payload.end(), options.auth_token.begin(), options.auth_token.end());
        auto initialization = cardio::cancellations::timeout(10000);
        co_await WriteWorker(host, std::move(command), initialization.get_cancellation());
        PrintAgentLogEvent("phase=supervisor-start generation=" + std::to_string(generation));
        auto exited = WatchExit(host, watch);
        co_await WatchProgress(pulse, watch);
        watch->ended.cancel();
        co_await exited;
        failure = watch->expired ? "progress deadline exceeded" : "host exit code=" + std::to_string(watch->exit_code);
      } catch (const std::exception& exception) { failure = exception.what(); }
      const bool requested_exit = SupervisorExitRequested(registry, generation);
      // Never place a replacement alongside an unconfirmed old instance.
      while (!co_await StopOperationWorker(host)) {
        PrintAgentLogEvent("phase=supervisor-stop-pending generation=" + std::to_string(generation));
        co_await cardio::promises::delay(1000);
      }
      host.reset(); // Closing the host Job also releases its owned helper Jobs.
      while (!co_await StopOrphanWorkers(registry, generation)) {
        PrintAgentLogEvent("phase=supervisor-orphan-stop-pending generation=" + std::to_string(generation));
        co_await cardio::promises::delay(1000);
      }
      RetireRecoveryGeneration(registry, generation);
      if (requested_exit || (watch->exited && watch->exit_code == 0)) {
        PrintAgentLogEvent("phase=supervisor-exit reason=normal shutdown graceful=" +
            std::to_string(watch->exited && watch->exit_code == 0));
        *result = 0;
        break;
      }
      PrintAgentLogEvent("phase=supervisor-restart generation=" + std::to_string(generation) + " reason=" + SanitizeAgentLogField(failure));
      co_await cardio::promises::delay(1000);
    }
  } catch (const std::exception& exception) { *error = exception.what(); *result = 1; }
  if (host) co_await StopOperationWorker(host);
  recovery_stop.cancel();
  co_await recovering;
  ReportPendingRecovery(registry);
  co_await StopFileLogger(logger);
  SetAgentLogSink({});
  CloseHandle(pulse);
}

static cardio::promise<void> ObserveSupervision(ServerOptions options, int* result, std::string* error) {
  try { co_await Supervise(std::move(options), result, error); }
  catch (const std::exception& exception) { *result = 1; *error = exception.what(); }
}

int RunSupervisor(const ServerOptions& options, std::string* error) {
  int result = 1;
  try {
    cardio::dispatcher_host_win32_auto dispatcher;
    auto supervising = ObserveSupervision(options, &result, error);
    dispatcher.park();
  } catch (const std::exception& exception) { *error = exception.what(); }
  return result;
}
}
