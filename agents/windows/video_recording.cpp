// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <winsock2.h>
#include "../vendor/cardio/cardio.h"
#include "video_recording.h"

#include <windows.h>

#include <new>
#include <string>

#include "win32_video.h"

namespace agent_rover {

static cardio::promise<void> WaitRecording(HANDLE thread, uint32_t milliseconds, bool* finished) {
  auto timeout = cardio::cancellations::timeout(milliseconds);
  try { co_await cardio::from_win32_handle(thread, timeout.get_cancellation()); *finished = true; }
  catch (const std::exception&) {}
}

static bool FinishRecordingWait(HANDLE thread, uint32_t milliseconds) {
  bool finished = false;
  cardio::dispatcher_host_win32_auto dispatcher;
  auto wait = WaitRecording(thread, milliseconds, &finished);
  dispatcher.park();
  return finished;
}

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
  if (!FinishRecordingWait(store->thread, std::min<uint32_t>(context->request.duration_ms, 600000) + 120000)) {
    *error = "Video result deadline exceeded; native recording ownership is retained.";
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
  // This is the isolated process's EOF path. If the codec is still executing,
  // the parent terminates the process; never free memory under a live thread.
  if (!FinishRecordingWait(store->thread, 200)) return;
  CloseHandle(store->thread);
  if (context->succeeded) {
    RemoveVideoCaptureResult(context->result);
  }
  delete context;
  *store = {};
}

}  // namespace agent_rover
