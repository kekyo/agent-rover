// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_UTIL_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_UTIL_H

#include <string>

namespace agent_rover {

/** Tests whether a path has an absolute Windows drive or UNC root.
 * @param path UTF-16 path, including optional extended-length prefix.
 * @return Whether the path is independent of the current drive/directory. */
bool IsAbsoluteWindowsPath(const std::wstring& path);

/**
 * Converts a UTF-16 Windows string to UTF-8.
 *
 * @param value UTF-16 input.
 * @return UTF-8 output, or an empty string when conversion fails.
 */
std::string WideToUtf8(const std::wstring& value);

/**
 * Converts a UTF-8 wire string to UTF-16 for Win32 APIs.
 *
 * @param value UTF-8 input.
 * @return UTF-16 output, or an empty string when conversion fails.
 */
std::wstring Utf8ToWide(const std::string& value);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_UTIL_H
