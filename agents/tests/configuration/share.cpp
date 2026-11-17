// agent-rover - UNC share-root configuration validation
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <cstdio>
#include <cstring>
#include "agent_configuration.h"

static const std::wstring share = L"\\\\workspace-server\\workspace-share";
static std::wstring directory;
static decltype(&CreateFileW) create_file;
static decltype(&GetFileAttributesW) attributes;

static DWORD WINAPI ShareAttributes(LPCWSTR path) {
  // The documented GetFileAttributesW behavior for a network share root.
  if (std::wstring(path) == share) { SetLastError(ERROR_BAD_NETPATH); return INVALID_FILE_ATTRIBUTES; }
  return attributes(path);
}
static HANDLE WINAPI OpenShare(LPCWSTR path, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file) {
  return create_file(std::wstring(path) == share ? directory.c_str() : path,
      access, sharing, security, disposition, flags, template_file);
}
#ifdef _WIN64
#define ATTR_IMPORT "__imp_GetFileAttributesW"
#define FILE_IMPORT "__imp_CreateFileW"
#else
#define ATTR_IMPORT "__imp__GetFileAttributesW@4"
#define FILE_IMPORT "__imp__CreateFileW@28"
#endif
extern "C" {
decltype(&GetFileAttributesW) TestShareAttributes asm(ATTR_IMPORT) = ShareAttributes;
decltype(&CreateFileW) TestOpenShare asm(FILE_IMPORT) = OpenShare;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  const auto kernel = GetModuleHandleW(L"kernel32.dll");
  auto symbol = GetProcAddress(kernel, "CreateFileW");
  static_assert(sizeof(symbol) == sizeof(create_file));
  std::memcpy(&create_file, &symbol, sizeof(symbol));
  symbol = GetProcAddress(kernel, "GetFileAttributesW");
  std::memcpy(&attributes, &symbol, sizeof(symbol));
  if (!create_file || !attributes) return 3;
  directory = argv[1];
  if (!SetEnvironmentVariableW(L"HOME", directory.c_str())) return 4;
  const wchar_t* arguments[] = {L"agent.exe", L"--safe-base-dir", share.c_str()};
  const auto result = agent_rover::LoadAgentConfiguration(3, arguments);
  if (!result.ok || result.options.safe_base_dir != share) {
    std::puts("Existing UNC share root was rejected"); return 1;
  }
  std::puts("UNC share root passed");
  return 0;
}
