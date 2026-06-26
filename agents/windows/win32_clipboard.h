// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_CLIPBOARD_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_CLIPBOARD_H

#include <string>

namespace agent_rover {

/**
 * Reads Unicode text from the clipboard.
 *
 * @param text Receives UTF-8 text, or an empty string when text is unavailable.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ReadClipboardText(std::string* text, std::string* error);

/**
 * Replaces the clipboard with Unicode text.
 *
 * @param text UTF-8 text to write.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool WriteClipboardText(const std::string& text, std::string* error);

/**
 * Clears all clipboard contents.
 *
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ClearClipboard(std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_CLIPBOARD_H
