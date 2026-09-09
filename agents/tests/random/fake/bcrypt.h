#pragma once
using NTSTATUS = int32_t;
using BCRYPT_ALG_HANDLE = void*;
#define BCRYPT_SUCCESS(status) ((status) >= 0)
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 2
#define BCRYPT_RNG_ALGORITHM L"RNG"
