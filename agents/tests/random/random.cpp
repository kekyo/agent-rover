#include "auth.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <iostream>
static std::string mode;
static int loaded = 0, providers = 0, legacy_calls = 0;
static void Check(bool value) { if (!value) { std::cerr << "random backend: " << mode << '\n'; std::exit(1); } }
UINT GetSystemDirectoryW(wchar_t* output, UINT) { std::wcscpy(output, L"C:\\Windows\\System32"); return std::wcslen(output); }
HMODULE LoadLibraryW(const wchar_t* path) {
  Check(std::wstring(path).find(L"C:\\Windows\\System32\\") == 0);
  if (mode == "legacy" || mode == "legacy-failure" || mode == "acquire-failure") return nullptr;
  ++loaded; return reinterpret_cast<void*>(1);
}
BOOL FreeLibrary(HMODULE) { --loaded; return TRUE; }
static NTSTATUS Open(BCRYPT_ALG_HANDLE* handle, const wchar_t*, const wchar_t*, ULONG) {
  if (mode == "open-failure") return -1;
  *handle = reinterpret_cast<void*>(2); ++providers; return 0;
}
static NTSTATUS Close(BCRYPT_ALG_HANDLE, ULONG) { --providers; return 0; }
static NTSTATUS Random(BCRYPT_ALG_HANDLE handle, PUCHAR output, ULONG size, ULONG flags) {
  Check(handle != nullptr && flags == 0);
  if (mode == "cng-failure") return -1;
  std::fill(output, output + size, 0xa5); return 0;
}
FARPROC GetProcAddress(HMODULE, const char* name) {
  if (mode == "missing-export") return nullptr;
  if (std::strcmp(name, "BCryptGenRandom") == 0) return reinterpret_cast<FARPROC>(&Random);
  if (std::strcmp(name, "BCryptOpenAlgorithmProvider") == 0) return reinterpret_cast<FARPROC>(&Open);
  if (std::strcmp(name, "BCryptCloseAlgorithmProvider") == 0) return reinterpret_cast<FARPROC>(&Close);
  return nullptr;
}
BOOL CryptAcquireContextW(HCRYPTPROV* handle, const wchar_t* container, const wchar_t* provider, DWORD type, DWORD flags) {
  ++legacy_calls;
  Check(container == nullptr && provider != nullptr && type == PROV_RSA_FULL && (flags & CRYPT_VERIFYCONTEXT) == CRYPT_VERIFYCONTEXT);
  if (mode == "acquire-failure") return FALSE;
  *handle = 1; ++providers; return TRUE;
}
BOOL CryptGenRandom(HCRYPTPROV, DWORD size, BYTE* output) {
  if (mode == "legacy-failure") return FALSE;
  std::fill(output, output + size, 0x5a); return TRUE;
}
BOOL CryptReleaseContext(HCRYPTPROV, DWORD) { --providers; return TRUE; }
int main(int argc, char** argv) {
  Check(argc == 2); mode = argv[1];
  const bool success = mode.find("failure") == std::string::npos;
  std::vector<unsigned char> challenge;
  std::string error, token;
  Check(agent_rover::GenerateAuthChallenge(&challenge, &error) == success);
  if (success) {
    Check(challenge.size() == agent_rover::kAuthChallengeBytes);
    Check(challenge.front() == (mode == "modern" ? 0xa5 : 0x5a));
  } else Check(!error.empty());
  Check(agent_rover::GenerateAuthToken(&token, &error) == success);
  if (success) Check(token.size() == agent_rover::kAuthTokenLength);
  if (mode == "modern" || mode == "cng-failure" || mode == "open-failure") Check(legacy_calls == 0);
  Check(loaded == 0 && providers == 0);
}
