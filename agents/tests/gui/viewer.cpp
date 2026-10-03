#include "agent_gui.h"
#include "agent_log.h"
#include <commctrl.h>
#include <cassert>
#include <cstdio>

int wmain() {
  bool exit_requested = false;
  const auto gui = agent_rover::CreateAgentGui({"127.0.0.1", 39743, true, "viewer-test-token"}, [&] { exit_requested = true; });
  const auto window = FindWindowW(L"AgentRoverLogViewer", L"agent-rover logs (39743)");
  assert(window);
  const auto banner = GetDlgItem(window, 100), token = GetDlgItem(window, 101), list = GetDlgItem(window, 102);
  assert(banner && token && list && banner != token && token != list);
  wchar_t text[512] = {};
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
