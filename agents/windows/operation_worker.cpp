// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "operation_worker.h"
#include "agent_log.h"
#include "command_line.h"
#include "json_protocol.h"
#include "win32_util.h"
#include "win32_video.h"
#include "win32_cleanup.h"
#include "managed_launch.h"
#include "recovery_registry.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace agent_rover {

struct OperationWorker {
  HANDLE input = INVALID_HANDLE_VALUE;
  HANDLE output = INVALID_HANDLE_VALUE;
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  HelperRole role = HelperRole::Probe;
  std::vector<unsigned char> capture_root;
  Worker recovery;
  bool resources_recovered = false;
  std::map<uint32_t, Worker> applications;
  uint32_t next_application = 0;
  bool captures_output = false;
  uint32_t recovery_slot = kNoRecoverySlot, process_slot = kNoRecoverySlot;
  ~OperationWorker() {
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (process) CloseHandle(process);
    if (job) CloseHandle(job);
  }
};
static bool helper_breakaway = false;
void SetHelperBreakaway(bool enabled) { helper_breakaway = enabled; }
HANDLE WorkerProcess(const Worker& worker) { return worker->process; }
bool WorkerHasCaptureRoot(const Worker& worker) { return !worker->capture_root.empty(); }
Worker AdoptRecoveryRoot(const std::vector<unsigned char>& ownership) {
  auto worker = std::make_shared<OperationWorker>();
  worker->role = HelperRole::Operations;
  worker->capture_root = ownership;
  return worker;
}

static std::runtime_error NativeFailure(const char* operation) {
  const DWORD code = GetLastError();
  return std::runtime_error(std::string(operation) + " Win32=" + std::to_string(code));
}

static HANDLE CreateWorkerPipe(HANDLE* child, bool parent_reads) {
  static uint64_t next_pipe = 0;
  const auto name = L"\\\\.\\pipe\\agent-rover-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(++next_pipe);
  const auto parent = CreateNamedPipeW(name.c_str(),
      (parent_reads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
  if (parent == INVALID_HANDLE_VALUE) throw NativeFailure("CreateNamedPipeW");
  SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, TRUE};
  *child = CreateFileW(name.c_str(), parent_reads ? GENERIC_WRITE : GENERIC_READ,
      0, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (*child == INVALID_HANDLE_VALUE) { const auto error = NativeFailure("CreateFileW(pipe)"); CloseHandle(parent); throw error; }
  // Opening the client in this process has already connected this unique pipe.
  return parent;
}

Worker StartOperationWorker(HelperRole role, uint64_t max_transfer_bytes) {
  auto worker = std::make_shared<OperationWorker>();
  worker->role = role;
  HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
  try {
    worker->job = CreateJobObjectW(nullptr, nullptr);
    if (!worker->job) throw NativeFailure("CreateJobObjectW(worker)");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    if (role != HelperRole::PersistentLaunch)
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (role == HelperRole::Operations || role == HelperRole::Server) limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    // Opening the log folder must not tie a new Explorer process to the logger.
    if (role == HelperRole::FileLogger) limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (!SetInformationJobObject(worker->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
      throw NativeFailure("SetInformationJobObject(worker)");
    worker->input = CreateWorkerPipe(&input, false);
    worker->output = CreateWorkerPipe(&output, true);
    wchar_t path[32768] = {};
    if (!GetModuleFileNameW(nullptr, path, 32768)) throw NativeFailure("GetModuleFileNameW");
    const auto mode = role == HelperRole::Probe ? L"--agent-probe" :
        role == HelperRole::Server ? L"--agent-server" :
        role == HelperRole::FileLogger ? L"--agent-log-worker" :
        (role == HelperRole::Launch || role == HelperRole::PersistentLaunch) ? L"--agent-launch-worker" :
        role == HelperRole::Cleanup ? L"--agent-cleanup-worker" : L"--agent-worker";
    std::vector<std::wstring> arguments = {mode};
    if (role == HelperRole::Operations) {
      arguments.insert(arguments.end(), {L"--max-transfer-size",
          std::to_wstring(max_transfer_bytes / kBytesPerMiB)});
    }
    auto command = BuildCommandLine(path, arguments);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(path, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | (helper_breakaway ? CREATE_BREAKAWAY_FROM_JOB : 0),
        nullptr, nullptr, &startup, &process)) throw NativeFailure("CreateProcessW(worker)");
    worker->process = process.hProcess;
    CloseHandle(process.hThread);
    // Helpers wait for their first command before launching user code. A parent
    // death before assignment closes the pipe; no suspended orphan is created.
    if (!AssignProcessToJobObject(worker->job, worker->process)) {
      const auto error = NativeFailure("AssignProcessToJobObject(worker)");
      TerminateProcess(worker->process, ERROR_PROCESS_ABORTED);
      throw error;
    }
    if (role == HelperRole::Operations) worker->process_slot = RegisterRecoveryWorker(worker->process);
  } catch (...) {
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    throw;
  }
  CloseHandle(input);
  CloseHandle(output);
  return worker;
}

static cardio::promise<void> ReadPipe(HANDLE pipe, std::span<unsigned char> bytes,
    cardio::cancellation cancellation) {
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::read(pipe, std::as_writable_bytes(bytes), cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
}

cardio::promise<void> WriteWorker(Worker worker, WorkerMessage message, cardio::cancellation cancellation) {
  if (message.payload.size() > kMaxJsonPayloadBytes) throw std::runtime_error("Worker message limit exceeded.");
  uint32_t header[2] = {static_cast<uint32_t>(message.kind), static_cast<uint32_t>(message.payload.size())};
  std::span<const std::byte> bytes = std::as_bytes(std::span(header));
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::write(worker->input, bytes, cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
  bytes = std::as_bytes(std::span(message.payload));
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::write(worker->input, bytes, cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
}

cardio::promise<WorkerMessage> ReadWorker(Worker worker, cardio::cancellation cancellation) {
  uint32_t header[2] = {};
  co_await ReadPipe(worker->output, {reinterpret_cast<unsigned char*>(header), sizeof(header)}, cancellation);
  if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker message limit exceeded.");
  WorkerMessage message = {static_cast<WorkerMessageKind>(header[0]), std::vector<unsigned char>(header[1])};
  co_await ReadPipe(worker->output, message.payload, cancellation);
  if (worker->role == HelperRole::Operations && message.kind == WorkerMessageKind::CaptureRoot) {
    if (!worker->capture_root.empty() || message.payload.size() <= sizeof(CaptureIdentity) || message.payload.size() > 32768)
      throw std::runtime_error("Invalid capture ownership message.");
    worker->capture_root = message.payload;
  }
  co_return message;
}

cardio::promise<void> InitializeOperationWorker(Worker worker) {
  auto deadline = cardio::cancellations::timeout(3000);
  const auto ready = co_await ReadWorker(worker, deadline.get_cancellation());
  if (ready.kind != WorkerMessageKind::Complete) throw std::runtime_error("Missing worker readiness message.");
}

cardio::promise<bool> HandleWorkerOwnership(Worker worker, WorkerMessage message, cardio::cancellation cancellation) {
  if (message.kind == WorkerMessageKind::ReserveCapture || message.kind == WorkerMessageKind::CaptureRoot) {
    if (message.kind == WorkerMessageKind::ReserveCapture) {
      if (worker->recovery_slot != kNoRecoverySlot) throw std::runtime_error("Duplicate capture reservation.");
      worker->recovery_slot = ReserveRecoveryRoot(worker->process);
    } else CommitRecoveryRoot(worker->recovery_slot, worker->capture_root);
    WorkerMessage reply = {WorkerMessageKind::Complete, {}};
    co_await WriteWorker(worker, std::move(reply), cancellation);
    co_return true;
  }
  if (message.kind == WorkerMessageKind::ReleaseLaunch) {
    if (message.payload.size() != sizeof(uint32_t)) throw std::runtime_error("Invalid launch release.");
    uint32_t id = 0; std::memcpy(&id, message.payload.data(), sizeof(id));
    if (!worker->applications.erase(id)) throw std::runtime_error("Unknown launch owner.");
    WorkerMessage reply = {WorkerMessageKind::Complete, {}};
    co_await WriteWorker(worker, std::move(reply), cancellation);
    co_return true;
  }
  if (message.kind != WorkerMessageKind::Launch) co_return false;
  if (worker->applications.size() >= 64) throw std::runtime_error("Owned launch limit reached.");
  const auto options = DecodeManagedLaunch(message.payload);
  const auto id = ++worker->next_application;
  if (!id) throw std::runtime_error("Owned launch identifier exhausted.");
  auto launcher = StartOperationWorker(options.kill_tree_on_release ? HelperRole::Launch : HelperRole::PersistentLaunch);
  launcher->captures_output = !options.launch.stdout_path.empty() || !options.launch.stderr_path.empty();
  worker->applications.emplace(id, launcher);
  co_await WriteWorker(launcher, std::move(message), cancellation);
  auto reply = co_await ReadWorker(launcher, cancellation);
  if (reply.kind == WorkerMessageKind::Launch) {
    ManagedProcessHandles handles;
    if (reply.payload.size() != sizeof(handles)) throw std::runtime_error("Invalid launch handles.");
    std::memcpy(&handles, reply.payload.data(), sizeof(handles));
    HANDLE target = nullptr;
    if (!DuplicateHandle(launcher->process, handles.process, worker->process,
        &target, 0, FALSE, DUPLICATE_SAME_ACCESS)) throw NativeFailure("DuplicateHandle(launched process)");
    handles.process = target;
    if (!DuplicateHandle(GetCurrentProcess(), launcher->job, worker->process,
        &target, 0, FALSE, DUPLICATE_SAME_ACCESS)) throw NativeFailure("DuplicateHandle(owned Job)");
    handles.job = target;
    handles.ownership_id = id;
    std::memcpy(reply.payload.data(), &handles, sizeof(handles));
    PrintAgentLogEvent("phase=owned-launch process=" + std::to_string(handles.process_id) +
        " worker=" + std::to_string(GetProcessId(worker->process)) +
        " capturedStreams=" + std::to_string(static_cast<unsigned int>(!options.launch.stdout_path.empty()) +
            static_cast<unsigned int>(!options.launch.stderr_path.empty())) +
        " killOnRelease=" + std::to_string(options.kill_tree_on_release));
  } else if (reply.kind != WorkerMessageKind::LaunchError) throw std::runtime_error("Invalid launch reply.");
  if (!co_await StopOperationWorker(launcher)) throw std::runtime_error("Launch helper termination unconfirmed.");
  if (reply.kind == WorkerMessageKind::LaunchError) worker->applications.erase(id);
  co_await WriteWorker(worker, std::move(reply), cancellation);
  co_return true;
}

static bool JobEmpty(HANDLE job) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION state = {};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &state, sizeof(state), nullptr))
    throw NativeFailure("QueryInformationJobObject(owned processes)");
  return state.ActiveProcesses == 0;
}

static cardio::promise<void> WaitForJobExit(HANDLE job, cardio::cancellation cancellation) {
  // Termination prevents successful new launches in this Job. Wait on native
  // process handles rather than polling an accounting counter or a Job handle
  // (a Job is not generally signaled when its last process exits).
  for (unsigned int snapshot = 0; snapshot < 16 && !JobEmpty(job); ++snapshot) {
    cancellation.throw_if_cancellation_requested();
    std::vector<ULONG_PTR> storage(130);
    for (;;) {
      if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, storage.data(),
          storage.size() * sizeof(ULONG_PTR), nullptr)) break;
      if (GetLastError() != ERROR_MORE_DATA || storage.size() >= 65538)
        throw NativeFailure("QueryInformationJobObject(process ids)");
      storage.resize(storage.size() * 2);
    }
    const auto list = reinterpret_cast<const JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
    bool observed = false;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
      const auto process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, list->ProcessIdList[i]);
      if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
        throw NativeFailure("OpenProcess(owned exit)");
      }
      BOOL member = FALSE;
      if (!IsProcessInJob(process, job, &member)) { const auto error = NativeFailure("IsProcessInJob"); CloseHandle(process); throw error; }
      if (!member) { CloseHandle(process); continue; } // A recycled PID is never a termination target.
      try { co_await cardio::from_win32_handle(process, cancellation); }
      catch (...) { CloseHandle(process); throw; }
      CloseHandle(process);
      observed = true;
    }
    if (!observed) break;
  }
  if (!JobEmpty(job)) throw std::runtime_error("Owned process exit could not be confirmed.");
}

cardio::promise<bool> StopOperationWorker(Worker worker) {
  if (!worker || !worker->process) co_return true;
  // Only call after pending pipe I/O has completed (including native cancellation).
  if (worker->input != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(worker->input, INVALID_HANDLE_VALUE));
  bool exited = false;
  {
    auto grace = cardio::cancellations::timeout(250);
    try { co_await cardio::from_win32_handle(worker->process, grace.get_cancellation()); exited = true; }
    catch (const cardio::canceled_exception&) {}
  }
  if (!exited) {
    if (!TerminateProcess(worker->process, ERROR_TIMEOUT)) {
      const auto code = GetLastError();
      if (WaitForSingleObject(worker->process, 0) != WAIT_OBJECT_0) {
        PrintAgentLogEvent("phase=worker-termination-failed worker=" + std::to_string(GetProcessId(worker->process)) +
            " Win32=" + std::to_string(code));
        co_return false;
      }
    }
    auto deadline = cardio::cancellations::timeout(2000);
    try { co_await cardio::from_win32_handle(worker->process, deadline.get_cancellation()); exited = true; }
    catch (const cardio::canceled_exception&) {}
  }
  if (!exited || worker->role != HelperRole::Operations) co_return exited;
  try {
    auto deadline = cardio::cancellations::timeout(5000);
    size_t terminated = 0, preserved = 0;
    for (const auto& [id, launcher] : worker->applications) {
      if (launcher->role == HelperRole::PersistentLaunch) {
        if (!co_await StopOperationWorker(launcher)) co_return false;
        ++preserved;
        continue;
      }
      if (!TerminateJobObject(launcher->job, ERROR_PROCESS_ABORTED)) throw NativeFailure("TerminateJobObject(owned launch)");
      co_await WaitForJobExit(launcher->job, deadline.get_cancellation());
      ++terminated;
    }
    std::erase_if(worker->applications, [](const auto& entry) {
      return entry.second->role == HelperRole::PersistentLaunch && !entry.second->captures_output;
    });
    PrintAgentLogEvent("phase=owned-processes-stopped worker=" + std::to_string(GetProcessId(worker->process)) +
        " terminatedTrees=" + std::to_string(terminated) + " preservedTrees=" + std::to_string(preserved));
    ReleaseRecoveryWorker(worker->process_slot);
    worker->process_slot = kNoRecoverySlot;
    if (worker->capture_root.empty()) {
      ReleaseRecoveryRoot(worker->recovery_slot);
      worker->recovery_slot = kNoRecoverySlot;
    }
  } catch (const std::exception& error) {
    PrintAgentLogEvent(std::string("phase=owned-processes-stop-failed reason=") + error.what());
    co_return false;
  }
  co_return true;
}

cardio::promise<bool> RecoverOperationWorker(Worker worker) {
  if (worker->role != HelperRole::Operations || worker->resources_recovered) co_return true;
  try {
    // Opted-out applications may still write captured output. Their Job stays
    // registered, but they neither hold a desktop permit nor an execution slot.
    for (const auto& [id, launcher] : worker->applications) if (!JobEmpty(launcher->job)) co_return false;
    worker->applications.clear();
  } catch (const std::exception& error) {
    PrintAgentLogEvent(std::string("phase=capture-wait reason=") + error.what());
    co_return false;
  }
  if (worker->capture_root.empty()) co_return true;
  bool recovered = false;
  if (worker->recovery) {
    if (!co_await StopOperationWorker(worker->recovery)) co_return false;
    worker->recovery.reset();
  }
  try {
    worker->recovery = StartOperationWorker(HelperRole::Cleanup);
    auto deadline = cardio::cancellations::timeout(3000);
    WorkerMessage command = {WorkerMessageKind::CaptureRoot, worker->capture_root};
    co_await WriteWorker(worker->recovery, std::move(command), deadline.get_cancellation());
    const auto response = co_await ReadWorker(worker->recovery, deadline.get_cancellation());
    recovered = response.kind == WorkerMessageKind::Complete;
    if (!recovered) PrintAgentLogEvent("phase=cleanup-failed " + std::string(response.payload.begin(), response.payload.end()));
  } catch (const std::exception& error) { PrintAgentLogEvent(std::string("phase=cleanup-failed reason=") + error.what()); }
  const auto cleanup_exited = co_await StopOperationWorker(worker->recovery);
  if (cleanup_exited) worker->recovery.reset();
  worker->resources_recovered = recovered && cleanup_exited;
  if (worker->resources_recovered) {
    ReleaseRecoveryRoot(worker->recovery_slot);
    worker->recovery_slot = kNoRecoverySlot;
  }
  co_return worker->resources_recovered;
}

static void BlockingWrite(const void* data, size_t size) {
  auto bytes = static_cast<const unsigned char*>(data);
  while (size) {
    DWORD written = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes,
        static_cast<DWORD>(std::min<size_t>(size, 65536)), &written, nullptr) || !written)
      throw NativeFailure("WriteFile(worker IPC)");
    bytes += written;
    size -= written;
  }
}

static bool BlockingRead(void* data, size_t size) {
  auto bytes = static_cast<unsigned char*>(data);
  while (size) {
    DWORD read = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes,
        static_cast<DWORD>(std::min<size_t>(size, 65536)), &read, nullptr) || !read) return false;
    bytes += read;
    size -= read;
  }
  return true;
}

void SendWorkerReply(WorkerMessageKind kind, const std::vector<unsigned char>& payload) {
  uint32_t header[2] = {static_cast<uint32_t>(kind), static_cast<uint32_t>(payload.size())};
  BlockingWrite(header, sizeof(header));
  BlockingWrite(payload.data(), payload.size());
}

void SendWorkerReplyText(WorkerMessageKind kind, const std::string& text) {
  SendWorkerReply(kind, {text.begin(), text.end()});
}

bool ReadWorkerCommand(WorkerMessage* message) {
  uint32_t header[2] = {};
  if (!BlockingRead(header, sizeof(header))) return false;
  if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker input exceeds limit.");
  message->kind = static_cast<WorkerMessageKind>(header[0]);
  message->payload.resize(header[1]);
  return BlockingRead(message->payload.data(), message->payload.size());
}

int RunCleanupWorker() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  try {
    WorkerMessage command;
    if (!ReadWorkerCommand(&command) || command.kind != WorkerMessageKind::CaptureRoot ||
        command.payload.size() <= sizeof(CaptureIdentity) || command.payload.size() > 32768) return 1;
    CaptureIdentity identity = {};
    std::memcpy(&identity, command.payload.data(), sizeof(identity));
    const std::string path(command.payload.begin() + sizeof(identity), command.payload.end());
    OperationError error;
    CleanupPolicy policy; policy.recursive = true; policy.ignore_missing = true; policy.managed_cleanup = true;
    const auto owned = RestoreCaptureIdentity(path, identity, &error);
    const auto missing = !owned && (error.os_code == ERROR_FILE_NOT_FOUND || error.os_code == ERROR_PATH_NOT_FOUND);
    if (missing || (owned && RemoveWithPolicy(path, policy, &error))) {
      SendWorkerReply(WorkerMessageKind::Complete, {});
      return 0;
    }
    SendWorkerReplyText(WorkerMessageKind::LogError,
        error.native_operation + " Win32=" + std::to_string(error.os_code) +
        " stage=" + (error.stage.empty() ? "fileCleanup" : error.stage) + " path=" + error.path);
  } catch (...) {}
  return 1;
}

static void SendWorkerChunk(const BinaryTransferChunk& chunk) {
  std::vector<unsigned char> payload;
  EncodeBinaryTransferChunkPayload(chunk, &payload);
  SendWorkerReply(WorkerMessageKind::Binary, payload);
}

static void SendWorkerFile(const OutboundFileTransfer& transfer) {
  HANDLE file = CreateFileW(Utf8ToWide(transfer.path).c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw NativeFailure("CreateFileW(video transfer)");
  try {
    uint64_t offset = 0;
    uint32_t sequence = 0;
    do {
      BinaryTransferChunk chunk = {};
      chunk.transfer_id = transfer.transfer_id;
      chunk.content_type = transfer.content_type;
      chunk.sequence = sequence++;
      chunk.data.resize(static_cast<size_t>(std::min<uint64_t>(65536, transfer.total_bytes - offset)));
      DWORD read = 0;
      if (!chunk.data.empty() && (!ReadFile(file, chunk.data.data(), chunk.data.size(), &read, nullptr) || !read))
        throw NativeFailure("ReadFile(video transfer)");
      chunk.data.resize(read);
      offset += read;
      chunk.final = offset == transfer.total_bytes;
      if (chunk.final) {
        chunk.total_bytes = transfer.total_bytes;
        chunk.has_total_bytes = true;
        chunk.sha256 = transfer.sha256;
        chunk.has_sha256 = true;
      }
      SendWorkerChunk(chunk);
    } while (offset < transfer.total_bytes);
  } catch (...) { CloseHandle(file); throw; }
  CloseHandle(file);
}

int RunOperationWorker(bool probe, uint64_t max_transfer_bytes) {
  const auto input = GetStdHandle(STD_INPUT_HANDLE);
  const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
  // Child applications must never inherit our protocol pipe handles.
  SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
  BinaryTransferStore transfers = {};
  transfers.max_transfer_bytes = max_transfer_bytes;
  VideoRecordingStore recordings = {};
  try {
    if (probe) {
      uint32_t header[2] = {};
      if (!BlockingRead(header, sizeof(header)) || header[1] > 255) return 1;
      std::string host(header[1], '\0');
      if (!BlockingRead(host.data(), host.size())) return 1;
      WSADATA data = {};
      if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
      in_addr address = {};
      address.s_addr = host.empty() ? INADDR_ANY : inet_addr(host.c_str());
      if (address.s_addr == INADDR_NONE && host != "255.255.255.255") {
        const auto entry = gethostbyname(host.c_str());
        if (!entry || !entry->h_addr_list || !entry->h_addr_list[0]) return 1;
        std::memcpy(&address, entry->h_addr_list[0], sizeof(address));
      }
      std::vector<unsigned char> detected(5);
      detected[0] = static_cast<unsigned char>(IsVideoCaptureSupported());
      std::memcpy(detected.data() + 1, &address, sizeof(address));
      SendWorkerReply(WorkerMessageKind::Capability, detected);
      WSACleanup();
      return 0;
    }
    bool owns_capture = false;
    InstallManagedLaunchBroker();
    SendWorkerReply(WorkerMessageKind::Complete, {});
    SetAgentLogSink([](const AgentLogRecord& record) { SendWorkerReplyText(WorkerMessageKind::Log, record.event); });
    for (;;) {
      uint32_t header[2] = {};
      if (!BlockingRead(header, sizeof(header))) break;
      if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker input exceeds limit.");
      std::vector<unsigned char> payload(header[1]);
      if (!BlockingRead(payload.data(), payload.size())) break;
      if (header[0] == static_cast<uint32_t>(WorkerMessageKind::Binary)) {
        BinaryTransferChunk chunk = {};
        std::string error;
        if (!DecodeBinaryTransferChunkPayload(payload, &chunk, &error) ||
            !AcceptBinaryTransferChunk(&transfers, chunk, &error)) throw std::runtime_error(error);
      } else if (header[0] == static_cast<uint32_t>(WorkerMessageKind::Json)) {
        const std::string request(payload.begin(), payload.end());
        std::string id, method;
        if (!ReadAgentRequest(request, &id, &method)) throw std::runtime_error("Invalid worker request.");
        if (!owns_capture && (method == "process.createCaptureDirectory" ||
            method == "agent.recordVideo" || method == "window.recordVideo")) {
          SendWorkerReply(WorkerMessageKind::ReserveCapture, {});
          WorkerMessage acknowledged;
          if (!ReadWorkerCommand(&acknowledged) || acknowledged.kind != WorkerMessageKind::Complete)
            throw std::runtime_error("Capture reservation was not acknowledged.");
          std::string directory;
          OperationError error;
          CaptureIdentity identity = {};
          if (!CreateCaptureDirectory(&directory, &error) || !GetCaptureIdentity(directory, &identity))
            throw std::runtime_error("Could not establish connection capture ownership.");
          std::vector<unsigned char> ownership(sizeof(identity));
          std::memcpy(ownership.data(), &identity, sizeof(identity));
          ownership.insert(ownership.end(), directory.begin(), directory.end());
          SendWorkerReply(WorkerMessageKind::CaptureRoot, ownership);
          if (!ReadWorkerCommand(&acknowledged) || acknowledged.kind != WorkerMessageKind::Complete)
            throw std::runtime_error("Capture owner disconnected before acknowledgement.");
          SetCaptureRoot(directory);
          owns_capture = true;
        }
        std::vector<BinaryTransferChunk> chunks;
        OutboundFileTransfer file = {};
        const auto response = HandleJsonRequest(request, &transfers, &recordings, &chunks, &file);
        PrintAgentLogEvent(CreateAgentResultLog(response));
        try {
          for (const auto& chunk : chunks) SendWorkerChunk(chunk);
          SendWorkerReplyText(WorkerMessageKind::Json, response);
          if (file.present) SendWorkerFile(file);
        } catch (...) {
          if (file.present) { DeleteFileW(Utf8ToWide(file.path).c_str()); RemoveDirectoryW(Utf8ToWide(file.directory).c_str()); }
          throw;
        }
        if (file.present) { DeleteFileW(Utf8ToWide(file.path).c_str()); RemoveDirectoryW(Utf8ToWide(file.directory).c_str()); }
      } else throw std::runtime_error("Unknown worker command.");
      SendWorkerReply(WorkerMessageKind::Complete, {});
    }
    CancelVideoRecording(&recordings);
    return 0;
  } catch (...) {
    // The host records the failed operation and reaps this isolated process.
    return 1;
  }
}

}  // namespace agent_rover
