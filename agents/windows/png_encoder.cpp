// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "png_encoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace agent_rover {

static void AppendU32Be(std::vector<unsigned char>* output, uint32_t value) {
  output->push_back(static_cast<unsigned char>((value >> 24) & 0xff));
  output->push_back(static_cast<unsigned char>((value >> 16) & 0xff));
  output->push_back(static_cast<unsigned char>((value >> 8) & 0xff));
  output->push_back(static_cast<unsigned char>(value & 0xff));
}

static uint32_t Crc32(const unsigned char* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t index = 0; index < size; index += 1) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; bit += 1) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
  }
  return crc ^ 0xffffffffu;
}

static uint32_t Adler32(const std::vector<unsigned char>& data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (const unsigned char byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

static void AppendChunk(
    std::vector<unsigned char>* png,
    const char type[4],
    const std::vector<unsigned char>& data) {
  AppendU32Be(png, static_cast<uint32_t>(data.size()));
  const size_t crc_start = png->size();
  png->insert(png->end(), type, type + 4);
  png->insert(png->end(), data.begin(), data.end());
  const uint32_t crc = Crc32(png->data() + crc_start, png->size() - crc_start);
  AppendU32Be(png, crc);
}

static std::vector<unsigned char> FilterScanlines(
    uint32_t width,
    uint32_t height,
    const std::vector<unsigned char>& rgba) {
  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<unsigned char> filtered;
  filtered.reserve((stride + 1) * static_cast<size_t>(height));
  for (uint32_t y = 0; y < height; y += 1) {
    filtered.push_back(0);
    const size_t offset = static_cast<size_t>(y) * stride;
    filtered.insert(
        filtered.end(), rgba.begin() + static_cast<long>(offset),
        rgba.begin() + static_cast<long>(offset + stride));
  }
  return filtered;
}

static std::vector<unsigned char> ZlibStore(
    const std::vector<unsigned char>& data) {
  std::vector<unsigned char> output;
  output.push_back(0x78);
  output.push_back(0x01);

  size_t offset = 0;
  while (offset < data.size() || data.empty()) {
    const size_t remaining = data.size() - offset;
    const uint16_t block_length =
        static_cast<uint16_t>(std::min<size_t>(remaining, 65535));
    const bool final_block = offset + block_length >= data.size();
    output.push_back(final_block ? 0x01 : 0x00);
    output.push_back(static_cast<unsigned char>(block_length & 0xff));
    output.push_back(static_cast<unsigned char>((block_length >> 8) & 0xff));
    const uint16_t nlen = static_cast<uint16_t>(~block_length);
    output.push_back(static_cast<unsigned char>(nlen & 0xff));
    output.push_back(static_cast<unsigned char>((nlen >> 8) & 0xff));
    output.insert(output.end(), data.begin() + static_cast<long>(offset),
                  data.begin() + static_cast<long>(offset + block_length));
    offset += block_length;
    if (data.empty()) {
      break;
    }
  }
  AppendU32Be(&output, Adler32(data));
  return output;
}

bool EncodePngRgba(
    uint32_t width,
    uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::vector<unsigned char>* png,
    std::string* error) {
  if (width == 0 || height == 0) {
    *error = "PNG dimensions must be non-zero.";
    return false;
  }
  if (rgba.size() != static_cast<size_t>(width) * height * 4) {
    *error = "RGBA buffer size does not match PNG dimensions.";
    return false;
  }

  static const unsigned char signature[8] = {0x89, 'P',  'N',  'G',
                                             '\r', '\n', 0x1a, '\n'};
  png->assign(signature, signature + sizeof(signature));

  std::vector<unsigned char> ihdr;
  AppendU32Be(&ihdr, width);
  AppendU32Be(&ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  AppendChunk(png, "IHDR", ihdr);

  const std::vector<unsigned char> filtered = FilterScanlines(width, height, rgba);
  const std::vector<unsigned char> idat = ZlibStore(filtered);
  AppendChunk(png, "IDAT", idat);
  AppendChunk(png, "IEND", std::vector<unsigned char>());
  return true;
}

}  // namespace agent_rover
