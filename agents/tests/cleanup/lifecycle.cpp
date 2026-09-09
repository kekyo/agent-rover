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
  return 0;
}
