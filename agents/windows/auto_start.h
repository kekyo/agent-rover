// agent-rover - Current-user startup registration
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WINDOWS_AUTO_START_H
#define AGENT_ROVER_WINDOWS_AUTO_START_H
#include <string>

namespace agent_rover {
/** Observed HKCU Run registration; this does not report Windows startup policy. */
struct AutoStartState {
  /** Whether an AgentRover value exists, including unrecognized values. */
  bool registered = false;
  /** Whether the value contains only this copy's quoted executable path. */
  bool current_copy = false;
  /** Registered executable path without arguments; empty if unrecognized. */
  std::wstring registered_path;
};
/** Gets the current executable's absolute path without command-line arguments.
 * @param path Receives the UTF-16 path. @param error Receives a diagnostic.
 * @return Whether the path was obtained without truncation. */
bool GetAutoStartExecutablePath(std::wstring* path, std::wstring* error);
/** Reads the current user's AgentRover Run value without creating registry keys.
 * @param executable Absolute path of the current copy.
 * @param state Receives the registration and ownership state.
 * @param error Receives a diagnostic without registration arguments.
 * @return Whether the state was read successfully, including an absent value. */
bool ReadAutoStartRegistration(const std::wstring& executable, AutoStartState* state, std::wstring* error);
/** Changes the current user's Run registration and rereads the result.
 * @param executable Absolute path of the current copy.
 * @param enabled True replaces the value with this copy's quoted path; false removes only its own value.
 * @param state Receives the registration after the operation.
 * @param error Receives a diagnostic without command-line arguments.
 * @return Whether the requested operation and subsequent read succeeded.
 * @remarks Uses the 64-bit view on 64-bit Windows and enforces the Run 260-character limit.
 * No INI settings, tokens, or current command-line options are copied. */
bool WriteAutoStartRegistration(const std::wstring& executable, bool enabled, AutoStartState* state, std::wstring* error);
}
#endif
