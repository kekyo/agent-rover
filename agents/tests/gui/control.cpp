#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <string>

// Debugger-style fault injection, never used for product synchronization.
static int LoggerThreads(DWORD parent, const std::wstring& mode) {
  const auto processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32W process = {}; process.dwSize = sizeof(process);
  DWORD logger = 0;
  unsigned int children = 0;
  if (Process32FirstW(processes, &process)) do {
    if (process.th32ParentProcessID == parent) { logger = process.th32ProcessID; ++children; }
  } while (Process32NextW(processes, &process));
  CloseHandle(processes);
  // Before the first operation the capability probe has exited; only the logger remains.
  if (children != 1) return 20;
  const auto threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 thread = {}; thread.dwSize = sizeof(thread);
  unsigned int count = 0;
  int result = 0;
  if (Thread32First(threads, &thread)) do {
    if (thread.th32OwnerProcessID != logger) continue;
    const auto handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, thread.th32ThreadID);
    if (!handle) { result = 21; continue; }
    const auto previous = mode == L"resume-logger" ? ResumeThread(handle) : SuspendThread(handle);
    if (mode == L"logger-held") ResumeThread(handle);
    if (previous == static_cast<DWORD>(-1) || (mode != L"hold-logger" && previous == 0)) result = 22;
    CloseHandle(handle); ++count;
  } while (Thread32Next(threads, &thread));
  CloseHandle(threads);
  return result ? result : count ? 0 : 23;
}

static int VerifyLogs(const std::wstring& directory) {
  WIN32_FIND_DATAW entry = {};
  const auto search = FindFirstFileW((directory + L"\\agent-rover\\logs\\agent-rover-*.log").c_str(), &entry);
  if (search == INVALID_HANDLE_VALUE) return 30;
  std::string all;
  unsigned int files = 0;
  do {
    const auto file = CreateFileW((directory + L"\\agent-rover\\logs\\" + entry.cFileName).c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) { FindClose(search); return 31; }
    char buffer[65536]; DWORD size = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &size, nullptr) && size) all.append(buffer, size);
    CloseHandle(file); ++files;
  } while (FindNextFileW(search, &entry));
  FindClose(search);
  if (files > 5 || std::count(all.begin(), all.end(), '\n') <= 1000) return 32;
  for (const auto text : {" seq=1 ", "version=", "method=agent.capabilities phase=received", "phase=sent", "elapsedMs=", "phase=shutdown"})
    if (all.find(text) == std::string::npos) return 33;
  char secret[256] = {};
  const auto size = GetEnvironmentVariableA("AGENT_ROVER_TEST_SECRET", secret, sizeof(secret));
  if (!size || size >= sizeof(secret) || all.find(secret) != std::string::npos) return 34;
  return 0;
}

static HANDLE entered = nullptr, release = nullptr;
static bool armed = false;
static LRESULT CALLBACK BlockedWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (armed && message == WM_WINDOWPOSCHANGING) {
    SetEvent(entered);
    std::puts("entered blocking window operation");
    std::fflush(stdout);
    WaitForSingleObject(release, INFINITE);
  }
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window, message, wparam, lparam);
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const std::wstring mode = argv[1], name = argv[2];
  if (mode == L"logs") return VerifyLogs(name);
  if (mode == L"capture-file") {
    using FinalPath = DWORD (WINAPI*)(HANDLE, LPWSTR, DWORD, DWORD);
    const auto path = reinterpret_cast<FinalPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW"));
    wchar_t wide[32768] = {}; char utf8[32768] = {};
    if (!path || !path(GetStdHandle(STD_OUTPUT_HANDLE), wide, 32768, 0)) return 40;
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr)) return 41;
    std::puts(utf8); return 0;
  }
  if (mode == L"block") {
    entered = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\" + name + L"-entered").c_str());
    release = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\" + name + L"-release").c_str());
    WNDCLASSW type = {};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"AgentRoverBlockedFixture";
    type.lpfnWndProc = BlockedWindow;
    if (!entered || !release || !RegisterClassW(&type)) return 3;
    const auto window = CreateWindowExW(0, type.lpszClassName, name.c_str(), WS_OVERLAPPEDWINDOW,
        100, 100, 300, 200, nullptr, nullptr, type.hInstance, nullptr);
    if (!window) return 4;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    armed = true;
    std::puts("blocking fixture ready"); std::fflush(stdout);
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
  }
  if (mode == L"entered" || mode == L"release") {
    const auto event = OpenEventW(mode == L"entered" ? SYNCHRONIZE : EVENT_MODIFY_STATE, FALSE,
        (L"Local\\" + name + (mode == L"entered" ? L"-entered" : L"-release")).c_str());
    if (!event) return 5;
    const bool success = mode == L"entered" ? WaitForSingleObject(event, 15000) == WAIT_OBJECT_0 : SetEvent(event);
    CloseHandle(event);
    return success ? 0 : 6;
  }
  const auto window = FindWindowW(L"AgentRoverLogViewer", (L"agent-rover logs (" + name + L")").c_str());
  if (!window) return 7;
  if (mode == L"hold-logger" || mode == L"logger-held" || mode == L"resume-logger") {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    return LoggerThreads(pid, mode);
  }
  if (mode == L"log-failure") {
    wchar_t status[2048] = {};
    DWORD_PTR ignored = 0;
    if (!SendMessageTimeoutW(GetDlgItem(window, 103), WM_GETTEXT, 2048, reinterpret_cast<LPARAM>(status),
        SMTO_ABORTIFHUNG, 5000, &ignored)) return 35;
    return std::wstring(status).find(L"Save failed") != std::wstring::npos ? 0 : 36;
  }
  if (mode == L"exit") {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(window, WM_COMMAND, 3, 0);
    const auto result = WaitForSingleObject(process, 15000);
    CloseHandle(process);
    return result == WAIT_OBJECT_0 ? 0 : 8;
  }
  if (mode == L"inspect") {
    DWORD_PTR ignored = 0;
    if (!SendMessageTimeoutW(window, WM_COMMAND, 1, 0, SMTO_ABORTIFHUNG, 5000, &ignored)) return 9;
    if (!IsWindowVisible(window)) return 10;
    for (int id = 100; id <= 103; ++id) if (!GetDlgItem(window, id)) return 11;
    const auto list = GetDlgItem(window, 102);
    const auto count = SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
    if (count < 1 || count > 1000) return 12;
    RECT before = {}, after = {}, outer = {};
    GetWindowRect(list, &before);
    GetWindowRect(window, &outer);
    SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left + 50, outer.bottom - outer.top + 50,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(list, &after);
    if (after.right - after.left <= before.right - before.left || after.bottom - after.top <= before.bottom - before.top) return 13;
    SendMessageW(window, WM_CLOSE, 0, 0);
    if (IsWindowVisible(window) || !IsWindow(window)) return 14;
    SendMessageW(window, WM_COMMAND, 1, 0);
    if (!IsWindowVisible(window)) return 15;
    std::puts("GUI controls, bounded ListView, resizing, hide and reopen passed");
    return 0;
  }
  return 16;
}
