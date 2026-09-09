// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_WIN32_DESKTOP_H
#define AGENT_ROVER_WINDOWS_WIN32_DESKTOP_H
#include <windows.h>
#include <string>
#include <vector>
#include "win32_windows.h"

namespace agent_rover {

/** Screen monitor geometry and configured effective DPI. */
struct ScreenMonitor {
  /** Identifier valid for the current display configuration. */
  std::string id;
  /** Platform monitor name. */
  std::string name;
  /** Monitor bounds in physical screen pixels. */
  WindowRect bounds;
  /** Work area excluding taskbars and system toolbars. */
  WindowRect work_area;
  /** Whether this monitor is the primary monitor. */
  bool primary;
  /** Effective DPI, or zero when unavailable. Not physical panel density. */
  uint32_t dpi;
};

/** Two consecutive matching observations of the desktop configuration. */
struct DesktopInfo {
  /** Bounding rectangle of all monitors, including gaps between monitors. */
  WindowRect bounds;
  /** Monitors sorted by identifier. */
  std::vector<ScreenMonitor> monitors;
  /** Canonical configuration data used to derive the wire revision. */
  std::string configuration_key;
};

/**
 * Reads geometry and effective DPI using a per-monitor aware calling thread.
 * @param desktop Receives a complete observation on success.
 * @param error Receives a diagnostic on failure.
 * @return true after two consecutive matching observations, false after three
 * mismatches or an enumeration error. This is not an OS-wide atomic snapshot.
 */
bool ReadDesktop(DesktopInfo* desktop, std::string* error);

/**
 * Reads a window's associated monitor using MONITOR_DEFAULTTONULL.
 * @param window Target window; minimized windows use their pre-minimize bounds.
 * @return Identifier, or empty when offscreen or unavailable.
 */
std::string ReadWindowMonitorId(HWND window);

/**
 * Reads DPI applied to the target window, independently of monitor DPI.
 * @param window Target window.
 * @return Effective window DPI, or zero when unavailable.
 */
uint32_t ReadWindowDpi(HWND window);

/**
 * Reads the target window's DPI awareness, independently of caller awareness.
 * @param window Target window.
 * @return Wire awareness name, or empty when unavailable.
 */
std::string ReadWindowDpiAwareness(HWND window);

/**
 * Reads the virtual screen bounds.
 * @param bounds Receives physical screen bounds.
 * @param error Receives a diagnostic on failure.
 * @return true on success.
 */
bool GetScreenBounds(WindowRect* bounds, std::string* error);

/**
 * Lists monitors in the current screen session.
 * @param monitors Receives monitor geometry and DPI entries.
 * @param error Receives a diagnostic on failure.
 * @return true on success.
 */
bool ListScreenMonitors(std::vector<ScreenMonitor>* monitors, std::string* error);

}  // namespace agent_rover
#endif
