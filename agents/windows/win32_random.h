// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_WIN32_RANDOM_H
#define AGENT_ROVER_WINDOWS_WIN32_RANDOM_H
#include <cstddef>
#include <string>
namespace agent_rover {
/**
 * Fills a buffer using the OS cryptographic random generator.
 * @param bytes Caller-owned destination.
 * @param size Number of bytes to generate (at most ULONG_MAX).
 * @param error Receives an error on failure.
 * @return true on success; false without substituting predictable randomness.
 * @remarks Uses CryptoAPI only when the CNG DLL or required exports are absent.
 */
bool GenerateSecureRandom(unsigned char* bytes, size_t size, std::string* error);
}
#endif
