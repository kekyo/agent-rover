// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_files.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>

#include "binary_codec.h"
#include "win32_util.h"

namespace agent_rover {

static bool IsSeparator(wchar_t ch) {
  return ch == L'\\' || ch == L'/';
}

static bool DirectoryExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool CreateOneDirectory(const std::wstring& path, std::string* error) {
  if (path.empty() || DirectoryExists(path)) {
    return true;
  }
  if (CreateDirectoryW(path.c_str(), nullptr)) {
    return true;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS && DirectoryExists(path)) {
    return true;
  }
  *error = "CreateDirectoryW failed. path=" + WideToUtf8(path) +
           " win32Error=" + std::to_string(GetLastError());
  return false;
}

static std::string Win32PathError(
    const std::string& message,
    const std::wstring& path,
    DWORD error_code) {
  return message + " path=" + WideToUtf8(path) +
         " win32Error=" + std::to_string(error_code);
}

static std::string FileTimeIso(const FILETIME& file_time) {
  SYSTEMTIME system_time = {};
  if (!FileTimeToSystemTime(&file_time, &system_time)) {
    return std::string();
  }
  char buffer[32] = {};
  std::snprintf(
      buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
      system_time.wYear, system_time.wMonth, system_time.wDay,
      system_time.wHour, system_time.wMinute, system_time.wSecond,
      system_time.wMilliseconds);
  return std::string(buffer);
}

static bool IsDangerousRemovePath(const std::wstring& path) {
  if (path.empty()) {
    return true;
  }
  if (path.size() == 2 && path[1] == L':') {
    return true;
  }
  if (path.size() == 3 && path[1] == L':' && IsSeparator(path[2])) {
    return true;
  }
  return false;
}

static std::wstring JoinPath(
    const std::wstring& directory,
    const std::wstring& name) {
  if (directory.empty() || IsSeparator(directory[directory.size() - 1])) {
    return directory + name;
  }
  return directory + L"\\" + name;
}

static bool EnsureParentDirectories(
    const std::wstring& path,
    std::string* error) {
  const size_t separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return true;
  }
  const std::wstring parent = path.substr(0, separator);
  if (parent.empty() || DirectoryExists(parent)) {
    return true;
  }

  size_t index = 0;
  if (parent.size() >= 2 && parent[1] == L':') {
    index = 2;
  }
  while (index < parent.size() && IsSeparator(parent[index])) {
    index += 1;
  }

  while (index < parent.size()) {
    const size_t next = parent.find_first_of(L"\\/", index);
    const size_t end = next == std::wstring::npos ? parent.size() : next;
    const std::wstring current = parent.substr(0, end);
    if (!current.empty() && !CreateOneDirectory(current, error)) {
      return false;
    }
    if (next == std::wstring::npos) {
      break;
    }
    index = next + 1;
  }
  return CreateOneDirectory(parent, error);
}

bool ReadFileBytes(
    const std::string& path,
    std::vector<unsigned char>* data,
    std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "File path is empty or invalid UTF-8.";
    return false;
  }

  HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *error = Win32PathError(
        "CreateFileW for read failed.", wide_path, GetLastError());
    return false;
  }

  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) >
          static_cast<unsigned long long>(0xffffffffu)) {
    CloseHandle(file);
    *error = "File size is unsupported.";
    return false;
  }

  data->assign(static_cast<size_t>(size.QuadPart), 0);
  size_t offset = 0;
  while (offset < data->size()) {
    const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data->size() - offset, 64 * 1024));
    DWORD read = 0;
    if (!ReadFile(file, data->data() + offset, chunk, &read, nullptr)) {
      const DWORD error_code = GetLastError();
      CloseHandle(file);
      *error = Win32PathError("ReadFile failed.", wide_path, error_code);
      return false;
    }
    if (read == 0) {
      break;
    }
    offset += read;
  }
  data->resize(offset);
  CloseHandle(file);
  return true;
}

bool WriteFileBytes(
    const std::string& path,
    const std::vector<unsigned char>& data,
    std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "File path is empty or invalid UTF-8.";
    return false;
  }
  if (!EnsureParentDirectories(wide_path, error)) {
    return false;
  }

  HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *error = Win32PathError(
        "CreateFileW for write failed.", wide_path, GetLastError());
    return false;
  }

  size_t offset = 0;
  while (offset < data.size()) {
    const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data.size() - offset, 64 * 1024));
    DWORD written = 0;
    if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr)) {
      const DWORD error_code = GetLastError();
      CloseHandle(file);
      *error = Win32PathError("WriteFile failed.", wide_path, error_code);
      return false;
    }
    if (written == 0) {
      CloseHandle(file);
      *error = "WriteFile wrote zero bytes.";
      return false;
    }
    offset += written;
  }
  CloseHandle(file);
  return true;
}

bool PathExists(const std::string& path) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    return false;
  }
  return GetFileAttributesW(wide_path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool StatPath(const std::string& path, FileStat* stat, std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "File path is empty or invalid UTF-8.";
    return false;
  }
  WIN32_FILE_ATTRIBUTE_DATA data = {};
  if (!GetFileAttributesExW(
          wide_path.c_str(), GetFileExInfoStandard, &data)) {
    *error = "GetFileAttributesExW failed.";
    return false;
  }
  const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  stat->type = directory ? "directory" : "file";
  stat->size =
      directory ? 0
                : (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                      static_cast<uint64_t>(data.nFileSizeLow);
  stat->created_at = FileTimeIso(data.ftCreationTime);
  stat->modified_at = FileTimeIso(data.ftLastWriteTime);
  return true;
}

bool MakeDirectory(
    const std::string& path,
    bool recursive,
    std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "Directory path is empty or invalid UTF-8.";
    return false;
  }
  if (recursive && !EnsureParentDirectories(wide_path, error)) {
    return false;
  }
  return CreateOneDirectory(wide_path, error);
}

bool ReadDirectoryEntries(
    const std::string& path,
    std::vector<DirectoryEntry>* entries,
    std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "Directory path is empty or invalid UTF-8.";
    return false;
  }
  entries->clear();
  const std::wstring pattern = JoinPath(wide_path, L"*");
  WIN32_FIND_DATAW data = {};
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) {
    *error = "FindFirstFileW failed.";
    return false;
  }
  do {
    const std::wstring name(data.cFileName);
    if (name == L"." || name == L"..") {
      continue;
    }
    const std::string child_path = WideToUtf8(JoinPath(wide_path, name));
    DirectoryEntry entry = {};
    entry.name = WideToUtf8(name);
    if (!StatPath(child_path, &entry.stat, error)) {
      FindClose(find);
      return false;
    }
    entries->push_back(entry);
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return true;
}

static std::wstring JoinRelativePath(
    const std::wstring& relative,
    const std::wstring& name) {
  return relative.empty() ? name : relative + L"\\" + name;
}

static std::string ManifestRelativePath(const std::wstring& relative) {
  std::string output = WideToUtf8(relative);
  for (char& ch : output) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return output;
}

static bool ReadDirectoryManifestRecursive(
    const std::wstring& root,
    const std::wstring& relative,
    std::vector<DirectoryManifestEntry>* entries,
    std::string* error) {
  const std::wstring directory = relative.empty() ? root : JoinPath(root, relative);
  const std::wstring pattern = JoinPath(directory, L"*");
  WIN32_FIND_DATAW data = {};
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) {
    const DWORD error_code = GetLastError();
    if (error_code == ERROR_FILE_NOT_FOUND) {
      return true;
    }
    *error = Win32PathError("FindFirstFileW failed.", directory, error_code);
    return false;
  }
  do {
    const std::wstring name(data.cFileName);
    if (name == L"." || name == L"..") {
      continue;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      continue;
    }
    const std::wstring child_relative = JoinRelativePath(relative, name);
    const std::wstring child_path = JoinPath(root, child_relative);
    const bool directory_entry =
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    DirectoryManifestEntry entry = {};
    entry.path = ManifestRelativePath(child_relative);
    entry.type = directory_entry ? "directory" : "file";
    entry.size =
        directory_entry
            ? 0
            : (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                  static_cast<uint64_t>(data.nFileSizeLow);
    entry.modified_at = FileTimeIso(data.ftLastWriteTime);
    entry.has_sha256 = false;
    if (!directory_entry) {
      std::vector<unsigned char> bytes;
      if (!ReadFileBytes(WideToUtf8(child_path), &bytes, error)) {
        FindClose(find);
        return false;
      }
      entry.sha256 = Sha256Hex(bytes);
      entry.has_sha256 = true;
    }
    entries->push_back(entry);
    if (directory_entry &&
        !ReadDirectoryManifestRecursive(root, child_relative, entries, error)) {
      FindClose(find);
      return false;
    }
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return true;
}

bool ReadDirectoryManifest(
    const std::string& path,
    std::vector<DirectoryManifestEntry>* entries,
    std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty()) {
    *error = "Directory path is empty or invalid UTF-8.";
    return false;
  }
  if (!DirectoryExists(wide_path)) {
    *error = Win32PathError(
        "Directory does not exist.", wide_path, ERROR_PATH_NOT_FOUND);
    return false;
  }
  entries->clear();
  return ReadDirectoryManifestRecursive(wide_path, L"", entries, error);
}

bool RemovePath(const std::string& path, bool recursive, std::string* error) {
  const std::wstring wide_path = Utf8ToWide(path);
  if (IsDangerousRemovePath(wide_path)) {
    *error = "Refusing to remove dangerous path.";
    return false;
  }
  FileStat stat = {};
  if (!StatPath(path, &stat, error)) {
    return false;
  }
  if (stat.type == "directory") {
    if (recursive) {
      std::vector<DirectoryEntry> entries;
      if (!ReadDirectoryEntries(path, &entries, error)) {
        return false;
      }
      for (const DirectoryEntry& entry : entries) {
        if (!RemovePath(
                WideToUtf8(JoinPath(wide_path, Utf8ToWide(entry.name))), true,
                error)) {
          return false;
        }
      }
    }
    if (!RemoveDirectoryW(wide_path.c_str())) {
      *error = Win32PathError(
          "RemoveDirectoryW failed.", wide_path, GetLastError());
      return false;
    }
    return true;
  }
  if (!DeleteFileW(wide_path.c_str())) {
    *error = Win32PathError("DeleteFileW failed.", wide_path, GetLastError());
    return false;
  }
  return true;
}

bool RenamePath(
    const std::string& from,
    const std::string& to,
    std::string* error) {
  const std::wstring wide_from = Utf8ToWide(from);
  const std::wstring wide_to = Utf8ToWide(to);
  if (wide_from.empty() || wide_to.empty()) {
    *error = "Rename path is empty or invalid UTF-8.";
    return false;
  }
  if (!EnsureParentDirectories(wide_to, error)) {
    return false;
  }
  if (!MoveFileExW(
          wide_from.c_str(), wide_to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    *error = Win32PathError("MoveFileExW failed.", wide_to, GetLastError());
    return false;
  }
  return true;
}

bool MakeTempDirectory(
    const std::string& prefix,
    std::string* path,
    std::string* error) {
  for (int index = 0; index < 1000; index += 1) {
    const std::string candidate = prefix + std::to_string(index);
    if (PathExists(candidate)) {
      continue;
    }
    if (MakeDirectory(candidate, true, error)) {
      *path = candidate;
      return true;
    }
  }
  *error = "Unable to create temporary directory.";
  return false;
}

}  // namespace agent_rover
