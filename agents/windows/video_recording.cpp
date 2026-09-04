// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "video_recording.h"

#include <windows.h>

#include <new>
#include <string>

#include "win32_video.h"

namespace agent_rover {

struct VideoRecordingContext {
  std::string recording_id;
  VideoCaptureRequest request;
  volatile LONG cancelled;
  VideoCaptureResult result;
  std::string error;
  bool succeeded;
};

static DWORD WINAPI VideoRecordingThread(void* state) {
  VideoRecordingContext* context =
      static_cast<VideoRecordingContext*>(state);
  context->succeeded = RecordVideoToFile(
      context->request,
      &context->cancelled,
      &context->result,
      &context->error);
  return 0;
}

static VideoRecordingContext* Context(VideoRecordingStore* store) {
  return static_cast<VideoRecordingContext*>(store->context);
}

bool StartVideoRecording(
    VideoRecordingStore* store,
    const std::string& recording_id,
    const VideoCaptureRequest& request,
    std::string* error) {
  if (store->thread != nullptr || store->context != nullptr) {
    *error = "A video recording is already active for this connection.";
    return false;
  }

  VideoRecordingContext* context = new (std::nothrow) VideoRecordingContext();
  if (context == nullptr) {
    *error = "Unable to allocate video recording state.";
    return false;
  }
  context->recording_id = recording_id;
  context->request = request;
  context->cancelled = 0;
  context->succeeded = false;

  HANDLE thread = CreateThread(
      nullptr, 0, VideoRecordingThread, context, 0, nullptr);
  if (thread == nullptr) {
    delete context;
    *error = "CreateThread failed while starting video recording.";
    return false;
  }
  store->thread = thread;
  store->context = context;
  return true;
}

bool TakeVideoRecordingResult(
    VideoRecordingStore* store,
    const std::string& recording_id,
    VideoCaptureResult* result,
    std::string* error) {
  VideoRecordingContext* context = Context(store);
  if (store->thread == nullptr || context == nullptr) {
    *error = "No video recording is available.";
    return false;
  }
  if (context->recording_id != recording_id) {
    *error = "Unknown video recording id.";
    return false;
  }
  if (WaitForSingleObject(store->thread, INFINITE) != WAIT_OBJECT_0) {
    *error = "Waiting for the video recording worker failed.";
    return false;
  }

  CloseHandle(store->thread);
  store->thread = nullptr;
  store->context = nullptr;
  const bool succeeded = context->succeeded;
  if (succeeded) {
    *result = context->result;
  } else {
    *error = context->error.empty()
                 ? "Video recording failed."
                 : context->error;
  }
  delete context;
  return succeeded;
}

void CancelVideoRecording(VideoRecordingStore* store) {
  VideoRecordingContext* context = Context(store);
  if (store->thread == nullptr || context == nullptr) {
    *store = {};
    return;
  }
  InterlockedExchange(&context->cancelled, 1);
  WaitForSingleObject(store->thread, INFINITE);
  CloseHandle(store->thread);
  if (context->succeeded) {
    RemoveVideoCaptureResult(context->result);
  }
  delete context;
  *store = {};
}

}  // namespace agent_rover
