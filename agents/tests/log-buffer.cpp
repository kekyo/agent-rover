#include "agent_log.h"
#include <cassert>
#include <string>

int main() {
  unsigned int delivered = 0;
  agent_rover::SetAgentLogSink([&](const agent_rover::AgentLogRecord& record) {
    assert(record.sequence == ++delivered);
  });
  for (unsigned int i = 0; i < 1100; ++i)
    agent_rover::PrintAgentLogEvent("record " + std::to_string(i));
  const auto& records = agent_rover::AgentLogRecords();
  assert(records.size() == 1000);
  assert(records.front().sequence == 101 && records.back().sequence == 1100);
  agent_rover::PrintAgentLogEvent(std::string(100000, 'x') + "\r\ninjected");
  assert(records.size() == 1000);
  assert(records.back().event.size() <= 4096);
  assert(records.back().event.ends_with("[truncated]"));
  assert(records.back().event.find('\n') == std::string::npos);
  assert(delivered == 1101);
  agent_rover::SetAgentLogSink({});
}
