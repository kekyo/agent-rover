// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_BINARY_CODEC_H
#define AGENT_ROVER_WINDOWS_AGENT_BINARY_CODEC_H

#include <string>
#include <vector>

namespace agent_rover {

/**
 * Decodes RFC 4648 base64 text.
 *
 * @param text Base64 input.
 * @param data Receives decoded bytes.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool Base64Decode(
    const std::string& text,
    std::vector<unsigned char>* data,
    std::string* error);

/**
 * Computes SHA-256 and returns raw digest bytes.
 *
 * @param data Input bytes.
 * @return 32-byte SHA-256 digest.
 */
std::vector<unsigned char> Sha256Digest(const std::vector<unsigned char>& data);

/**
 * Computes SHA-256 and returns lowercase hexadecimal text.
 *
 * @param data Input bytes.
 * @return SHA-256 digest as lowercase hex.
 */
std::string Sha256Hex(const std::vector<unsigned char>& data);

/**
 * Computes HMAC-SHA-256.
 *
 * @param key HMAC key bytes.
 * @param data Input bytes.
 * @return 32-byte HMAC-SHA-256 digest.
 */
std::vector<unsigned char> HmacSha256(
    const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& data);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_BINARY_CODEC_H
