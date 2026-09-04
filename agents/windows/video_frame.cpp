// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "video_frame.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace agent_rover {

static bool PixelByteCount(
    int width,
    int height,
    size_t* byte_count) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const uint64_t count =
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  if (count > std::numeric_limits<size_t>::max()) {
    return false;
  }
  *byte_count = static_cast<size_t>(count);
  return true;
}

VideoFrameSize CreateVideoFrameSize(const WindowRect& initial_bounds) {
  if (initial_bounds.width <= 0 || initial_bounds.height <= 0 ||
      initial_bounds.width == std::numeric_limits<int>::max() ||
      initial_bounds.height == std::numeric_limits<int>::max()) {
    return {0, 0};
  }
  return {
      (initial_bounds.width + 1) & ~1,
      (initial_bounds.height + 1) & ~1,
  };
}

WindowRect SelectVideoCaptureBounds(
    VideoWindowTracking tracking,
    const WindowRect& initial_bounds,
    const WindowRect& current_bounds) {
  return tracking == VideoWindowTracking::FollowWindow ? current_bounds
                                                       : initial_bounds;
}

bool ComposeVideoFrameBgra(
    const WindowRect& requested_bounds,
    const WindowRect& visible_bounds,
    const std::vector<unsigned char>& visible_bgra,
    const VideoFrameSize& frame_size,
    std::vector<unsigned char>* frame_bgra,
    std::string* error) {
  if (frame_bgra == nullptr || error == nullptr) {
    return false;
  }
  size_t source_bytes = 0;
  size_t output_bytes = 0;
  if (!PixelByteCount(
          visible_bounds.width, visible_bounds.height, &source_bytes) ||
      source_bytes != visible_bgra.size()) {
    *error = "Visible BGRA pixel data has invalid dimensions.";
    return false;
  }
  if (!PixelByteCount(frame_size.width, frame_size.height, &output_bytes)) {
    *error = "Video frame dimensions are invalid.";
    return false;
  }

  frame_bgra->assign(output_bytes, 0);
  for (size_t index = 3; index < output_bytes; index += 4) {
    (*frame_bgra)[index] = 255;
  }

  const int64_t copy_left = std::max<int64_t>(
      visible_bounds.x, requested_bounds.x);
  const int64_t copy_top = std::max<int64_t>(
      visible_bounds.y, requested_bounds.y);
  const int64_t copy_right = std::min<int64_t>(
      static_cast<int64_t>(visible_bounds.x) + visible_bounds.width,
      static_cast<int64_t>(requested_bounds.x) + frame_size.width);
  const int64_t copy_bottom = std::min<int64_t>(
      static_cast<int64_t>(visible_bounds.y) + visible_bounds.height,
      static_cast<int64_t>(requested_bounds.y) + frame_size.height);
  if (copy_right <= copy_left || copy_bottom <= copy_top) {
    return true;
  }

  const size_t copy_width = static_cast<size_t>(copy_right - copy_left);
  const size_t rows = static_cast<size_t>(copy_bottom - copy_top);
  const size_t source_x =
      static_cast<size_t>(copy_left - visible_bounds.x);
  const size_t source_y =
      static_cast<size_t>(copy_top - visible_bounds.y);
  const size_t destination_x =
      static_cast<size_t>(copy_left - requested_bounds.x);
  const size_t destination_y =
      static_cast<size_t>(copy_top - requested_bounds.y);
  const size_t source_stride =
      static_cast<size_t>(visible_bounds.width) * 4;
  const size_t destination_stride = static_cast<size_t>(frame_size.width) * 4;
  for (size_t row = 0; row < rows; row += 1) {
    const unsigned char* source =
        visible_bgra.data() + (source_y + row) * source_stride + source_x * 4;
    unsigned char* destination = frame_bgra->data() +
                                 (destination_y + row) * destination_stride +
                                 destination_x * 4;
    std::memcpy(destination, source, copy_width * 4);
  }
  return true;
}

}  // namespace agent_rover
