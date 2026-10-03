// agent-rover - File persistence confined to an isolated helper
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "log_storage.h"
#include "win32_util.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace agent_rover {

struct LogStorage {
  std::wstring directory, path;
  std::string session;
  HANDLE file = INVALID_HANDLE_VALUE;
  uint64_t bytes = 0;
  uint32_t generation = 0;
  ~LogStorage() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};

static std::runtime_error StorageError(const char* operation, DWORD code) {
  return std::runtime_error(std::string(operation) + " Win32=" + std::to_string(code));
}

static void PruneClosedLogs(const std::wstring& directory) {
  struct Entry { std::wstring name; uint64_t modified; };
  std::vector<Entry> entries;
  WIN32_FIND_DATAW data = {};
  const auto search = FindFirstFileW((directory + L"\\agent-rover-*.log").c_str(), &data);
  if (search == INVALID_HANDLE_VALUE) {
    const auto code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND) return;
    throw StorageError("FindFirstFileW(logs)", code);
  }
  DWORD result = ERROR_SUCCESS;
  do {
    if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
    if (entries.size() == 4096) { result = ERROR_TOO_MANY_OPEN_FILES; break; }
    const uint64_t modified = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
        data.ftLastWriteTime.dwLowDateTime;
    entries.push_back({data.cFileName, modified});
  } while (FindNextFileW(search, &data));
  if (result == ERROR_SUCCESS && GetLastError() != ERROR_NO_MORE_FILES) result = GetLastError();
  FindClose(search);
  if (result != ERROR_SUCCESS) throw StorageError("FindNextFileW(logs)", result);
  std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
    return left.modified == right.modified ? left.name < right.name : left.modified < right.modified;
  });
  size_t remaining = entries.size();
  for (const auto& entry : entries) {
    if (remaining < 5) break;  // Reserve one of the five files for the new writer.
    if (DeleteFileW((directory + L"\\" + entry.name).c_str())) { --remaining; continue; }
    const auto code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND) { --remaining; continue; }
    // Active writers deny FILE_SHARE_DELETE, including writers in other instances.
    if (code != ERROR_SHARING_VIOLATION) throw StorageError("DeleteFileW(log rotation)", code);
  }
}

static void RotateLog(LogStorage* storage) {
  if (storage->file != INVALID_HANDLE_VALUE) {
    const auto file = storage->file;
    storage->file = INVALID_HANDLE_VALUE;
    if (!CloseHandle(file)) throw StorageError("CloseHandle(log rotation)", GetLastError());
  }
  PruneClosedLogs(storage->directory);
  for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
    storage->path = storage->directory + L"\\agent-rover-" + Utf8ToWide(storage->session) + L"-" +
        std::to_wstring(storage->generation++) + L".log";
    storage->file = CreateFileW(storage->path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (storage->file != INVALID_HANDLE_VALUE) { storage->bytes = 0; return; }
    const auto code = GetLastError();
    if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) throw StorageError("CreateFileW(log)", code);
  }
  throw std::runtime_error("Log filename collision limit exceeded.");
}

LogStorageHandle OpenLogStorage(const std::wstring& directory, const std::string& session) {
  if (session.empty() || session.size() > 128 || session.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") != std::string::npos)
    throw std::runtime_error("Invalid log session identifier.");
  auto storage = std::make_shared<LogStorage>();
  storage->directory = directory;
  storage->session = session;
  RotateLog(storage.get());
  return storage;
}

cardio::promise<void> AppendLogStorage(LogStorageHandle storage, std::string lines) {
  if (lines.size() > 66560) throw std::runtime_error("Log batch size limit exceeded.");
  if (storage->bytes && storage->bytes + lines.size() > 10 * 1024 * 1024) RotateLog(storage.get());
  auto bytes = std::as_bytes(std::span(lines));
  while (!bytes.empty()) {
    const auto written = co_await cardio::win32::write(storage->file, bytes, storage->bytes);
    if (!written) throw std::runtime_error("WriteFile(log) returned zero bytes.");
    storage->bytes += written;
    bytes = bytes.subspan(written);
  }
}

void FlushLogStorage(const LogStorageHandle& storage) {
  if (!FlushFileBuffers(storage->file)) throw StorageError("FlushFileBuffers(log)", GetLastError());
}

std::wstring LogStoragePath(const LogStorageHandle& storage) { return storage->path; }

}  // namespace agent_rover
