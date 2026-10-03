// agent-rover - File persistence confined to an isolated helper
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WINDOWS_LOG_STORAGE_H
#define AGENT_ROVER_WINDOWS_LOG_STORAGE_H
#include <winsock2.h>
#include <cardio.h>
#include <memory>
#include <string>

namespace agent_rover {
/** Isolated logger's file and rotation state. */
struct LogStorage;
/** Storage ownership retained by each native write. */
using LogStorageHandle = std::shared_ptr<LogStorage>;
/** Creates a unique append file and prunes old, closed files.
 * @param directory Existing, user-writable log directory.
 * @param session Filename-safe startup identifier. @return Storage owner.
 * @remarks Performs metadata I/O; call only from an isolated helper. */
LogStorageHandle OpenLogStorage(const std::wstring& directory, const std::string& session);
/** Appends complete UTF-8 lines and rotates before exceeding 10 MiB.
 * @param storage Destination. @param lines Complete bounded batch retained until completion.
 * @return Completion promise. @remarks Must run in the isolated logger dispatcher. */
cardio::promise<void> AppendLogStorage(LogStorageHandle storage, std::string lines);
/** Flushes the OS cache for orderly shutdown.
 * @param storage Destination. @remarks Blocking metadata operation, isolated by the process boundary. */
void FlushLogStorage(const LogStorageHandle& storage);
/** Reads the active filename without I/O.
 * @param storage Destination. @return Full UTF-16 path. */
std::wstring LogStoragePath(const LogStorageHandle& storage);
}  // namespace agent_rover
#endif
