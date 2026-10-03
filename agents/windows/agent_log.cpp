// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "agent_log.h"

#include <cstdio>
#include <ctime>
#include <utility>

namespace agent_rover {

static std::deque<AgentLogRecord> log_records;
static uint64_t log_sequence = 0;
static std::function<void(const AgentLogRecord&)> log_sink;

void SetAgentLogSink(std::function<void(const AgentLogRecord&)> sink) {
  log_sink = std::move(sink);
}

const std::deque<AgentLogRecord>& AgentLogRecords() { return log_records; }

static std::string CurrentLogTimestamp() {
  const std::time_t now = std::time(nullptr);
  const std::tm* utc = std::gmtime(&now);
  if (utc == nullptr) {
    return "0000-00-00T00:00:00Z";
  }

  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", utc) == 0) {
    return "0000-00-00T00:00:00Z";
  }
  return std::string(buffer);
}

static std::string NumberText(uint32_t value) {
  return std::to_string(static_cast<unsigned long>(value));
}

std::string SanitizeAgentLogField(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (const char ch : value) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    output.push_back(byte < 0x20 || byte == 0x7f ? '?' : ch);
  }
  return output;
}

std::string CreateAgentLogLine(
    const std::string& timestamp,
    const std::string& event) {
  return "agent-rover agent event: " + SanitizeAgentLogField(timestamp) + " " +
         SanitizeAgentLogField(event);
}

std::string CreateAgentConnectionAcceptedLogEvent(
    uint32_t connection_id,
    const std::string& endpoint) {
  return "connection #" + NumberText(connection_id) + " accepted from " +
         SanitizeAgentLogField(endpoint);
}

std::string CreateAgentConnectionStateLogEvent(
    uint32_t connection_id,
    const std::string& state) {
  return "connection #" + NumberText(connection_id) + " " +
         SanitizeAgentLogField(state);
}

std::string CreateAgentConnectionDisconnectedLogEvent(
    uint32_t connection_id,
    const std::string& reason) {
  return "connection #" + NumberText(connection_id) + " disconnected: " +
         SanitizeAgentLogField(reason);
}

std::string CreateAgentApplicationLaunchedLogEvent(
    uint32_t process_id,
    const std::string& name,
    const std::string& path) {
  return "application launched pid=" + NumberText(process_id) +
         " name=" + SanitizeAgentLogField(name) +
         " path=" + SanitizeAgentLogField(path);
}

std::string CreateAgentApplicationLaunchFailedLogEvent(
    const std::string& path,
    const std::string& reason) {
  return "application launch failed path=" + SanitizeAgentLogField(path) +
         " reason=" + SanitizeAgentLogField(reason);
}

std::string CreateAgentManagedProcessLaunchedLogEvent(
    uint32_t managed_id,
    uint32_t process_id,
    const std::string& name,
    const std::string& path) {
  return "managed process launched managedId=" + NumberText(managed_id) +
         " pid=" + NumberText(process_id) +
         " name=" + SanitizeAgentLogField(name) +
         " path=" + SanitizeAgentLogField(path);
}

std::string CreateAgentManagedProcessLaunchFailedLogEvent(
    const std::string& path,
    const std::string& reason) {
  return "managed process launch failed path=" + SanitizeAgentLogField(path) +
         " reason=" + SanitizeAgentLogField(reason);
}

std::string CreateAgentManagedProcessOperationLogEvent(
    const std::string& action,
    uint32_t managed_id) {
  return "managed process " + SanitizeAgentLogField(action) +
         " managedId=" + NumberText(managed_id);
}

std::string CreateAgentManagedProcessOperationFailedLogEvent(
    const std::string& action,
    uint32_t managed_id,
    const std::string& reason) {
  return "managed process " + SanitizeAgentLogField(action) +
         " failed managedId=" + NumberText(managed_id) +
         " reason=" + SanitizeAgentLogField(reason);
}

std::string CreateAgentProcessKilledLogEvent(uint32_t process_id) {
  return "process killed pid=" + NumberText(process_id);
}

std::string CreateAgentProcessKillFailedLogEvent(
    uint32_t process_id,
    const std::string& reason) {
  return "process kill failed pid=" + NumberText(process_id) +
         " reason=" + SanitizeAgentLogField(reason);
}

void PrintAgentLogEvent(const std::string& event) {
  std::string text = event.substr(0, 4096);
  if (event.size() > 4096) {
    text.resize(4084);
    // Do not leave a partial UTF-8 code point before the truncation marker.
    while (!text.empty() && (static_cast<unsigned char>(event[text.size()]) & 0xc0) == 0x80)
      text.pop_back();
    text += " [truncated]";
  }
  AgentLogRecord record = {++log_sequence, CurrentLogTimestamp(), SanitizeAgentLogField(text)};
  if (log_records.size() == 1000) log_records.pop_front();
  log_records.push_back(std::move(record));
  if (log_sink) log_sink(log_records.back());
}

}  // namespace agent_rover
