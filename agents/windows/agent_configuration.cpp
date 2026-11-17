// agent-rover - Windows startup configuration
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <userenv.h>
#include <algorithm>

#include "agent_configuration.h"
#include "win32_files.h"
#include "win32_util.h"

namespace agent_rover {

static bool Separator(wchar_t ch) { return ch == L'\\' || ch == L'/'; }

static bool AbsolutePath(const std::wstring& path) {
  auto value = path;
  if (value.rfind(L"\\\\?\\UNC\\", 0) == 0) value = L"\\\\" + value.substr(8);
  else if (value.rfind(L"\\\\?\\", 0) == 0) value = value.substr(4);
  if (value.size() >= 3 && ((value[0] >= L'A' && value[0] <= L'Z') ||
      (value[0] >= L'a' && value[0] <= L'z')) && value[1] == L':' && Separator(value[2])) return true;
  if (value.size() < 5 || !Separator(value[0]) || !Separator(value[1]) ||
      value[2] == L'.' || value[2] == L'?' || Separator(value[2])) return false;
  const auto share = value.find_first_of(L"\\/", 2);
  return share != std::wstring::npos && share + 1 < value.size() && !Separator(value[share + 1]);
}

static bool DirectoryPath(const std::wstring& value, const std::wstring& source,
    std::wstring* result, std::wstring* error) {
  if (!AbsolutePath(value)) {
    *error = source + L": Expected an absolute Windows drive or UNC directory.";
    return false;
  }
  std::vector<wchar_t> buffer(32768);
  const auto length = GetFullPathNameW(value.c_str(), buffer.size(), buffer.data(), nullptr);
  if (!length || length >= buffer.size()) {
    *error = source + L": Cannot resolve directory (Win32=" + std::to_wstring(GetLastError()) + L").";
    return false;
  }
  result->assign(buffer.data(), length);
  const auto attributes = GetFileAttributesW(result->c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    *error = source + L": Expected an existing directory: " + *result + L".";
    return false;
  }
  return true;
}

static bool ConfigPath(std::wstring* path, std::wstring* error) {
  std::vector<wchar_t> buffer(32768);
  SetLastError(ERROR_SUCCESS);
  const auto size = GetEnvironmentVariableW(L"HOME", buffer.data(), buffer.size());
  if (size >= buffer.size() || (!size && GetLastError() != ERROR_SUCCESS && GetLastError() != ERROR_ENVVAR_NOT_FOUND)) {
    *error = L"HOME: Cannot read the environment variable.";
    return false;
  }
  std::wstring home;
  if (size) home.assign(buffer.data(), size);
  else {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      *error = L"HOME: OpenProcessToken failed (Win32=" + std::to_wstring(GetLastError()) + L").";
      return false;
    }
    DWORD count = buffer.size();
    const auto ok = GetUserProfileDirectoryW(token, buffer.data(), &count);
    const auto code = GetLastError();
    CloseHandle(token);
    if (!ok) {
      *error = L"HOME: GetUserProfileDirectoryW failed (Win32=" + std::to_wstring(code) + L").";
      return false;
    }
    home = buffer.data();
  }
  if (!DirectoryPath(home, L"HOME", &home, error)) return false;
  if (!Separator(home.back())) home += L'\\';
  *path = home + L".agent-rover-agent.ini";
  return true;
}

AgentCommandLineParseResult LoadAgentConfiguration(int argc, const wchar_t* const argv[]) {
  auto result = ParseAgentCommandLine(argc, argv);
  if (!result.ok || result.help_requested) return result;
  std::wstring path;
  if (!ConfigPath(&path, &result.error)) { result.ok = false; return result; }
  OperationError error;
  std::vector<unsigned char> bytes;
  // Bound allocation independently from file-transfer settings, which are not
  // resolved yet. ReadFileBytes uses overlapped cardio I/O before supervision.
  if (!ReadFileBytes(WideToUtf8(path), 1024 * 1024, &bytes, &error) && error.os_code != ERROR_FILE_NOT_FOUND) {
    result.ok = false;
    result.error = path + L": Cannot read configuration: " + Utf8ToWide(error.message);
    return result;
  }
  result = ParseAgentConfiguration(argc, argv, bytes, path);
  if (!result.ok) return result;
  auto& directory = result.options.safe_base_dir;
  auto source = result.options.safe_base_dir_line ?
      path + L":" + std::to_wstring(result.options.safe_base_dir_line) + L" [safe-base-dir]" : L"--safe-base-dir";
  if (directory.empty()) {
    std::vector<wchar_t> temporary(32768);
    const auto length = GetTempPathW(temporary.size(), temporary.data());
    if (!length || length >= temporary.size()) {
      result.ok = false;
      result.error = L"GetTempPathW failed (Win32=" + std::to_wstring(GetLastError()) + L").";
      return result;
    }
    directory.assign(temporary.data(), length);
    source = L"OS temporary directory";
  }
  result.ok = DirectoryPath(directory, source, &directory, &result.error);
  return result;
}

}
