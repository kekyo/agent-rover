// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { nativeTestPaths } from './helpers/native-paths';

const { repositoryDirectory, windowsAgentDirectory } = nativeTestPaths(
  import.meta.url
);

const execFileChecked = async (
  file: string,
  args: readonly string[],
  cwd: string
): Promise<void> => {
  await new Promise<void>((resolve, reject) => {
    execFile(file, args, { cwd }, (error, stdout, stderr) => {
      if (error === null) {
        resolve();
      } else {
        reject(
          new Error(`${file} failed\nstdout:\n${stdout}\nstderr:\n${stderr}`)
        );
      }
    });
  });
};

describe('native window bounds helpers', () => {
  it('uses DWM extended frame bounds when available and falls back safely', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-bounds-'));
    const fakeWindowsHeader = join(directory, 'windows.h');
    const harness = join(directory, 'window_bounds_test.cpp');
    const executable = join(directory, 'window_bounds_test');
    await writeFile(
      fakeWindowsHeader,
      String.raw`
#ifndef WINDOWS_H
#define WINDOWS_H

#include <cstddef>

#define FALSE 0
#define TRUE 1
#define MAX_PATH 260
#define WINAPI

#define GWL_STYLE -16
#define WS_CHILD 0x40000000
struct POINT { long x; long y; };
typedef long LONG_PTR;
typedef int BOOL;
typedef unsigned long DWORD;
typedef void* HMODULE;
typedef long HRESULT;
typedef void* HWND;
typedef void* DPI_AWARENESS_CONTEXT;
typedef const char* LPCSTR;
typedef const wchar_t* LPCWSTR;
typedef void* PVOID;
typedef unsigned int UINT;
typedef void (*FARPROC)();

struct RECT {
  long left;
  long top;
  long right;
  long bottom;
};

extern "C" BOOL GetWindowRect(HWND window, RECT* rect);
extern "C" BOOL GetClientRect(HWND window, RECT* rect);
extern "C" int MapWindowPoints(HWND from, HWND to, POINT* points, UINT count);
extern "C" void SetLastError(DWORD error);
extern "C" DWORD GetLastError();
extern "C" LONG_PTR GetWindowLongPtrW(HWND window, int index);
extern "C" HWND GetParent(HWND window);
extern "C" BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
extern "C" UINT GetSystemDirectoryW(wchar_t* buffer, UINT size);
extern "C" HMODULE LoadLibraryW(LPCWSTR path);
extern "C" HMODULE GetModuleHandleW(LPCWSTR name);
extern "C" FARPROC GetProcAddress(HMODULE module, LPCSTR name);
extern "C" BOOL FreeLibrary(HMODULE module);

#endif
`,
      'utf8'
    );
    await writeFile(
      harness,
      String.raw`
#include "win32_window_bounds.h"

#include <cwchar>
#include <iostream>
#include <string>

namespace {

static HWND kWindow = reinterpret_cast<HWND>(0x1234);

RECT fallback_rect = {10, 20, 210, 120};
RECT dwm_rect = {12, 22, 208, 118};
bool get_window_rect_ok = true;
bool load_library_ok = true;
bool proc_available = true;
HRESULT dwm_result = 0;
DWORD requested_attribute = 0;
DWORD requested_size = 0;
int dwm_call_count = 0;
int free_library_count = 0;
std::wstring loaded_library_path;
bool is_child = false;
DWORD last_error = 0;
RECT moved_rect = {};

void Reset() {
  fallback_rect = {10, 20, 210, 120};
  dwm_rect = {12, 22, 208, 118};
  get_window_rect_ok = true;
  load_library_ok = true;
  proc_available = true;
  dwm_result = 0;
  requested_attribute = 0;
  requested_size = 0;
  dwm_call_count = 0;
  free_library_count = 0;
  loaded_library_path.clear();
}

bool ExpectRect(
    const agent_rover::WindowRect& actual,
    int x,
    int y,
    int width,
    int height,
    const char* name) {
  if (actual.x == x && actual.y == y && actual.width == width &&
      actual.height == height) {
    return true;
  }
  std::cerr << name << " mismatch: " << actual.x << "," << actual.y << ","
            << actual.width << "," << actual.height << "\n";
  return false;
}

HRESULT WINAPI FakeDwmGetWindowAttribute(
    HWND,
    DWORD attribute,
    PVOID value,
    DWORD size) {
  dwm_call_count += 1;
  requested_attribute = attribute;
  requested_size = size;
  *static_cast<RECT*>(value) = dwm_rect;
  return dwm_result;
}

}  // namespace

extern "C" BOOL GetWindowRect(HWND, RECT* rect) {
  if (!get_window_rect_ok) {
    return FALSE;
  }
  *rect = fallback_rect;
  return TRUE;
}

extern "C" BOOL GetClientRect(HWND, RECT* rect) {
  *rect = {0, 0, 180, 70}; return TRUE;
}
extern "C" int MapWindowPoints(HWND from, HWND, POINT* points, UINT count) {
  for (UINT i = 0; i < count; ++i) {
    points[i].x += from == nullptr ? 300 : -300;
    points[i].y += from == nullptr ? -50 : 50;
  }
  return 1;
}
extern "C" void SetLastError(DWORD value) { last_error = value; }
extern "C" DWORD GetLastError() { return last_error; }
extern "C" LONG_PTR GetWindowLongPtrW(HWND, int) { return is_child ? WS_CHILD : 0; }
extern "C" HWND GetParent(HWND) { return kWindow; }
extern "C" BOOL MoveWindow(HWND, int x, int y, int width, int height, BOOL) {
  moved_rect = {x, y, x + width, y + height}; return TRUE;
}

extern "C" UINT GetSystemDirectoryW(wchar_t* buffer, UINT size) {
  const wchar_t* path = L"C:\\Windows\\System32";
  const UINT length = static_cast<UINT>(std::wcslen(path));
  if (size <= length) {
    return length + 1;
  }
  std::wmemcpy(buffer, path, length + 1);
  return length;
}

extern "C" HMODULE GetModuleHandleW(LPCWSTR) { return nullptr; }

extern "C" HMODULE LoadLibraryW(LPCWSTR path) {
  loaded_library_path = path;
  return load_library_ok ? reinterpret_cast<HMODULE>(0x5678) : nullptr;
}

extern "C" FARPROC GetProcAddress(HMODULE, LPCSTR name) {
  if (!proc_available || std::string(name) != "DwmGetWindowAttribute") {
    return nullptr;
  }
  return reinterpret_cast<FARPROC>(&FakeDwmGetWindowAttribute);
}

extern "C" BOOL FreeLibrary(HMODULE) {
  free_library_count += 1;
  return TRUE;
}

int main() {
  agent_rover::WindowRect bounds = {};
  std::string error;

  Reset();
  if (!agent_rover::ReadWindowOuterBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, 10, 20, 200, 100, "outer")) return 1;
  if (!agent_rover::ReadWindowClientBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, -300, 50, 180, 70, "client screen")) return 1;
  if (!agent_rover::MoveWindowPhysical(kWindow, {-280, 80, 120, 60}, &error) ||
      moved_rect.left != -280 || moved_rect.top != 80) return 1;
  is_child = true;
  if (!agent_rover::MoveWindowPhysical(kWindow, {-280, 80, 120, 60}, &error) ||
      moved_rect.left != 20 || moved_rect.top != 30 ||
      moved_rect.right - moved_rect.left != 120) return 1;
  is_child = false;

  Reset();
  if (!agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error)) {
    std::cerr << "DWM success path failed: " << error << "\n";
    return 1;
  }
  if (!ExpectRect(bounds, 12, 22, 196, 96, "dwm") ||
      loaded_library_path != L"C:\\Windows\\System32\\dwmapi.dll" ||
      requested_attribute != 9 || requested_size != sizeof(RECT) ||
      dwm_call_count != 1 || free_library_count != 1) {
    return 1;
  }

  Reset();
  proc_available = false;
  if (!agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, 10, 20, 200, 100, "missing proc") ||
      dwm_call_count != 0 || free_library_count != 1) {
    return 1;
  }

  Reset();
  load_library_ok = false;
  if (!agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, 10, 20, 200, 100, "missing dll") ||
      loaded_library_path != L"C:\\Windows\\System32\\dwmapi.dll" ||
      free_library_count != 0) {
    return 1;
  }

  Reset();
  dwm_result = -1;
  if (!agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, 10, 20, 200, 100, "dwm failure") ||
      dwm_call_count != 1 || free_library_count != 1) {
    return 1;
  }

  Reset();
  dwm_rect = {50, 50, 40, 70};
  if (!agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error) ||
      !ExpectRect(bounds, 10, 20, 200, 100, "invalid dwm rect") ||
      dwm_call_count != 1 || free_library_count != 1) {
    return 1;
  }

  Reset();
  get_window_rect_ok = false;
  error.clear();
  if (agent_rover::ReadWindowFrameBounds(kWindow, &bounds, &error) ||
      error != "GetWindowRect failed.") {
    std::cerr << "GetWindowRect failure was not reported correctly: " << error
              << "\n";
    return 1;
  }

  return 0;
}
`,
      'utf8'
    );

    try {
      await execFileChecked(
        'g++',
        [
          '-std=c++20',
          '-I',
          directory,
          '-I',
          windowsAgentDirectory,
          join(windowsAgentDirectory, 'win32_window_bounds.cpp'),
          harness,
          '-o',
          executable,
        ],
        repositoryDirectory
      );
      await execFileChecked(executable, [], repositoryDirectory);
      expect(true).toBe(true);
    } finally {
      await rm(directory, {
        force: true,
        recursive: true,
      });
    }
  });
});
