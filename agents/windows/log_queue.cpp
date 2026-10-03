// agent-rover - Bounded diagnostic log delivery
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "log_queue.h"
#include <algorithm>
#include <utility>

namespace agent_rover {

static void RecordLoss(LogQueue* queue, uint64_t count, uint64_t first, uint64_t last) {
  if (!count) return;
  queue->dropped += count;
  queue->first_dropped = queue->first_dropped ? std::min(queue->first_dropped, first) : first;
  queue->last_dropped = std::max(queue->last_dropped, last);
}

void QueueLogRecord(LogQueue* queue, const std::string& session, const AgentLogRecord& record) {
  std::string line = record.timestamp + " session=" + session + " seq=" +
      std::to_string(record.sequence) + " " + record.event + "\n";
  if (queue->records.size() >= 4096 || queue->bytes + line.size() > 4 * 1024 * 1024) {
    RecordLoss(queue, 1, record.sequence, record.sequence);
    return;
  }
  queue->bytes += line.size();
  queue->records.push_back({record.sequence, std::move(line)});
}

LogBatch TakeLogBatch(LogQueue* queue) {
  LogBatch batch;
  while (!queue->records.empty() && batch.data.size() + queue->records.front().line.size() <= 65536) {
    const auto& record = queue->records.front();
    if (!batch.count) batch.first = record.sequence;
    batch.last = record.sequence;
    ++batch.count;
    batch.data += record.line;
    queue->bytes -= record.line.size();
    queue->records.pop_front();
  }
  return batch;
}

void MarkLogBatchUnconfirmed(LogQueue* queue, const LogBatch& batch) {
  RecordLoss(queue, batch.count, batch.first, batch.last);
}

}  // namespace agent_rover
