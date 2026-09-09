// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_random.h"
#include <windows.h>
#include <bcrypt.h>
#include <limits>

namespace agent_rover {

bool GenerateSecureRandom(unsigned char* bytes, size_t size, std::string* error) {
  if (size > std::numeric_limits<ULONG>::max()) {
    *error = "Random buffer is too large.";
    return false;
  }
  // Use an absolute system path: LOAD_LIBRARY_SEARCH_SYSTEM32 is unavailable
  // on unpatched legacy Windows. The module and provider are owned here.
  wchar_t directory[MAX_PATH] = {};
  const auto length = GetSystemDirectoryW(directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    *error = "GetSystemDirectoryW failed for the random generator.";
    return false;
  }
  const auto path = std::wstring(directory, length) + L"\\bcrypt.dll";
  const auto module = LoadLibraryW(path.c_str());
  if (module != nullptr) {
    using OpenProc = NTSTATUS (WINAPI*)(BCRYPT_ALG_HANDLE*, const wchar_t*, const wchar_t*, ULONG);
    using RandomProc = NTSTATUS (WINAPI*)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
    using CloseProc = NTSTATUS (WINAPI*)(BCRYPT_ALG_HANDLE, ULONG);
    const auto open = reinterpret_cast<OpenProc>(GetProcAddress(module, "BCryptOpenAlgorithmProvider"));
    const auto random = reinterpret_cast<RandomProc>(GetProcAddress(module, "BCryptGenRandom"));
    const auto close = reinterpret_cast<CloseProc>(GetProcAddress(module, "BCryptCloseAlgorithmProvider"));
    if (open != nullptr && random != nullptr && close != nullptr) {
      BCRYPT_ALG_HANDLE provider = nullptr;
      auto status = open(&provider, BCRYPT_RNG_ALGORITHM, nullptr, 0);
      if (BCRYPT_SUCCESS(status)) {
        // Explicit provider works on Vista RTM too; SYSTEM_PREFERRED_RNG
        // requires Vista SP2. A runtime failure must not silently downgrade.
        // https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom
        status = random(provider, bytes, static_cast<ULONG>(size), 0);
        close(provider, 0);
      }
      FreeLibrary(module);
      if (!BCRYPT_SUCCESS(status)) *error = "CNG random generation failed.";
      return BCRYPT_SUCCESS(status);
    }
    FreeLibrary(module);
  }
  // CryptoAPI is intentionally limited to legacy compatibility (including XP).
  // No persisted key container or interactive provider is needed for randomness.
  // https://learn.microsoft.com/windows/win32/api/wincrypt/nf-wincrypt-cryptacquirecontextw
  HCRYPTPROV provider = 0;
  if (!CryptAcquireContextW(&provider, nullptr, MS_DEF_PROV_W, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
    *error = "CryptAcquireContextW failed for the legacy random generator.";
    return false;
  }
  const bool success = CryptGenRandom(provider, static_cast<DWORD>(size), bytes) != FALSE;
  CryptReleaseContext(provider, 0);
  if (!success) *error = "CryptGenRandom failed.";
  return success;
}

}  // namespace agent_rover
