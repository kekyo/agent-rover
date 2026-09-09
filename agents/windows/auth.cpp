// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "auth.h"

#include "../protocol_version.h"
#include "binary_codec.h"

#ifdef _WIN32
#include "win32_random.h"
#endif

#include <algorithm>
#include <cstdint>

namespace agent_rover {

static bool FillRandomBytes(
    std::vector<unsigned char>* bytes,
    size_t byte_count,
    std::string* error) {
  bytes->assign(byte_count, 0);
#ifdef _WIN32
  return GenerateSecureRandom(bytes->data(), bytes->size(), error);
#else
  *error = "Random byte generation is only available on Windows.";
  return false;
#endif
}

std::string Base64UrlEncode(const std::vector<unsigned char>& data) {
  static const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  std::string output;
  output.reserve(((data.size() + 2) / 3) * 4);
  for (size_t index = 0; index < data.size(); index += 3) {
    const uint32_t first = data[index];
    const uint32_t second = index + 1 < data.size() ? data[index + 1] : 0;
    const uint32_t third = index + 2 < data.size() ? data[index + 2] : 0;
    const uint32_t triple = (first << 16) | (second << 8) | third;

    output.push_back(alphabet[(triple >> 18) & 0x3f]);
    output.push_back(alphabet[(triple >> 12) & 0x3f]);
    if (index + 1 < data.size()) {
      output.push_back(alphabet[(triple >> 6) & 0x3f]);
    }
    if (index + 2 < data.size()) {
      output.push_back(alphabet[triple & 0x3f]);
    }
  }
  return output;
}

bool GenerateAuthToken(std::string* token, std::string* error) {
  std::vector<unsigned char> bytes;
  if (!FillRandomBytes(&bytes, kAuthTokenRandomBytes, error)) {
    return false;
  }
  *token = Base64UrlEncode(bytes);
  return token->size() == kAuthTokenLength;
}

bool GenerateAuthChallenge(
    std::vector<unsigned char>* challenge,
    std::string* error) {
  return FillRandomBytes(challenge, kAuthChallengeBytes, error);
}

std::vector<unsigned char> CreateAuthChallengeResponse(
    const std::string& token,
    const std::vector<unsigned char>& challenge) {
  std::vector<unsigned char> key(token.begin(), token.end());
  std::vector<unsigned char> message;
  message.reserve(sizeof(kAuthChallengePrefix) + challenge.size());
  message.insert(
      message.end(),
      kAuthChallengePrefix,
      kAuthChallengePrefix + sizeof(kAuthChallengePrefix));
  message.insert(message.end(), challenge.begin(), challenge.end());
  return HmacSha256(key, message);
}

bool AuthResponseEquals(
    const std::vector<unsigned char>& actual,
    const std::vector<unsigned char>& expected) {
  const size_t length = std::max(actual.size(), expected.size());
  unsigned char difference =
      static_cast<unsigned char>(actual.size() ^ expected.size());
  for (size_t index = 0; index < length; index += 1) {
    const unsigned char actual_ch =
        index < actual.size() ? actual[index] : 0;
    const unsigned char expected_ch =
        index < expected.size() ? expected[index] : 0;
    difference = static_cast<unsigned char>(difference | (actual_ch ^ expected_ch));
  }
  return difference == 0;
}

}  // namespace agent_rover
