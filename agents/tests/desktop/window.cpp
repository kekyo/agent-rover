// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <windows.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 1;
  auto context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
  if (std::wcscmp(argv[1], L"unaware") == 0) context = DPI_AWARENESS_CONTEXT_UNAWARE;
  if (std::wcscmp(argv[1], L"system") == 0) context = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
  if (SetThreadDpiAwarenessContext(context) == nullptr) return 2;
  const auto window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", argv[2],
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 480,
      nullptr, nullptr, nullptr, nullptr);
  if (window == nullptr) return 3;
  const auto child = CreateWindowExW(0, L"STATIC", L"desktop child",
      WS_CHILD | WS_VISIBLE, 20, 40, 120, 60, window, nullptr, nullptr, nullptr);
  if (child == nullptr) return 4;
  std::printf("{\"systemDpi\":%u}\n", GetDpiForSystem());
  std::fflush(stdout);
  MSG message = {};
  while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return 0;
}
