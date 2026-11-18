// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_COMMAND_LINE_H
#define AGENT_ROVER_WINDOWS_AGENT_COMMAND_LINE_H

#include <cstdint>
#include <string>
#include <vector>

#include "transfer_limits.h"

namespace agent_rover {

/** Parsed native Windows agent startup options. */
struct AgentCommandLineOptions {
  /** Bind host address. */
  std::string host;
  /** Bind TCP port. */
  uint16_t port;
  /** Whether clients must authenticate before using the session. */
  bool auth_required;
  /** Explicit auth token, or empty when the agent should generate one. */
  std::string auth_token;
  /** Maximum whole-file read and retained incoming transfer size in bytes. */
  uint64_t max_transfer_bytes = kDefaultMaxTransferBytes;
  /** Whether multiple independent driver sessions may connect at once. */
  bool multiple_connections = false;
  /** Requested Windows workspace path, or empty to use the OS temporary directory. */
  std::wstring safe_base_dir = {};
  /** Absolute INI path used by the public startup path; empty for internal modes. */
  std::wstring config_path = {};
  /** Source line of an INI workspace value, or zero for CLI/defaults. */
  size_t safe_base_dir_line = 0;
};

/** Result returned when parsing native Windows agent startup arguments. */
struct AgentCommandLineParseResult {
  /** Whether parsing completed without an error. */
  bool ok;
  /** Whether the user requested usage text instead of starting the agent. */
  bool help_requested;
  /** Parsed startup options. */
  AgentCommandLineOptions options;
  /** Human-readable parse error when ok is false. */
  std::wstring error;
};

/**
 * Quotes one Windows command line argument.
 *
 * @param argument Argument before command-line quoting.
 * @return Quoted argument suitable for CreateProcessW command lines.
 */
std::wstring QuoteCommandLineArgument(const std::wstring& argument);

/**
 * Builds a complete Windows command line.
 *
 * @param executable Executable path or command name.
 * @param arguments Command line arguments.
 * @return Command line with argv[0] followed by every argument.
 */
std::wstring BuildCommandLine(
    const std::wstring& executable,
    const std::vector<std::wstring>& arguments);

/**
 * Parses native Windows agent startup arguments.
 *
 * @param argc Argument count including argv[0].
 * @param argv Argument vector including argv[0].
 * @return Parsed options, a help request, or a human-readable parse error.
 */
AgentCommandLineParseResult ParseAgentCommandLine(
    int argc,
    const wchar_t* const argv[]);

/**
 * Resolves explicit CLI fields over a strict UTF-8 or UTF-16LE INI document.
 *
 * @param argc Argument count including argv[0].
 * @param argv Argument vector including argv[0].
 * @param document INI bytes; empty when the file does not exist.
 * @param path INI path for diagnostics and the local edit command.
 * @return Selected and validated options, a help request, or a redacted diagnostic.
 * @remarks This function performs no I/O. CLI authentication replaces both INI
 * authentication fields. Unselected INI values still undergo syntax validation.
 */
AgentCommandLineParseResult ParseAgentConfiguration(
    int argc,
    const wchar_t* const argv[],
    const std::vector<unsigned char>& document,
    const std::wstring& path);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_COMMAND_LINE_H
