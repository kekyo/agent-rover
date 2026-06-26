// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_input.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "win32_util.h"

#ifndef MOUSEEVENTF_HWHEEL
#define MOUSEEVENTF_HWHEEL 0x01000
#endif

namespace agent_rover {

static std::string LowerAscii(const std::string& value) {
  std::string result = value;
  for (char& ch : result) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return result;
}

static bool SendSingleInput(INPUT* input, std::string* error) {
  const UINT sent = SendInput(1, input, sizeof(INPUT));
  if (sent != 1) {
    *error = "SendInput failed.";
    return false;
  }
  return true;
}

static bool SendVirtualKeyCode(
    WORD virtual_key,
    bool down,
    std::string* error) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = virtual_key;
  input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  return SendSingleInput(&input, error);
}

static bool VirtualKeyFromName(
    const std::string& key,
    WORD* virtual_key,
    std::string* error) {
  const std::string normalized = LowerAscii(key);
  if (normalized == "alt") {
    *virtual_key = VK_MENU;
    return true;
  }
  if (normalized == "control" || normalized == "ctrl") {
    *virtual_key = VK_CONTROL;
    return true;
  }
  if (normalized == "meta" || normalized == "win" || normalized == "windows") {
    *virtual_key = VK_LWIN;
    return true;
  }
  if (normalized == "shift") {
    *virtual_key = VK_SHIFT;
    return true;
  }
  if (normalized == "enter") {
    *virtual_key = VK_RETURN;
    return true;
  }
  if (normalized == "escape" || normalized == "esc") {
    *virtual_key = VK_ESCAPE;
    return true;
  }
  if (normalized == "tab") {
    *virtual_key = VK_TAB;
    return true;
  }
  if (normalized == "space") {
    *virtual_key = VK_SPACE;
    return true;
  }
  if (normalized == "backspace") {
    *virtual_key = VK_BACK;
    return true;
  }
  if (normalized == "delete") {
    *virtual_key = VK_DELETE;
    return true;
  }
  if (normalized == "end") {
    *virtual_key = VK_END;
    return true;
  }
  if (normalized == "home") {
    *virtual_key = VK_HOME;
    return true;
  }
  if (normalized == "arrowleft" || normalized == "left") {
    *virtual_key = VK_LEFT;
    return true;
  }
  if (normalized == "arrowright" || normalized == "right") {
    *virtual_key = VK_RIGHT;
    return true;
  }
  if (normalized == "arrowup" || normalized == "up") {
    *virtual_key = VK_UP;
    return true;
  }
  if (normalized == "arrowdown" || normalized == "down") {
    *virtual_key = VK_DOWN;
    return true;
  }
  if (normalized.size() >= 2 && normalized[0] == 'f') {
    const int number = std::atoi(normalized.c_str() + 1);
    if (number >= 1 && number <= 24) {
      *virtual_key = static_cast<WORD>(VK_F1 + number - 1);
      return true;
    }
  }
  const std::wstring wide = Utf8ToWide(key);
  if (wide.size() == 1) {
    const SHORT value = VkKeyScanW(wide[0]);
    if (value != -1) {
      *virtual_key = static_cast<WORD>(value & 0xff);
      return true;
    }
  }
  *error = "Unsupported key: " + key + ".";
  return false;
}

static bool SendVirtualKey(
    const std::string& key,
    bool down,
    std::string* error) {
  WORD virtual_key = 0;
  if (!VirtualKeyFromName(key, &virtual_key, error)) {
    return false;
  }
  return SendVirtualKeyCode(virtual_key, down, error);
}

static bool ReleaseShortcutModifiers(std::string* error) {
  const WORD modifiers[] = {
      VK_SHIFT,    VK_LSHIFT,   VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
      VK_RCONTROL, VK_MENU,     VK_LMENU,  VK_RMENU,   VK_LWIN,
      VK_RWIN,
  };
  for (const WORD modifier : modifiers) {
    if (!SendVirtualKeyCode(modifier, false, error)) {
      return false;
    }
  }
  return true;
}

static bool SendModifiers(
    const std::vector<std::string>& modifiers,
    bool down,
    std::string* error) {
  if (down) {
    for (const std::string& modifier : modifiers) {
      if (!SendVirtualKey(modifier, true, error)) {
        return false;
      }
    }
    return true;
  }
  for (auto iterator = modifiers.rbegin(); iterator != modifiers.rend();
       ++iterator) {
    if (!SendVirtualKey(*iterator, false, error)) {
      return false;
    }
  }
  return true;
}

static bool SendKeyPress(
    const std::string& key,
    const std::vector<std::string>& modifiers,
    std::string* error) {
  if (!SendModifiers(modifiers, true, error)) {
    return false;
  }
  if (!SendVirtualKey(key, true, error) ||
      !SendVirtualKey(key, false, error)) {
    SendModifiers(modifiers, false, error);
    return false;
  }
  return SendModifiers(modifiers, false, error);
}

static bool SendUnicodeText(const std::string& text, std::string* error) {
  if (!ReleaseShortcutModifiers(error)) {
    return false;
  }
  const std::wstring wide = Utf8ToWide(text);
  for (const wchar_t ch : wide) {
    INPUT down = {};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = ch;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    INPUT inputs[2] = {down, up};
    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
      *error = "SendInput unicode text failed.";
      return false;
    }
    Sleep(20);
  }
  return ReleaseShortcutModifiers(error);
}

static bool MouseButtonFlags(
    const std::string& button,
    bool down,
    DWORD* flags,
    std::string* error) {
  const std::string normalized = LowerAscii(button);
  if (normalized == "left") {
    *flags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    return true;
  }
  if (normalized == "middle") {
    *flags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    return true;
  }
  if (normalized == "right") {
    *flags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    return true;
  }
  *error = "Unsupported mouse button: " + button + ".";
  return false;
}

static bool SendMouseButton(
    const std::string& button,
    bool down,
    std::string* error) {
  DWORD flags = 0;
  if (!MouseButtonFlags(button, down, &flags, error)) {
    return false;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = flags;
  return SendSingleInput(&input, error);
}

static bool MoveMouse(const InputPoint& point, std::string* error) {
  if (!SetCursorPos(point.x, point.y)) {
    *error = "SetCursorPos failed.";
    return false;
  }
  return true;
}

static bool SendWheel(DWORD flags, int delta, std::string* error) {
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.mouseData = static_cast<DWORD>(delta);
  input.mi.dwFlags = flags;
  return SendSingleInput(&input, error);
}

bool PerformInput(const InputOperation& operation, std::string* error) {
  if (operation.kind == "keyboard.down") {
    return SendVirtualKey(operation.key, true, error);
  }
  if (operation.kind == "keyboard.up") {
    return SendVirtualKey(operation.key, false, error);
  }
  if (operation.kind == "keyboard.press") {
    return SendKeyPress(operation.key, operation.modifiers, error);
  }
  if (operation.kind == "keyboard.type") {
    return SendUnicodeText(operation.text, error);
  }
  if (operation.kind == "mouse.move") {
    return MoveMouse(operation.point, error);
  }
  if (operation.kind == "mouse.click") {
    if (!SendModifiers(operation.modifiers, true, error) ||
        !MoveMouse(operation.point, error) ||
        !SendMouseButton(operation.button, true, error) ||
        !SendMouseButton(operation.button, false, error)) {
      SendModifiers(operation.modifiers, false, error);
      return false;
    }
    return SendModifiers(operation.modifiers, false, error);
  }
  if (operation.kind == "mouse.drag") {
    if (!SendModifiers(operation.modifiers, true, error) ||
        !MoveMouse(operation.from, error) ||
        !SendMouseButton(operation.button, true, error) ||
        !MoveMouse(operation.to, error) ||
        !SendMouseButton(operation.button, false, error)) {
      SendModifiers(operation.modifiers, false, error);
      return false;
    }
    return SendModifiers(operation.modifiers, false, error);
  }
  if (operation.kind == "mouse.wheel") {
    if (operation.has_point && !MoveMouse(operation.point, error)) {
      return false;
    }
    if (operation.delta_y != 0 &&
        !SendWheel(MOUSEEVENTF_WHEEL, operation.delta_y, error)) {
      return false;
    }
    if (operation.delta_x != 0 &&
        !SendWheel(MOUSEEVENTF_HWHEEL, operation.delta_x, error)) {
      return false;
    }
    return true;
  }
  *error = "Unsupported input operation: " + operation.kind + ".";
  return false;
}

}  // namespace agent_rover
