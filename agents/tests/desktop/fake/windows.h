#ifndef WINDOWS_H
#define WINDOWS_H
#include <cstdint>
#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define MONITORINFOF_PRIMARY 1
#define MONITOR_DEFAULTTONULL 0
#define WS_EX_TOOLWINDOW 128
#define WS_EX_NOACTIVATE 0x08000000
#define WS_POPUP 0x80000000
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 reinterpret_cast<void*>(-4)
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED reinterpret_cast<void*>(-5)
using HMODULE = void*;
using FARPROC = void (*)();
HMODULE GetModuleHandleW(const wchar_t*);
FARPROC GetProcAddress(HMODULE, const char*);
using BOOL = int;
using DWORD = unsigned long;
using UINT = unsigned int;
using LPARAM = intptr_t;
using HWND = void*;
using HMONITOR = void*;
using HDC = void*;
using DPI_AWARENESS_CONTEXT = void*;
enum DPI_AWARENESS { DPI_AWARENESS_INVALID = -1, DPI_AWARENESS_UNAWARE, DPI_AWARENESS_SYSTEM_AWARE, DPI_AWARENESS_PER_MONITOR_AWARE };
struct RECT { long left; long top; long right; long bottom; };
using LPRECT = RECT*;
struct MONITORINFO { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; };
using LPMONITORINFO = MONITORINFO*;
struct MONITORINFOEXW { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; wchar_t szDevice[32]; };
BOOL EnumDisplayMonitors(HDC, const RECT*, BOOL (*callback)(HMONITOR,HDC,LPRECT,LPARAM), LPARAM);
BOOL GetMonitorInfoW(HMONITOR, LPMONITORINFO);
HMONITOR MonitorFromWindow(HWND, DWORD);
HWND CreateWindowExW(DWORD,const wchar_t*,const wchar_t*,DWORD,int,int,int,int,HWND,void*,void*,void*);
BOOL DestroyWindow(HWND);
UINT GetDpiForWindow(HWND);
DPI_AWARENESS_CONTEXT GetThreadDpiAwarenessContext();
DPI_AWARENESS_CONTEXT GetWindowDpiAwarenessContext(HWND);
DPI_AWARENESS GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT);
BOOL AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT,DPI_AWARENESS_CONTEXT);
#endif
