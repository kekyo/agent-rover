// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_clipboard.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "win32_util.h"

namespace agent_rover {

static LRESULT CALLBACK ClipboardWindowProc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

static HWND CreateClipboardOwnerWindow(std::string* error) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const wchar_t* class_name = L"AgentRoverClipboardOwnerWindow";
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = ClipboardWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (RegisterClassW(&window_class) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    *error = "RegisterClassW for clipboard owner failed.";
    return nullptr;
  }

  HWND window = CreateWindowExW(
      0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance,
      nullptr);
  if (window == nullptr) {
    *error = "CreateWindowExW for clipboard owner failed.";
  }
  return window;
}

static bool OpenClipboardWithRetry(HWND owner, std::string* error) {
  DWORD last_error = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 20; attempt += 1) {
    if (OpenClipboard(owner)) {
      return true;
    }
    last_error = GetLastError();
    Sleep(25);
  }
  *error = "OpenClipboard failed with GetLastError " +
           std::to_string(static_cast<unsigned long>(last_error)) + ".";
  return false;
}

bool ReadClipboardText(std::string* text, std::string* error) {
  if (text == nullptr) {
    *error = "Clipboard text output is null.";
    return false;
  }
  text->clear();
  HWND owner = CreateClipboardOwnerWindow(error);
  if (owner == nullptr) {
    return false;
  }
  if (!OpenClipboardWithRetry(owner, error)) {
    DestroyWindow(owner);
    return false;
  }
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    CloseClipboard();
    DestroyWindow(owner);
    return true;
  }
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle == nullptr) {
    CloseClipboard();
    DestroyWindow(owner);
    *error = "GetClipboardData failed.";
    return false;
  }
  const wchar_t* wide_text = static_cast<const wchar_t*>(GlobalLock(handle));
  if (wide_text == nullptr) {
    CloseClipboard();
    DestroyWindow(owner);
    *error = "GlobalLock for clipboard text failed.";
    return false;
  }
  *text = WideToUtf8(std::wstring(wide_text));
  GlobalUnlock(handle);
  CloseClipboard();
  DestroyWindow(owner);
  return true;
}

bool WriteClipboardText(const std::string& text, std::string* error) {
  const std::wstring wide_text = Utf8ToWide(text);
  if (!text.empty() && wide_text.empty()) {
    *error = "Clipboard text is invalid UTF-8.";
    return false;
  }
  const size_t bytes = (wide_text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
  if (memory == nullptr) {
    *error = "GlobalAlloc for clipboard text failed.";
    return false;
  }
  void* locked = GlobalLock(memory);
  if (locked == nullptr) {
    GlobalFree(memory);
    *error = "GlobalLock for clipboard text failed.";
    return false;
  }
  std::memcpy(locked, wide_text.c_str(), bytes);
  GlobalUnlock(memory);

  HWND owner = CreateClipboardOwnerWindow(error);
  if (owner == nullptr) {
    GlobalFree(memory);
    return false;
  }
  if (!OpenClipboardWithRetry(owner, error)) {
    DestroyWindow(owner);
    GlobalFree(memory);
    return false;
  }
  if (!EmptyClipboard()) {
    CloseClipboard();
    DestroyWindow(owner);
    GlobalFree(memory);
    *error = "EmptyClipboard failed.";
    return false;
  }
  if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    CloseClipboard();
    DestroyWindow(owner);
    GlobalFree(memory);
    *error = "SetClipboardData failed.";
    return false;
  }
  memory = nullptr;
  CloseClipboard();
  DestroyWindow(owner);
  return true;
}

bool ClearClipboard(std::string* error) {
  HWND owner = CreateClipboardOwnerWindow(error);
  if (owner == nullptr) {
    return false;
  }
  if (!OpenClipboardWithRetry(owner, error)) {
    DestroyWindow(owner);
    return false;
  }
  if (!EmptyClipboard()) {
    CloseClipboard();
    DestroyWindow(owner);
    *error = "EmptyClipboard failed.";
    return false;
  }
  CloseClipboard();
  DestroyWindow(owner);
  return true;
}

}  // namespace agent_rover
