// agent-rover - Windows cleanup ownership and repair
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WIN32_CLEANUP_H
#define AGENT_ROVER_WIN32_CLEANUP_H

#include <string>
#include <cstdint>
#include "operation_error.h"

namespace agent_rover {
/** Policy for a single native removal attempt. */
struct CleanupPolicy {
  /** Remove directory contents. */
  bool recursive = false;
  /** Accept an absent target. */
  bool ignore_missing = false;
  /** Clear only the read-only bit. */
  bool clear_read_only = false;
  /** Add the current user's missing removal access without replacing the DACL. */
  bool grant_delete = false;
  /** Require a registered native capture directory and enable its repair policy. */
  bool managed_cleanup = false;
};
/** Creates and registers an exclusively created capture directory.
 * @param path Receives the native-selected directory.
 * @param error Receives failure information.
 * @return true when creation and registration succeed.
 */
bool CreateCaptureDirectory(std::string* path, OperationError* error);
/** Native identity retained across an isolated worker's lifetime. */
struct CaptureIdentity {
  /** Volume serial and 64-bit file index. */
  uint32_t volume, index_high, index_low;
};
/** Reads a registered directory's identity without filesystem I/O.
 * @param path Owned directory. @param identity Receives its identity. @return Whether registered. */
bool GetCaptureIdentity(const std::string& path, CaptureIdentity* identity);
/** Restores trusted ownership in the private cleanup helper; never accepts network input.
 * @param path Owned directory. @param identity Original identity. @param error Receives failure.
 * @return Whether the same directory still exists and was registered. */
bool RestoreCaptureIdentity(const std::string& path, const CaptureIdentity& identity, OperationError* error);
/** Selects the connection-owned parent for future capture directories.
 * @param path Owned directory, or empty for the OS temporary directory.
 * @remarks Set once before any recording thread starts. Does not change child environment variables. */
void SetCaptureRoot(const std::string& path);
/** Gets the capture parent without changing the process environment.
 * @return UTF-8 parent directory, including a trailing separator. */
std::string CaptureRoot();
/** Removes a target within the selected ownership and repair boundary.
 * @param path Requested target.
 * @param policy Single-attempt policy; retry timing belongs to the driver.
 * @param error Receives native failure and all repair/rollback results.
 * @return true when the target entry is absent.
 */
bool RemoveWithPolicy(const std::string& path, const CleanupPolicy& policy, OperationError* error);
}
#endif
