// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "binary_codec.h"
#include <algorithm>

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

static int Base64Value(unsigned char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+') {
    return 62;
  }
  if (ch == '/') {
    return 63;
  }
  return -1;
}

static bool IsBase64Whitespace(unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool Base64Decode(
    const std::string& text,
    std::vector<unsigned char>* data,
    std::string* error) {
  std::vector<unsigned char> cleaned;
  cleaned.reserve(text.size());
  for (const unsigned char ch : text) {
    if (!IsBase64Whitespace(ch)) {
      cleaned.push_back(ch);
    }
  }
  if (cleaned.size() % 4 != 0) {
    *error = "Base64 input length must be a multiple of four.";
    return false;
  }

  data->clear();
  data->reserve((cleaned.size() / 4) * 3);
  for (size_t index = 0; index < cleaned.size(); index += 4) {
    const bool third_padded = cleaned[index + 2] == '=';
    const bool fourth_padded = cleaned[index + 3] == '=';
    const int first = Base64Value(cleaned[index]);
    const int second = Base64Value(cleaned[index + 1]);
    const int third = third_padded ? 0 : Base64Value(cleaned[index + 2]);
    const int fourth = fourth_padded ? 0 : Base64Value(cleaned[index + 3]);
    if (first < 0 || second < 0 || third < 0 || fourth < 0) {
      *error = "Base64 input contains an invalid character.";
      return false;
    }
    if (third_padded && !fourth_padded) {
      *error = "Base64 padding is invalid.";
      return false;
    }
    if ((third_padded || fourth_padded) && index + 4 != cleaned.size()) {
      *error = "Base64 padding is only allowed at the end.";
      return false;
    }

    const uint32_t triple = (static_cast<uint32_t>(first) << 18) |
                            (static_cast<uint32_t>(second) << 12) |
                            (static_cast<uint32_t>(third) << 6) |
                            static_cast<uint32_t>(fourth);
    data->push_back(static_cast<unsigned char>((triple >> 16) & 0xff));
    if (!third_padded) {
      data->push_back(static_cast<unsigned char>((triple >> 8) & 0xff));
    }
    if (!fourth_padded) {
      data->push_back(static_cast<unsigned char>(triple & 0xff));
    }
  }
  return true;
}

static uint32_t RotateRight(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

static uint32_t Choose(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

static uint32_t Majority(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t BigSigma0(uint32_t value) {
  return RotateRight(value, 2) ^ RotateRight(value, 13) ^
         RotateRight(value, 22);
}

static uint32_t BigSigma1(uint32_t value) {
  return RotateRight(value, 6) ^ RotateRight(value, 11) ^
         RotateRight(value, 25);
}

static uint32_t SmallSigma0(uint32_t value) {
  return RotateRight(value, 7) ^ RotateRight(value, 18) ^ (value >> 3);
}

static uint32_t SmallSigma1(uint32_t value) {
  return RotateRight(value, 17) ^ RotateRight(value, 19) ^ (value >> 10);
}

static uint32_t ReadBigEndian32(const unsigned char* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

static void AppendBigEndian64(std::vector<unsigned char>* data, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    data->push_back(static_cast<unsigned char>((value >> shift) & 0xff));
  }
}

static void AppendBigEndian32Bytes(
    std::vector<unsigned char>* output,
    uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    output->push_back(static_cast<unsigned char>((value >> shift) & 0xff));
  }
}

static void CompressSha256(Sha256State* state, const unsigned char* data) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
      0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
      0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  auto& h = state->words;
  uint32_t w[64] = {};
  for (int index = 0; index < 16; index += 1) {
    w[index] = ReadBigEndian32(&data[static_cast<size_t>(index * 4)]);
  }
  for (int index = 16; index < 64; index += 1) {
    w[index] = SmallSigma1(w[index - 2]) + w[index - 7] +
               SmallSigma0(w[index - 15]) + w[index - 16];
  }

  uint32_t a = h[0];
  uint32_t b = h[1];
  uint32_t c = h[2];
  uint32_t d = h[3];
  uint32_t e = h[4];
  uint32_t f = h[5];
  uint32_t g = h[6];
  uint32_t hh = h[7];

  for (int index = 0; index < 64; index += 1) {
    const uint32_t t1 =
        hh + BigSigma1(e) + Choose(e, f, g) + k[index] + w[index];
    const uint32_t t2 = BigSigma0(a) + Majority(a, b, c);
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

Sha256State CreateSha256() {
  return {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, {}, 0, 0};
}

void UpdateSha256(Sha256State* state, const unsigned char* data, size_t size) {
  state->total_bytes += size;
  while (size != 0) {
    const auto count = std::min(size, state->buffer.size() - state->buffered);
    std::copy_n(data, count, state->buffer.data() + state->buffered);
    state->buffered += count;
    data += count;
    size -= count;
    if (state->buffered == state->buffer.size()) {
      CompressSha256(state, state->buffer.data());
      state->buffered = 0;
    }
  }
}

std::vector<unsigned char> FinishSha256(Sha256State state) {
  const uint64_t bit_length = state.total_bytes * 8;
  std::vector<unsigned char> padding(1, 0x80);
  while ((state.buffered + padding.size()) % 64 != 56) padding.push_back(0);
  AppendBigEndian64(&padding, bit_length);
  UpdateSha256(&state, padding.data(), padding.size());
  std::vector<unsigned char> output;
  output.reserve(32);
  for (const auto value : state.words) AppendBigEndian32Bytes(&output, value);
  return output;
}

std::vector<unsigned char> Sha256Digest(const std::vector<unsigned char>& data) {
  auto state = CreateSha256();
  UpdateSha256(&state, data.data(), data.size());
  return FinishSha256(state);
}

std::string Sha256Hex(const std::vector<unsigned char>& data) {
  static const char* digits = "0123456789abcdef";
  const std::vector<unsigned char> digest = Sha256Digest(data);
  std::string output;
  output.reserve(digest.size() * 2);
  for (const unsigned char byte : digest) {
    output.push_back(digits[(byte >> 4) & 0x0f]);
    output.push_back(digits[byte & 0x0f]);
  }
  return output;
}

std::vector<unsigned char> HmacSha256(
    const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& data) {
  static constexpr size_t kBlockBytes = 64;

  std::vector<unsigned char> normalized_key =
      key.size() > kBlockBytes ? Sha256Digest(key) : key;
  normalized_key.resize(kBlockBytes, 0);

  std::vector<unsigned char> inner;
  std::vector<unsigned char> outer;
  inner.reserve(kBlockBytes + data.size());
  outer.reserve(kBlockBytes + 32);
  for (size_t index = 0; index < kBlockBytes; index += 1) {
    inner.push_back(static_cast<unsigned char>(normalized_key[index] ^ 0x36));
    outer.push_back(static_cast<unsigned char>(normalized_key[index] ^ 0x5c));
  }
  inner.insert(inner.end(), data.begin(), data.end());
  const std::vector<unsigned char> inner_digest = Sha256Digest(inner);
  outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
  return Sha256Digest(outer);
}

}  // namespace agent_rover
