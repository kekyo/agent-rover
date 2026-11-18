// agent-rover - Exclusive public workspace creation
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "win32_files.h"
#include "win32_random.h"
#include "win32_util.h"

static decltype(&CreateDirectoryW) create_directory;
static std::wstring observed_prefix, collision;
static unsigned calls;
static bool denied;
static bool file_collision, exhausted, random_failure;

namespace agent_rover {
// Deterministic entropy and failure injection; production uses the OS RNG.
bool GenerateSecureRandom(unsigned char* bytes, size_t size, std::string* error) {
  if (random_failure) { *error = "Injected random failure"; return false; }
  static unsigned char sequence = 0;
  std::memset(bytes, ++sequence, size);
  return true;
}
}

static void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

static BOOL WINAPI CreateWithCompetitor(LPCWSTR path, LPSECURITY_ATTRIBUTES security) {
  if (std::wstring(path).starts_with(observed_prefix)) {
    ++calls;
    if (denied) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (exhausted) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    if (calls == 1) {
      collision = path;
      agent_rover::OperationError error;
      if (!file_collision) Require(create_directory(path, nullptr), "Could not create competing directory");
      Require(agent_rover::WriteFileBytes(agent_rover::WideToUtf8(collision + (file_collision ? L"" : L"\\owned.txt")),
          {'k', 'e', 'e', 'p'}, &error), "Could not write competing data");
    }
  }
  return create_directory(path, security);
}
#ifdef _WIN64
#define CREATE_IMPORT "__imp_CreateDirectoryW"
#else
#define CREATE_IMPORT "__imp__CreateDirectoryW@8"
#endif
extern "C" {
// Force the ownership race at the OS boundary, after any existence checks.
decltype(&CreateDirectoryW) TestCreateDirectory asm(CREATE_IMPORT) = CreateWithCompetitor;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  const auto symbol = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateDirectoryW");
  static_assert(sizeof(symbol) == sizeof(create_directory));
  std::memcpy(&create_directory, &symbol, sizeof(symbol));
  if (!create_directory) return 3;
  using namespace agent_rover;
  const std::string base = WideToUtf8(argv[1]);
  unsigned failures = 0;
  for (const auto mode : {"race", "file-race", "denied", "missing-parent", "exhausted", "random-failure", "prefixes"}) {
    try {
      calls = 0;
      denied = std::string(mode) == "denied";
      file_collision = std::string(mode) == "file-race";
      exhausted = std::string(mode) == "exhausted";
      random_failure = std::string(mode) == "random-failure";
      observed_prefix = std::wstring(argv[1]) + L"\\" + Utf8ToWide(mode) + L"-";
      std::string path;
      OperationError error;
      if (std::string(mode) == "race" || file_collision) {
        Require(MakeTempDirectory(base, WideToUtf8(observed_prefix), &path, &error), "Creation failed after collision");
        Require(path != WideToUtf8(collision), "Returned a directory owned by a competitor");
        std::vector<unsigned char> contents;
        Require(ReadFileBytes(WideToUtf8(collision + (file_collision ? L"" : L"\\owned.txt")), 100, &contents, &error) &&
            contents == std::vector<unsigned char>({'k', 'e', 'e', 'p'}), "Changed competing data");
      } else if (denied) {
        Require(!MakeTempDirectory(base, WideToUtf8(observed_prefix), &path, &error), "Accepted denied creation");
        Require(error.os_code == ERROR_ACCESS_DENIED && calls == 1, "Retried or lost access-denied failure");
      } else if (std::string(mode) == "missing-parent") {
        Require(!MakeTempDirectory(base, base + "\\missing\\case-", &path, &error), "Created missing parent implicitly");
        Require(!PathExists(base + "\\missing"), "Created missing parent on failure");
      } else if (exhausted) {
        Require(!MakeTempDirectory(base, WideToUtf8(observed_prefix), &path, &error), "Accepted an existing directory");
        Require(calls > 1 && calls <= 256 && error.os_code == ERROR_ALREADY_EXISTS, "Collision retries were unbounded or lost the cause");
      } else if (random_failure) {
        Require(!MakeTempDirectory(base, WideToUtf8(observed_prefix), &path, &error) && calls == 0 &&
            error.message == "Injected random failure", "Random failure was ignored");
      } else {
        Require(MakeTempDirectory(base, std::nullopt, &path, &error) && path.starts_with(base + "\\agent-rover-"), "Default prefix did not use the suggested base");
        const auto first = path;
        Require(MakeTempDirectory(base, std::nullopt, &path, &error) && path != first, "Reused a public directory");
        Require(MakeTempDirectory(base + "\\", "case-", &path, &error) && path.starts_with(base + "\\case-"), "Name prefix did not use the suggested base");
        Require(MakeTempDirectory(base, "試験-", &path, &error) && path.starts_with(base + "\\試験-"), "Unicode prefix failed");
        Require(MakeTempDirectory("C:\\absent-unused-base", base + "\\absolute-", &path, &error) &&
            path.starts_with(base + "\\absolute-"), "Explicit absolute prefix was not honored");
        for (const std::string invalid : {"", "..", ".", "nested/name-", "nested\\name-", "C:relative-", "\\rooted-"}) {
          Require(!MakeTempDirectory(base, invalid, &path, &error) && error.reason == "invalidArgument", "Ambiguous relative prefix was accepted");
        }
        Require(!MakeTempDirectory(base, std::string("nul\0name", 8), &path, &error) && error.reason == "invalidArgument", "Embedded NUL was accepted");
      }
      std::printf("%s passed\n", mode);
    } catch (const std::exception& error) { ++failures; std::printf("%s: %s\n", mode, error.what()); }
  }
  return failures ? 1 : 0;
}
