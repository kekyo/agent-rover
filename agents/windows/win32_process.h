// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H

#include "operation_error.h"

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

/** Managed process launch request. */
struct ManagedProcessLaunchOptions {
  /** Application launch options. */
  ApplicationLaunchOptions launch;
  /** Whether release or kill should terminate the process tree. */
  bool kill_tree_on_release;
};

/** Managed process metadata returned after launch. */
struct ManagedProcess {
  /** Agent-side managed process id. */
  uint32_t managed_id;
  /** Operating system process metadata. */
  ApplicationProcess process;
  /** Captured stdout path, or empty when stdout is not captured. */
  std::string stdout_path;
  /** Captured stderr path, or empty when stderr is not captured. */
  std::string stderr_path;
};

/** Process snapshot returned to the driver. */
struct ProcessSnapshot {
  /** Operating system process id. */
  uint32_t id;
  /** Executable name when available. */
  std::string name;
  /** Full executable path when available. */
  std::string path;
  /** Whether parent_process_id is available. */
  bool has_parent_process_id;
  /** Parent operating system process id when available. */
  uint32_t parent_process_id;
  /** Process creation timestamp as an ISO string when available. */
  std::string created_at;
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
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool LaunchApplication(
    const ApplicationLaunchOptions& options,
    ApplicationProcess* process,
    OperationError* error);

/**
 * Launches a managed application and keeps its process handle.
 *
 * @param options Launch options.
 * @param process Receives managed process metadata.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool LaunchManagedProcess(
    const ManagedProcessLaunchOptions& options,
    ManagedProcess* process,
    OperationError* error);

/**
 * Reads one process snapshot.
 *
 * @param process_id Operating system process id.
 * @param snapshot Receives process snapshot.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool SnapshotProcess(
    uint32_t process_id,
    ProcessSnapshot* snapshot,
    OperationError* error);

/**
 * Reads one managed process snapshot from its retained handle.
 *
 * @param managed_id Agent-side managed process id.
 * @param snapshot Receives process snapshot.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool SnapshotManagedProcess(
    uint32_t managed_id,
    ProcessSnapshot* snapshot,
    OperationError* error);

/**
 * Lists running processes.
 *
 * @param options Process list options.
 * @param processes Receives process snapshots.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool ListProcesses(
    const ProcessListOptions& options,
    std::vector<ProcessSnapshot>* processes,
    OperationError* error);

/**
 * Terminates a process.
 *
 * @param process_id Operating system process id.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool KillProcess(uint32_t process_id, OperationError* error);

/**
 * Terminates a managed process.
 *
 * @param managed_id Agent-side managed process id.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool KillManagedProcess(uint32_t managed_id, OperationError* error);

/**
 * Releases a managed process and closes retained handles.
 *
 * @param managed_id Agent-side managed process id.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool ReleaseManagedProcess(uint32_t managed_id, OperationError* error);

/**
 * Checks all members of a managed Job, including descendants whose parent exited.
 * @param managed_id Managed resource identifier.
 * @param running Receives whether any member is still active.
 * @param error Receives a structured query failure.
 * @return Whether the state could be read.
 */
bool ManagedProcessRunning(uint32_t managed_id, bool* running, OperationError* error);

/**
 * Reads a live output snapshot or completed output after the root exits.
 * @param managed_id Managed resource identifier.
 * @param stderr_stream Selects stderr instead of stdout.
 * @param data Receives UTF-8 capture bytes.
 * @param error Receives busy while descendants still write, or another native failure.
 * @return Whether the selected output is available.
 */
bool ReadManagedCapture(uint32_t managed_id, bool stderr_stream,
                         std::vector<unsigned char>* data, OperationError* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_PROCESS_H
