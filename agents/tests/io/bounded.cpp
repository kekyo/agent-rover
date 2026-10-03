#include "win32_files.h"
#include "video_recording.h"
#include <cstdio>

static HANDLE entered = nullptr, release = nullptr;
namespace agent_rover {
// A deterministic stand-in for a codec blocked inside a native call.
bool RecordVideoToFile(const VideoCaptureRequest&, volatile LONG*, VideoCaptureResult*, std::string*) {
  SetEvent(entered);
  WaitForSingleObject(release, INFINITE);
  return false;
}
void RemoveVideoCaptureResult(const VideoCaptureResult&) {}
}

int wmain(int argc, wchar_t** argv) {
  using namespace agent_rover;
  if (argc == 2 && std::wstring(argv[1]) == L"video") {
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    VideoRecordingStore store = {}; VideoCaptureRequest request = {}; std::string error;
    request.duration_ms = 1;
    if (!StartVideoRecording(&store, "stalled", request, &error) || WaitForSingleObject(entered, 10000) != WAIT_OBJECT_0) return 2;
    CancelVideoRecording(&store);
    if (!store.thread || !store.context || WaitForSingleObject(store.thread, 0) != WAIT_TIMEOUT) return 3;
    // A deadline must retain native ownership until actual completion.
    SetEvent(release);
    if (WaitForSingleObject(store.thread, 10000) != WAIT_OBJECT_0) return 4;
    CancelVideoRecording(&store);
    if (store.thread || store.context) return 5;
    CloseHandle(entered); CloseHandle(release);
  } else {
    wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    if (!GetTempFileNameW(directory, L"ari", 0, path)) return 6;
    const auto file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 7;
    SetFilePointer(file, 64 * 1024 * 1024 + 1, nullptr, FILE_BEGIN);
    SetEndOfFile(file); CloseHandle(file);
    char utf8[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
    std::vector<unsigned char> bytes;
    OperationError error;
    const auto accepted = ReadFileBytes(utf8, 64ull * 1024 * 1024, &bytes, &error);
    if (accepted || !bytes.empty()) return 8;
    if (!ReadFileBytes(utf8, 65ull * 1024 * 1024, &bytes, &error) ||
        bytes.size() != 64ull * 1024 * 1024 + 1) return 11;
    DeleteFileW(path);
    const std::vector<unsigned char> expected = {'a', 0, 'b', 0xff};
    if (!WriteFileBytes(utf8, expected, &error) || !ReadFileBytes(utf8, 4, &bytes, &error)) return 9;
    if (bytes != expected) return 10;
    bytes.clear();
    if (ReadFileBytes(utf8, 3, &bytes, &error) || !bytes.empty()) return 12;
    if (!ReadCaptureFileBytes(utf8, true, 4, &bytes, &error) || bytes != expected) return 13;
    bytes.clear();
    if (ReadCaptureFileBytes(utf8, true, 3, &bytes, &error) || !bytes.empty()) return 14;
    DeleteFileW(path);
  }
  std::puts("bounded native operation passed");
  return 0;
}
