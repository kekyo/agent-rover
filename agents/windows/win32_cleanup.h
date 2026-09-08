// agent-rover - Windows cleanup ownership and repair
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WIN32_CLEANUP_H
#define AGENT_ROVER_WIN32_CLEANUP_H

#include <string>
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
/** Removes a target within the selected ownership and repair boundary.
 * @param path Requested target.
 * @param policy Single-attempt policy; retry timing belongs to the driver.
 * @param error Receives native failure and all repair/rollback results.
 * @return true when the target entry is absent.
 */
bool RemoveWithPolicy(const std::string& path, const CleanupPolicy& policy, OperationError* error);
}
#endif
