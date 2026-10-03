// agent-rover - Native cleanup failure injection at Win32 call boundaries
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#include <windows.h>
#include <cstdio>
#include <string>

static bool fail_assign = false;
static bool fail_terminate = false;
static HANDLE fail_close = nullptr;

static BOOL WINAPI TestAssign(HANDLE job, HANDLE process) {
  if (fail_assign) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  return AssignProcessToJobObject(job, process);
}
static BOOL WINAPI TestTerminate(HANDLE job, UINT code) {
  if (fail_terminate) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  return TerminateJobObject(job, code);
}
static BOOL WINAPI TestClose(HANDLE handle) {
  if (handle == fail_close) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
  return CloseHandle(handle);
}

// Only this test translation unit substitutes the three failure boundaries.
#define AssignProcessToJobObject TestAssign
#define TerminateJobObject TestTerminate
#define CloseHandle TestClose
#include "../../windows/win32_process.cpp"
#undef CloseHandle
#undef TerminateJobObject
#undef AssignProcessToJobObject

int wmain(int argc, wchar_t** argv) {
  if (argc == 2 && std::wstring(argv[1]) == L"standard-handles") {
    if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_CHAR) return 51;
    for (const auto stream : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
      DWORD written = 0;
      if (!WriteFile(GetStdHandle(stream), "captured\n", 9, &written, nullptr) || written != 9) return 52;
    }
    return 0;
  }
  if (argc > 1) {
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return 2;
    WaitForSingleObject(event, 60000);
    CloseHandle(event);
    return 0;
  }
  agent_rover::ManagedProcessLaunchOptions options = {};
  options.launch.path = agent_rover::WideToUtf8(argv[0]);
  options.launch.arguments = {"child"};
  options.launch.create_no_window = true;
  options.kill_tree_on_release = true;
  agent_rover::ManagedProcess child = {};
  agent_rover::OperationError error;
  // A GUI parent has no console handles; each partial redirection must still
  // supply valid, independent standard handles to the console child.
  const auto original_input = GetStdHandle(STD_INPUT_HANDLE);
  const auto original_output = GetStdHandle(STD_OUTPUT_HANDLE);
  const auto original_error = GetStdHandle(STD_ERROR_HANDLE);
  SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE);
  SetStdHandle(STD_OUTPUT_HANDLE, INVALID_HANDLE_VALUE);
  SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
  wchar_t temporary[MAX_PATH] = {}, output_path[MAX_PATH] = {}, error_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temporary) || !GetTempFileNameW(temporary, L"ar1", 0, output_path) ||
      !GetTempFileNameW(temporary, L"ar2", 0, error_path)) return 53;
  options.launch.arguments = {"standard-handles"};
  for (int mask = 1; mask <= 3; ++mask) {
    options.launch.stdout_path = mask & 1 ? agent_rover::WideToUtf8(output_path) : "";
    options.launch.stderr_path = mask & 2 ? agent_rover::WideToUtf8(error_path) : "";
    if (!agent_rover::LaunchManagedProcess(options, &child, &error)) return 54;
    const auto process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, child.process.id);
    if (!process || WaitForSingleObject(process, 30000) != WAIT_OBJECT_0) return 55;
    DWORD code = 0;
    if (!GetExitCodeProcess(process, &code)) return 56;
    CloseHandle(process);
    if (code != 0) { std::fprintf(stderr, "Standard-handle child failed: mask=%d code=%lu\n", mask, code); return 57; }
    if (!agent_rover::ReleaseManagedProcess(child.managed_id, &error)) return 58;
  }
  SetStdHandle(STD_INPUT_HANDLE, original_input);
  SetStdHandle(STD_OUTPUT_HANDLE, original_output);
  SetStdHandle(STD_ERROR_HANDLE, original_error);
  DeleteFileW(output_path);
  DeleteFileW(error_path);
  options.launch.arguments = {"child"};
  options.launch.stdout_path.clear();
  options.launch.stderr_path.clear();
  fail_assign = true;
  if (agent_rover::LaunchManagedProcess(options, &child, &error)) {
    fail_assign = false;
    agent_rover::KillManagedProcess(child.managed_id, &error);
    agent_rover::ReleaseManagedProcess(child.managed_id, &error);
    std::fputs("Job assignment failure was accepted.\n", stderr);
    return 20;
  }
  fail_assign = false;
  if (!agent_rover::LaunchManagedProcess(options, &child, &error)) return 21;
  const auto observer = OpenProcess(SYNCHRONIZE, FALSE, child.process.id);
  fail_terminate = true;
  if (agent_rover::ReleaseManagedProcess(child.managed_id, &error)) return 22;
  agent_rover::ProcessSnapshot snapshot = {};
  if (!agent_rover::SnapshotManagedProcess(child.managed_id, &snapshot, &error) || !snapshot.running) {
    std::fputs("Failed termination lost the managed process.\n", stderr);
    return 23;
  }
  fail_terminate = false;
  if (!agent_rover::KillManagedProcess(child.managed_id, &error)) return 24;
  if (WaitForSingleObject(observer, 10000) != WAIT_OBJECT_0) return 25;
  CloseHandle(observer);
  fail_close = agent_rover::g_managed_processes.at(child.managed_id).process;
  if (agent_rover::ReleaseManagedProcess(child.managed_id, &error)) {
    std::fputs("Failed handle close was accepted.\n", stderr);
    return 26;
  }
  fail_close = nullptr;
  if (!agent_rover::ReleaseManagedProcess(child.managed_id, &error)) return 27;
  if (!agent_rover::ReleaseManagedProcess(child.managed_id, &error)) return 28;
  if (!agent_rover::LaunchManagedProcess(options, &child, &error)) return 60;
  const auto owned = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, child.process.id);
  auto& owned_entry = agent_rover::g_managed_processes.at(child.managed_id);
  CloseHandle(owned_entry.job);
  owned_entry.job = nullptr;
  // Losing an isolated worker must honor the explicit kill-on-release policy.
  const auto stopped = WaitForSingleObject(owned, 10000) == WAIT_OBJECT_0;
  if (!stopped) { TerminateProcess(owned, 201); WaitForSingleObject(owned, 30000); }
  CloseHandle(owned);
  CloseHandle(owned_entry.process);
  agent_rover::g_managed_processes.erase(child.managed_id);
  if (!stopped) { std::fputs("Lost worker left a kill-on-release process running.\n", stderr); return 61; }
  return 0;
}
