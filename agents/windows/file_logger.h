// agent-rover - Asynchronous, bounded file log delivery
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WINDOWS_FILE_LOGGER_H
#define AGENT_ROVER_WINDOWS_FILE_LOGGER_H
#include "async_io.h"
#include "agent_log.h"
#include <functional>

namespace agent_rover {
/** Dispatcher-owned queue and isolated file writer. */
struct FileLogger;
/** File logger lifetime, including pending IPC and shutdown. */
using FileLoggerHandle = std::shared_ptr<FileLogger>;
/** Starts isolated persistence without waiting for disk initialization.
 * @param status Nonblocking GUI status callback. @return Logger owner. */
FileLoggerHandle CreateFileLogger(std::function<void(const std::string&)> status);
/** Adds a bounded record; never waits for disk or IPC.
 * @param logger Destination. @param record Event to persist. */
void EnqueueFileLog(const FileLoggerHandle& logger, const AgentLogRecord& record);
/** Requests opening the log directory from the isolated process.
 * @param logger Destination whose directory should be opened. */
void OpenFileLogFolder(const FileLoggerHandle& logger);
/** Drains/flushed logs for at most two seconds, then performs bounded child recovery.
 * @param logger Destination. @return Completion promise. */
cardio::promise<void> StopFileLogger(FileLoggerHandle logger);
/** Runs the private file logger role; all disk metadata calls stay in this process.
 * @return Process exit code. */
int RunFileLogWorker();
}  // namespace agent_rover
#endif
