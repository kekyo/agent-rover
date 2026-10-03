#include "log_queue.h"
#include <cassert>
#include <string>

int main() {
  agent_rover::LogQueue queue;
  const std::string session = "test-session";
  for (unsigned int i = 1; i <= 1200; ++i) {
    const agent_rover::AgentLogRecord record = {i, "2026-10-03T00:00:00Z", std::string(4000, 'x')};
    agent_rover::QueueLogRecord(&queue, session, record);
  }
  assert(queue.bytes <= 4 * 1024 * 1024 && queue.records.size() <= 4096);
  assert(queue.dropped > 0);
  assert(queue.first_dropped > 0 && queue.last_dropped == 1200);
  const auto before = queue.bytes;
  const auto batch = agent_rover::TakeLogBatch(&queue);
  assert(batch.count > 0 && batch.data.size() <= 65536);
  assert(queue.bytes < before);
  assert(batch.data.find("session=test-session seq=1") != std::string::npos);
  agent_rover::MarkLogBatchUnconfirmed(&queue, batch);
  assert(queue.first_dropped == 1 && queue.last_dropped == 1200);
  assert(queue.dropped >= batch.count);
}
