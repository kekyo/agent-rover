#include "agent_gui.h"
#include "agent_log.h"
#include <commctrl.h>
#include <shellapi.h>
#include <cassert>
#include <cstdio>

static HICON tray_icon = nullptr;
#ifdef _WIN64
#define TRAY_IMPORT "__imp_Shell_NotifyIconW"
#else
#define TRAY_IMPORT "__imp__Shell_NotifyIconW@8"
#endif
static BOOL WINAPI ObserveNotifyIcon(DWORD operation, PNOTIFYICONDATAW icon) {
  if (operation == NIM_ADD) tray_icon = icon->hIcon;
  const auto notify = reinterpret_cast<decltype(&Shell_NotifyIconW)>(
      GetProcAddress(GetModuleHandleW(L"shell32.dll"), "Shell_NotifyIconW"));
  assert(notify);
  return notify(operation, icon);
}
extern "C" {
// Observe the real shell call without adding test hooks to the viewer.
BOOL (WINAPI* TestNotifyIcon)(DWORD, PNOTIFYICONDATAW) asm(TRAY_IMPORT) = ObserveNotifyIcon;
}

int wmain(int argc, wchar_t** argv) {
  assert(argc >= 2);
  const std::wstring mode = argv[1];
  bool exit_requested = false;
  const auto gui = agent_rover::CreateAgentGui({"127.0.0.1", 39743, mode != L"no-auth", "viewer-test-token"}, [&] { exit_requested = true; });
  const auto window = FindWindowW(L"AgentRoverLogViewer", L"agent-rover logs (39743)");
  assert(window);
  const auto banner = GetDlgItem(window, 100), token = GetDlgItem(window, 101), list = GetDlgItem(window, 102);
  assert(banner && token && list && banner != token && token != list);
  wchar_t text[512] = {};
  if (mode == L"icon") {
    assert(argc == 3);
    HICON large = nullptr, small = nullptr;
    assert(ExtractIconExW(argv[2], 0, &large, &small, 1) > 0);
    assert(large && small);
    DestroyIcon(large);
    DestroyIcon(small);
    const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    assert(icon);
    assert(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON)) == icon);
    assert(tray_icon == icon);
    tray_icon = nullptr;
    SendMessageW(window, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    assert(tray_icon == icon);
    std::puts("executable, viewer and restored tray icons passed");
    return 0;
  }
  const auto copy = GetDlgItem(window, 104);
  if (mode == L"no-auth") {
    assert(!copy);
    GetWindowTextW(token, text, 512);
    assert(std::wstring(text) == L"Authentication disabled");
    std::puts("authentication disabled without a Copy button passed");
    return 0;
  }
  if (mode == L"copy") {
    assert(copy);
    SendMessageW(window, WM_COMMAND, 1, 0);
    assert(IsWindowVisible(copy) && IsWindowEnabled(copy));
    GetWindowTextW(copy, text, 512);
    assert(std::wstring(text) == L"Copy");
    assert(SetWindowPos(window, nullptr, 0, 0, 480, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    RECT token_bounds = {}, copy_bounds = {}, list_bounds = {};
    GetWindowRect(token, &token_bounds);
    GetWindowRect(copy, &copy_bounds);
    GetWindowRect(list, &list_bounds);
    assert(token_bounds.right <= copy_bounds.left && token_bounds.right > token_bounds.left);
    assert(copy_bounds.bottom <= list_bounds.top && copy_bounds.right > copy_bounds.left);
    SendMessageW(token, EM_SETSEL, 8, 11);
    SendMessageW(copy, BM_CLICK, 0, 0);
    assert(OpenClipboard(window));
    const auto data = GetClipboardData(CF_UNICODETEXT);
    assert(data);
    const auto copied = static_cast<const wchar_t*>(GlobalLock(data));
    assert(copied && std::wstring(copied) == L"viewer-test-token");
    GlobalUnlock(data);
    CloseClipboard();
    DWORD start = 0, end = 0;
    SendMessageW(token, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    assert(start == 8 && end == 11);
    std::puts("Copy button copies only the token and preserves selection passed");
    return 0;
  }
  GetWindowTextW(token, text, 512);
  assert(std::wstring(text) == L"Token: viewer-test-token");
  GetWindowTextW(banner, text, 512);
  assert(std::wstring(text).find(L"native windows agent [") != std::wstring::npos);
  for (unsigned int i = 0; i < 1100; ++i) agent_rover::PrintAgentLogEvent(std::to_string(i));
  SendMessageW(window, WM_COMMAND, 1, 0);
  assert(IsWindowVisible(window));
  assert(ListView_GetItemCount(list) == 1000);
  RECT before = {}, after = {};
  GetWindowRect(list, &before);
  assert(SetWindowPos(window, nullptr, 0, 0, 1100, 800, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
  GetWindowRect(list, &after);
  assert(after.right - after.left > before.right - before.left);
  assert(after.bottom - after.top > before.bottom - before.top);
  SendMessageW(window, WM_CLOSE, 0, 0);
  assert(!IsWindowVisible(window) && IsWindow(window));
  agent_rover::PrintAgentLogEvent("hidden event");
  SendMessageW(window, WM_COMMAND, 1, 0);
  assert(IsWindowVisible(window) && ListView_GetItemCount(list) == 1000);
  assert(agent_rover::AgentLogRecords().back().event == "hidden event");
  SendMessageW(window, WM_COMMAND, 3, 0);
  assert(exit_requested);
  std::puts("viewer: separate banner/token, 1000 rows, resize, hide/reopen, exit passed");
  return 0;
}
