// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_windows.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "win32_util.h"
#include "win32_window_bounds.h"

namespace agent_rover {

struct EnumContext {
  std::string parent_id;
  std::vector<WindowInfo>* windows;
};

static std::string HandleToId(HWND window) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%llX",
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(window)));
  return std::string(buffer);
}

static bool IdToHandle(const std::string& value, HWND* window) {
  const char* start = value.c_str();
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    start += 2;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(start, &end, 16);
  if (end == start || *end != '\0' || parsed == 0) {
    return false;
  }
  *window = reinterpret_cast<HWND>(static_cast<uintptr_t>(parsed));
  return true;
}

static bool ResolveWindowById(
    const std::string& value,
    HWND* window,
    std::string* error) {
  if (!IdToHandle(value, window)) {
    *error = "Invalid window id.";
    return false;
  }
  if (!IsWindow(*window)) {
    *error = "Window id does not reference an existing window.";
    return false;
  }
  return true;
}

static HWND RootWindow(HWND window) {
  const HWND root = GetAncestor(window, GA_ROOT);
  return root == nullptr ? window : root;
}

static bool BelongsToActiveRoot(HWND window) {
  const HWND foreground = GetForegroundWindow();
  return foreground != nullptr && RootWindow(foreground) == RootWindow(window);
}

static bool ReadWindowPlacement(
    HWND window,
    WINDOWPLACEMENT* placement,
    std::string* error) {
  *placement = {};
  placement->length = sizeof(WINDOWPLACEMENT);
  if (!GetWindowPlacement(window, placement)) {
    if (error != nullptr) {
      *error = "GetWindowPlacement failed.";
    }
    return false;
  }
  return true;
}

static std::string Basename(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

static std::string ReadWindowTitle(HWND window) {
  const int length = GetWindowTextLengthW(window);
  if (length <= 0) {
    return std::string();
  }
  std::wstring title(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(window, &title[0], length + 1);
  if (copied <= 0) {
    return std::string();
  }
  title.resize(static_cast<size_t>(copied));
  return WideToUtf8(title);
}

static std::string ReadClassName(HWND window) {
  wchar_t class_name[256] = {};
  const int length = GetClassNameW(window, class_name, 256);
  if (length <= 0) {
    return std::string();
  }
  return WideToUtf8(std::wstring(class_name, class_name + length));
}

static int ReadControlId(HWND window) {
  if (GetParent(window) == nullptr) {
    return 0;
  }
  return GetDlgCtrlID(window);
}

static HWND FocusedWindow() {
  const HWND foreground = GetForegroundWindow();
  if (foreground != nullptr) {
    const DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    if (thread_id != 0 && GetGUIThreadInfo(thread_id, &info) &&
        info.hwndFocus != nullptr) {
      return info.hwndFocus;
    }
  }
  return GetFocus();
}

static std::string ReadProcessName(DWORD process_id) {
  if (process_id == 0) {
    return std::string();
  }
  HANDLE process =
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                  process_id);
  if (process == nullptr) {
    return std::string();
  }

  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameExW(process, nullptr, path, MAX_PATH);
  CloseHandle(process);
  if (length == 0) {
    return std::string();
  }
  return Basename(WideToUtf8(std::wstring(path, path + length)));
}

static WindowRect ReadWindowRect(HWND window) {
  WindowRect bounds = {};
  if (!ReadWindowFrameBounds(window, &bounds, nullptr)) {
    return {0, 0, 0, 0};
  }
  return bounds;
}

static WindowInfo ReadWindowInfo(HWND window, const std::string& parent_id) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  WINDOWPLACEMENT placement = {};
  const bool has_placement = ReadWindowPlacement(window, &placement, nullptr);
  return {
      HandleToId(window),
      parent_id,
      ReadWindowTitle(window),
      ReadClassName(window),
      ReadControlId(window),
      IsWindowVisible(window) != FALSE,
      BelongsToActiveRoot(window),
      FocusedWindow() == window,
      IsWindowEnabled(window) != FALSE,
      has_placement && placement.showCmd == SW_SHOWMINIMIZED,
      has_placement && placement.showCmd == SW_SHOWMAXIMIZED,
      ReadWindowRect(window),
      {static_cast<uint32_t>(process_id), ReadProcessName(process_id)},
  };
}

static bool SnapshotWindow(
    HWND window,
    WindowInfo* info,
    std::string* error) {
  if (!IsWindow(window)) {
    *error = "Window handle does not reference an existing window.";
    return false;
  }
  *info = ReadWindowInfo(window, "");
  return true;
}

static bool ActivateWindow(
    HWND window,
    WindowInfo* info,
    std::string* error) {
  const HWND root = RootWindow(window);
  WINDOWPLACEMENT placement = {};
  if (ReadWindowPlacement(root, &placement, nullptr) &&
      placement.showCmd == SW_SHOWMINIMIZED) {
    ShowWindow(root, SW_RESTORE);
  }
  if (!BringWindowToTop(root)) {
    *error = "BringWindowToTop failed.";
    return false;
  }
  SetForegroundWindow(root);
  if (!BelongsToActiveRoot(window)) {
    *error = "SetForegroundWindow was denied.";
    return false;
  }
  return SnapshotWindow(window, info, error);
}

static BOOL CALLBACK EnumWindowCallback(HWND window, LPARAM parameter) {
  EnumContext* context = reinterpret_cast<EnumContext*>(parameter);
  context->windows->push_back(ReadWindowInfo(window, context->parent_id));
  return TRUE;
}

bool ListTopLevelWindows(std::vector<WindowInfo>* windows, std::string* error) {
  windows->clear();
  EnumContext context = {"", windows};
  if (!EnumWindows(EnumWindowCallback, reinterpret_cast<LPARAM>(&context))) {
    *error = "EnumWindows failed.";
    return false;
  }
  return true;
}

bool ListChildWindows(
    const std::string& parent_id,
    std::vector<WindowInfo>* windows,
    std::string* error) {
  HWND parent = nullptr;
  if (!IdToHandle(parent_id, &parent)) {
    *error = "Invalid parent window id.";
    return false;
  }

  windows->clear();
  EnumContext context = {parent_id, windows};
  EnumChildWindows(
      parent, EnumWindowCallback, reinterpret_cast<LPARAM>(&context));
  return true;
}

bool SnapshotWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error) {
  HWND handle = nullptr;
  if (!ResolveWindowById(window_id, &handle, error)) {
    return false;
  }
  return SnapshotWindow(handle, window, error);
}

bool ActivateWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error) {
  HWND handle = nullptr;
  if (!ResolveWindowById(window_id, &handle, error)) {
    return false;
  }
  return ActivateWindow(handle, window, error);
}

bool FocusWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error) {
  HWND handle = nullptr;
  if (!ResolveWindowById(window_id, &handle, error)) {
    return false;
  }
  if (!ActivateWindow(handle, window, error)) {
    return false;
  }
  if (handle == RootWindow(handle)) {
    return SnapshotWindow(handle, window, error);
  }

  const DWORD current_thread = GetCurrentThreadId();
  const DWORD target_thread = GetWindowThreadProcessId(handle, nullptr);
  bool attached = false;
  if (target_thread != 0 && target_thread != current_thread) {
    if (!AttachThreadInput(current_thread, target_thread, TRUE)) {
      *error = "AttachThreadInput failed.";
      return false;
    }
    attached = true;
  }

  SetLastError(ERROR_SUCCESS);
  SetFocus(handle);
  const bool focused = GetFocus() == handle;
  const DWORD last_error = GetLastError();
  if (attached) {
    AttachThreadInput(current_thread, target_thread, FALSE);
  }
  if (!focused) {
    *error = "SetFocus failed with GetLastError " +
             std::to_string(static_cast<unsigned long>(last_error)) + ".";
    return false;
  }
  return SnapshotWindow(handle, window, error);
}

bool ShowWindowById(
    const std::string& window_id,
    const std::string& state,
    WindowInfo* window,
    std::string* error) {
  HWND handle = nullptr;
  if (!ResolveWindowById(window_id, &handle, error)) {
    return false;
  }

  int command = 0;
  if (state == "minimized") {
    command = SW_MINIMIZE;
  } else if (state == "maximized") {
    command = SW_MAXIMIZE;
  } else if (state == "restored") {
    command = SW_RESTORE;
  } else {
    *error = "Unsupported window show state.";
    return false;
  }
  ShowWindow(handle, command);
  return SnapshotWindow(handle, window, error);
}

bool SetWindowBoundsById(
    const std::string& window_id,
    const WindowRect& bounds,
    WindowInfo* window,
    std::string* error) {
  HWND handle = nullptr;
  if (!ResolveWindowById(window_id, &handle, error)) {
    return false;
  }
  if (bounds.width <= 0 || bounds.height <= 0) {
    *error = "window.setBounds requires positive width and height.";
    return false;
  }
  if (!MoveWindow(handle, bounds.x, bounds.y, bounds.width, bounds.height, TRUE)) {
    *error = "MoveWindow failed.";
    return false;
  }
  return SnapshotWindow(handle, window, error);
}

bool CloseWindowById(const std::string& window_id, std::string* error) {
  HWND window = nullptr;
  if (!ResolveWindowById(window_id, &window, error)) {
    return false;
  }
  if (!PostMessageW(window, WM_CLOSE, 0, 0)) {
    *error = "PostMessageW WM_CLOSE failed.";
    return false;
  }
  return true;
}

}  // namespace agent_rover
