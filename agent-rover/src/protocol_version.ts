// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

/** Current JSON protocol version spoken by driver and agent. */
export const protocolVersion = '2026-07-01';

/** TCP frame protocol version spoken by driver and agent. */
export const tcpFrameVersion = 2;

/**
 * Prefix included in the TCP auth challenge HMAC message.
 *
 * @remarks The terminal NUL byte is part of the wire protocol input.
 */
export const authChallengePrefix = Buffer.from(
  'agent-rover-auth-v1\0',
  'ascii'
);

/** Capability identifier reported by agents that support TCP frame transport. */
export const tcpFrameCapabilityId = 'transport.tcp-frame-v1';
