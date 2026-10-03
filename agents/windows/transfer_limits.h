// agent-rover - File transfer limits
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#ifndef AGENT_ROVER_WINDOWS_TRANSFER_LIMITS_H
#define AGENT_ROVER_WINDOWS_TRANSFER_LIMITS_H

#include <cstdint>

namespace agent_rover {

/** Number of bytes in one mebibyte. */
inline constexpr uint64_t kBytesPerMiB = 1024 * 1024;
/** Default whole-file read and retained incoming transfer budget. */
inline constexpr uint64_t kDefaultMaxTransferBytes = 64 * kBytesPerMiB;

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_TRANSFER_LIMITS_H
