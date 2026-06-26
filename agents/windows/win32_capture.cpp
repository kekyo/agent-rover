// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_capture.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "png_encoder.h"
#include "win32_util.h"
#include "win32_window_bounds.h"

#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif
#ifndef SM_XVIRTUALSCREEN
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#endif

namespace agent_rover {

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

static WindowRect RectToWindowRect(const RECT& rect) {
  return {
      rect.left,
      rect.top,
      std::max<int>(1, static_cast<int>(rect.right - rect.left)),
      std::max<int>(1, static_cast<int>(rect.bottom - rect.top)),
  };
}

static WindowRect VirtualScreenRect() {
  return {
      GetSystemMetrics(SM_XVIRTUALSCREEN),
      GetSystemMetrics(SM_YVIRTUALSCREEN),
      std::max<int>(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)),
      std::max<int>(1, GetSystemMetrics(SM_CYVIRTUALSCREEN)),
  };
}

static bool IntersectRectangles(
    const WindowRect& first,
    const WindowRect& second,
    WindowRect* output) {
  const int left = std::max(first.x, second.x);
  const int top = std::max(first.y, second.y);
  const int right = std::min(first.x + first.width, second.x + second.width);
  const int bottom =
      std::min(first.y + first.height, second.y + second.height);
  if (right <= left || bottom <= top) {
    return false;
  }
  output->x = left;
  output->y = top;
  output->width = right - left;
  output->height = bottom - top;
  return true;
}

static std::vector<unsigned char> BgraToRgba(
    const std::vector<unsigned char>& bgra) {
  std::vector<unsigned char> rgba(bgra.size());
  for (size_t index = 0; index < bgra.size(); index += 4) {
    rgba[index] = bgra[index + 2];
    rgba[index + 1] = bgra[index + 1];
    rgba[index + 2] = bgra[index];
    rgba[index + 3] = 255;
  }
  return rgba;
}

static bool CaptureScreenRect(
    const WindowRect& bounds,
    WindowScreenshot* screenshot,
    std::string* error) {
  if (bounds.width <= 0 || bounds.height <= 0) {
    *error = "Capture bounds width and height must be positive.";
    return false;
  }
  WindowRect visible_bounds = {};
  if (!IntersectRectangles(bounds, VirtualScreenRect(), &visible_bounds)) {
    *error = "Capture bounds are outside the virtual screen.";
    return false;
  }

  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    *error = "GetDC failed.";
    return false;
  }
  HDC memory = CreateCompatibleDC(screen);
  if (memory == nullptr) {
    ReleaseDC(nullptr, screen);
    *error = "CreateCompatibleDC failed.";
    return false;
  }
  HBITMAP bitmap =
      CreateCompatibleBitmap(screen, visible_bounds.width, visible_bounds.height);
  if (bitmap == nullptr) {
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    *error = "CreateCompatibleBitmap failed.";
    return false;
  }
  HGDIOBJ old_object = SelectObject(memory, bitmap);
  const BOOL copied =
      BitBlt(memory, 0, 0, visible_bounds.width, visible_bounds.height, screen,
             visible_bounds.x, visible_bounds.y, SRCCOPY | CAPTUREBLT);
  SelectObject(memory, old_object);
  if (!copied) {
    const DWORD last_error = GetLastError();
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    *error = "BitBlt failed with GetLastError " +
             std::to_string(static_cast<unsigned long>(last_error)) + ".";
    return false;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = visible_bounds.width;
  info.bmiHeader.biHeight = -visible_bounds.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::vector<unsigned char> bgra(
      static_cast<size_t>(visible_bounds.width) * visible_bounds.height * 4);
  const int scan_lines = GetDIBits(
      memory, bitmap, 0, static_cast<UINT>(visible_bounds.height), bgra.data(),
      &info, DIB_RGB_COLORS);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  if (scan_lines != visible_bounds.height) {
    *error = "GetDIBits failed.";
    return false;
  }

  const std::vector<unsigned char> rgba = BgraToRgba(bgra);
  std::vector<unsigned char> png;
  if (!EncodePngRgba(
          static_cast<uint32_t>(visible_bounds.width),
          static_cast<uint32_t>(visible_bounds.height), rgba, &png, error)) {
    return false;
  }
  screenshot->image = png;
  screenshot->bounds = bounds;
  screenshot->visible_bounds = visible_bounds;
  screenshot->clipped =
      bounds.x != visible_bounds.x || bounds.y != visible_bounds.y ||
      bounds.width != visible_bounds.width ||
      bounds.height != visible_bounds.height;
  return true;
}

bool CaptureWindowScreenshot(
    const std::string& window_id,
    WindowScreenshot* screenshot,
    std::string* error) {
  HWND window = nullptr;
  if (!IdToHandle(window_id, &window)) {
    *error = "Invalid window id.";
    return false;
  }
  WindowRect bounds = {};
  if (!ReadWindowFrameBounds(window, &bounds, error)) {
    return false;
  }
  return CaptureScreenRect(bounds, screenshot, error);
}

bool GetScreenBounds(WindowRect* bounds, std::string* error) {
  if (bounds == nullptr) {
    *error = "Screen bounds output is null.";
    return false;
  }
  *bounds = VirtualScreenRect();
  return true;
}

struct MonitorEnumState {
  std::vector<ScreenMonitor>* monitors;
  std::string* error;
};

static BOOL CALLBACK AppendMonitorInfo(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM data) {
  MonitorEnumState* state = reinterpret_cast<MonitorEnumState*>(data);
  MONITORINFOEXW info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info))) {
    *state->error = "GetMonitorInfoW failed.";
    return FALSE;
  }
  const std::string fallback_id =
      "monitor-" + std::to_string(state->monitors->size() + 1);
  const std::string name = WideToUtf8(std::wstring(info.szDevice));
  ScreenMonitor entry = {};
  entry.id = name.empty() ? fallback_id : name;
  entry.name = name;
  entry.bounds = RectToWindowRect(info.rcMonitor);
  entry.work_area = RectToWindowRect(info.rcWork);
  entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  entry.scale_factor = 1.0;
  state->monitors->push_back(entry);
  return TRUE;
}

bool ListScreenMonitors(
    std::vector<ScreenMonitor>* monitors,
    std::string* error) {
  if (monitors == nullptr) {
    *error = "Screen monitors output is null.";
    return false;
  }
  monitors->clear();
  MonitorEnumState state = {monitors, error};
  if (!EnumDisplayMonitors(
          nullptr, nullptr, AppendMonitorInfo, reinterpret_cast<LPARAM>(&state))) {
    if (error->empty()) {
      *error = "EnumDisplayMonitors failed.";
    }
    return false;
  }
  return true;
}

bool ReadScreenCursor(ScreenCursor* cursor, std::string* error) {
  if (cursor == nullptr) {
    *error = "Screen cursor output is null.";
    return false;
  }
  CURSORINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetCursorInfo(&info)) {
    *error = "GetCursorInfo failed.";
    return false;
  }
  cursor->x = info.ptScreenPos.x;
  cursor->y = info.ptScreenPos.y;
  cursor->visible = (info.flags & CURSOR_SHOWING) != 0;
  return true;
}

bool CaptureScreenScreenshot(
    const WindowRect* rect,
    WindowScreenshot* screenshot,
    std::string* error) {
  const WindowRect bounds = rect == nullptr ? VirtualScreenRect() : *rect;
  return CaptureScreenRect(bounds, screenshot, error);
}

}  // namespace agent_rover
