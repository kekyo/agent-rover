// agent-rover - Exclusive caller-owned workspace directories
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <windows.h>
#include <array>
#include "win32_files.h"
#include "win32_random.h"
#include "win32_util.h"

namespace agent_rover {

bool MakeTempDirectory(const std::string& safe_base_dir,
    const std::optional<std::string>& prefix, std::string* path, OperationError* error) {
  auto resolved = Utf8ToWide(prefix.value_or("agent-rover-"));
  if (resolved.empty() || resolved.find(L'\0') != std::wstring::npos ||
      (!IsAbsoluteWindowsPath(resolved) && (resolved == L"." || resolved == L".." ||
          resolved.find_first_of(L"\\/:") != std::wstring::npos))) {
    *error = "file.mkdtemp prefix must be a nonempty name or absolute Windows path.";
    error->reason = "invalidArgument";
    return false;
  }
  if (!IsAbsoluteWindowsPath(resolved)) {
    auto base = Utf8ToWide(safe_base_dir);
    if (!IsAbsoluteWindowsPath(base) || base.find(L'\0') != std::wstring::npos) {
      *error = "The configured workspace directory is invalid.";
      return false;
    }
    if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
    resolved = base + resolved;
  }
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    std::array<unsigned char, 16> bytes;
    std::string random_error;
    if (!GenerateSecureRandom(bytes.data(), bytes.size(), &random_error)) {
      *error = random_error;
      return false;
    }
    auto candidate = resolved;
    for (const auto byte : bytes) {
      candidate += L"0123456789abcdef"[byte >> 4];
      candidate += L"0123456789abcdef"[byte & 15];
    }
    // Creation is the ownership decision. Never accept an existing directory,
    // and never remove or inspect a competitor's contents after a collision.
    if (CreateDirectoryW(candidate.c_str(), nullptr)) {
      *path = WideToUtf8(candidate);
      return true;
    }
    const auto code = GetLastError();
    *error = MakeOperationError("CreateDirectoryW", WideToUtf8(candidate), code);
    if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) return false;
  }
  error->message = "Temporary directory name collisions exhausted the retry limit.";
  return false;
}
}
