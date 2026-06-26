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
 * Reads window bounds suitable for snapshots and screenshots.
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

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_WINDOW_BOUNDS_H
