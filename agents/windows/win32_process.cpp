// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_process.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "command_line.h"
#include "win32_util.h"

namespace agent_rover {

struct ManagedProcessEntry {
  uint32_t managed_id;
  uint32_t process_id;
  std::string name;
  std::string path;
  HANDLE process;
  HANDLE job;
  bool kill_tree_on_release;
};

static std::map<uint32_t, ManagedProcessEntry> g_managed_processes;
static uint32_t g_next_managed_process_id = 1;

static std::string Basename(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

static std::string FileTimeToIsoUtc(const FILETIME& file_time) {
  SYSTEMTIME system_time = {};
  if (!FileTimeToSystemTime(&file_time, &system_time)) {
    return std::string();
  }
  char buffer[32] = {};
  std::snprintf(
      buffer, sizeof(buffer), "%04hu-%02hu-%02huT%02hu:%02hu:%02hu.%03huZ",
      system_time.wYear, system_time.wMonth, system_time.wDay,
      system_time.wHour, system_time.wMinute, system_time.wSecond,
      system_time.wMilliseconds);
  return std::string(buffer);
}

static std::string ReadProcessCreatedAt(HANDLE process) {
  FILETIME created_at = {};
  FILETIME exited_at = {};
  FILETIME kernel_time = {};
  FILETIME user_time = {};
  if (!GetProcessTimes(
          process, &created_at, &exited_at, &kernel_time, &user_time)) {
    return std::string();
  }
  return FileTimeToIsoUtc(created_at);
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

static bool ReadParentProcessId(
    uint32_t process_id,
    uint32_t* parent_process_id) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID == static_cast<DWORD>(process_id)) {
        *parent_process_id =
            static_cast<uint32_t>(entry.th32ParentProcessID);
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

static ProcessSnapshot MissingProcessSnapshot(uint32_t process_id) {
  return {
      process_id,
      std::string(),
      std::string(),
      false,
      0,
      std::string(),
      false,
      false,
      0,
  };
}

static bool SnapshotProcessHandle(
    uint32_t process_id,
    HANDLE process,
    const std::string& fallback_name,
    const std::string& fallback_path,
    bool has_parent_process_id,
    uint32_t parent_process_id,
    ProcessSnapshot* snapshot,
    std::string* error) {
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process, &exit_code)) {
    *error = "GetExitCodeProcess failed.";
    return false;
  }
  const bool running = exit_code == STILL_ACTIVE;
  std::string path = running ? ReadProcessPath(process) : fallback_path;
  if (path.empty()) {
    path = fallback_path;
  }
  std::string name = Basename(path);
  if (name.empty()) {
    name = fallback_name;
  }
  *snapshot = {
      process_id,
      name,
      path,
      has_parent_process_id,
      parent_process_id,
      ReadProcessCreatedAt(process),
      running,
      !running,
      running ? 0 : static_cast<uint32_t>(exit_code),
  };
  return true;
}

static bool CreateApplicationProcess(
    const ApplicationLaunchOptions& options,
    DWORD extra_creation_flags,
    PROCESS_INFORMATION* process_information,
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
  DWORD creation_flags = options.create_no_window ? CREATE_NO_WINDOW : 0;
  creation_flags |= extra_creation_flags;
  if (!environment_block.empty()) {
    creation_flags |= CREATE_UNICODE_ENVIRONMENT;
  }

  const BOOL created = CreateProcessW(
      nullptr, mutable_command_line.data(), nullptr, nullptr,
      inherit_handles ? TRUE : FALSE, creation_flags,
      environment_block.empty() ? nullptr
                                : const_cast<wchar_t*>(environment_block.c_str()),
      working_directory.empty() ? nullptr : working_directory.c_str(),
      &startup, process_information);
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

  process->id = static_cast<uint32_t>(process_information->dwProcessId);
  process->name = Basename(options.path);
  return true;
}

bool LaunchApplication(
    const ApplicationLaunchOptions& options,
    ApplicationProcess* process,
    std::string* error) {
  PROCESS_INFORMATION process_information = {};
  if (!CreateApplicationProcess(options, 0, &process_information, process, error)) {
    return false;
  }
  CloseHandle(process_information.hThread);
  CloseHandle(process_information.hProcess);
  return true;
}

bool LaunchManagedProcess(
    const ManagedProcessLaunchOptions& options,
    ManagedProcess* process,
    std::string* error) {
  HANDLE job = nullptr;
  DWORD extra_creation_flags = 0;
  if (options.kill_tree_on_release) {
    job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
      extra_creation_flags |= CREATE_SUSPENDED;
    }
  }

  PROCESS_INFORMATION process_information = {};
  ApplicationProcess application_process = {};
  if (!CreateApplicationProcess(
          options.launch, extra_creation_flags, &process_information,
          &application_process, error)) {
    if (job != nullptr) {
      CloseHandle(job);
    }
    return false;
  }

  if (job != nullptr && !AssignProcessToJobObject(
                             job, process_information.hProcess)) {
    CloseHandle(job);
    job = nullptr;
  }
  if (extra_creation_flags != 0 &&
      ResumeThread(process_information.hThread) == static_cast<DWORD>(-1)) {
    TerminateProcess(process_information.hProcess, 1);
    CloseHandle(process_information.hThread);
    CloseHandle(process_information.hProcess);
    if (job != nullptr) {
      CloseHandle(job);
    }
    *error = "ResumeThread failed.";
    return false;
  }

  const std::string process_path = ReadProcessPath(process_information.hProcess);
  if (!process_path.empty()) {
    application_process.name = Basename(process_path);
  }
  CloseHandle(process_information.hThread);

  uint32_t managed_id = 0;
  managed_id = g_next_managed_process_id;
  g_next_managed_process_id += 1;
  if (g_next_managed_process_id == 0) {
    g_next_managed_process_id = 1;
  }
  ManagedProcessEntry entry = {
      managed_id,
      application_process.id,
      application_process.name,
      process_path,
      process_information.hProcess,
      job,
      options.kill_tree_on_release,
  };
  g_managed_processes[managed_id] = entry;

  *process = {
      managed_id,
      application_process,
      options.launch.stdout_path,
      options.launch.stderr_path,
  };
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
  const std::string path = ReadProcessPath(process);
  const std::string name = Basename(path);
  uint32_t parent_process_id = 0;
  const bool has_parent_process_id =
      ReadParentProcessId(process_id, &parent_process_id);
  if (!SnapshotProcessHandle(
          process_id, process, name, path, has_parent_process_id,
          parent_process_id, snapshot, error)) {
    CloseHandle(process);
    return false;
  }
  CloseHandle(process);
  return true;
}

static bool SnapshotProcessWithKnownParent(
    uint32_t process_id,
    bool has_parent_process_id,
    uint32_t parent_process_id,
    ProcessSnapshot* snapshot,
    std::string* error) {
  HANDLE process =
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                  static_cast<DWORD>(process_id));
  if (process == nullptr) {
    *snapshot = MissingProcessSnapshot(process_id);
    return true;
  }
  const std::string path = ReadProcessPath(process);
  const std::string name = Basename(path);
  if (!SnapshotProcessHandle(
          process_id, process, name, path, has_parent_process_id,
          parent_process_id, snapshot, error)) {
    CloseHandle(process);
    return false;
  }
  CloseHandle(process);
  return true;
}

bool ListProcesses(
    const ProcessListOptions& options,
    std::vector<ProcessSnapshot>* processes,
    std::string* error) {
  HANDLE process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (process_snapshot == INVALID_HANDLE_VALUE) {
    *error = "CreateToolhelp32Snapshot failed.";
    return false;
  }

  processes->clear();
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(process_snapshot, &entry)) {
    CloseHandle(process_snapshot);
    *error = "Process32FirstW failed.";
    return false;
  }
  do {
    if (entry.th32ProcessID == 0) {
      continue;
    }
    const uint32_t process_id = static_cast<uint32_t>(entry.th32ProcessID);
    ProcessSnapshot snapshot = {};
    std::string snapshot_error;
    if (!SnapshotProcessWithKnownParent(
            process_id, true,
            static_cast<uint32_t>(entry.th32ParentProcessID), &snapshot,
            &snapshot_error) ||
        !snapshot.running) {
      continue;
    }
    if (!options.name.empty() && snapshot.name != options.name) {
      continue;
    }
    processes->push_back(snapshot);
  } while (Process32NextW(process_snapshot, &entry));
  CloseHandle(process_snapshot);
  return true;
}

bool SnapshotManagedProcess(
    uint32_t managed_id,
    ProcessSnapshot* snapshot,
    std::string* error) {
  const auto iterator = g_managed_processes.find(managed_id);
  if (iterator == g_managed_processes.end()) {
    *error = "Unknown managed process id.";
    return false;
  }
  const ManagedProcessEntry& entry = iterator->second;
  uint32_t parent_process_id = 0;
  const bool has_parent_process_id =
      ReadParentProcessId(entry.process_id, &parent_process_id);
  return SnapshotProcessHandle(
      entry.process_id, entry.process, entry.name, entry.path,
      has_parent_process_id, parent_process_id, snapshot, error);
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

bool KillManagedProcess(uint32_t managed_id, std::string* error) {
  const auto iterator = g_managed_processes.find(managed_id);
  if (iterator == g_managed_processes.end()) {
    *error = "Unknown managed process id.";
    return false;
  }
  const ManagedProcessEntry& entry = iterator->second;
  if (entry.job != nullptr && entry.kill_tree_on_release) {
    if (!TerminateJobObject(entry.job, 1)) {
      *error = "TerminateJobObject failed.";
      return false;
    }
    return true;
  }
  if (!TerminateProcess(entry.process, 1)) {
    *error = "TerminateProcess failed.";
    return false;
  }
  return true;
}

bool ReleaseManagedProcess(uint32_t managed_id, std::string* error) {
  const auto iterator = g_managed_processes.find(managed_id);
  if (iterator == g_managed_processes.end()) {
    return true;
  }
  ManagedProcessEntry entry = iterator->second;
  g_managed_processes.erase(iterator);

  if (entry.kill_tree_on_release) {
    if (entry.job != nullptr) {
      if (!TerminateJobObject(entry.job, 1)) {
        CloseHandle(entry.process);
        CloseHandle(entry.job);
        *error = "TerminateJobObject failed.";
        return false;
      }
    } else {
      ProcessSnapshot snapshot = {};
      uint32_t parent_process_id = 0;
      const bool has_parent_process_id =
          ReadParentProcessId(entry.process_id, &parent_process_id);
      if (!SnapshotProcessHandle(
              entry.process_id, entry.process, entry.name, entry.path,
              has_parent_process_id, parent_process_id, &snapshot, error)) {
        CloseHandle(entry.process);
        return false;
      }
      if (snapshot.running && !TerminateProcess(entry.process, 1)) {
        CloseHandle(entry.process);
        *error = "TerminateProcess failed.";
        return false;
      }
    }
  }

  CloseHandle(entry.process);
  if (entry.job != nullptr) {
    CloseHandle(entry.job);
  }
  return true;
}

}  // namespace agent_rover
