// agent-rover - Operation deadline policy
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_OPERATION_LIMITS_H
#define AGENT_ROVER_OPERATION_LIMITS_H
#include <algorithm>
#include <cstdint>
#include <string_view>
namespace agent_rover {
/** Selects the absolute operation budget, including queue time.
 * @param method Protocol method. @param video_duration_ms Last requested recording duration.
 * @return Deadline in milliseconds. Native side effects already submitted may finish later. */
inline uint32_t OperationBudgetMs(std::string_view method, uint32_t video_duration_ms) {
  if (method == "video.result") return std::min<uint32_t>(video_duration_ms, 600000) + 120000;
  if (method.starts_with("window.") || method.starts_with("input.") || method.starts_with("clipboard.")) return 30000;
  return 120000;
}
/** Computes the remaining budget without resetting the original deadline.
 * @param budget_ms Total budget. @param elapsed_ms Time since request receipt.
 * @return Zero when the queued operation has expired. */
inline uint32_t RemainingOperationMs(uint32_t budget_ms, uint64_t elapsed_ms) {
  return elapsed_ms >= budget_ms ? 0 : budget_ms - static_cast<uint32_t>(elapsed_ms);
}
}
#endif
