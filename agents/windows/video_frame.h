// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_VIDEO_FRAME_H
#define AGENT_ROVER_WINDOWS_AGENT_VIDEO_FRAME_H

#include <string>
#include <vector>

#include "win32_windows.h"

namespace agent_rover {

/** Window position behavior used while recording video. */
enum class VideoWindowTracking {
  /** Capture the window's current screen position for every frame. */
  FollowWindow,
  /** Keep capturing the screen rectangle selected at recording start. */
  InitialBounds,
};

/** Fixed encoded video frame size in pixels. */
struct VideoFrameSize {
  /** Even frame width, or zero when the source width is invalid. */
  int width;
  /** Even frame height, or zero when the source height is invalid. */
  int height;
};

/**
 * Creates the fixed even-sized frame used by an H.264 recording.
 *
 * @param initial_bounds Capture bounds at recording start.
 * @return Even frame dimensions, or zero dimensions for invalid bounds.
 */
VideoFrameSize CreateVideoFrameSize(const WindowRect& initial_bounds);

/**
 * Selects the screen rectangle to capture for the next window video frame.
 *
 * @param tracking Configured window tracking behavior.
 * @param initial_bounds Window bounds at recording start.
 * @param current_bounds Current window bounds.
 * @return Screen rectangle to capture.
 */
WindowRect SelectVideoCaptureBounds(
    VideoWindowTracking tracking,
    const WindowRect& initial_bounds,
    const WindowRect& current_bounds);

/**
 * Places visible BGRA pixels into a fixed video frame.
 *
 * @remarks Pixels outside the visible capture area are opaque black. Pixels
 * beyond the fixed output dimensions are cropped.
 * @param requested_bounds Requested screen capture rectangle.
 * @param visible_bounds Screen rectangle represented by visible_bgra.
 * @param visible_bgra Tightly packed top-down BGRA pixels.
 * @param frame_size Fixed encoded frame dimensions.
 * @param frame_bgra Receives tightly packed top-down BGRA output pixels.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ComposeVideoFrameBgra(
    const WindowRect& requested_bounds,
    const WindowRect& visible_bounds,
    const std::vector<unsigned char>& visible_bgra,
    const VideoFrameSize& frame_size,
    std::vector<unsigned char>* frame_bgra,
    std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_VIDEO_FRAME_H
