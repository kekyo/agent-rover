// agent-rover - Private host/helper IPC, never serialized on the network
// Copyright (c) Kouji Matsui. Under MIT.
#ifndef AGENT_ROVER_UPDATE_PRIVATE_H
#define AGENT_ROVER_UPDATE_PRIVATE_H
#include "agent_update.h"
#include <memory>
#include <stdexcept>
namespace agent_rover {
struct UpdateSettings {
  uint16_t port;
  uint64_t max_transfer_bytes;
  bool auth, multiple, generated;
  char host[1024], token[32769], config[131073], workspace[131073];
};
struct UpdateBridge {
  DWORD magic;
  char error[512];
  HANDLE joined, offered, supervisor;
  WSAPROTOCOL_INFOW connection;
  UpdateSettings settings;
  wchar_t original[32768], working_directory[32768];
  char id[37], request_id[129], version[61], source_version[61], hash[65], source_hash[65];
  uint32_t protocol;
};
struct KernelHandleCloser {
  void operator()(void* h) const {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
};
using KernelHandle = std::unique_ptr<void, KernelHandleCloser>;
struct UpdateViewCloser {
  void operator()(UpdateBridge* p) const {
    if (p) UnmapViewOfFile(p);
  }
};
using UpdateView = std::unique_ptr<UpdateBridge, UpdateViewCloser>;
inline constexpr DWORD kUpdateMagic = 0x31555241;
bool IsAgentUpdateId(const std::string& id);
std::wstring AgentImagePath();
std::string AgentImageHash(const std::wstring& path);
std::wstring AgentUpdateDirectory(const std::wstring& original, const std::string& id);
KernelHandle CreateUpdateMapping();
UpdateView MapUpdate(HANDLE h);
cardio::promise<std::string> ReadAgentUpdateRecord();
void SaveAgentUpdateResult(const std::wstring& image, const std::string& record);
}  // namespace agent_rover
#endif
