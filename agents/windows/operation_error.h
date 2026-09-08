// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_OPERATION_ERROR_H
#define AGENT_ROVER_OPERATION_ERROR_H

#include <cstdint>
#include <string>

namespace agent_rover {

/** Failure captured at the operating-system operation boundary. */
struct OperationError {
  /** Human-readable diagnostic, never used to classify the error. */
  std::string message;
  /** Name of the failing native API. */
  std::string native_operation;
  /** Actual failing entry, including children during recursive operations. */
  std::string path;
  /** Saved OS error code, zero when no native error was reported. */
  uint32_t os_code = 0;
  /** Machine-readable cause; accessDenied alone does not prove a lock. */
  std::string reason = "unknown";
  /** Incomplete resource-release stage, or empty for other operations. */
  std::string stage;

  /** Assigns a non-OS diagnostic and clears stale native information. */
  OperationError& operator=(const std::string& diagnostic) {
    message = diagnostic;
    native_operation.clear();
    path.clear();
    os_code = 0;
    reason = "unknown";
    stage.clear();
    return *this;
  }
};

/**
 * Builds a failure from a saved Win32 code.
 * @param operation Native API name.
 * @param path Actual affected path, or empty for non-file operations.
 * @param code Error captured immediately after the API failed.
 * @return Structured failure without consulting GetLastError again.
 */
OperationError MakeOperationError(const std::string& operation,
                                  const std::string& path, uint32_t code);
}
#endif
