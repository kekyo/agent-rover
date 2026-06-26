// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_process.h"

#include <windows.h>
#include <psapi.h>

#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "command_line.h"
#include "win32_util.h"

namespace agent_rover {

static std::string Basename(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

static std::wstring Lowercase(const std::wstring& value) {
  std::wstring result = value;
  for (wchar_t& ch : result) {
    ch = static_cast<wchar_t>(std::towlower(ch));
  }
  return result;
}

static std::vector<std::wstring> WideArguments(
    const std::vector<std::string>& arguments) {
  std::vector<std::wstring> result;
  result.reserve(arguments.size());
  for (const std::string& argument : arguments) {
    result.push_back(Utf8ToWide(argument));
  }
  return result;
}

static std::wstring BuildEnvironmentBlock(
    const std::map<std::string, std::string>& overrides) {
  if (overrides.empty()) {
    return std::wstring();
  }
  std::map<std::wstring, std::wstring> entries;
  LPWCH current_environment = GetEnvironmentStringsW();
  if (current_environment != nullptr) {
    const wchar_t* cursor = current_environment;
    while (*cursor != L'\0') {
      const std::wstring entry(cursor);
      const size_t separator = entry.find(L'=');
      if (separator != std::wstring::npos && separator != 0) {
        entries[Lowercase(entry.substr(0, separator))] = entry;
      }
      cursor += entry.size() + 1;
    }
    FreeEnvironmentStringsW(current_environment);
  }

  for (const auto& override_entry : overrides) {
    const std::wstring key = Utf8ToWide(override_entry.first);
    const std::wstring value = Utf8ToWide(override_entry.second);
    if (!key.empty()) {
      entries[Lowercase(key)] = key + L"=" + value;
    }
  }

  std::wstring block;
  for (const auto& entry : entries) {
    block += entry.second;
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

static bool OpenRedirectFile(
    const std::string& path,
    HANDLE* handle,
    std::string* error) {
  *handle = nullptr;
  if (path.empty()) {
    return true;
  }
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "Redirect path is empty or invalid UTF-8.";
    return false;
  }
  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  *handle = CreateFileW(
      wide_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (*handle == INVALID_HANDLE_VALUE) {
    *handle = nullptr;
    *error = "CreateFileW for process redirect failed.";
    return false;
  }
  return true;
}

static std::string ReadProcessPath(HANDLE process) {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameExW(process, nullptr, path, MAX_PATH);
  if (length == 0) {
    return std::string();
  }
  return WideToUtf8(std::wstring(path, path + length));
}

static ProcessSnapshot MissingProcessSnapshot(uint32_t process_id) {
  return {
      process_id,
      std::string(),
      std::string(),
      false,
      false,
      0,
  };
}

bool LaunchApplication(
    const ApplicationLaunchOptions& options,
    ApplicationProcess* process,
    std::string* error) {
  const std::wstring executable = Utf8ToWide(options.path);
  if (executable.empty()) {
    *error = "applications.launch requires a non-empty path.";
    return false;
  }

  const std::vector<std::wstring> arguments = WideArguments(options.arguments);
  std::wstring command_line = BuildCommandLine(executable, arguments);
  std::vector<wchar_t> mutable_command_line(
      command_line.begin(), command_line.end());
  mutable_command_line.push_back(L'\0');

  const std::wstring working_directory = Utf8ToWide(options.working_directory);
  const std::wstring environment_block =
      BuildEnvironmentBlock(options.environment);
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  HANDLE stdout_handle = nullptr;
  HANDLE stderr_handle = nullptr;
  if (!OpenRedirectFile(options.stdout_path, &stdout_handle, error) ||
      !OpenRedirectFile(options.stderr_path, &stderr_handle, error)) {
    if (stdout_handle != nullptr) {
      CloseHandle(stdout_handle);
    }
    if (stderr_handle != nullptr) {
      CloseHandle(stderr_handle);
    }
    return false;
  }
  const bool inherit_handles =
      stdout_handle != nullptr || stderr_handle != nullptr;
  if (inherit_handles) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput =
        stdout_handle == nullptr ? GetStdHandle(STD_OUTPUT_HANDLE)
                                 : stdout_handle;
    startup.hStdError =
        stderr_handle == nullptr ? GetStdHandle(STD_ERROR_HANDLE)
                                 : stderr_handle;
  }
  PROCESS_INFORMATION process_information = {};
  DWORD creation_flags = options.create_no_window ? CREATE_NO_WINDOW : 0;
  if (!environment_block.empty()) {
    creation_flags |= CREATE_UNICODE_ENVIRONMENT;
  }

  const BOOL created = CreateProcessW(
      nullptr, mutable_command_line.data(), nullptr, nullptr,
      inherit_handles ? TRUE : FALSE, creation_flags,
      environment_block.empty() ? nullptr
                                : const_cast<wchar_t*>(environment_block.c_str()),
      working_directory.empty() ? nullptr : working_directory.c_str(),
      &startup, &process_information);
  if (stdout_handle != nullptr) {
    CloseHandle(stdout_handle);
  }
  if (stderr_handle != nullptr) {
    CloseHandle(stderr_handle);
  }
  if (!created) {
    *error = "CreateProcessW failed.";
    return false;
  }

  process->id = static_cast<uint32_t>(process_information.dwProcessId);
  process->name = Basename(options.path);
  CloseHandle(process_information.hThread);
  CloseHandle(process_information.hProcess);
  return true;
}

bool SnapshotProcess(
    uint32_t process_id,
    ProcessSnapshot* snapshot,
    std::string* error) {
  HANDLE process =
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                  static_cast<DWORD>(process_id));
  if (process == nullptr) {
    *snapshot = MissingProcessSnapshot(process_id);
    return true;
  }
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process, &exit_code)) {
    CloseHandle(process);
    *error = "GetExitCodeProcess failed.";
    return false;
  }
  const bool running = exit_code == STILL_ACTIVE;
  const std::string path = ReadProcessPath(process);
  CloseHandle(process);
  *snapshot = {
      process_id,
      Basename(path),
      path,
      running,
      !running,
      running ? 0 : static_cast<uint32_t>(exit_code),
  };
  return true;
}

bool ListProcesses(
    const ProcessListOptions& options,
    std::vector<ProcessSnapshot>* processes,
    std::string* error) {
  DWORD needed = 0;
  std::vector<DWORD> ids(4096);
  while (true) {
    if (!EnumProcesses(ids.data(), static_cast<DWORD>(ids.size() * sizeof(DWORD)),
                       &needed)) {
      *error = "EnumProcesses failed.";
      return false;
    }
    if (needed < ids.size() * sizeof(DWORD)) {
      break;
    }
    ids.resize(ids.size() * 2);
  }

  processes->clear();
  const size_t count = needed / sizeof(DWORD);
  for (size_t index = 0; index < count; index += 1) {
    if (ids[index] == 0) {
      continue;
    }
    ProcessSnapshot snapshot = {};
    std::string snapshot_error;
    if (!SnapshotProcess(ids[index], &snapshot, &snapshot_error) ||
        !snapshot.running) {
      continue;
    }
    if (!options.name.empty() && snapshot.name != options.name) {
      continue;
    }
    processes->push_back(snapshot);
  }
  return true;
}

bool KillProcess(uint32_t process_id, std::string* error) {
  HANDLE process =
      OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process_id));
  if (process == nullptr) {
    *error = "OpenProcess for terminate failed.";
    return false;
  }
  if (!TerminateProcess(process, 1)) {
    CloseHandle(process);
    *error = "TerminateProcess failed.";
    return false;
  }
  CloseHandle(process);
  return true;
}

}  // namespace agent_rover
