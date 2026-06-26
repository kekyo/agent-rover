// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_PNG_ENCODER_H
#define AGENT_ROVER_WINDOWS_AGENT_PNG_ENCODER_H

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

/**
 * Encodes RGBA pixels as a PNG image.
 *
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param rgba RGBA pixels, four bytes per pixel.
 * @param png Receives PNG bytes.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool EncodePngRgba(
    uint32_t width,
    uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::vector<unsigned char>* png,
    std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_PNG_ENCODER_H
