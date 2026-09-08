// agent-rover - Windows cleanup test fixture
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#include <windows.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include "../../windows/command_line.h"

static bool Output(HANDLE handle, const std::string& text) {
  size_t offset = 0;
  while (offset < text.size()) {
    DWORD written = 0;
    if (!WriteFile(handle, text.data() + offset, static_cast<DWORD>(text.size() - offset), &written, nullptr) || written == 0) return false;
    offset += written;
  }
  return true;
}

static bool Touch(const wchar_t* path) {
  const auto file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  return file != INVALID_HANDLE_VALUE && CloseHandle(file);
}

static DWORD RepairableDenial(wchar_t* path, bool directory) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return GetLastError();
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> user(size);
  if (!GetTokenInformation(token, TokenUser, user.data(), size, &size)) { const auto code = GetLastError(); CloseHandle(token); return code; }
  CloseHandle(token);
  EXPLICIT_ACCESS_W entry = {};
  entry.grfAccessPermissions = READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  entry.grfAccessMode = GRANT_ACCESS;
  entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
  PACL acl = nullptr;
  auto code = SetEntriesInAclW(1, &entry, nullptr, &acl);
  if (code != ERROR_SUCCESS) return code;
  if (directory) {
    const auto handle = CreateFileW(path, MAXIMUM_ALLOWED, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    code = handle == INVALID_HANDLE_VALUE ? GetLastError() : SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  } else code = SetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
  LocalFree(acl);
  return code;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  const std::wstring mode = argv[1];
  if (mode == L"empty") return 0;
  if (mode == L"damage-capture") {
    wchar_t path[32768] = {};
    const auto size = GetFinalPathNameByHandleW(GetStdHandle(STD_OUTPUT_HANDLE), path, 32768, FILE_NAME_OPENED);
    if (size == 0 || size >= 32768) return 40;
    if (!SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY)) return 41;
    return RepairableDenial(path, false);
  }
  if (mode == L"capture" && argc == 4) {
    const auto event = CreateEventW(nullptr, TRUE, FALSE, argv[3]);
    if (event == nullptr) return 30;
    if (!Output(GetStdHandle(STD_OUTPUT_HANDLE), "start\n") || !Output(GetStdHandle(STD_ERROR_HANDLE), "error-start\n") || !Touch(argv[2])) return 31;
    const auto waited = WaitForSingleObject(event, 60000);
    CloseHandle(event);
    if (waited != WAIT_OBJECT_0) return 32;
    return Output(GetStdHandle(STD_OUTPUT_HANDLE), std::string(131077, 'x') + "\n終端\n") &&
        Output(GetStdHandle(STD_ERROR_HANDLE), "error-end\n") ? 0 : 33;
  }
  if (mode == L"capture-tree" && argc == 4) {
    const auto line = agent_rover::BuildCommandLine(argv[0], {L"capture", argv[2], argv[3]});
    std::vector<wchar_t> command(line.begin(), line.end()); command.push_back(0);
    STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return 34;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 0;
  }
  if (mode == L"readonly" || mode == L"writable") {
    const auto attributes = GetFileAttributesW(argv[2]);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 3;
    const auto updated = mode == L"readonly"
        ? attributes | FILE_ATTRIBUTE_READONLY
        : attributes & ~FILE_ATTRIBUTE_READONLY;
    return SetFileAttributesW(argv[2], updated) ? 0 : 4;
  }
  if (mode == L"deny" || mode == L"restore") {
    // Keep explicit ACL-management access while denying deletion and listing.
    if (mode == L"deny") return RepairableDenial(argv[2], (GetFileAttributesW(argv[2]) & FILE_ATTRIBUTE_DIRECTORY) != 0);
    return SetNamedSecurityInfoW(argv[2], SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr);
  }
  if (mode == L"signal") {
    const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]);
    if (event == nullptr) return 6;
    const bool ok = SetEvent(event);
    CloseHandle(event);
    return ok ? 0 : 7;
  }
  if (mode == L"hold" && argc == 5) {
    const auto event = CreateEventW(nullptr, TRUE, FALSE, argv[4]);
    const auto file = CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (event == nullptr || file == INVALID_HANDLE_VALUE) return 8;
    if (!Touch(argv[3])) return 9;
    const auto result = WaitForSingleObject(event, 60000);
    CloseHandle(file);
    CloseHandle(event);
    return result == WAIT_OBJECT_0 ? 0 : 10;
  }
  return 2;
}
