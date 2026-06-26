// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_FRAME_H
#define AGENT_ROVER_WINDOWS_AGENT_FRAME_H

#include <winsock2.h>

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

/**
 * Reads one complete frame from a blocking socket.
 *
 * @param socket Connected Winsock socket.
 * @param max_payload_bytes Maximum payload bytes accepted for this frame.
 * @param frame Receives the decoded frame on success.
 * @param error Receives a human-readable error on failure.
 * @return true when a frame was read, otherwise false.
 */
bool ReadFrame(
    SOCKET socket,
    uint32_t max_payload_bytes,
    Frame* frame,
    std::string* error);

/**
 * Reads and validates one fixed-size frame header.
 *
 * @param socket Connected Winsock socket.
 * @param header Receives the decoded frame header on success.
 * @param error Receives a human-readable error on failure.
 * @return true when a frame header was read, otherwise false.
 */
bool ReadFrameHeader(SOCKET socket, FrameHeader* header, std::string* error);

/**
 * Reads one frame payload after its header has already been accepted.
 *
 * @param socket Connected Winsock socket.
 * @param payload_length Exact payload byte count to read.
 * @param payload Receives the decoded frame payload on success.
 * @param error Receives a human-readable error on failure.
 * @return true when the payload was read, otherwise false.
 */
bool ReadFramePayload(
    SOCKET socket,
    uint32_t payload_length,
    std::vector<unsigned char>* payload,
    std::string* error);

/**
 * Writes one complete frame to a blocking socket.
 *
 * @param socket Connected Winsock socket.
 * @param frame Frame to send.
 * @param error Receives a human-readable error on failure.
 * @return true when the frame was written, otherwise false.
 */
bool WriteFrame(SOCKET socket, const Frame& frame, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_FRAME_H
