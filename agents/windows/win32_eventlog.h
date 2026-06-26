// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_EVENTLOG_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_EVENTLOG_H

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

/** Event log query options. */
struct EventLogQuery {
  /** Provider/source filter, or empty for all sources. */
  std::string source;
  /** Lower ISO timestamp bound, or empty for no lower bound. */
  std::string since;
  /** Maximum entries to return. */
  uint32_t max_entries;
};

/** Event log entry returned to the driver. */
struct EventLogEntry {
  /** Event id. */
  uint32_t id;
  /** Provider/source name. */
  std::string provider;
  /** Severity label. */
  std::string level;
  /** UTC ISO timestamp. */
  std::string timestamp;
  /** Rendered or fallback message. */
  std::string message;
};

/**
 * Reads recent Application event log entries.
 *
 * @param query Query options.
 * @param entries Receives log entries.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ReadEventLogs(
    const EventLogQuery& query,
    std::vector<EventLogEntry>* entries,
    std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_EVENTLOG_H
