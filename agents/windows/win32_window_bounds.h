// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOW_BOUNDS_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOW_BOUNDS_H

#include <windows.h>

#include <string>

#include "win32_windows.h"

namespace agent_rover {

/**
 * Reads visible frame bounds suitable for screenshots.
 *
 * @remarks Uses DWM extended frame bounds when available, and falls back to
 * GetWindowRect on older systems or when DWM does not provide a valid visible
 * frame rectangle.
 * @param window Window handle.
 * @param bounds Receives the selected bounds.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ReadWindowFrameBounds(
    HWND window,
    WindowRect* bounds,
    std::string* error);

/**
 * Reads the outer rectangle, including invisible resize borders.
 * @param window Window handle.
 * @param bounds Receives physical screen coordinates.
 * @param error Receives a diagnostic on failure, or nullptr.
 * @return true on success. The caller must be per-monitor DPI aware.
 */
bool ReadWindowOuterBounds(HWND window, WindowRect* bounds, std::string* error);

/**
 * Reads the client rectangle in physical screen coordinates.
 * @param window Window handle.
 * @param bounds Receives the client rectangle, including empty rectangles.
 * @param error Receives a diagnostic on failure, or nullptr.
 * @return true on success. The caller must be per-monitor DPI aware.
 */
bool ReadWindowClientBounds(HWND window, WindowRect* bounds, std::string* error);

/**
 * Sets an outer rectangle using physical screen coordinates, also for children.
 * @param window Window handle.
 * @param bounds Requested outer rectangle with positive dimensions.
 * @param error Receives a diagnostic on failure, or nullptr.
 * @return true on success. The application may constrain the resulting bounds.
 */
bool MoveWindowPhysical(HWND window, const WindowRect& bounds, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOW_BOUNDS_H
