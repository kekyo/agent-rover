// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_AGENT_LOG_H
#define AGENT_ROVER_WINDOWS_AGENT_AGENT_LOG_H

#include <cstdint>
#include <string>

namespace agent_rover {

/**
 * Replaces control characters so one logged field cannot inject extra lines.
 *
 * @param value Field text.
 * @return Sanitized field text.
 */
std::string SanitizeAgentLogField(const std::string& value);

/**
 * Creates one complete agent lifecycle log line.
 *
 * @param timestamp ISO-8601 UTC timestamp.
 * @param event Human-readable lifecycle event text.
 * @return Complete log line without a trailing newline.
 */
std::string CreateAgentLogLine(
    const std::string& timestamp,
    const std::string& event);

/**
 * Creates a connection accepted event.
 *
 * @param connection_id Agent-side connection id.
 * @param endpoint Peer endpoint such as host:port.
 * @return Event text.
 */
std::string CreateAgentConnectionAcceptedLogEvent(
    uint32_t connection_id,
    const std::string& endpoint);

/**
 * Creates a simple connection state event.
 *
 * @param connection_id Agent-side connection id.
 * @param state State text such as authenticated or ready.
 * @return Event text.
 */
std::string CreateAgentConnectionStateLogEvent(
    uint32_t connection_id,
    const std::string& state);

/**
 * Creates a connection disconnected event.
 *
 * @param connection_id Agent-side connection id.
 * @param reason Human-readable disconnect reason.
 * @return Event text.
 */
std::string CreateAgentConnectionDisconnectedLogEvent(
    uint32_t connection_id,
    const std::string& reason);

/**
 * Creates an application launched event.
 *
 * @param process_id Operating system process id.
 * @param name Process executable name.
 * @param path Launch path.
 * @return Event text.
 */
std::string CreateAgentApplicationLaunchedLogEvent(
    uint32_t process_id,
    const std::string& name,
    const std::string& path);

/**
 * Creates an application launch failure event.
 *
 * @param path Launch path.
 * @param reason Human-readable failure reason.
 * @return Event text.
 */
std::string CreateAgentApplicationLaunchFailedLogEvent(
    const std::string& path,
    const std::string& reason);

/**
 * Creates a managed process launched event.
 *
 * @param managed_id Agent-side managed process id.
 * @param process_id Operating system process id.
 * @param name Process executable name.
 * @param path Launch path.
 * @return Event text.
 */
std::string CreateAgentManagedProcessLaunchedLogEvent(
    uint32_t managed_id,
    uint32_t process_id,
    const std::string& name,
    const std::string& path);

/**
 * Creates a managed process launch failure event.
 *
 * @param path Launch path.
 * @param reason Human-readable failure reason.
 * @return Event text.
 */
std::string CreateAgentManagedProcessLaunchFailedLogEvent(
    const std::string& path,
    const std::string& reason);

/**
 * Creates a managed process operation event.
 *
 * @param action Past-tense operation text such as killed or released.
 * @param managed_id Agent-side managed process id.
 * @return Event text.
 */
std::string CreateAgentManagedProcessOperationLogEvent(
    const std::string& action,
    uint32_t managed_id);

/**
 * Creates a managed process operation failure event.
 *
 * @param action Operation text such as kill or release.
 * @param managed_id Agent-side managed process id.
 * @param reason Human-readable failure reason.
 * @return Event text.
 */
std::string CreateAgentManagedProcessOperationFailedLogEvent(
    const std::string& action,
    uint32_t managed_id,
    const std::string& reason);

/**
 * Creates a process killed event.
 *
 * @param process_id Operating system process id.
 * @return Event text.
 */
std::string CreateAgentProcessKilledLogEvent(uint32_t process_id);

/**
 * Creates a process kill failure event.
 *
 * @param process_id Operating system process id.
 * @param reason Human-readable failure reason.
 * @return Event text.
 */
std::string CreateAgentProcessKillFailedLogEvent(
    uint32_t process_id,
    const std::string& reason);

/**
 * Writes one lifecycle event to stdout.
 *
 * @param event Human-readable lifecycle event text.
 */
void PrintAgentLogEvent(const std::string& event);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_AGENT_LOG_H
