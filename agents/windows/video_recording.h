// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_VIDEO_RECORDING_H
#define AGENT_ROVER_WINDOWS_AGENT_VIDEO_RECORDING_H

#include <windows.h>

#include <string>

#include "win32_video.h"

namespace agent_rover {

/** Opaque state for one connection's asynchronous video recording. */
struct VideoRecordingStore {
  /** Worker thread handle, or null when no recording is retained. */
  HANDLE thread;
  /** Internal worker state owned by this store. */
  void* context;
};

/**
 * Starts a video recording on a background thread.
 *
 * @param store Connection-owned recording state.
 * @param recording_id Identifier returned to the protocol caller.
 * @param request Validated capture request.
 * @param error Receives a human-readable error on failure.
 * @return true when the recording worker was started.
 */
bool StartVideoRecording(
    VideoRecordingStore* store,
    const std::string& recording_id,
    const VideoCaptureRequest& request,
    std::string* error);

/**
 * Waits for and consumes a completed video recording.
 *
 * @param store Connection-owned recording state.
 * @param recording_id Expected recording identifier.
 * @param result Receives the temporary MP4 and capture metadata.
 * @param error Receives a human-readable error on failure.
 * @return true when recording completed successfully.
 */
bool TakeVideoRecordingResult(
    VideoRecordingStore* store,
    const std::string& recording_id,
    VideoCaptureResult* result,
    std::string* error);

/**
 * Cancels a retained recording and removes any temporary output.
 *
 * @param store Connection-owned recording state.
 */
void CancelVideoRecording(VideoRecordingStore* store);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_VIDEO_RECORDING_H
