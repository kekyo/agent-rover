// agent-rover - Configuration editing behavior and shell failures
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <shellapi.h>
#include <cassert>
#include <cstdio>
#include "config_file.h"
#include "command_line.h"
#include "win32_files.h"
#include "win32_util.h"

static std::wstring expected_path;
static DWORD failure = 0;
static unsigned calls = 0;
#ifdef _WIN64
#define SHELL_IMPORT "__imp_ShellExecuteExW"
#else
#define SHELL_IMPORT "__imp__ShellExecuteExW@4"
#endif
static BOOL WINAPI ObserveOpen(SHELLEXECUTEINFOW* info) {
  ++calls;
  assert(std::wstring(info->lpFile) == expected_path && std::wstring(info->lpVerb) == L"open");
  assert(info->lpParameters == nullptr);
  assert((info->fMask & SEE_MASK_NOASYNC) && (info->fMask & SEE_MASK_FLAG_NO_UI));
  if (failure) { SetLastError(failure); return FALSE; }
  return TRUE;
}
extern "C" {
// Observe the shell boundary without changing the product's file operations.
BOOL (WINAPI* TestOpenConfig)(SHELLEXECUTEINFOW*) asm(SHELL_IMPORT) = ObserveOpen;
}

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  using namespace agent_rover;
  expected_path = std::wstring(argv[1]) + L"\\.agent-rover-agent.ini";
  std::wstring error;
  assert(OpenAgentConfigurationFile(expected_path, &error));
  OperationError io_error;
  std::vector<unsigned char> bytes;
  assert(ReadFileBytes(WideToUtf8(expected_path), 1048576, &bytes, &io_error));
  assert(bytes.size() > 2 && bytes[0] == 255 && bytes[1] == 254);
  const wchar_t* arguments[] = {L"agent.exe"};
  const auto parsed = ParseAgentConfiguration(1, arguments, bytes, expected_path);
  assert(parsed.ok && parsed.options.port == 39397 && parsed.options.auth_required && parsed.options.auth_token.empty());
  const std::string existing = "[agent]\nport=43121\nunsafe-token=keep-private-token\n";
  const std::vector<unsigned char> original(existing.begin(), existing.end());
  assert(WriteFileBytes(WideToUtf8(expected_path), original, &io_error));
  assert(OpenAgentConfigurationFile(expected_path, &error));
  assert(ReadFileBytes(WideToUtf8(expected_path), 1048576, &bytes, &io_error) && bytes == original);
  assert(ParseAgentConfiguration(1, arguments, bytes, expected_path).options.port == 43121);
  for (const auto code : {ERROR_NO_ASSOCIATION, ERROR_ACCESS_DENIED}) {
    failure = code;
    assert(!OpenAgentConfigurationFile(expected_path, &error));
    assert(error.find(expected_path) != std::wstring::npos && error.find(L"Notepad") != std::wstring::npos);
    assert(error.find(L"keep-private-token") == std::wstring::npos);
    assert(ReadFileBytes(WideToUtf8(expected_path), 1048576, &bytes, &io_error) && bytes == original);
  }
  const auto opened = calls;
  assert(!OpenAgentConfigurationFile(std::wstring(argv[1]) + L"\\missing\\.agent-rover-agent.ini", &error));
  assert(calls == opened);
  assert(DeleteFileW(expected_path.c_str()));
  std::puts("Exclusive template, existing contents, Unicode paths and shell errors passed");
  return 0;
}
