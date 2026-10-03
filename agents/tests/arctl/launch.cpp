// agent-rover - Persistent launch test fixture
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#include <windows.h>
#include <cstdio>
#include <string>

static std::string Utf8(const wchar_t* value) {
  const auto size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  result.pop_back();
  return result;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  const auto ready = CreateEventW(nullptr, TRUE, FALSE, argv[2]);
  if (!ready) return 3;
  if (std::wstring(argv[1]) == L"wait-ready") {
    const auto result = WaitForSingleObject(ready, 30000);
    CloseHandle(ready);
    return result == WAIT_OBJECT_0 ? 0 : 4;
  }
  wchar_t value[32768] = {};
  GetCurrentDirectoryW(32768, value);
  std::printf("cwd=%s\n", Utf8(value).c_str());
  GetEnvironmentVariableW(L"ARCTL_TEST", value, 32768);
  std::printf("env=%s\n", Utf8(value).c_str());
  value[0] = 0;
  GetEnvironmentVariableW(L"ARCTL_EMPTY", value, 32768);
  std::printf("empty=%s\n", Utf8(value).c_str());
  for (int i = 3; i < argc; ++i) std::printf("arg=%s\n", Utf8(argv[i]).c_str());
  std::fprintf(stderr, "launch-error-output\n");
  std::fflush(stdout);
  std::fflush(stderr);
  SetEvent(ready);
  // Remain alive until the test explicitly kills this process. The CLI must
  // return without waiting for exit, and disconnecting must not terminate it.
  const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  WaitForSingleObject(stop, INFINITE);
  CloseHandle(stop);
  CloseHandle(ready);
  return 0;
}
