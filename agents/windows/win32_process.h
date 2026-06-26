// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agent_rover {

/** Application launch request. */
struct ApplicationLaunchOptions {
  /** Executable path or command name. */
  std::string path;
  /** Command line arguments. */
  std::vector<std::string> arguments;
  /** Working directory, or empty to inherit the current directory. */
  std::string working_directory;
  /** Environment variables to add or override. */
  std::map<std::string, std::string> environment;
  /** File path receiving stdout, or empty to inherit stdout. */
  std::string stdout_path;
  /** File path receiving stderr, or empty to inherit stderr. */
  std::string stderr_path;
  /** Whether to suppress console window creation. */
  bool create_no_window;
};

/** Process metadata returned after launch. */
struct ApplicationProcess {
  /** Operating system process id. */
  uint32_t id;
  /** Executable name. */
  std::string name;
};

/** Process snapshot returned to the driver. */
struct ProcessSnapshot {
  /** Operating system process id. */
  uint32_t id;
  /** Executable name when available. */
  std::string name;
  /** Full executable path when available. */
  std::string path;
  /** Whether the process is still running. */
  bool running;
  /** Whether exit_code is available. */
  bool has_exit_code;
  /** Exit code when available. */
  uint32_t exit_code;
};

/** Process list request options. */
struct ProcessListOptions {
  /** Optional executable name filter. */
  std::string name;
};

/**
 * Launches an application with CreateProcessW.
 *
 * @param options Launch options.
 * @param process Receives launched process metadata.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool LaunchApplication(
    const ApplicationLaunchOptions& options,
    ApplicationProcess* process,
    std::string* error);

/**
 * Reads one process snapshot.
 *
 * @param process_id Operating system process id.
 * @param snapshot Receives process snapshot.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool SnapshotProcess(
    uint32_t process_id,
    ProcessSnapshot* snapshot,
    std::string* error);

/**
 * Lists running processes.
 *
 * @param options Process list options.
 * @param processes Receives process snapshots.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ListProcesses(
    const ProcessListOptions& options,
    std::vector<ProcessSnapshot>* processes,
    std::string* error);

/**
 * Terminates a process.
 *
 * @param process_id Operating system process id.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool KillProcess(uint32_t process_id, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H
