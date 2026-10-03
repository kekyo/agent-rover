// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "agent_gui.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include "agent_log.h"
#include "version_banner.h"
#include "win32_util.h"

namespace agent_rover {

static constexpr UINT kTrayMessage = WM_APP + 41;
static constexpr UINT kLogTimer = 1;
static constexpr UINT kShowLogs = 1, kOpenLogs = 2, kExit = 3;

struct AgentGui {
  HWND window = nullptr, banner = nullptr, token = nullptr, list = nullptr, status = nullptr;
  NOTIFYICONDATAW tray = {};
  UINT taskbar_created = 0;
  bool refresh_pending = false;
  uint64_t first_sequence = 0;
  std::wstring cell;
  std::function<void()> exit, open_logs;
  ~AgentGui() {
    Shell_NotifyIconW(NIM_DELETE, &tray);
    if (window) DestroyWindow(window);
  }
};

static void Layout(AgentGui* gui) {
  RECT bounds = {};
  GetClientRect(gui->window, &bounds);
  const int width = std::max(0L, bounds.right - 20);
  MoveWindow(gui->banner, 10, 10, width, 72, TRUE);
  MoveWindow(gui->token, 10, 88, width, 24, TRUE);
  MoveWindow(gui->list, 10, 122, width, std::max(0L, bounds.bottom - 165), TRUE);
  MoveWindow(gui->status, 10, std::max(124L, bounds.bottom - 33), width, 28, TRUE);
  ListView_SetColumnWidth(gui->list, 2, std::max(160, width - 280));
}

static void RefreshLogs(AgentGui* gui) {
  const auto& records = AgentLogRecords();
  const int old_count = ListView_GetItemCount(gui->list);
  const int top = ListView_GetTopIndex(gui->list);
  const bool at_end = old_count == 0 || top + ListView_GetCountPerPage(gui->list) >= old_count - 1;
  const uint64_t first = records.empty() ? 0 : records.front().sequence;
  if (first != gui->first_sequence) {
    // Ring indices must not turn a selected old event into a different event.
    ListView_SetItemState(gui->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  }
  gui->first_sequence = first;
  ListView_SetItemCountEx(gui->list, records.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
  InvalidateRect(gui->list, nullptr, FALSE);
  if (at_end && !records.empty()) ListView_EnsureVisible(gui->list, records.size() - 1, FALSE);
}

static void ShowLogs(AgentGui* gui) {
  RefreshLogs(gui);
  ShowWindow(gui->window, SW_SHOWNORMAL);
  SetForegroundWindow(gui->window);
}

static void ShowTrayMenu(AgentGui* gui) {
  const auto menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kShowLogs, L"Show logs");
  AppendMenuW(menu, MF_STRING | (gui->open_logs ? 0 : MF_GRAYED), kOpenLogs, L"Open log folder");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kExit, L"Exit");
  POINT point = {};
  GetCursorPos(&point);
  SetForegroundWindow(gui->window);
  const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
      point.x, point.y, 0, gui->window, nullptr);
  DestroyMenu(menu);
  PostMessageW(gui->window, WM_NULL, 0, 0);
  if (command) PostMessageW(gui->window, WM_COMMAND, command, 0);
}

static LRESULT CALLBACK ViewerWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  const auto gui = reinterpret_cast<AgentGui*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!gui) return DefWindowProcW(window, message, wparam, lparam);
  if (message == gui->taskbar_created && gui->taskbar_created) {
    Shell_NotifyIconW(NIM_ADD, &gui->tray);
    return 0;
  }
  switch (message) {
    case WM_SIZE: if (gui->list) Layout(gui); return 0;
    case WM_GETMINMAXINFO: {
      const auto info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {480, 300};
      return 0;
    }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wparam && gui->exit) gui->exit(); return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == kShowLogs) ShowLogs(gui);
      else if (LOWORD(wparam) == kOpenLogs && gui->open_logs) gui->open_logs();
      else if (LOWORD(wparam) == kExit && gui->exit) gui->exit();
      return 0;
    case kTrayMessage:
      if (lparam == WM_LBUTTONDBLCLK) ShowLogs(gui);
      else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) ShowTrayMenu(gui);
      return 0;
    case WM_TIMER:
      if (wparam == kLogTimer) {
        KillTimer(window, kLogTimer);
        gui->refresh_pending = false;
        if (IsWindowVisible(window)) RefreshLogs(gui);
      }
      return 0;
    case WM_NOTIFY: {
      const auto info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
      if (info->hdr.hwndFrom == gui->list && info->hdr.code == LVN_GETDISPINFOW && (info->item.mask & LVIF_TEXT)) {
        const auto& records = AgentLogRecords();
        if (info->item.iItem >= 0 && static_cast<size_t>(info->item.iItem) < records.size()) {
          const auto& record = records[info->item.iItem];
          gui->cell = Utf8ToWide(info->item.iSubItem == 0 ? record.timestamp :
              info->item.iSubItem == 1 ? std::to_string(record.sequence) : record.event);
          lstrcpynW(info->item.pszText, gui->cell.c_str(), info->item.cchTextMax);
        }
      }
      return 0;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

AgentGuiHandle CreateAgentGui(const ServerOptions& options, std::function<void()> exit) {
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
  if (!InitCommonControlsEx(&controls)) throw std::runtime_error("InitCommonControlsEx failed.");
  auto gui = std::make_shared<AgentGui>();
  gui->exit = std::move(exit);
  WNDCLASSW type = {};
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpfnWndProc = ViewerWindowProc;
  type.lpszClassName = L"AgentRoverLogViewer";
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  type.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("RegisterClassW(log viewer) failed.");
  const auto title = L"agent-rover logs (" + std::to_wstring(options.port) + L")";
  gui->window = CreateWindowExW(0, type.lpszClassName, title.c_str(),
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, nullptr, nullptr, type.hInstance, gui.get());
  if (!gui->window) throw std::runtime_error("CreateWindowExW(log viewer) failed.");
  const auto create = [&](const wchar_t* name, const std::wstring& text, DWORD style, int id) {
    const auto control = CreateWindowExW(0, name, text.c_str(), WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, gui->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), type.hInstance, nullptr);
    if (!control) throw std::runtime_error("CreateWindowExW(log control) failed.");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    return control;
  };
  gui->banner = create(L"STATIC", Utf8ToWide(BuildAgentVersionBanner()), SS_LEFT, 100);
  gui->token = create(L"EDIT", options.auth_required ? L"Token: " + Utf8ToWide(options.auth_token) :
      L"Authentication disabled", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 101);
  gui->list = create(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, 102);
  SendMessageW(gui->list, LVM_SETUNICODEFORMAT, TRUE, 0);
  ListView_SetExtendedListViewStyle(gui->list, LVS_EX_FULLROWSELECT);
  const wchar_t* labels[] = {L"UTC time", L"Sequence", L"Event"};
  for (int index = 0; index < 3; ++index) {
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(labels[index]);
    column.cx = index == 0 ? 185 : index == 1 ? 85 : 500;
    SendMessageW(gui->list, LVM_INSERTCOLUMNW, index, reinterpret_cast<LPARAM>(&column));
  }
  gui->status = create(L"STATIC", L"Starting...", SS_LEFT, 103);
  gui->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  // With the XP SDK target, the structure ends after guidItem (the V3 layout).
  gui->tray.cbSize = sizeof(gui->tray);
  gui->tray.hWnd = gui->window;
  gui->tray.uID = 1;
  gui->tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  gui->tray.uCallbackMessage = kTrayMessage;
  gui->tray.hIcon = type.hIcon;
  lstrcpynW(gui->tray.szTip, L"agent-rover", 128);
  if (!Shell_NotifyIconW(NIM_ADD, &gui->tray)) ShowLogs(gui.get());
  Layout(gui.get());
  return gui;
}

void NotifyAgentGuiLog(const AgentGuiHandle& gui) {
  if (!gui->refresh_pending) {
    gui->refresh_pending = true;
    SetTimer(gui->window, kLogTimer, 50, nullptr);
  }
}

void SetAgentGuiStatus(const AgentGuiHandle& gui, const std::string& status) {
  SetWindowTextW(gui->status, Utf8ToWide(status).c_str());
}

void SetAgentGuiOpenLogs(const AgentGuiHandle& gui, std::function<void()> open) {
  gui->open_logs = std::move(open);
}

}  // namespace agent_rover
