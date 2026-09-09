// agent-rover - Windows cleanup ownership and repair
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

// Declarations only: newer entry points are resolved dynamically so ordinary
// removal does not introduce new mandatory imports on legacy Windows.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <aclapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include "win32_cleanup.h"
#include "win32_files.h"
#include "win32_util.h"

namespace agent_rover {

using FinalName = DWORD (WINAPI*)(HANDLE, LPWSTR, DWORD, DWORD);
using SetInformation = BOOL (WINAPI*)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
static const DWORD kFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
static const DWORD kShare = FILE_SHARE_READ | FILE_SHARE_WRITE;
static std::map<std::wstring, BY_HANDLE_FILE_INFORMATION> capture_directories;
static uint64_t next_capture_directory = 1;

static bool Failure(const char* operation, const std::string& path, DWORD code, OperationError* error) {
  *error = MakeOperationError(operation, path, code);
  return false;
}

static bool SameObject(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
  return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
      a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

static bool AbsolutePath(const std::string& path, std::wstring* full, OperationError* error) {
  const auto wide = Utf8ToWide(path);
  // Repair is deliberately restricted to unambiguous local filesystem paths.
  // Device names, streams, UNC paths and drive-relative names are not repair targets.
  if (wide.size() < 4 || wide[1] != L':' || (wide[2] != L'/' && wide[2] != L'\\') ||
      wide.find(L':', 2) != std::wstring::npos || wide.find_first_of(L"*?\0", 0, 3) != std::wstring::npos) {
    Failure("CleanupPath", path, ERROR_INVALID_PARAMETER, error); error->reason = "invalidArgument"; return false;
  }
  std::vector<wchar_t> buffer(32768);
  const auto size = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
  if (size == 0 || size >= buffer.size()) return Failure("GetFullPathNameW", path, size == 0 ? GetLastError() : ERROR_FILENAME_EXCED_RANGE, error);
  *full = std::wstring(buffer.data(), size);
  std::replace(full->begin(), full->end(), L'/', L'\\');
  while (full->size() > 3 && full->back() == L'\\') full->pop_back();
  if (full->size() <= 3) { Failure("CleanupPath", path, ERROR_INVALID_PARAMETER, error); error->reason = "invalidArgument"; return false; }
  return true;
}

static bool UserToken(std::vector<unsigned char>* user, OperationError* error) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return Failure("OpenProcessToken", "", GetLastError(), error);
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  const auto code = GetLastError();
  if (code != ERROR_INSUFFICIENT_BUFFER) { CloseHandle(token); return Failure("GetTokenInformation", "", code, error); }
  user->resize(size);
  const bool ok = GetTokenInformation(token, TokenUser, user->data(), size, &size);
  const auto saved = GetLastError();
  CloseHandle(token);
  return ok || Failure("GetTokenInformation", "", saved, error);
}

static bool PinParents(const std::wstring& path, std::vector<HANDLE>* handles, OperationError* error) {
  for (size_t end = 3; end < path.size(); ) {
    const auto parent = path.substr(0, end);
    const auto handle = CreateFileW(parent.c_str(), 0, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return Failure("CreateFileW", WideToUtf8(parent), GetLastError(), error);
    handles->push_back(handle);
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(handle, &info)) return Failure("GetFileInformationByHandle", WideToUtf8(parent), GetLastError(), error);
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      Failure("CleanupBoundary", WideToUtf8(parent), ERROR_NOT_SUPPORTED, error); error->reason = "unsupported"; return false;
    }
    const auto next = path.find(L'\\', end == 3 ? end : end + 1);
    if (next == std::wstring::npos) break;
    end = next;
  }
  return true;
}

static bool RemoveEntry(const std::wstring& wide, const CleanupPolicy& policy, const BY_HANDLE_FILE_INFORMATION* expected, FinalName final_name,
                        SetInformation set_information, OperationError* error) {
  const auto path = WideToUtf8(wide);
  // MAXIMUM_ALLOWED also makes SetSecurityInfo avoid propagating inherited ACEs
  // to children. The handle never enables privileges or takes ownership.
  auto handle = CreateFileW(wide.c_str(), MAXIMUM_ALLOWED, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const auto code = GetLastError();
    if (policy.ignore_missing && (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)) return true;
    return Failure("CreateFileW", path, code, error);
  }
  BY_HANDLE_FILE_INFORMATION original = {};
  if (!GetFileInformationByHandle(handle, &original)) {
    const auto code = GetLastError(); CloseHandle(handle); return Failure("GetFileInformationByHandle", path, code, error);
  }
  if (expected != nullptr && !SameObject(*expected, original)) {
    CloseHandle(handle);
    Failure("CleanupIdentity", path, ERROR_INVALID_DATA, error); error->reason = "unsupported"; return false;
  }
  const bool directory = (original.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const bool reparse = (original.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  HANDLE security_recovery = INVALID_HANDLE_VALUE;
  HANDLE attribute_recovery = INVALID_HANDLE_VALUE;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PACL old_acl = nullptr;
  std::vector<CleanupRepair> repairs;
  bool granted = false;
  bool cleared = false;

  const auto allow_repair = [&](const char* action) {
    if (!reparse && (directory || original.nNumberOfLinks == 1)) return true;
    repairs.push_back({path, action, "skipped", ERROR_NOT_SUPPORTED});
    Failure("CleanupBoundary", path, ERROR_NOT_SUPPORTED, error); error->reason = "unsupported";
    return false;
  };
  const auto refresh = [&]() {
    // Retain rollback handles to the original object across this necessary gap.
    // Reopening the pathname must match its identity before any further change.
    CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
    const auto next = CreateFileW(wide.c_str(), MAXIMUM_ALLOWED, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
    if (next == INVALID_HANDLE_VALUE) return Failure("CreateFileW", path, GetLastError(), error);
    BY_HANDLE_FILE_INFORMATION info = {};
    const bool read = GetFileInformationByHandle(next, &info);
    const auto code = GetLastError();
    if (!read || !SameObject(original, info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      CloseHandle(next); Failure("CleanupIdentity", path, read ? ERROR_INVALID_DATA : code, error); error->reason = "unsupported"; return false;
    }
    handle = next;
    return true;
  };
  const auto grant = [&]() {
    if (!policy.grant_delete || granted) return false;
    if (!allow_repair("grantDelete")) return false;
    CleanupRepair repair{path, "grantDelete", "failed"};
    // The existing handle pins this pathname. ReOpenFile can require directory
    // traversal before the missing traversal permission has been repaired.
    const char* operation = "CreateFileW";
    security_recovery = CreateFileW(wide.c_str(), READ_CONTROL | WRITE_DAC, kShare | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, kFlags, nullptr);
    DWORD code = security_recovery == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (code == ERROR_SUCCESS) { operation = "GetSecurityInfo"; code = GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_acl, nullptr, &descriptor); }
    if (code == ERROR_SUCCESS && old_acl != nullptr) {
      // Do not reinterpret or weaken existing deny rules, including group and
      // conditional ACEs. Such DACLs require an explicit policy decision by the caller.
      for (DWORD index = 0; index < old_acl->AceCount; ++index) {
        void* ace = nullptr;
        if (!GetAce(old_acl, index, &ace)) { operation = "GetAce"; code = GetLastError(); break; }
        const auto type = static_cast<ACE_HEADER*>(ace)->AceType;
        if (type == ACCESS_DENIED_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE ||
            type == ACCESS_DENIED_CALLBACK_ACE_TYPE || type == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE) {
          repair.outcome = "skipped"; repair.os_code = ERROR_ACCESS_DENIED; repairs.push_back(repair);
          error->reason = "permissionDenied";
          return false;
        }
      }
    }
    std::vector<unsigned char> user;
    OperationError token_error;
    if (code == ERROR_SUCCESS && !UserToken(&user, &token_error)) { code = token_error.os_code; operation = "GetTokenInformation"; }
    PACL updated = nullptr;
    if (code == ERROR_SUCCESS) {
      EXPLICIT_ACCESS_W entry = {};
      entry.grfAccessPermissions = DELETE | FILE_READ_ATTRIBUTES | (directory ? FILE_LIST_DIRECTORY | FILE_TRAVERSE : 0) |
          (policy.clear_read_only ? FILE_WRITE_ATTRIBUTES : 0);
      entry.grfAccessMode = GRANT_ACCESS;
      entry.grfInheritance = NO_INHERITANCE;
      entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
      entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
      operation = "SetEntriesInAclW";
      code = SetEntriesInAclW(1, &entry, old_acl, &updated);
      if (code == ERROR_SUCCESS) { operation = "SetSecurityInfo"; code = SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, updated, nullptr); }
    }
    if (updated != nullptr) LocalFree(updated);
    repair.os_code = code;
    if (code == ERROR_SUCCESS) { granted = true; repair.outcome = "applied"; }
    repairs.push_back(repair);
    if (code != ERROR_SUCCESS) { Failure(operation, path, code, error); error->reason = "permissionDenied"; return false; }
    return refresh();
  };
  const auto clear = [&]() {
    if (!allow_repair("clearReadOnly")) return false;
    attribute_recovery = CreateFileW(wide.c_str(), FILE_WRITE_ATTRIBUTES, kShare | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, kFlags, nullptr);
    if (attribute_recovery == INVALID_HANDLE_VALUE) {
      const auto saved = GetLastError();
      Failure("CreateFileW", path, saved, error);
      if (saved != ERROR_ACCESS_DENIED || !grant()) return false;
      attribute_recovery = CreateFileW(wide.c_str(), FILE_WRITE_ATTRIBUTES, kShare | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, kFlags, nullptr);
    }
    DWORD code = attribute_recovery == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    FILE_BASIC_INFO basic = {};
    basic.FileAttributes = original.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY;
    if (basic.FileAttributes == 0) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (code == ERROR_SUCCESS && !set_information(attribute_recovery, FileBasicInfo, &basic, sizeof(basic))) code = GetLastError();
    repairs.push_back({path, "clearReadOnly", code == ERROR_SUCCESS ? "applied" : "failed", code});
    if (code != ERROR_SUCCESS) return Failure("ClearReadOnly", path, code, error);
    cleared = true;
    return true;
  };
  const auto remove = [&]() {
    if (!directory && (original.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
      if (!policy.clear_read_only) { Failure("DeleteFileW", path, ERROR_ACCESS_DENIED, error); error->reason = "readOnly"; return false; }
      if (!clear()) return false;
    }
    if (directory && !reparse && policy.recursive) {
      std::vector<DirectoryEntry> entries;
      if (!ReadDirectoryEntries(path, &entries, error)) {
        if (error->os_code != ERROR_ACCESS_DENIED || !grant()) return false;
        if (!ReadDirectoryEntries(path, &entries, error)) { error->reason = "permissionDenied"; return false; }
      }
      auto child_policy = policy; child_policy.ignore_missing = true;
      for (const auto& entry : entries) if (!RemoveEntry(wide + L"\\" + Utf8ToWide(entry.name), child_policy, nullptr, final_name, set_information, error)) return false;
    }
    FILE_DISPOSITION_INFO disposition = {TRUE};
    if (!set_information(handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
      const auto code = GetLastError();
      Failure("SetFileInformationByHandle", path, code, error);
      if (code != ERROR_ACCESS_DENIED || !grant()) return false;
      if (!set_information(handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
        Failure("SetFileInformationByHandle", path, GetLastError(), error);
        if (error->os_code == ERROR_ACCESS_DENIED) error->reason = "permissionDenied";
        return false;
      }
    }
    return true;
  };
  const bool removed = remove();
  if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  if (!removed) {
    if (cleared) {
      FILE_BASIC_INFO basic = {}; basic.FileAttributes = original.dwFileAttributes;
      const auto code = set_information(attribute_recovery, FileBasicInfo, &basic, sizeof(basic)) ? ERROR_SUCCESS : GetLastError();
      for (auto& repair : repairs) if (repair.action == "clearReadOnly" && repair.outcome == "applied") { repair.restoration = code == ERROR_SUCCESS ? "restored" : "failed"; repair.restore_os_code = code; }
    }
    if (granted) {
      // MAXIMUM_ALLOWED preserves the no-propagation contract during rollback too.
      // The retained handle identifies the original object even after a rename.
      // Obtain its current name, then pin and verify identity before restoring it.
      std::vector<wchar_t> name(32768);
      const auto length = final_name(security_recovery, name.data(), static_cast<DWORD>(name.size()), FILE_NAME_OPENED);
      auto restore = INVALID_HANDLE_VALUE;
      DWORD code = ERROR_SUCCESS;
      if (length == 0 || length >= name.size()) code = length == 0 ? GetLastError() : ERROR_FILENAME_EXCED_RANGE;
      else {
        restore = CreateFileW(name.data(), MAXIMUM_ALLOWED, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
        if (restore == INVALID_HANDLE_VALUE) code = GetLastError();
        else {
          BY_HANDLE_FILE_INFORMATION info = {};
          if (!GetFileInformationByHandle(restore, &info)) code = GetLastError();
          else if (!SameObject(original, info)) code = ERROR_INVALID_DATA;
          else code = SetSecurityInfo(restore, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, old_acl, nullptr);
        }
      }
      if (restore != INVALID_HANDLE_VALUE) CloseHandle(restore);
      for (auto& repair : repairs) if (repair.action == "grantDelete" && repair.outcome == "applied") { repair.restoration = code == ERROR_SUCCESS ? "restored" : "failed"; repair.restore_os_code = code; }
    }
    error->repairs.insert(error->repairs.end(), repairs.begin(), repairs.end());
  }
  if (attribute_recovery != INVALID_HANDLE_VALUE) CloseHandle(attribute_recovery);
  if (security_recovery != INVALID_HANDLE_VALUE) CloseHandle(security_recovery);
  if (descriptor != nullptr) LocalFree(descriptor);
  if (!removed) return false;
  bool exists = false;
  if (!CheckPathExists(path, &exists, error) || exists) {
    if (exists) Failure("CleanupCompletion", path, ERROR_BUSY, error);
    error->repairs.insert(error->repairs.end(), repairs.begin(), repairs.end());
    return false;
  }
  return true;
}

bool RemoveWithPolicy(const std::string& path, const CleanupPolicy& requested, OperationError* error) {
  auto policy = requested;
  if (!policy.clear_read_only && !policy.grant_delete && !policy.managed_cleanup)
    return RemovePath(path, policy.recursive, policy.ignore_missing, error);
  std::wstring full;
  if (!AbsolutePath(path, &full, error)) return false;
  std::vector<HANDLE> parents;
  bool result = PinParents(full, &parents, error);
  if (!result && policy.ignore_missing && error->reason == "notFound") {
    for (const auto parent : parents) CloseHandle(parent);
    if (policy.managed_cleanup) capture_directories.erase(full);
    return true;
  }
  const BY_HANDLE_FILE_INFORMATION* expected = nullptr;
  if (result && policy.managed_cleanup) {
    const auto registered = capture_directories.find(full);
    const auto handle = CreateFileW(full.c_str(), 0, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
    const auto code = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    BY_HANDLE_FILE_INFORMATION info = {};
    if (policy.ignore_missing && (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)) {
      if (registered != capture_directories.end()) capture_directories.erase(registered);
      for (const auto parent : parents) CloseHandle(parent);
      return true;
    }
    result = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info) &&
        registered != capture_directories.end() && SameObject(registered->second, info);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!result) { Failure("CleanupOwnership", path, ERROR_INVALID_DATA, error); error->reason = "unsupported"; }
    else expected = &registered->second;
    policy.clear_read_only = true; policy.grant_delete = true;
  }
  const auto kernel = GetModuleHandleW(L"kernel32.dll");
  const auto final_name = reinterpret_cast<FinalName>(GetProcAddress(kernel, "GetFinalPathNameByHandleW"));
  const auto set_information = reinterpret_cast<SetInformation>(GetProcAddress(kernel, "SetFileInformationByHandle"));
  if (result && (final_name == nullptr || set_information == nullptr)) {
    // Ordinary deletion remains available where repair APIs do not exist.
    result = RemovePath(path, policy.recursive, policy.ignore_missing, error);
    if (!result) error->reason = "unsupported";
  } else if (result) result = RemoveEntry(full, policy, expected, final_name, set_information, error);
  for (const auto parent : parents) CloseHandle(parent);
  if (result && policy.managed_cleanup) capture_directories.erase(full);
  return result;
}

bool CreateCaptureDirectory(std::string* path, OperationError* error) {
  std::vector<unsigned char> user;
  if (!UserToken(&user, error)) return false;
  EXPLICIT_ACCESS_W entry = {};
  entry.grfAccessPermissions = FILE_ALL_ACCESS;
  entry.grfAccessMode = GRANT_ACCESS;
  entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
  entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
  PACL acl = nullptr;
  const auto code = SetEntriesInAclW(1, &entry, nullptr, &acl);
  if (code != ERROR_SUCCESS) return Failure("SetEntriesInAclW", "", code, error);
  SECURITY_DESCRIPTOR descriptor = {};
  InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION);
  SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE);
  SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
  SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
  wchar_t temporary[MAX_PATH + 1] = {};
  const auto length = GetTempPathW(MAX_PATH, temporary);
  bool created = false;
  if (length == 0 || length > MAX_PATH) Failure("GetTempPathW", "", GetLastError(), error);
  else for (unsigned int index = 0; index < 1000; ++index) {
    // Never recycle a name during this agent's lifetime: a late cleanup request
    // for an earlier process must not name a newer process's capture directory.
    const auto candidate = std::wstring(temporary) + L"agent-rover-managed-process-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(next_capture_directory++);
    if (!CreateDirectoryW(candidate.c_str(), &security)) {
      const auto saved = GetLastError();
      if (saved == ERROR_ALREADY_EXISTS) continue;
      Failure("CreateDirectoryW", WideToUtf8(candidate), saved, error); break;
    }
    const auto handle = CreateFileW(candidate.c_str(), 0, kShare, nullptr, OPEN_EXISTING, kFlags, nullptr);
    BY_HANDLE_FILE_INFORMATION info = {};
    created = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    if (!created) Failure("RegisterCaptureDirectory", WideToUtf8(candidate), GetLastError(), error);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (created) { capture_directories[candidate] = info; *path = WideToUtf8(candidate); }
    break;
  }
  LocalFree(acl);
  return created;
}
}
