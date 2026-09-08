// agent-rover - Real filesystem repair boundary and rollback tests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include "../../windows/win32_cleanup.h"
#include "../../windows/win32_files.h"
#include "../../windows/win32_util.h"

static std::wstring swap_path;
static std::wstring displaced_path;
static std::wstring replacement_path;
static int maximum_opens = 0;
static int security_calls = 0;
static bool fail_restore = false;
static bool fail_disposition = false;
static agent_rover::OperationError test_error;

static HANDLE WINAPI TestCreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                     DWORD disposition, DWORD flags, HANDLE template_file) {
  if (!swap_path.empty() && path == swap_path && access == MAXIMUM_ALLOWED && ++maximum_opens == 2) {
    if (!MoveFileW(swap_path.c_str(), displaced_path.c_str()) || !MoveFileW(replacement_path.c_str(), swap_path.c_str())) return INVALID_HANDLE_VALUE;
  }
  return CreateFileW(path, access, share, security, disposition, flags, template_file);
}
static DWORD WINAPI TestSetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner, PSID group, PACL acl, PACL sacl) {
  if (++security_calls == 2 && fail_restore) return ERROR_ACCESS_DENIED;
  return SetSecurityInfo(handle, type, info, owner, group, acl, sacl);
}
static BOOL WINAPI TestSetInformation(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS type, LPVOID data, DWORD size) {
  if (type == FileDispositionInfo && fail_disposition) { SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
  return SetFileInformationByHandle(handle, type, data, size);
}
static FARPROC WINAPI TestGetProcAddress(HMODULE module, LPCSTR name) {
  if (std::string(name) == "SetFileInformationByHandle") return reinterpret_cast<FARPROC>(TestSetInformation);
  return GetProcAddress(module, name);
}
#define CreateFileW TestCreateFileW
#define SetSecurityInfo TestSetSecurityInfo
#define GetProcAddress TestGetProcAddress
#include "../../windows/win32_cleanup.cpp"
#undef GetProcAddress
#undef SetSecurityInfo
#undef CreateFileW

static bool Deny(const std::wstring& path) {
  std::vector<unsigned char> user;
  agent_rover::OperationError error;
  if (!agent_rover::UserToken(&user, &error)) return false;
  EXPLICIT_ACCESS_W entry = {};
  entry.grfAccessPermissions = READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  entry.grfAccessMode = GRANT_ACCESS;
  entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
  PACL acl = nullptr;
  if (SetEntriesInAclW(1, &entry, nullptr, &acl) != ERROR_SUCCESS) return false;
  const auto handle = CreateFileW(path.c_str(), MAXIMUM_ALLOWED, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  const bool ok = handle != INVALID_HANDLE_VALUE && SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS;
  if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  LocalFree(acl);
  return ok;
}
static bool Restore(const std::wstring& path) {
  std::vector<unsigned char> user;
  agent_rover::OperationError error;
  if (!agent_rover::UserToken(&user, &error)) return false;
  EXPLICIT_ACCESS_W entry = {};
  entry.grfAccessPermissions = FILE_ALL_ACCESS;
  entry.grfAccessMode = GRANT_ACCESS;
  entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
  PACL acl = nullptr;
  if (SetEntriesInAclW(1, &entry, nullptr, &acl) != ERROR_SUCCESS) return false;
  const bool ok = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS;
  LocalFree(acl);
  return ok;
}
static bool IsDenied(const std::wstring& path) {
  WIN32_FIND_DATAW data = {};
  const auto find = FindFirstFileW((path + L"\\*").c_str(), &data);
  if (find != INVALID_HANDLE_VALUE) { FindClose(find); return false; }
  return GetLastError() == ERROR_ACCESS_DENIED;
}
static bool Touch(const std::wstring& path) {
  const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  return file != INVALID_HANDLE_VALUE && CloseHandle(file);
}

static bool RemovalAcl(const std::wstring& path, DWORD allowed, DWORD denied, bool restrict_owner) {
  std::vector<unsigned char> user;
  agent_rover::OperationError error;
  if (!agent_rover::UserToken(&user, &error)) return false;
  unsigned char owner_rights[SECURITY_MAX_SID_SIZE] = {};
  DWORD size = sizeof(owner_rights);
  if (!CreateWellKnownSid(WinCreatorOwnerRightsSid, nullptr, owner_rights, &size)) return false;
  // Build a canonical DACL with separate deny and allow ACEs for the same SID.
  alignas(ACL) unsigned char storage[1024] = {};
  const auto acl = reinterpret_cast<PACL>(storage);
  const auto sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
  if (!InitializeAcl(acl, sizeof(storage), ACL_REVISION) ||
      (denied != 0 && !AddAccessDeniedAceEx(acl, ACL_REVISION, 0, denied, sid)) ||
      !AddAccessAllowedAceEx(acl, ACL_REVISION, 0, allowed, sid) ||
      (restrict_owner && !AddAccessAllowedAceEx(acl, ACL_REVISION, 0, READ_CONTROL, owner_rights))) return false;
  const bool ok = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS;
  return ok;
}

static int RunTests(int argc, wchar_t** argv) {
  if (argc != 2) return 1;
  using namespace agent_rover;
  std::wstring root = argv[1];
  std::replace(root.begin(), root.end(), L'/', L'\\');
  auto& error = test_error;
  CleanupPolicy policy; policy.recursive = true; policy.grant_delete = true;
  // A failed child removal restores the parent's original inability to enumerate.
  const auto denied = root + L"\\rollback";
  if (!CreateDirectoryW(denied.c_str(), nullptr) || !Touch(denied + L"\\file") ||
      !SetFileAttributesW((denied + L"\\file").c_str(), FILE_ATTRIBUTE_READONLY) || !Deny(denied)) return 10;
  security_calls = 0;
  if (RemoveWithPolicy(WideToUtf8(denied), policy, &error) || error.reason != "readOnly" ||
      error.repairs.empty() || error.repairs.back().restoration != "restored" || !IsDenied(denied)) return 11;
  // Failure to restore is reported together with the original child failure.
  security_calls = 0; fail_restore = true;
  if (RemoveWithPolicy(WideToUtf8(denied), policy, &error) || error.reason != "readOnly" ||
      error.repairs.empty() || error.repairs.back().restoration != "failed" || error.repairs.back().restore_os_code != ERROR_ACCESS_DENIED) return 12;
  fail_restore = false;
  if (!Restore(denied)) return 13;
  policy.clear_read_only = true;
  if (!RemoveWithPolicy(WideToUtf8(denied), policy, &error)) return 14;

  const auto readonly = root + L"\\readonly-rollback";
  if (!Touch(readonly) || !SetFileAttributesW(readonly.c_str(), FILE_ATTRIBUTE_READONLY)) return 20;
  fail_disposition = true;
  if (RemoveWithPolicy(WideToUtf8(readonly), policy, &error) || error.os_code != ERROR_SHARING_VIOLATION ||
      (GetFileAttributesW(readonly.c_str()) & FILE_ATTRIBUTE_READONLY) == 0 || error.repairs.empty() || error.repairs.back().restoration != "restored") return 21;
  fail_disposition = false;
  if (!RemoveWithPolicy(WideToUtf8(readonly), policy, &error)) return 22;

  // A hard link shares attributes with an entry outside the requested subtree.
  const auto outside = root + L"\\outside";
  const auto link = root + L"\\link";
  if (!Touch(outside) || !CreateHardLinkW(link.c_str(), outside.c_str(), nullptr) || !SetFileAttributesW(outside.c_str(), FILE_ATTRIBUTE_READONLY)) return 30;
  if (RemoveWithPolicy(WideToUtf8(link), policy, &error) || error.reason != "unsupported" ||
      (GetFileAttributesW(outside.c_str()) & FILE_ATTRIBUTE_READONLY) == 0) return 31;
  SetFileAttributesW(outside.c_str(), FILE_ATTRIBUTE_NORMAL);
  if (!DeleteFileW(link.c_str()) || !DeleteFileW(outside.c_str())) return 32;

  // An attacker substitutes the pathname while the repaired handle is reopened.
  swap_path = root + L"\\swap"; displaced_path = root + L"\\displaced"; replacement_path = root + L"\\replacement";
  if (!CreateDirectoryW(swap_path.c_str(), nullptr) || !Touch(swap_path + L"\\original") ||
      !CreateDirectoryW(replacement_path.c_str(), nullptr) || !Touch(replacement_path + L"\\preserved") || !Deny(swap_path)) return 40;
  maximum_opens = 0;
  if (RemoveWithPolicy(WideToUtf8(swap_path), policy, &error) || error.native_operation != "CleanupIdentity" ||
      GetFileAttributesW((swap_path + L"\\preserved").c_str()) == INVALID_FILE_ATTRIBUTES || !IsDenied(displaced_path)) return 41;
  swap_path.clear();
  if (!Restore(displaced_path)) return 42;
  if (!RemoveWithPolicy(WideToUtf8(root + L"\\swap"), policy, &error) || !RemoveWithPolicy(WideToUtf8(displaced_path), policy, &error)) return 43;

  // A registered directory cannot lend automatic repair to a replacement.
  std::string captured;
  if (!CreateCaptureDirectory(&captured, &error)) return 50;
  const auto original = Utf8ToWide(captured);
  if (!MoveFileW(original.c_str(), (original + L"-old").c_str()) || !CreateDirectoryW(original.c_str(), nullptr) ||
      !Touch(original + L"\\retained") || !SetFileAttributesW((original + L"\\retained").c_str(), FILE_ATTRIBUTE_READONLY)) return 51;
  auto managed = policy; managed.managed_cleanup = true;
  if (RemoveWithPolicy(captured, managed, &error) || error.native_operation != "CleanupOwnership" ||
      (GetFileAttributesW((original + L"\\retained").c_str()) & FILE_ATTRIBUTE_READONLY) == 0) return 52;
  if (!RemoveWithPolicy(captured, policy, &error) || !RemoveWithPolicy(WideToUtf8(original + L"-old"), policy, &error)) return 53;
  // Registration must still match when the deletion handle is acquired.
  if (!CreateCaptureDirectory(&captured, &error)) return 54;
  swap_path = Utf8ToWide(captured); displaced_path = swap_path + L"-moved"; replacement_path = root + L"\\replacement";
  if (!CreateDirectoryW(replacement_path.c_str(), nullptr) || !Touch(replacement_path + L"\\preserved")) return 55;
  maximum_opens = 1;
  const bool removed_replacement = RemoveWithPolicy(captured, managed, &error);
  const bool replacement_exists = GetFileAttributesW((swap_path + L"\\preserved").c_str()) != INVALID_FILE_ATTRIBUTES;
  swap_path.clear();
  auto cleanup = policy; cleanup.ignore_missing = true;
  if (!RemoveWithPolicy(captured, cleanup, &test_error) || !RemoveWithPolicy(WideToUtf8(displaced_path), cleanup, &test_error)) return 56;
  if (removed_replacement || !replacement_exists) return 57;

  // Late cleanup of an old directory must not remove a newer capture directory.
  std::string first, second;
  if (!CreateCaptureDirectory(&first, &error) || !RemoveWithPolicy(first, policy, &error) || !CreateCaptureDirectory(&second, &error)) return 60;
  managed.ignore_missing = true;
  const bool old_cleaned = RemoveWithPolicy(first, managed, &error);
  bool second_exists = false;
  if (!CheckPathExists(second, &second_exists, &error) || !RemoveWithPolicy(second, cleanup, &error)) return 61;
  if (!old_cleaned || !second_exists) return 62;

  const auto restricted = root + L"\\restricted";
  if (!CreateDirectoryW(restricted.c_str(), nullptr) || !RemovalAcl(restricted, FILE_ALL_ACCESS & ~FILE_DELETE_CHILD, 0, false)) return 70;
  for (const bool immutable : {false, true}) {
    const auto file = restricted + (immutable ? L"\\immutable" : L"\\explicit-deny");
    if (!Touch(file)) return 71;
    const auto recovery = CreateFileW(file.c_str(), READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    PACL acl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (recovery == INVALID_HANDLE_VALUE || GetSecurityInfo(recovery, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS) return 72;
    if (!RemovalAcl(file, FILE_ALL_ACCESS & ~DELETE & (immutable ? ~WRITE_DAC : 0xffffffffu), DELETE | (immutable ? WRITE_DAC : 0), immutable)) return 73;
    const bool removed = RemoveWithPolicy(WideToUtf8(file), policy, &error);
    const bool refused = !removed && error.reason == "permissionDenied" && !error.repairs.empty() &&
        (error.repairs.back().outcome == "skipped" || (immutable && error.repairs.back().outcome == "failed")) &&
        error.repairs.back().restoration == "notNeeded";
    const auto deletion = CreateFileW(file.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    const bool still_denied = deletion == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
    if (deletion != INVALID_HANDLE_VALUE) CloseHandle(deletion);
    const auto restored = SetSecurityInfo(recovery, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    LocalFree(descriptor); CloseHandle(recovery);
    if (restored != ERROR_SUCCESS || !DeleteFileW(file.c_str())) return 74;
    if (!refused || !still_denied) return immutable ? 76 : 75;
  }
  if (!RemoveDirectoryW(restricted.c_str())) return 77;

  // Neither a junction target nor a path reached through it is a repair target.
  const auto junction = root + L"\\junction";
  const auto target = root + L"\\junction-target";
  if (!CreateDirectoryW(target.c_str(), nullptr) || !Touch(target + L"\\retained") || !SetFileAttributesW((target + L"\\retained").c_str(), FILE_ATTRIBUTE_READONLY)) return 80;
  const auto line = L"cmd.exe /d /c mklink /J \"" + junction + L"\" \"" + target + L"\"";
  std::vector<wchar_t> command(line.begin(), line.end()); command.push_back(0);
  STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return 81;
  CloseHandle(process.hThread);
  const auto waited = WaitForSingleObject(process.hProcess, 60000);
  DWORD exit_code = 1; GetExitCodeProcess(process.hProcess, &exit_code); CloseHandle(process.hProcess);
  if (waited != WAIT_OBJECT_0 || exit_code != 0) return 82;
  if (RemoveWithPolicy(WideToUtf8(junction + L"\\retained"), policy, &error) || error.reason != "unsupported") return 83;
  if (!RemoveWithPolicy(WideToUtf8(junction), policy, &error) || (GetFileAttributesW((target + L"\\retained").c_str()) & FILE_ATTRIBUTE_READONLY) == 0) return 84;
  if (!RemoveWithPolicy(WideToUtf8(target), policy, &error)) return 85;
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  const auto result = RunTests(argc, argv);
  std::fprintf(stderr, "repair result=%d operation=%s path=%s reason=%s code=%lu\n", result, test_error.native_operation.c_str(), test_error.path.c_str(), test_error.reason.c_str(), static_cast<unsigned long>(test_error.os_code));
  for (const auto& repair : test_error.repairs) std::fprintf(stderr, "repair=%s outcome=%s code=%lu restore=%s restoreCode=%lu\n", repair.action.c_str(), repair.outcome.c_str(), static_cast<unsigned long>(repair.os_code), repair.restoration.c_str(), static_cast<unsigned long>(repair.restore_os_code));
  fail_restore = false; fail_disposition = false; swap_path.clear();
  if (argc == 2) {
    agent_rover::CleanupPolicy policy; policy.recursive = true; policy.ignore_missing = true;
    policy.clear_read_only = true; policy.grant_delete = true;
    for (const auto* name : {L"rollback", L"readonly-rollback", L"outside", L"link", L"swap", L"displaced", L"replacement", L"restricted", L"junction", L"junction-target"}) {
      const auto path = std::wstring(argv[1]) + L"\\" + name;
      if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
      Restore(path);
      SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
      SetFileAttributesW((path + L"\\file").c_str(), FILE_ATTRIBUTE_NORMAL);
      agent_rover::OperationError error;
      if (!agent_rover::RemoveWithPolicy(agent_rover::WideToUtf8(path), policy, &error)) return 99;
    }
  }
  return result;
}
