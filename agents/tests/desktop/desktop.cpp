#include "win32_desktop.h"
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <string>

static int enumeration = 0;
static bool changing = false;
static bool unavailable = false;
static bool info_failure = false;
static bool probe_failure = false;
static int live_probes = 0;
static bool offscreen = false;
static UINT window_dpi = 96;
static DPI_AWARENESS awareness = DPI_AWARENESS_UNAWARE;
static HMONITOR Left() { return reinterpret_cast<void*>(1); }
static HMONITOR Primary() { return reinterpret_cast<void*>(2); }
static HWND Target() { return reinterpret_cast<void*>(3); }

namespace agent_rover {
std::string WideToUtf8(const std::wstring& value) { return {value.begin(), value.end()}; }
}
BOOL GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO output) {
  if (info_failure) return FALSE;
  auto* info = reinterpret_cast<MONITORINFOEXW*>(output);
  if (monitor == Left()) {
    info->rcMonitor = {-1920, -200, 0, 880};
    info->rcWork = {-1920, -160, 0, 880};
    info->dwFlags = 0;
    std::wcscpy(info->szDevice, L"left");
  } else {
    info->rcMonitor = {0, 0, 2560, 1440};
    info->rcWork = {0, 0, 2560, 1400};
    info->dwFlags = MONITORINFOF_PRIMARY;
    std::wcscpy(info->szDevice, L"primary");
  }
  return TRUE;
}
BOOL EnumDisplayMonitors(HDC, const RECT*, BOOL (*callback)(HMONITOR,HDC,LPRECT,LPARAM), LPARAM data) {
  ++enumeration;
  // Enumeration order must not affect the configuration identity.
  if (enumeration % 2 == 0) return callback(Primary(),nullptr,nullptr,data) && callback(Left(),nullptr,nullptr,data);
  return callback(Left(),nullptr,nullptr,data) && callback(Primary(),nullptr,nullptr,data);
}
HMONITOR MonitorFromWindow(HWND hwnd, DWORD) { return hwnd == Target() ? (offscreen ? nullptr : Primary()) : hwnd; }
HWND CreateWindowExW(DWORD ex, const wchar_t*, const wchar_t*, DWORD style,
                     int x, int, int, int, HWND parent, void*, void*, void*) {
  if (style != WS_POPUP || (ex & WS_EX_NOACTIVATE) == 0 || parent != nullptr) std::abort();
  if (probe_failure) return nullptr;
  ++live_probes;
  return x < 0 ? Left() : Primary();
}
BOOL DestroyWindow(HWND) { --live_probes; return TRUE; }
UINT GetDpiForWindow(HWND hwnd) {
  if (hwnd == Target()) return window_dpi;
  if (unavailable) return 0;
  if (hwnd == Left()) return 96;
  return changing ? 120 + enumeration : 144;
}
DPI_AWARENESS_CONTEXT GetThreadDpiAwarenessContext() { return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2; }
DPI_AWARENESS_CONTEXT GetWindowDpiAwarenessContext(HWND hwnd) {
  return hwnd == Target() ? reinterpret_cast<void*>(10 + awareness) : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
}
DPI_AWARENESS GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT value) {
  return value == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ? DPI_AWARENESS_PER_MONITOR_AWARE : awareness;
}
BOOL AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT a, DPI_AWARENESS_CONTEXT b) { return a == b; }
static void Check(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
  agent_rover::DesktopInfo desktop = {};
  std::string error;
  Check(agent_rover::ReadDesktop(&desktop, &error), "read desktop");
  Check(desktop.bounds.x == -1920 && desktop.bounds.y == -200 && desktop.bounds.width == 4480 && desktop.bounds.height == 1640, "virtual bounds");
  Check(desktop.monitors.size() == 2 && desktop.monitors[0].dpi == 96 && desktop.monitors[1].dpi == 144, "mixed DPI");
  Check(desktop.monitors[0].work_area.y == -160 && desktop.monitors[1].primary, "work area and primary");
  const auto key = desktop.configuration_key;
  Check(agent_rover::ReadDesktop(&desktop, &error) && key == desktop.configuration_key, "stable identity");
  Check(agent_rover::ReadWindowMonitorId(Target()) == "primary", "target monitor");
  Check(agent_rover::ReadWindowDpiAwareness(Target()) == "unaware", "unaware target");
  Check(agent_rover::ReadWindowDpi(Target()) == 96, "target DPI differs from monitor");
  awareness = DPI_AWARENESS_SYSTEM_AWARE;
  Check(agent_rover::ReadWindowDpiAwareness(Target()) == "system", "system target");
  awareness = DPI_AWARENESS_PER_MONITOR_AWARE;
  window_dpi = 144;
  Check(agent_rover::ReadWindowDpiAwareness(Target()) == "per-monitor" && agent_rover::ReadWindowDpi(Target()) == 144, "per-monitor target");
  offscreen = true;
  Check(agent_rover::ReadWindowMonitorId(Target()).empty(), "offscreen target");
  unavailable = true;
  window_dpi = 0;
  Check(agent_rover::ReadDesktop(&desktop, &error) && desktop.monitors[1].dpi == 0 && desktop.configuration_key != key, "unknown DPI is not 96");
  Check(agent_rover::ReadWindowDpi(Target()) == 0, "unknown window DPI");
  unavailable = false;
  probe_failure = true;
  Check(agent_rover::ReadDesktop(&desktop, &error) && desktop.monitors[1].dpi == 0, "probe failure is unknown");
  probe_failure = false;
  changing = true;
  Check(!agent_rover::ReadDesktop(&desktop, &error), "changing configuration must not succeed");
  changing = false;
  info_failure = true;
  Check(!agent_rover::ReadDesktop(&desktop, &error), "enumeration failure must not return partial success");
  Check(live_probes == 0, "probe lifetime");
}
