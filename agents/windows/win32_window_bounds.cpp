// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_window_bounds.h"

#include <windows.h>

#include <string>

namespace agent_rover {

constexpr DWORD kDwmwaExtendedFrameBounds = 9;
constexpr wchar_t kDwmApiDllName[] = L"dwmapi.dll";

typedef HRESULT(WINAPI* DwmGetWindowAttributeProc)(
    HWND hwnd,
    DWORD dwAttribute,
    PVOID pvAttribute,
    DWORD cbAttribute);

static WindowRect RectToWindowRect(const RECT& rect) {
  return {
      static_cast<int>(rect.left),
      static_cast<int>(rect.top),
      static_cast<int>(rect.right - rect.left),
      static_cast<int>(rect.bottom - rect.top),
  };
}

static bool IsValidRect(const RECT& rect) {
  return rect.right > rect.left && rect.bottom > rect.top;
}

static bool BuildSystemDllPath(const wchar_t* dll_name, std::wstring* path) {
  wchar_t system_directory[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return false;
  }

  path->assign(system_directory, system_directory + length);
  if (!path->empty() && path->back() != L'\\' && path->back() != L'/') {
    path->push_back(L'\\');
  }
  path->append(dll_name);
  return true;
}

static bool TryReadDwmExtendedFrameBounds(HWND window, RECT* rect) {
  std::wstring path;
  if (!BuildSystemDllPath(kDwmApiDllName, &path)) {
    return false;
  }

  HMODULE module = LoadLibraryW(path.c_str());
  if (module == nullptr) {
    return false;
  }

  FARPROC raw_proc = GetProcAddress(module, "DwmGetWindowAttribute");
  if (raw_proc == nullptr) {
    FreeLibrary(module);
    return false;
  }

  DwmGetWindowAttributeProc proc =
      reinterpret_cast<DwmGetWindowAttributeProc>(raw_proc);
  RECT dwm_rect = {};
  const HRESULT result = proc(
      window, kDwmwaExtendedFrameBounds, &dwm_rect, sizeof(dwm_rect));
  FreeLibrary(module);
  if (result < 0 || !IsValidRect(dwm_rect)) {
    return false;
  }

  *rect = dwm_rect;
  return true;
}

bool ReadWindowFrameBounds(
    HWND window,
    WindowRect* bounds,
    std::string* error) {
  if (bounds == nullptr) {
    if (error != nullptr) {
      *error = "Window bounds output is null.";
    }
    return false;
  }

  RECT rect = {};
  if (!GetWindowRect(window, &rect)) {
    if (error != nullptr) {
      *error = "GetWindowRect failed.";
    }
    return false;
  }

  RECT selected_rect = rect;
  RECT dwm_rect = {};
  if (TryReadDwmExtendedFrameBounds(window, &dwm_rect)) {
    selected_rect = dwm_rect;
  }

  *bounds = RectToWindowRect(selected_rect);
  return true;
}

bool ReadWindowOuterBounds(HWND window, WindowRect* bounds, std::string* error) {
  RECT rect = {};
  if (bounds == nullptr || !GetWindowRect(window, &rect)) {
    if (error != nullptr) *error = "GetWindowRect failed.";
    return false;
  }
  *bounds = RectToWindowRect(rect);
  return true;
}

bool ReadWindowClientBounds(HWND window, WindowRect* bounds, std::string* error) {
  RECT rect = {};
  if (bounds == nullptr || !GetClientRect(window, &rect)) {
    if (error != nullptr) *error = "GetClientRect failed.";
    return false;
  }
  // Mapping a rectangle also handles mirrored (RTL) client coordinates.
  SetLastError(0);
  if (MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&rect), 2) == 0 &&
      GetLastError() != 0) {
    if (error != nullptr) *error = "MapWindowPoints failed.";
    return false;
  }
  *bounds = RectToWindowRect(rect);
  return true;
}

// Restore the agent's awareness on every return path, including API failures.
struct ThreadDpiScope {
  DPI_AWARENESS_CONTEXT (WINAPI* set_context)(DPI_AWARENESS_CONTEXT) = nullptr;
  DPI_AWARENESS_CONTEXT previous = nullptr;
  ~ThreadDpiScope() { if (previous != nullptr) set_context(previous); }
};

bool MoveWindowPhysical(HWND window, const WindowRect& bounds, std::string* error) {
  RECT rect = {bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height};
  ThreadDpiScope scope;
  if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0) {
    const auto parent = GetParent(window);
    const auto user32 = GetModuleHandleW(L"user32.dll");
    using ContextProc = DPI_AWARENESS_CONTEXT (WINAPI*)(HWND);
    using ConvertProc = BOOL (WINAPI*)(HWND, POINT*);
    const auto context = user32 == nullptr ? nullptr : reinterpret_cast<ContextProc>(
        GetProcAddress(user32, "GetWindowDpiAwarenessContext"));
    const auto convert = user32 == nullptr ? nullptr : reinterpret_cast<ConvertProc>(
        GetProcAddress(user32, "PhysicalToLogicalPointForPerMonitorDPI"));
    scope.set_context = user32 == nullptr ? nullptr : reinterpret_cast<decltype(scope.set_context)>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (context != nullptr && convert != nullptr && scope.set_context != nullptr) {
      // Child MoveWindow arguments use the parent's client units. Convert the
      // physical screen corners to that parent's logical screen coordinates,
      // then map and move while observing the same DPI coordinate space.
      // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-physicaltologicalpointforpermonitordpi
      auto* points = reinterpret_cast<POINT*>(&rect);
      if (!convert(parent, &points[0]) || !convert(parent, &points[1])) {
        if (error != nullptr) *error = "PhysicalToLogicalPointForPerMonitorDPI failed.";
        return false;
      }
      scope.previous = scope.set_context(context(parent));
      if (scope.previous == nullptr) {
        if (error != nullptr) *error = "SetThreadDpiAwarenessContext failed.";
        return false;
      }
    }
    SetLastError(0);
    if (MapWindowPoints(nullptr, GetParent(window),
                        reinterpret_cast<POINT*>(&rect), 2) == 0 &&
        GetLastError() != 0) {
      if (error != nullptr) *error = "MapWindowPoints failed.";
      return false;
    }
  }
  if (!MoveWindow(window, rect.left, rect.top, rect.right - rect.left,
                  rect.bottom - rect.top, TRUE)) {
    if (error != nullptr) *error = "MoveWindow failed.";
    return false;
  }
  return true;
}

}  // namespace agent_rover
