// agent-rover - Current-user startup registration
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include "auto_start.h"
#include "win32_util.h"

namespace agent_rover {
static constexpr auto kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr auto kValue = L"AgentRover";

static bool RegistryFailure(const wchar_t* operation, LSTATUS code, std::wstring* error) {
  *error = std::wstring(operation) + L" failed (Win32=" + std::to_wstring(code) + L").";
  return false;
}

bool GetAutoStartExecutablePath(std::wstring* path, std::wstring* error) {
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), buffer.size());
  if (!length) return RegistryFailure(L"GetModuleFileNameW", GetLastError(), error);
  if (length >= buffer.size()) {
    *error = L"The executable path is too long to read without truncation.";
    return false;
  }
  path->assign(buffer.data(), length);
  if (!IsAbsoluteWindowsPath(*path)) { *error = L"The executable path is not absolute."; return false; }
  return true;
}

static bool ReadRegistration(HKEY key, const std::wstring& executable, AutoStartState* state, std::wstring* error) {
  *state = {};
  // RegQueryValueExW does not guarantee a terminator, even for REG_SZ.
  std::vector<wchar_t> buffer(32768);
  DWORD bytes = buffer.size() * sizeof(wchar_t), type = 0;
  const auto code = RegQueryValueExW(key, kValue, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes);
  if (code == ERROR_FILE_NOT_FOUND) return true;
  if (code != ERROR_SUCCESS) return RegistryFailure(L"RegQueryValueExW", code, error);
  state->registered = true;
  if (type != REG_SZ || bytes % sizeof(wchar_t)) return true;
  std::wstring command(buffer.data(), bytes / sizeof(wchar_t));
  if (!command.empty() && command.back() == L'\0') command.pop_back();
  if (command.empty() || command.find(L'\0') != std::wstring::npos) return true;
  state->current_copy = _wcsicmp(command.c_str(), (L"\"" + executable + L"\"").c_str()) == 0;
  int count = 0;
  const auto arguments = CommandLineToArgvW(command.c_str(), &count);
  if (arguments) {
    // External arguments can contain credentials. Display only an absolute path.
    if (count && IsAbsoluteWindowsPath(arguments[0])) state->registered_path = arguments[0];
    LocalFree(arguments);
  }
  return true;
}

bool ReadAutoStartRegistration(const std::wstring& executable, AutoStartState* state, std::wstring* error) {
  *state = {};
  HKEY key = nullptr;
  // KEY_WOW64_64KEY is ignored on 32-bit Windows, including supported XP.
  const auto code = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
  if (code == ERROR_FILE_NOT_FOUND) return true;
  if (code != ERROR_SUCCESS) return RegistryFailure(L"RegOpenKeyExW", code, error);
  const bool success = ReadRegistration(key, executable, state, error);
  RegCloseKey(key);
  return success;
}

bool WriteAutoStartRegistration(const std::wstring& executable, bool enabled, AutoStartState* state, std::wstring* error) {
  if (!IsAbsoluteWindowsPath(executable) || executable.find(L'\0') != std::wstring::npos || executable.find(L'"') != std::wstring::npos) {
    *error = L"Start at sign-in requires an absolute executable path without quotes.";
    return false;
  }
  const auto command = L"\"" + executable + L"\"";
  if (enabled && command.size() > 260) {
    *error = L"Start at sign-in requires a quoted command of at most 260 characters. Move this copy to a shorter path.";
    return false;
  }
  HKEY key = nullptr;
  const auto access = KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY;
  auto code = enabled ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, &key, nullptr) :
      RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, access, &key);
  if (!enabled && code == ERROR_FILE_NOT_FOUND) { *state = {}; return true; }
  if (code != ERROR_SUCCESS) return RegistryFailure(enabled ? L"RegCreateKeyExW" : L"RegOpenKeyExW", code, error);
  bool success = ReadRegistration(key, executable, state, error);
  if (success) {
    if (enabled) code = RegSetValueExW(key, kValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), (command.size() + 1) * sizeof(wchar_t));
    else if (state->current_copy) code = RegDeleteValueW(key, kValue);
    if (code != ERROR_SUCCESS && !(code == ERROR_FILE_NOT_FOUND && !enabled))
      success = RegistryFailure(enabled ? L"RegSetValueExW" : L"RegDeleteValueW", code, error);
    else {
      success = ReadRegistration(key, executable, state, error);
      if (success && state->current_copy != enabled) {
        *error = L"The startup registration changed during the operation. Open the menu to check its current state.";
        success = false;
      }
    }
  }
  RegCloseKey(key);
  return success;
}
}
