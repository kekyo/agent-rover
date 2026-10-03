// agent-rover - Bounded automatic restart policy
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_SUPERVISOR_LIMITS_H
#define AGENT_ROVER_SUPERVISOR_LIMITS_H
#include <cstdint>
#include <deque>
namespace agent_rover {
/** Reserves a host start, or returns the delay required by the rolling limit.
 * @param starts Monotonic start timestamps, bounded to three entries.
 * @param now_ms Current monotonic time in milliseconds.
 * @return Zero after reserving a start; otherwise milliseconds until a slot is available. */
inline uint64_t ReserveSupervisorStart(std::deque<uint64_t>* starts, uint64_t now_ms) {
  while (!starts->empty() && now_ms >= starts->front() && now_ms - starts->front() >= 60000)
    starts->pop_front();
  if (starts->size() >= 3) return now_ms < starts->front() ? 60000 : 60000 - (now_ms - starts->front());
  starts->push_back(now_ms);
  return 0;
}
}
#endif
