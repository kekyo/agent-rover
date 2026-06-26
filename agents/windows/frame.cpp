// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "frame.h"

#include "../protocol_version.h"

#include <algorithm>
#include <cstring>

namespace agent_rover {

static constexpr unsigned char kMagic[4] = {'T', 'R', 'V', 'R'};

static uint16_t ReadUInt16Le(const unsigned char* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

static uint32_t ReadUInt32Le(const unsigned char* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

static void WriteUInt16Le(unsigned char* data, uint16_t value) {
  data[0] = static_cast<unsigned char>(value & 0xff);
  data[1] = static_cast<unsigned char>((value >> 8) & 0xff);
}

static void WriteUInt32Le(unsigned char* data, uint32_t value) {
  data[0] = static_cast<unsigned char>(value & 0xff);
  data[1] = static_cast<unsigned char>((value >> 8) & 0xff);
  data[2] = static_cast<unsigned char>((value >> 16) & 0xff);
  data[3] = static_cast<unsigned char>((value >> 24) & 0xff);
}

static bool IsKnownFrameKind(uint16_t value) {
  return value >= static_cast<uint16_t>(FrameKind::Json) &&
         value <= static_cast<uint16_t>(FrameKind::AuthResponse);
}

static bool ReceiveExact(
    SOCKET socket,
    unsigned char* buffer,
    int length,
    std::string* error) {
  int offset = 0;
  while (offset < length) {
    const int received = recv(socket, reinterpret_cast<char*>(buffer + offset),
                              length - offset, 0);
    if (received == 0) {
      *error = "Socket closed while reading a frame.";
      return false;
    }
    if (received == SOCKET_ERROR) {
      *error = "recv failed.";
      return false;
    }
    offset += received;
  }
  return true;
}

static bool SendAll(
    SOCKET socket,
    const unsigned char* buffer,
    size_t length,
    std::string* error) {
  size_t offset = 0;
  while (offset < length) {
    const size_t remaining = length - offset;
    const int chunk = static_cast<int>(std::min<size_t>(remaining, 64 * 1024));
    const int sent =
        send(socket, reinterpret_cast<const char*>(buffer + offset), chunk, 0);
    if (sent == SOCKET_ERROR) {
      *error = "send failed.";
      return false;
    }
    offset += static_cast<size_t>(sent);
  }
  return true;
}

bool ReadFrame(
    SOCKET socket,
    uint32_t max_payload_bytes,
    Frame* frame,
    std::string* error) {
  FrameHeader header = {};
  if (!ReadFrameHeader(socket, &header, error)) {
    return false;
  }
  if (header.payload_length > max_payload_bytes) {
    *error = "Frame payload exceeds the configured limit.";
    return false;
  }

  frame->kind = header.kind;
  return ReadFramePayload(socket, header.payload_length, &frame->payload, error);
}

bool ReadFrameHeader(SOCKET socket, FrameHeader* header, std::string* error) {
  unsigned char bytes[kFrameHeaderBytes] = {};
  if (!ReceiveExact(socket, bytes, kFrameHeaderBytes, error)) {
    return false;
  }

  if (std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    *error = "Invalid frame magic.";
    return false;
  }

  const uint16_t version = ReadUInt16Le(bytes + 4);
  const uint16_t kind = ReadUInt16Le(bytes + 6);
  const uint32_t flags = ReadUInt32Le(bytes + 8);
  const uint32_t payload_length = ReadUInt32Le(bytes + 12);
  const uint32_t reserved = ReadUInt32Le(bytes + 16);

  if (version != kTcpFrameVersion) {
    *error = "Unsupported frame version.";
    return false;
  }
  if (!IsKnownFrameKind(kind)) {
    *error = "Unsupported frame kind.";
    return false;
  }
  if (flags != 0 || reserved != 0) {
    *error = "Frame flags and reserved fields must be zero.";
    return false;
  }
  header->kind = static_cast<FrameKind>(kind);
  header->payload_length = payload_length;
  return true;
}

bool ReadFramePayload(
    SOCKET socket,
    uint32_t payload_length,
    std::vector<unsigned char>* payload,
    std::string* error) {
  payload->assign(payload_length, 0);
  if (payload_length == 0) {
    return true;
  }
  return ReceiveExact(
      socket,
      payload->data(),
      static_cast<int>(payload->size()),
      error);
}

bool WriteFrame(SOCKET socket, const Frame& frame, std::string* error) {
  if (frame.payload.size() > 0xffffffffu) {
    *error = "Frame payload is too large.";
    return false;
  }

  std::vector<unsigned char> output(kFrameHeaderBytes + frame.payload.size());
  std::memcpy(output.data(), kMagic, sizeof(kMagic));
  WriteUInt16Le(output.data() + 4, kTcpFrameVersion);
  WriteUInt16Le(output.data() + 6, static_cast<uint16_t>(frame.kind));
  WriteUInt32Le(output.data() + 8, 0);
  WriteUInt32Le(output.data() + 12, static_cast<uint32_t>(frame.payload.size()));
  WriteUInt32Le(output.data() + 16, 0);
  std::copy(frame.payload.begin(), frame.payload.end(),
            output.begin() + kFrameHeaderBytes);

  return SendAll(socket, output.data(), output.size(), error);
}

}  // namespace agent_rover
