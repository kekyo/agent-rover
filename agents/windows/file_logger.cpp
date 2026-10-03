// agent-rover - Asynchronous, bounded file log delivery
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "file_logger.h"
#include "log_queue.h"
#include "log_storage.h"
#include "operation_worker.h"
#include "win32_util.h"
#include <shellapi.h>
#include <stdexcept>
#include <utility>

namespace agent_rover {

struct FileLogger {
  std::string session, path, failure;
  LogQueue queue;
  Worker worker;
  AsyncSignal ready;
  cardio::cancellation_source abort;
  cardio::promise<void> lifetime;
  std::function<void(const std::string&)> status;
  uint64_t reported_loss = 0;
  bool stopping = false, open_folder = false, finished = false;
};

static void UpdateLogStatus(FileLogger* logger) {
  std::string text = logger->path.empty() ? "Logs: initializing" : "Logs: " + logger->path;
  if (!logger->failure.empty()) text += " | " + logger->failure;
  if (logger->queue.dropped) text += " | lost/unconfirmed=" + std::to_string(logger->queue.dropped) +
      " seq=" + std::to_string(logger->queue.first_dropped) + ".." + std::to_string(logger->queue.last_dropped);
  logger->status(text);
}

static cardio::promise<void> LogExchange(FileLogger* logger, WorkerMessageKind kind, std::string text) {
  auto timeout = cardio::cancellations::timeout(5000);
  auto cancelled = cardio::cancellations::any(timeout.get_cancellation(), logger->abort.get_cancellation());
  WorkerMessage command;
  command.kind = kind;
  command.payload.assign(text.begin(), text.end());
  co_await WriteWorker(logger->worker, std::move(command), cancelled.get_cancellation());
  const auto response = co_await ReadWorker(logger->worker, cancelled.get_cancellation());
  if (response.kind == WorkerMessageKind::LogError)
    throw std::runtime_error(std::string(response.payload.begin(), response.payload.end()));
  if (response.kind != WorkerMessageKind::LogAcknowledged) throw std::runtime_error("Invalid logger acknowledgement.");
  if (!response.payload.empty()) logger->path.assign(response.payload.begin(), response.payload.end());
}

static cardio::promise<void> RunFileLogger(FileLogger* logger) {
  LogBatch pending;
  for (;;) {
    bool succeeded = false;
    try {
      if (logger->abort.get_cancellation().is_cancellation_requested()) break;
      logger->worker = StartOperationWorker(HelperRole::FileLogger);
      co_await LogExchange(logger, WorkerMessageKind::LogInitialize, logger->session);
      logger->failure.clear();
      UpdateLogStatus(logger);
      for (;;) {
        if (logger->queue.records.empty() && !logger->open_folder) {
          if (logger->stopping) break;
          co_await logger->ready.Wait(logger->abort.get_cancellation());
          continue;
        }
        if (!logger->stopping) co_await cardio::promises::delay(100, logger->abort.get_cancellation());
        if (logger->open_folder) {
          logger->open_folder = false;
          co_await LogExchange(logger, WorkerMessageKind::LogOpenFolder, "");
        }
        pending = TakeLogBatch(&logger->queue);
        if (pending.count) {
          const auto loss = logger->queue.dropped;
          if (loss != logger->reported_loss) {
            pending.data.insert(0, "# lost/unconfirmed=" + std::to_string(loss) + " session=" + logger->session +
                " seqRange=" + std::to_string(logger->queue.first_dropped) + ".." + std::to_string(logger->queue.last_dropped) + "\n");
          }
          co_await LogExchange(logger, WorkerMessageKind::LogBatch, pending.data);
          pending = {};
          logger->reported_loss = loss;
          UpdateLogStatus(logger);
        }
      }
      co_await LogExchange(logger, WorkerMessageKind::LogFlush, "");
      succeeded = true;
    } catch (const std::exception& error) {
      MarkLogBatchUnconfirmed(&logger->queue, pending);
      pending = {};
      logger->failure = std::string("Save failed: ") + error.what();
      UpdateLogStatus(logger);
    }
    const auto reaped = co_await StopOperationWorker(logger->worker);
    if (!reaped) {
      logger->failure += " | logger recovery deadline exceeded; writer quarantined";
      UpdateLogStatus(logger);
      break;
    }
    logger->worker.reset();
    if (succeeded || logger->stopping) break;
    try { co_await cardio::promises::delay(5000, logger->abort.get_cancellation()); }
    catch (const cardio::canceled_exception&) { break; }
  }
  while (!logger->queue.records.empty()) MarkLogBatchUnconfirmed(&logger->queue, TakeLogBatch(&logger->queue));
  logger->finished = true;
  UpdateLogStatus(logger);
}

FileLoggerHandle CreateFileLogger(std::function<void(const std::string&)> status) {
  auto logger = std::make_shared<FileLogger>();
  logger->status = std::move(status);
  FILETIME time = {};
  GetSystemTimeAsFileTime(&time);
  const uint64_t ticks = (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  logger->session = std::to_string(ticks) + "-" + std::to_string(GetCurrentProcessId());
  logger->lifetime = RunFileLogger(logger.get());
  return logger;
}

void EnqueueFileLog(const FileLoggerHandle& logger, const AgentLogRecord& record) {
  const auto before = logger->queue.dropped;
  QueueLogRecord(&logger->queue, logger->session, record);
  if (logger->finished) {
    while (!logger->queue.records.empty()) MarkLogBatchUnconfirmed(&logger->queue, TakeLogBatch(&logger->queue));
  }
  if (before != logger->queue.dropped) UpdateLogStatus(logger.get());
  logger->ready.Notify();
}

void OpenFileLogFolder(const FileLoggerHandle& logger) {
  if (!logger || logger->finished || logger->stopping) return;
  logger->open_folder = true;
  logger->ready.Notify();
}

cardio::promise<void> StopFileLogger(FileLoggerHandle logger) {
  if (!logger) co_return;
  logger->stopping = true;
  logger->ready.Notify();
  auto deadline = cardio::cancellations::timeout(2000);
  auto registration = deadline.get_cancellation().on_cancellation_requested([logger] { logger->abort.cancel(); });
  co_await logger->lifetime;
}

static std::wstring LogDirectory() {
  const auto size = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (!size || size > 32768) throw std::runtime_error("APPDATA is not a usable log directory.");
  std::wstring directory(size, L'\0');
  const auto length = GetEnvironmentVariableW(L"APPDATA", directory.data(), size);
  if (!length || length >= size) throw std::runtime_error("GetEnvironmentVariableW(APPDATA) failed.");
  directory.resize(length);
  for (const auto name : {L"agent-rover", L"logs"}) {
    directory += L"\\";
    directory += name;
    if (!CreateDirectoryW(directory.c_str(), nullptr)) {
      const auto code = GetLastError();
      const auto attributes = GetFileAttributesW(directory.c_str());
      if (code != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("CreateDirectoryW(logs) Win32=" + std::to_string(code));
    }
  }
  return directory;
}

static cardio::promise<void> PersistAndAcknowledge(LogStorageHandle storage, std::string text, bool* succeeded) {
  try {
    co_await AppendLogStorage(storage, std::move(text));
    SendWorkerReplyText(WorkerMessageKind::LogAcknowledged, WideToUtf8(LogStoragePath(storage)));
    *succeeded = true;
  } catch (const std::exception& error) {
    SendWorkerReplyText(WorkerMessageKind::LogError, error.what());
  }
}

int RunFileLogWorker() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  try {
    WorkerMessage command;
    if (!ReadWorkerCommand(&command) || command.kind != WorkerMessageKind::LogInitialize) return 1;
    const auto directory = LogDirectory();
    const std::string session(command.payload.begin(), command.payload.end());
    const auto storage = OpenLogStorage(directory, session);
    SendWorkerReplyText(WorkerMessageKind::LogAcknowledged, WideToUtf8(LogStoragePath(storage)));
    cardio::dispatcher_host_win32_auto dispatcher;
    while (ReadWorkerCommand(&command)) {
      if (command.kind == WorkerMessageKind::LogBatch) {
        bool succeeded = false;
        std::string text(command.payload.begin(), command.payload.end());
        auto writing = PersistAndAcknowledge(storage, std::move(text), &succeeded);
        dispatcher.park();
        if (!succeeded) return 1;
      } else if (command.kind == WorkerMessageKind::LogOpenFolder) {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) throw std::runtime_error("ShellExecuteW(log folder) code=" + std::to_string(result));
        SendWorkerReply(WorkerMessageKind::LogAcknowledged, {});
      } else if (command.kind == WorkerMessageKind::LogFlush) {
        FlushLogStorage(storage);
        SendWorkerReply(WorkerMessageKind::LogAcknowledged, {});
      } else throw std::runtime_error("Unexpected file logger command.");
    }
    return 0;
  } catch (const std::exception& error) {
    try { SendWorkerReplyText(WorkerMessageKind::LogError, error.what()); } catch (...) {}
    return 1;
  }
}

}  // namespace agent_rover
