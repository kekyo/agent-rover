#pragma once
#include <cstdint>
#define WINAPI
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define PROV_RSA_FULL 1
#define MS_DEF_PROV_W L"Microsoft Base Cryptographic Provider v1.0"
#define CRYPT_VERIFYCONTEXT 0xf0000000
#define CRYPT_SILENT 0x40
using DWORD = uint32_t;
using ULONG = uint32_t;
using UINT = unsigned int;
using BYTE = unsigned char;
using PUCHAR = BYTE*;
using BOOL = int;
using HMODULE = void*;
using HCRYPTPROV = uintptr_t;
using FARPROC = void (*)();
UINT GetSystemDirectoryW(wchar_t*, UINT);
HMODULE LoadLibraryW(const wchar_t*);
BOOL FreeLibrary(HMODULE);
FARPROC GetProcAddress(HMODULE, const char*);
BOOL CryptAcquireContextW(HCRYPTPROV*, const wchar_t*, const wchar_t*, DWORD, DWORD);
BOOL CryptGenRandom(HCRYPTPROV, DWORD, BYTE*);
BOOL CryptReleaseContext(HCRYPTPROV, DWORD);
