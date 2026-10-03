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

bool DecodeFrameHeader(const unsigned char* bytes, FrameHeader* header, std::string* error) {

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

std::vector<unsigned char> EncodeFrame(const Frame& frame) {
  std::vector<unsigned char> output(kFrameHeaderBytes + frame.payload.size());
  std::memcpy(output.data(), kMagic, sizeof(kMagic));
  WriteUInt16Le(output.data() + 4, kTcpFrameVersion);
  WriteUInt16Le(output.data() + 6, static_cast<uint16_t>(frame.kind));
  WriteUInt32Le(output.data() + 8, 0);
  WriteUInt32Le(output.data() + 12, static_cast<uint32_t>(frame.payload.size()));
  WriteUInt32Le(output.data() + 16, 0);
  std::copy(frame.payload.begin(), frame.payload.end(),
            output.begin() + kFrameHeaderBytes);

  return output;
}

}  // namespace agent_rover
