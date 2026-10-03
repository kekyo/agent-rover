// agent-rover - Visible capture test fixture
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#include <windows.h>
#include <string>

static unsigned int frame = 0;
static const COLORREF colors[] = { RGB(36, 180, 90), RGB(220, 40, 70), RGB(40, 90, 220) };

static LRESULT CALLBACK DisplayWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_TIMER) {
    ++frame;
    SetWindowPos(window, nullptr, 50 + (frame % 2) * 300, 60, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint = {};
    const auto dc = BeginPaint(window, &paint);
    SetDCBrushColor(dc, colors[frame % 3]);
    FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window, message, wparam, lparam);
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2 && argc != 3) return 2;
  SetProcessDPIAware();
  WNDCLASSW type = {};
  type.lpfnWndProc = DisplayWindow;
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"AgentRoverCaptureTest";
  if (!RegisterClassW(&type)) return 3;
  const auto readyName = std::wstring(argv[1]) + L"-ready";
  const auto ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
  const auto window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, type.lpszClassName, argv[1],
      WS_POPUP | WS_VISIBLE, 50, 60, 240, 180, nullptr, nullptr, type.hInstance, nullptr);
  if (!window || !ready) return 4;
  UpdateWindow(window);
  if (argc == 3 && std::wstring(argv[2]) == L"animate") SetTimer(window, 1, 250, nullptr);
  SetEvent(ready);
  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  CloseHandle(ready);
  return 0;
}
