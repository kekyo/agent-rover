// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_util.h"

#include <windows.h>

namespace agent_rover {

bool IsAbsoluteWindowsPath(const std::wstring& path) {
  const auto separator = [](wchar_t ch) { return ch == L'\\' || ch == L'/'; };
  auto value = path;
  if (value.rfind(L"\\\\?\\UNC\\", 0) == 0) value = L"\\\\" + value.substr(8);
  else if (value.rfind(L"\\\\?\\", 0) == 0) value = value.substr(4);
  if (value.size() >= 3 && ((value[0] >= L'A' && value[0] <= L'Z') ||
      (value[0] >= L'a' && value[0] <= L'z')) && value[1] == L':' && separator(value[2])) return true;
  if (value.size() < 5 || !separator(value[0]) || !separator(value[1]) ||
      value[2] == L'.' || value[2] == L'?' || separator(value[2])) return false;
  const auto share = value.find_first_of(L"\\/", 2);
  return share != std::wstring::npos && share + 1 < value.size() && !separator(value[share + 1]);
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return std::string();
  }
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0,
      nullptr, nullptr);
  if (length <= 0) {
    return std::string();
  }
  std::string result(static_cast<size_t>(length), '\0');
  const int written = WideCharToMultiByte(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
      length, nullptr, nullptr);
  if (written <= 0) {
    return std::string();
  }
  return result;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return std::wstring();
  }
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring result(static_cast<size_t>(length), L'\0');
  const int written = MultiByteToWideChar(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
      length);
  if (written <= 0) {
    return std::wstring();
  }
  return result;
}

}  // namespace agent_rover
