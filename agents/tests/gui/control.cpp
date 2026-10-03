#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <string>

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
    RECT before = {}, after = {};
    GetWindowRect(list, &before);
    SetWindowPos(window, nullptr, 0, 0, 1100, 800, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
