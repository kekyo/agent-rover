// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "operation_error.h"
#include <windows.h>

namespace agent_rover {
OperationError MakeOperationError(const std::string& operation,
                                  const std::string& path, uint32_t code) {
  OperationError error;
  error.native_operation = operation;
  error.path = path;
  error.os_code = code;
  error.message = operation + " failed. path=" + path + " win32Error=" + std::to_string(code);
  switch (code) {
    case ERROR_SHARING_VIOLATION: error.reason = "sharingViolation"; break;
    case ERROR_LOCK_VIOLATION: error.reason = "lockViolation"; break;
    case ERROR_ACCESS_DENIED: error.reason = "accessDenied"; break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: error.reason = "notFound"; break;
    case ERROR_DIR_NOT_EMPTY: error.reason = "directoryNotEmpty"; break;
    case ERROR_BUSY: error.reason = "busy"; break;
    default: break;
  }
  return error;
}
}
