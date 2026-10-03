#include "log_storage.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace agent_rover;
static void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
static std::vector<std::wstring> Files(const std::wstring& directory) {
  WIN32_FIND_DATAW entry = {};
  const auto search = FindFirstFileW((directory + L"\\agent-rover-*.log").c_str(), &entry);
  std::vector<std::wstring> files;
  if (search == INVALID_HANDLE_VALUE) return files;
  do {
    Require(entry.nFileSizeHigh == 0 && entry.nFileSizeLow <= 10 * 1024 * 1024, "File exceeds 10 MiB.");
    files.push_back(directory + L"\\" + entry.cFileName);
  } while (FindNextFileW(search, &entry));
  FindClose(search);
  return files;
}
static cardio::promise<void> Check(const std::wstring& directory, int* result) {
  try {
    auto other = OpenLogStorage(directory, "other");
    const auto other_path = LogStoragePath(other);
    auto storage = OpenLogStorage(directory, "rotation");
    std::string batch(65535, 'x'); batch += '\n';
    // Six generations exercise actual 10 MiB boundaries and pruning.
    for (unsigned int i = 0; i < 6 * 160 + 1; ++i) co_await AppendLogStorage(storage, batch);
    Require(Files(directory).size() == 5, "Closed logs were not pruned to five files.");
    Require(GetFileAttributesW(other_path.c_str()) != INVALID_FILE_ATTRIBUTES, "Active writer was deleted.");
    const auto final_path = LogStoragePath(storage);
    std::string marker = "UTF-8: 日本語\n";
    co_await AppendLogStorage(storage, marker);
    FlushLogStorage(storage);
    storage.reset(); other.reset();
    const auto file = CreateFileW(final_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Cannot read closed log.");
    std::string bytes(GetFileSize(file, nullptr), '\0'); DWORD read = 0;
    const auto ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr);
    CloseHandle(file);
    Require(ok && read == bytes.size() && bytes == batch + marker, "Async append offsets or UTF-8 bytes changed.");
    bool rejected = false;
    try { auto invalid = OpenLogStorage(directory + L"\\missing", "failure"); }
    catch (const std::exception&) { rejected = true; }
    Require(rejected, "Missing directory must report a failure.");
    *result = 0;
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
}
int wmain() {
  wchar_t temporary[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, temporary);
  const auto directory = std::wstring(temporary) + L"agent-rover-log-storage-" + std::to_wstring(GetCurrentProcessId());
  if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
  int result = 1;
  {
    cardio::dispatcher_host_win32_auto dispatcher;
    auto checking = Check(directory, &result);
    dispatcher.park();
  }
  for (const auto& file : Files(directory)) DeleteFileW(file.c_str());
  RemoveDirectoryW(directory.c_str());
  if (!result) std::puts("rotation, active writer, UTF-8, offsets and failure passed");
  return result;
}
