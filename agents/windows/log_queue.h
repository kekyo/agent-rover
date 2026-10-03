// agent-rover - Bounded diagnostic log delivery
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_WINDOWS_LOG_QUEUE_H
#define AGENT_ROVER_WINDOWS_LOG_QUEUE_H
#include "agent_log.h"
#include <deque>
#include <string>

namespace agent_rover {
/** Serialized record awaiting disk acknowledgement. */
struct QueuedLogRecord {
  /** Sequence shared with the viewer. */ uint64_t sequence;
  /** UTF-8 line including its newline. */ std::string line;
};
/** Dispatcher-owned, bounded disk queue independent of the viewer ring. */
struct LogQueue {
  /** Records in creation order. */ std::deque<QueuedLogRecord> records;
  /** Total serialized bytes. */ size_t bytes = 0;
  /** Total lost or unconfirmed records. */ uint64_t dropped = 0;
  /** First affected sequence, or zero. */ uint64_t first_dropped = 0;
  /** Last affected sequence, or zero. */ uint64_t last_dropped = 0;
};
/** One batch retained until it is acknowledged or declared unconfirmed. */
struct LogBatch {
  /** UTF-8 lines, at most 64 KiB. */ std::string data;
  /** Included record count. */ uint64_t count = 0;
  /** First included sequence. */ uint64_t first = 0;
  /** Last included sequence. */ uint64_t last = 0;
};
/** Enqueues without waiting, counting records rejected by either queue limit.
 * @param queue Destination (4 MiB / 4096 records). @param session Startup identifier.
 * @param record Bounded record from the collector. */
void QueueLogRecord(LogQueue* queue, const std::string& session, const AgentLogRecord& record);
/** Removes one bounded batch. The caller must retain it until acknowledged.
 * @param queue Source. @return Batch, possibly empty. */
LogBatch TakeLogBatch(LogQueue* queue);
/** Records an attempted batch whose persistence could not be confirmed.
 * @param queue Loss accounting. @param batch Unconfirmed batch. */
void MarkLogBatchUnconfirmed(LogQueue* queue, const LogBatch& batch);
}  // namespace agent_rover
#endif
