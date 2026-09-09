// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOWS_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOWS_H

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

/** Rectangle in physical screen pixels. */
struct WindowRect {
  /** Left coordinate. */
  int x;
  /** Top coordinate. */
  int y;
  /** Width in pixels. */
  int width;
  /** Height in pixels. */
  int height;
};

/** Process metadata for a window owner. */
struct WindowProcess {
  /** Operating system process id. */
  uint32_t id;
  /** Process executable name when available. */
  std::string name;
  /** Full executable path when available. */
  std::string path;
};

/** Window snapshot returned to the driver. */
struct WindowInfo {
  /** HWND encoded as 0x-prefixed uppercase hexadecimal. */
  std::string id;
  /** Parent window id, or empty for top-level windows. */
  std::string parent_id;
  /** Window title. */
  std::string title;
  /** Platform window class name. */
  std::string class_name;
  /** Platform control identifier, or 0 when unavailable. */
  int control_id;
  /** Whether Win32 reports the window as visible. */
  bool visible;
  /** Whether this window belongs to the active foreground root window. */
  bool active;
  /** Whether this window has keyboard focus. */
  bool focused;
  /** Whether Win32 reports the window as enabled for input. */
  bool enabled;
  /** Whether the window is minimized. */
  bool minimized;
  /** Whether the window is maximized. */
  bool maximized;
  /** Outer bounds including invisible resize borders, in physical screen pixels. */
  WindowRect bounds;
  /** Owning process metadata. */
  WindowProcess process;
  /** Visible frame bounds used for capture. */
  WindowRect frame_bounds;
  /** Client bounds in physical screen coordinates. */
  WindowRect client_bounds;
};

/**
 * Lists top-level windows.
 *
 * @param windows Receives window snapshots.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ListTopLevelWindows(std::vector<WindowInfo>* windows, std::string* error);

/**
 * Lists direct children of a window.
 *
 * @param parent_id Parent HWND encoded as 0x-prefixed hexadecimal.
 * @param windows Receives child window snapshots.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ListChildWindows(
    const std::string& parent_id,
    std::vector<WindowInfo>* windows,
    std::string* error);

/**
 * Reads the current snapshot for one window.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param window Receives the window snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool SnapshotWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error);

/**
 * Brings a window to the foreground and activates it.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param window Receives the updated window snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ActivateWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error);

/**
 * Sets keyboard focus to a window when Win32 allows it.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param window Receives the updated window snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool FocusWindowById(
    const std::string& window_id,
    WindowInfo* window,
    std::string* error);

/**
 * Changes a window show state.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param state Show state: minimized, maximized, or restored.
 * @param window Receives the updated window snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ShowWindowById(
    const std::string& window_id,
    const std::string& state,
    WindowInfo* window,
    std::string* error);

/**
 * Moves and resizes a window.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param bounds New window bounds in physical screen pixels.
 * @param window Receives the updated window snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool SetWindowBoundsById(
    const std::string& window_id,
    const WindowRect& bounds,
    WindowInfo* window,
    std::string* error);

/**
 * Requests that a window closes with WM_CLOSE.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param error Receives a human-readable error on failure.
 * @return true when the message was posted.
 */
bool CloseWindowById(const std::string& window_id, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOWS_H
