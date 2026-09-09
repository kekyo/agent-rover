// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_desktop.h"
#include <algorithm>
#include <utility>
#include "win32_util.h"

namespace agent_rover {

static WindowRect ToRect(const RECT& rect) {
  return {static_cast<int>(rect.left), static_cast<int>(rect.top),
          static_cast<int>(rect.right - rect.left), static_cast<int>(rect.bottom - rect.top)};
}

static std::string MonitorId(HMONITOR monitor, const MONITORINFOEXW& info) {
  const auto name = WideToUtf8(info.szDevice);
  return name.empty() ? "monitor-" + std::to_string(reinterpret_cast<uintptr_t>(monitor)) : name;
}

uint32_t ReadWindowDpi(HWND window) {
  return GetDpiForWindow(window);
}

std::string ReadWindowDpiAwareness(HWND window) {
  const auto context = GetWindowDpiAwarenessContext(window);
  if (context == nullptr) return {};
  if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    return "per-monitor-v2";
  if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED))
    return "unaware-gdi-scaled";
  switch (GetAwarenessFromDpiAwarenessContext(context)) {
    case DPI_AWARENESS_UNAWARE: return "unaware";
    case DPI_AWARENESS_SYSTEM_AWARE: return "system";
    case DPI_AWARENESS_PER_MONITOR_AWARE: return "per-monitor";
    default: return {};
  }
}

std::string ReadWindowMonitorId(HWND window) {
  const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
  MONITORINFOEXW info = {};
  info.cbSize = sizeof(info);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info)))
    return {};
  return MonitorId(monitor, info);
}

static uint32_t ReadMonitorDpi(HMONITOR monitor, const WindowRect& bounds) {
  // GetDpiForMonitor is not supported for per-monitor aware callers. A hidden,
  // non-activating per-monitor aware window observes the configured effective
  // DPI even when the target application is unaware or system aware.
  // https://learn.microsoft.com/windows/win32/api/shellscalingapi/nf-shellscalingapi-getdpiformonitor
  const auto probe = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      L"STATIC", L"", WS_POPUP, bounds.x + bounds.width / 2,
      bounds.y + bounds.height / 2, 1, 1, nullptr, nullptr, nullptr, nullptr);
  if (probe == nullptr) return 0;
  const auto dpi = MonitorFromWindow(probe, MONITOR_DEFAULTTONULL) == monitor
      ? ReadWindowDpi(probe) : 0;
  DestroyWindow(probe);
  return dpi;
}

struct MonitorEnumState {
  std::vector<ScreenMonitor>* monitors;
  std::string* error;
};

static BOOL CALLBACK AppendMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  auto* state = reinterpret_cast<MonitorEnumState*>(data);
  MONITORINFOEXW info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info))) {
    *state->error = "GetMonitorInfoW failed.";
    return FALSE;
  }
  const auto bounds = ToRect(info.rcMonitor);
  if (bounds.width <= 0 || bounds.height <= 0) {
    *state->error = "Monitor has an invalid screen rectangle.";
    return FALSE;
  }
  state->monitors->push_back({MonitorId(monitor, info), WideToUtf8(info.szDevice),
      bounds, ToRect(info.rcWork), (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
      ReadMonitorDpi(monitor, bounds)});
  return TRUE;
}

static std::string RectKey(const WindowRect& rect) {
  return std::to_string(rect.x) + "," + std::to_string(rect.y) + "," +
      std::to_string(rect.width) + "," + std::to_string(rect.height) + ";";
}

static bool ObserveDesktop(DesktopInfo* desktop, std::string* error) {
  desktop->monitors.clear();
  MonitorEnumState state = {&desktop->monitors, error};
  if (!EnumDisplayMonitors(nullptr, nullptr, AppendMonitor, reinterpret_cast<LPARAM>(&state))) {
    if (error->empty()) *error = "EnumDisplayMonitors failed.";
    return false;
  }
  if (desktop->monitors.empty()) {
    *error = "No desktop monitors are available.";
    return false;
  }
  std::sort(desktop->monitors.begin(), desktop->monitors.end(),
      [](const ScreenMonitor& a, const ScreenMonitor& b) { return a.id < b.id; });
  const auto& first = desktop->monitors.front().bounds;
  int left = first.x, top = first.y;
  int right = first.x + first.width, bottom = first.y + first.height;
  std::string key;
  for (const auto& monitor : desktop->monitors) {
    left = std::min(left, monitor.bounds.x);
    top = std::min(top, monitor.bounds.y);
    right = std::max(right, monitor.bounds.x + monitor.bounds.width);
    bottom = std::max(bottom, monitor.bounds.y + monitor.bounds.height);
    // Length-prefix strings so device names cannot produce ambiguous keys.
    key += std::to_string(monitor.id.size()) + ":" + monitor.id;
    key += std::to_string(monitor.name.size()) + ":" + monitor.name;
    key += RectKey(monitor.bounds) + RectKey(monitor.work_area);
    key += monitor.primary ? "1;" : "0;";
    key += std::to_string(monitor.dpi) + ";";
  }
  desktop->bounds = {left, top, right - left, bottom - top};
  desktop->configuration_key = key;
  return true;
}

bool ReadDesktop(DesktopInfo* desktop, std::string* error) {
  error->clear();
  if (GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) !=
      DPI_AWARENESS_PER_MONITOR_AWARE) {
    *error = "Physical screen coordinates require per-monitor DPI awareness.";
    return false;
  }
  DesktopInfo previous = {};
  if (!ObserveDesktop(&previous, error)) return false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    DesktopInfo current = {};
    if (!ObserveDesktop(&current, error)) return false;
    if (previous.configuration_key == current.configuration_key) {
      *desktop = std::move(current);
      return true;
    }
    previous = std::move(current);
  }
  *error = "Desktop configuration changed during capture.";
  return false;
}

bool GetScreenBounds(WindowRect* bounds, std::string* error) {
  DesktopInfo desktop = {};
  if (bounds == nullptr) { *error = "Screen bounds output is null."; return false; }
  if (!ReadDesktop(&desktop, error)) return false;
  *bounds = desktop.bounds;
  return true;
}

bool ListScreenMonitors(std::vector<ScreenMonitor>* monitors, std::string* error) {
  DesktopInfo desktop = {};
  if (monitors == nullptr) { *error = "Screen monitors output is null."; return false; }
  if (!ReadDesktop(&desktop, error)) return false;
  *monitors = std::move(desktop.monitors);
  return true;
}

}  // namespace agent_rover
