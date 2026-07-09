// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_AGENT_PROTOCOL_VERSION_H
#define AGENT_ROVER_AGENT_PROTOCOL_VERSION_H

#include <cstdint>

namespace agent_rover {

/** Current JSON protocol version spoken by driver and agent. */
constexpr char kProtocolVersion[] = "2026-07-07";

/** TCP frame protocol version spoken by driver and agent. */
constexpr uint16_t kTcpFrameVersion = 2;

/**
 * Prefix included in the TCP auth challenge HMAC message.
 *
 * @remarks The terminal NUL byte is part of the wire protocol input.
 */
constexpr char kAuthChallengePrefix[] = "agent-rover-auth-v1";

/** Capability identifier reported by agents that support TCP frame transport. */
constexpr char kTcpFrameCapabilityId[] = "transport.tcp-frame-v1";

}  // namespace agent_rover

#endif  // AGENT_ROVER_AGENT_PROTOCOL_VERSION_H
