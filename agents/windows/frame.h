// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_FRAME_H
#define AGENT_ROVER_WINDOWS_AGENT_FRAME_H

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

/** Maximum JSON payload size accepted by the native agent. */
constexpr uint32_t kMaxJsonPayloadBytes = 16 * 1024 * 1024;

/** Fixed TCP frame header byte length. */
constexpr int kFrameHeaderBytes = 20;

/** TCP frame kind values. */
enum class FrameKind : uint16_t {
  /** JSON protocol message. */
  Json = 1,
  /** Binary transfer chunk. */
  Binary = 2,
  /** Ping frame. */
  Ping = 3,
  /** Pong frame. */
  Pong = 4,
  /** Close frame. */
  Close = 5,
  /** Authentication challenge frame. */
  AuthChallenge = 6,
  /** Authentication challenge response frame. */
  AuthResponse = 7,
};

/** Decoded TCP frame header. */
struct FrameHeader {
  /** Frame kind discriminator. */
  FrameKind kind;
  /** Payload byte length announced by the frame header. */
  uint32_t payload_length;
};

/** Decoded TCP frame. */
struct Frame {
  /** Frame kind discriminator. */
  FrameKind kind;
  /** Raw payload bytes. */
  std::vector<unsigned char> payload;
};

/** Decodes a fixed header without performing I/O.
 * @param bytes Exactly kFrameHeaderBytes bytes. @param header Decoded values.
 * @param error Failure description. @return Whether the header is valid. */
bool DecodeFrameHeader(const unsigned char* bytes, FrameHeader* header, std::string* error);
/** Encodes a frame for asynchronous transmission.
 * @param frame Frame to encode. @return Header followed by payload. */
std::vector<unsigned char> EncodeFrame(const Frame& frame);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_FRAME_H
