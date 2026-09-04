// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_video.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "video_frame.h"
#include "win32_util.h"
#include "win32_window_bounds.h"

#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif
#ifndef SM_XVIRTUALSCREEN
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#endif

namespace agent_rover {

typedef HRESULT(WINAPI* MfStartupProc)(ULONG version, DWORD flags);
typedef HRESULT(WINAPI* MfShutdownProc)();
typedef HRESULT(WINAPI* MfCreateMediaTypeProc)(IMFMediaType** media_type);
typedef HRESULT(WINAPI* MfCreateMemoryBufferProc)(
    DWORD maximum_length,
    IMFMediaBuffer** buffer);
typedef HRESULT(WINAPI* MfCreateSampleProc)(IMFSample** sample);
typedef HRESULT(WINAPI* MfCreateSinkWriterFromUrlProc)(
    const WCHAR* url,
    IMFByteStream* byte_stream,
    IMFAttributes* attributes,
    IMFSinkWriter** sink_writer);

struct MediaFoundationApi {
  HMODULE platform_module;
  HMODULE read_write_module;
  MfStartupProc startup;
  MfShutdownProc shutdown;
  MfCreateMediaTypeProc create_media_type;
  MfCreateMemoryBufferProc create_memory_buffer;
  MfCreateSampleProc create_sample;
  MfCreateSinkWriterFromUrlProc create_sink_writer_from_url;
};

struct GdiFrameGrabber {
  HDC screen;
  HDC memory;
  HBITMAP bitmap;
  HGDIOBJ previous_object;
  unsigned char* bitmap_pixels;
  int bitmap_width;
  int bitmap_height;
};

struct MediaFoundationWriter {
  MediaFoundationApi api;
  IMFSinkWriter* writer;
  DWORD stream_index;
  bool com_initialized;
  bool mf_started;
};

static std::string HResultError(const char* operation, HRESULT result) {
  char code[16] = {};
  std::snprintf(
      code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
  return std::string(operation) + " failed with HRESULT " + code + ".";
}

static bool BuildSystemDllPath(
    const wchar_t* dll_name,
    std::wstring* path) {
  wchar_t system_directory[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return false;
  }
  path->assign(system_directory, system_directory + length);
  if (!path->empty() && path->back() != L'\\' && path->back() != L'/') {
    path->push_back(L'\\');
  }
  path->append(dll_name);
  return true;
}

static HMODULE LoadSystemLibrary(const wchar_t* name) {
  std::wstring path;
  return BuildSystemDllPath(name, &path) ? LoadLibraryW(path.c_str()) : nullptr;
}

static void CloseMediaFoundationApi(MediaFoundationApi* api) {
  if (api->read_write_module != nullptr) {
    FreeLibrary(api->read_write_module);
  }
  if (api->platform_module != nullptr) {
    FreeLibrary(api->platform_module);
  }
  *api = {};
}

template <typename Function>
static Function LoadFunction(HMODULE module, const char* name) {
  return reinterpret_cast<Function>(GetProcAddress(module, name));
}

static bool LoadMediaFoundationApi(
    MediaFoundationApi* api,
    std::string* error) {
  *api = {};
  api->platform_module = LoadSystemLibrary(L"mfplat.dll");
  api->read_write_module = LoadSystemLibrary(L"mfreadwrite.dll");
  if (api->platform_module == nullptr || api->read_write_module == nullptr) {
    CloseMediaFoundationApi(api);
    if (error != nullptr) {
      *error = "Windows Media Foundation is unavailable.";
    }
    return false;
  }
  api->startup =
      LoadFunction<MfStartupProc>(api->platform_module, "MFStartup");
  api->shutdown =
      LoadFunction<MfShutdownProc>(api->platform_module, "MFShutdown");
  api->create_media_type = LoadFunction<MfCreateMediaTypeProc>(
      api->platform_module, "MFCreateMediaType");
  api->create_memory_buffer = LoadFunction<MfCreateMemoryBufferProc>(
      api->platform_module, "MFCreateMemoryBuffer");
  api->create_sample =
      LoadFunction<MfCreateSampleProc>(api->platform_module, "MFCreateSample");
  api->create_sink_writer_from_url = LoadFunction<MfCreateSinkWriterFromUrlProc>(
      api->read_write_module, "MFCreateSinkWriterFromURL");
  if (api->startup == nullptr || api->shutdown == nullptr ||
      api->create_media_type == nullptr || api->create_memory_buffer == nullptr ||
      api->create_sample == nullptr ||
      api->create_sink_writer_from_url == nullptr) {
    CloseMediaFoundationApi(api);
    if (error != nullptr) {
      *error = "Required Windows Media Foundation functions are unavailable.";
    }
    return false;
  }
  return true;
}

template <typename Interface>
static void ReleaseInterface(Interface** value) {
  if (*value != nullptr) {
    (*value)->Release();
    *value = nullptr;
  }
}

static uint32_t VideoBitrate(
    const VideoFrameSize& size,
    uint32_t fps,
    uint32_t quality) {
  const double bits_per_pixel = 0.04 + static_cast<double>(quality) * 0.0026;
  const double calculated = static_cast<double>(size.width) * size.height * fps *
                            bits_per_pixel;
  return static_cast<uint32_t>(
      std::max<double>(500000.0, std::min<double>(100000000.0, calculated)));
}

static bool SetVideoTypeAttributes(
    IMFMediaType* type,
    const GUID& subtype,
    const VideoFrameSize& size,
    uint32_t fps,
    std::string* error) {
  HRESULT status = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(status)) {
    status = type->SetGUID(MF_MT_SUBTYPE, subtype);
  }
  if (SUCCEEDED(status)) {
    status = MFSetAttributeSize(
        type,
        MF_MT_FRAME_SIZE,
        static_cast<UINT32>(size.width),
        static_cast<UINT32>(size.height));
  }
  if (SUCCEEDED(status)) {
    status = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1);
  }
  if (SUCCEEDED(status)) {
    status = MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  }
  if (SUCCEEDED(status)) {
    status = type->SetUINT32(
        MF_MT_INTERLACE_MODE,
        static_cast<UINT32>(MFVideoInterlace_Progressive));
  }
  if (FAILED(status)) {
    *error = HResultError("Configuring Media Foundation video type", status);
    return false;
  }
  return true;
}

static void CloseMediaFoundationWriter(MediaFoundationWriter* state) {
  ReleaseInterface(&state->writer);
  if (state->mf_started) {
    state->api.shutdown();
  }
  CloseMediaFoundationApi(&state->api);
  if (state->com_initialized) {
    CoUninitialize();
  }
  *state = {};
}

static bool OpenMediaFoundationWriter(
    const std::wstring& path,
    const VideoFrameSize& size,
    uint32_t fps,
    uint32_t quality,
    MediaFoundationWriter* state,
    std::string* error) {
  *state = {};
  HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(status)) {
    *error = HResultError("CoInitializeEx", status);
    return false;
  }
  state->com_initialized = true;
  if (!LoadMediaFoundationApi(&state->api, error)) {
    CloseMediaFoundationWriter(state);
    return false;
  }
  status = state->api.startup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(status)) {
    *error = HResultError("MFStartup", status);
    CloseMediaFoundationWriter(state);
    return false;
  }
  state->mf_started = true;
  status = state->api.create_sink_writer_from_url(
      path.c_str(), nullptr, nullptr, &state->writer);
  if (FAILED(status)) {
    *error = HResultError("MFCreateSinkWriterFromURL", status);
    CloseMediaFoundationWriter(state);
    return false;
  }

  IMFMediaType* output_type = nullptr;
  status = state->api.create_media_type(&output_type);
  if (SUCCEEDED(status)) {
    if (!SetVideoTypeAttributes(
            output_type, MFVideoFormat_H264, size, fps, error)) {
      status = E_FAIL;
    }
  }
  if (SUCCEEDED(status)) {
    status = output_type->SetUINT32(
        MF_MT_AVG_BITRATE, VideoBitrate(size, fps, quality));
  }
  if (SUCCEEDED(status)) {
    status = state->writer->AddStream(output_type, &state->stream_index);
  }
  ReleaseInterface(&output_type);
  if (FAILED(status)) {
    if (error->empty()) {
      *error = HResultError("Configuring H.264 output", status);
    }
    CloseMediaFoundationWriter(state);
    return false;
  }

  IMFMediaType* input_type = nullptr;
  status = state->api.create_media_type(&input_type);
  if (SUCCEEDED(status)) {
    if (!SetVideoTypeAttributes(
            input_type, MFVideoFormat_RGB32, size, fps, error)) {
      status = E_FAIL;
    }
  }
  if (SUCCEEDED(status)) {
    status = state->writer->SetInputMediaType(
        state->stream_index, input_type, nullptr);
  }
  ReleaseInterface(&input_type);
  if (SUCCEEDED(status)) {
    status = state->writer->BeginWriting();
  }
  if (FAILED(status)) {
    if (error->empty()) {
      *error = HResultError("Configuring RGB32 encoder input", status);
    }
    CloseMediaFoundationWriter(state);
    return false;
  }
  return true;
}

static bool WriteMediaFoundationFrame(
    MediaFoundationWriter* state,
    const VideoFrameSize& size,
    const std::vector<unsigned char>& frame,
    uint64_t frame_index,
    uint32_t fps,
    std::string* error) {
  const uint64_t byte_count =
      static_cast<uint64_t>(size.width) * size.height * 4;
  if (byte_count != frame.size() || byte_count > 0xffffffffu) {
    *error = "Video frame byte size is invalid.";
    return false;
  }

  IMFMediaBuffer* buffer = nullptr;
  HRESULT status = state->api.create_memory_buffer(
      static_cast<DWORD>(byte_count), &buffer);
  BYTE* destination = nullptr;
  DWORD maximum_length = 0;
  DWORD current_length = 0;
  if (SUCCEEDED(status)) {
    status = buffer->Lock(&destination, &maximum_length, &current_length);
  }
  if (SUCCEEDED(status) && maximum_length >= byte_count) {
    const size_t stride = static_cast<size_t>(size.width) * 4;
    for (int row = 0; row < size.height; row += 1) {
      std::memcpy(
          destination + static_cast<size_t>(size.height - row - 1) * stride,
          frame.data() + static_cast<size_t>(row) * stride,
          stride);
    }
  } else if (SUCCEEDED(status)) {
    status = E_UNEXPECTED;
  }
  if (destination != nullptr) {
    const HRESULT unlock_status = buffer->Unlock();
    if (SUCCEEDED(status) && FAILED(unlock_status)) {
      status = unlock_status;
    }
  }
  if (SUCCEEDED(status)) {
    status = buffer->SetCurrentLength(static_cast<DWORD>(byte_count));
  }

  IMFSample* sample = nullptr;
  if (SUCCEEDED(status)) {
    status = state->api.create_sample(&sample);
  }
  if (SUCCEEDED(status)) {
    status = sample->AddBuffer(buffer);
  }
  const LONGLONG sample_time =
      static_cast<LONGLONG>((frame_index * 10000000ull) / fps);
  const LONGLONG next_sample_time =
      static_cast<LONGLONG>(((frame_index + 1) * 10000000ull) / fps);
  if (SUCCEEDED(status)) {
    status = sample->SetSampleTime(sample_time);
  }
  if (SUCCEEDED(status)) {
    status = sample->SetSampleDuration(next_sample_time - sample_time);
  }
  if (SUCCEEDED(status)) {
    status = state->writer->WriteSample(state->stream_index, sample);
  }
  ReleaseInterface(&sample);
  ReleaseInterface(&buffer);
  if (FAILED(status)) {
    *error = HResultError("Writing H.264 sample", status);
    return false;
  }
  return true;
}

static bool CloseAndFinalizeMediaFoundationWriter(
    MediaFoundationWriter* state,
    std::string* error) {
  const HRESULT status = state->writer->Finalize();
  if (FAILED(status)) {
    *error = HResultError("Finalizing MP4 output", status);
    CloseMediaFoundationWriter(state);
    return false;
  }
  CloseMediaFoundationWriter(state);
  return true;
}

static WindowRect VirtualScreenRect() {
  return {
      GetSystemMetrics(SM_XVIRTUALSCREEN),
      GetSystemMetrics(SM_YVIRTUALSCREEN),
      std::max<int>(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)),
      std::max<int>(1, GetSystemMetrics(SM_CYVIRTUALSCREEN)),
  };
}

static bool IntersectRectangles(
    const WindowRect& first,
    const WindowRect& second,
    WindowRect* output) {
  const int64_t left = std::max<int64_t>(first.x, second.x);
  const int64_t top = std::max<int64_t>(first.y, second.y);
  const int64_t right = std::min<int64_t>(
      static_cast<int64_t>(first.x) + first.width,
      static_cast<int64_t>(second.x) + second.width);
  const int64_t bottom = std::min<int64_t>(
      static_cast<int64_t>(first.y) + first.height,
      static_cast<int64_t>(second.y) + second.height);
  if (right <= left || bottom <= top) {
    return false;
  }
  *output = {
      static_cast<int>(left),
      static_cast<int>(top),
      static_cast<int>(right - left),
      static_cast<int>(bottom - top),
  };
  return true;
}

static void CloseGdiFrameGrabber(GdiFrameGrabber* grabber) {
  if (grabber->memory != nullptr && grabber->previous_object != nullptr) {
    SelectObject(grabber->memory, grabber->previous_object);
  }
  if (grabber->bitmap != nullptr) {
    DeleteObject(grabber->bitmap);
  }
  if (grabber->memory != nullptr) {
    DeleteDC(grabber->memory);
  }
  if (grabber->screen != nullptr) {
    ReleaseDC(nullptr, grabber->screen);
  }
  *grabber = {};
}

static bool OpenGdiFrameGrabber(
    GdiFrameGrabber* grabber,
    std::string* error) {
  *grabber = {};
  grabber->screen = GetDC(nullptr);
  if (grabber->screen == nullptr) {
    *error = "GetDC failed while starting video capture.";
    return false;
  }
  grabber->memory = CreateCompatibleDC(grabber->screen);
  if (grabber->memory == nullptr) {
    *error = "CreateCompatibleDC failed while starting video capture.";
    CloseGdiFrameGrabber(grabber);
    return false;
  }
  return true;
}

static bool ResizeGdiFrameGrabber(
    GdiFrameGrabber* grabber,
    int width,
    int height,
    std::string* error) {
  if (grabber->bitmap_width == width && grabber->bitmap_height == height) {
    return true;
  }
  if (grabber->bitmap != nullptr) {
    SelectObject(grabber->memory, grabber->previous_object);
    DeleteObject(grabber->bitmap);
    grabber->bitmap = nullptr;
    grabber->previous_object = nullptr;
    grabber->bitmap_pixels = nullptr;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  grabber->bitmap = CreateDIBSection(
      grabber->screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (grabber->bitmap == nullptr || pixels == nullptr) {
    *error = "CreateDIBSection failed while recording video.";
    return false;
  }
  grabber->previous_object = SelectObject(grabber->memory, grabber->bitmap);
  if (grabber->previous_object == nullptr ||
      grabber->previous_object == HGDI_ERROR) {
    *error = "SelectObject failed while recording video.";
    DeleteObject(grabber->bitmap);
    grabber->bitmap = nullptr;
    grabber->previous_object = nullptr;
    return false;
  }
  grabber->bitmap_pixels = static_cast<unsigned char*>(pixels);
  grabber->bitmap_width = width;
  grabber->bitmap_height = height;
  return true;
}

static void MakeBlackFrame(
    const VideoFrameSize& size,
    std::vector<unsigned char>* frame) {
  frame->assign(static_cast<size_t>(size.width) * size.height * 4, 0);
  for (size_t index = 3; index < frame->size(); index += 4) {
    (*frame)[index] = 255;
  }
}

static bool CaptureGdiVideoFrame(
    GdiFrameGrabber* grabber,
    const WindowRect& requested,
    const VideoFrameSize& frame_size,
    std::vector<unsigned char>* frame,
    bool* clipped,
    std::string* error) {
  WindowRect visible = {};
  if (!IntersectRectangles(requested, VirtualScreenRect(), &visible)) {
    MakeBlackFrame(frame_size, frame);
    *clipped = true;
    return true;
  }
  if (!ResizeGdiFrameGrabber(
          grabber, visible.width, visible.height, error)) {
    return false;
  }
  if (!BitBlt(
          grabber->memory,
          0,
          0,
          visible.width,
          visible.height,
          grabber->screen,
          visible.x,
          visible.y,
          SRCCOPY | CAPTUREBLT)) {
    *error = "BitBlt failed while recording video.";
    return false;
  }
  const size_t visible_bytes =
      static_cast<size_t>(visible.width) * visible.height * 4;
  const std::vector<unsigned char> visible_pixels(
      grabber->bitmap_pixels, grabber->bitmap_pixels + visible_bytes);
  if (!ComposeVideoFrameBgra(
          requested, visible, visible_pixels, frame_size, frame, error)) {
    return false;
  }
  *clipped =
      *clipped || requested.x != visible.x || requested.y != visible.y ||
      requested.width != visible.width || requested.height != visible.height ||
      requested.width > frame_size.width || requested.height > frame_size.height;
  return true;
}

static bool IdToHandle(const std::string& value, HWND* window) {
  const char* start = value.c_str();
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    start += 2;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(start, &end, 16);
  if (end == start || *end != '\0' || parsed == 0) {
    return false;
  }
  *window = reinterpret_cast<HWND>(static_cast<uintptr_t>(parsed));
  return true;
}

static bool CreateVideoTempPath(
    std::string* directory,
    std::wstring* path,
    std::string* error) {
  wchar_t temp_directory[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, temp_directory);
  if (length == 0 || length >= MAX_PATH) {
    *error = "GetTempPathW failed while creating video output.";
    return false;
  }
  wchar_t unique_path[MAX_PATH] = {};
  if (GetTempFileNameW(temp_directory, L"arv", 0, unique_path) == 0) {
    *error = "GetTempFileNameW failed while creating video output.";
    return false;
  }
  if (!DeleteFileW(unique_path) || !CreateDirectoryW(unique_path, nullptr)) {
    DeleteFileW(unique_path);
    *error = "CreateDirectoryW failed while creating video output.";
    return false;
  }
  std::wstring video_path(unique_path);
  video_path += L"\\capture.mp4";
  *directory = WideToUtf8(std::wstring(unique_path));
  *path = video_path;
  return true;
}

static uint64_t CounterValue() {
  LARGE_INTEGER value = {};
  QueryPerformanceCounter(&value);
  return static_cast<uint64_t>(value.QuadPart);
}

static bool IsCancelled(volatile LONG* cancelled) {
  return cancelled != nullptr &&
         InterlockedCompareExchange(cancelled, 0, 0) != 0;
}

bool IsVideoCaptureSupported() {
  MediaFoundationApi api = {};
  const bool available = LoadMediaFoundationApi(&api, nullptr);
  CloseMediaFoundationApi(&api);
  return available;
}

bool RecordVideoToFile(
    const VideoCaptureRequest& request,
    volatile LONG* cancelled,
    VideoCaptureResult* result,
    std::string* error) {
  *result = {};
  const VideoFrameSize frame_size = CreateVideoFrameSize(request.initial_bounds);
  if (frame_size.width == 0 || frame_size.height == 0 || request.fps == 0 ||
      request.quality == 0 || request.quality > 100 ||
      request.duration_ms == 0) {
    *error = "Video capture parameters are invalid.";
    return false;
  }

  HWND window = nullptr;
  if (!request.window_id.empty() && !IdToHandle(request.window_id, &window)) {
    *error = "Invalid window id for video capture.";
    return false;
  }
  std::wstring wide_path;
  if (!CreateVideoTempPath(&result->directory, &wide_path, error)) {
    return false;
  }
  result->path = WideToUtf8(wide_path);
  result->fps = request.fps;
  result->initial_bounds = request.initial_bounds;
  result->final_bounds = request.initial_bounds;

  MediaFoundationWriter writer = {};
  if (!OpenMediaFoundationWriter(
          wide_path,
          frame_size,
          request.fps,
          request.quality,
          &writer,
          error)) {
    RemoveVideoCaptureResult(*result);
    return false;
  }
  GdiFrameGrabber grabber = {};
  if (!OpenGdiFrameGrabber(&grabber, error)) {
    CloseMediaFoundationWriter(&writer);
    RemoveVideoCaptureResult(*result);
    return false;
  }

  LARGE_INTEGER frequency_value = {};
  if (!QueryPerformanceFrequency(&frequency_value) ||
      frequency_value.QuadPart <= 0) {
    *error = "QueryPerformanceFrequency failed while recording video.";
    CloseGdiFrameGrabber(&grabber);
    CloseMediaFoundationWriter(&writer);
    RemoveVideoCaptureResult(*result);
    return false;
  }
  const uint64_t frequency = static_cast<uint64_t>(frequency_value.QuadPart);
  const uint64_t started = CounterValue();
  const uint64_t target_frames =
      (static_cast<uint64_t>(request.duration_ms) * request.fps + 999) / 1000;
  uint64_t frame_index = 0;
  std::vector<unsigned char> frame;
  bool succeeded = true;
  while (frame_index < target_frames) {
    if (IsCancelled(cancelled)) {
      *error = "Video recording was cancelled.";
      succeeded = false;
      break;
    }
    const uint64_t deadline =
        started + (frame_index * frequency) / request.fps;
    uint64_t now = CounterValue();
    while (now < deadline) {
      const uint64_t remaining_ms =
          ((deadline - now) * 1000) / frequency;
      Sleep(static_cast<DWORD>(std::min<uint64_t>(remaining_ms, 10)));
      if (IsCancelled(cancelled)) {
        break;
      }
      now = CounterValue();
    }
    if (IsCancelled(cancelled)) {
      *error = "Video recording was cancelled.";
      succeeded = false;
      break;
    }

    now = CounterValue();
    const uint64_t elapsed_frame =
        ((now - started) * request.fps) / frequency;
    if (elapsed_frame > frame_index) {
      const uint64_t skipped =
          std::min<uint64_t>(elapsed_frame - frame_index,
                             target_frames - frame_index);
      result->dropped_frames += static_cast<uint32_t>(skipped);
      frame_index += skipped;
      if (frame_index >= target_frames) {
        break;
      }
    }

    WindowRect current_bounds = request.initial_bounds;
    if (window != nullptr &&
        !ReadWindowFrameBounds(window, &current_bounds, error)) {
      succeeded = false;
      break;
    }
    result->final_bounds = current_bounds;
    const WindowRect capture_bounds = SelectVideoCaptureBounds(
        request.tracking, request.initial_bounds, current_bounds);
    if (!CaptureGdiVideoFrame(
            &grabber,
            capture_bounds,
            frame_size,
            &frame,
            &result->clipped,
            error) ||
        !WriteMediaFoundationFrame(
            &writer,
            frame_size,
            frame,
            frame_index,
            request.fps,
            error)) {
      succeeded = false;
      break;
    }
    result->frame_count += 1;
    frame_index += 1;
  }

  CloseGdiFrameGrabber(&grabber);
  if (succeeded && result->frame_count == 0) {
    *error = "Video recording did not capture any frames.";
    succeeded = false;
  }
  if (succeeded && !CloseAndFinalizeMediaFoundationWriter(&writer, error)) {
    succeeded = false;
  } else if (!succeeded) {
    CloseMediaFoundationWriter(&writer);
  }
  if (!succeeded) {
    RemoveVideoCaptureResult(*result);
    return false;
  }
  result->duration_ms = static_cast<uint32_t>(
      (target_frames * 1000 + request.fps - 1) / request.fps);
  return true;
}

void RemoveVideoCaptureResult(const VideoCaptureResult& result) {
  const std::wstring path = Utf8ToWide(result.path);
  if (!path.empty()) {
    DeleteFileW(path.c_str());
  }
  const std::wstring directory = Utf8ToWide(result.directory);
  if (!directory.empty()) {
    RemoveDirectoryW(directory.c_str());
  }
}

}  // namespace agent_rover
