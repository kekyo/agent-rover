// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_VIDEO_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_VIDEO_H

#include <windows.h>

#include <cstdint>
#include <string>

#include "video_frame.h"
#include "win32_windows.h"

namespace agent_rover {

/** Parameters for one screen or window video recording. */
struct VideoCaptureRequest {
  /** Window id to follow, or empty for a fixed screen rectangle. */
  std::string window_id;
  /** Capture bounds resolved at recording start. */
  WindowRect initial_bounds;
  /** Window movement behavior. Ignored when window_id is empty. */
  VideoWindowTracking tracking;
  /** Requested duration in milliseconds. */
  uint32_t duration_ms;
  /** Nominal encoded frames per second. */
  uint32_t fps;
  /** Encoder quality from 1 through 100. */
  uint32_t quality;
};

/** Completed H.264 MP4 recording and capture metadata. */
struct VideoCaptureResult {
  /** Directory owning the temporary MP4 file. */
  std::string directory;
  /** Temporary MP4 file path. */
  std::string path;
  /** Actual encoded timeline duration in milliseconds. */
  uint32_t duration_ms;
  /** Nominal encoded frames per second. */
  uint32_t fps;
  /** Number of samples written to the encoder. */
  uint32_t frame_count;
  /** Number of capture deadlines skipped while recording. */
  uint32_t dropped_frames;
  /** Window or screen bounds at recording start. */
  WindowRect initial_bounds;
  /** Window or screen bounds at recording end. */
  WindowRect final_bounds;
  /** Whether any capture frame was clipped. */
  bool clipped;
};

/**
 * Checks whether the Media Foundation functions required for recording exist.
 *
 * @return true when the recording backend can be loaded.
 */
bool IsVideoCaptureSupported();

/**
 * Records visible GDI screen pixels into a temporary H.264 MP4 file.
 *
 * @param request Validated recording parameters.
 * @param cancelled Interlocked cancellation flag checked between frames.
 * @param result Receives the completed temporary file and metadata.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool RecordVideoToFile(
    const VideoCaptureRequest& request,
    volatile LONG* cancelled,
    VideoCaptureResult* result,
    std::string* error);

/**
 * Removes a temporary video file and its owning directory.
 *
 * @param result Recording whose temporary resources should be removed.
 */
void RemoveVideoCaptureResult(const VideoCaptureResult& result);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_VIDEO_H
