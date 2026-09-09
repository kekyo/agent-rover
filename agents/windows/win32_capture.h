// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_CAPTURE_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_CAPTURE_H

#include <string>
#include <vector>

#include "win32_windows.h"
#include "win32_desktop.h"

namespace agent_rover {

/** Window screenshot response. */
struct WindowScreenshot {
  /** PNG image bytes. */
  std::vector<unsigned char> image;
  /** Captured bounds. */
  WindowRect bounds;
  /** Visible bounds. */
  WindowRect visible_bounds;
  /** Whether capture bounds were clipped. */
  bool clipped;
};

/** Screen cursor state. */
struct ScreenCursor {
  /** Cursor x coordinate in physical screen pixels. */
  int x;
  /** Cursor y coordinate in physical screen pixels. */
  int y;
  /** Whether the cursor is visible. */
  bool visible;
};

/**
 * Captures the visible screen pixels inside window bounds.
 *
 * @param window_id HWND encoded as 0x-prefixed hexadecimal.
 * @param screenshot Receives PNG screenshot data.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool CaptureWindowScreenshot(
    const std::string& window_id,
    WindowScreenshot* screenshot,
    std::string* error);

/**
 * Reads current cursor position and visibility.
 *
 * @param cursor Receives cursor state.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ReadScreenCursor(ScreenCursor* cursor, std::string* error);

/**
 * Captures visible screen pixels inside the virtual screen or a rectangle.
 *
 * @param rect Optional requested screen rectangle, or nullptr for the whole
 * virtual screen.
 * @param screenshot Receives PNG screenshot data.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool CaptureScreenScreenshot(
    const WindowRect* rect,
    WindowScreenshot* screenshot,
    std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_CAPTURE_H
