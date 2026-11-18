// agent-rover - Local configuration document editing
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <shellapi.h>
#include <cardio.h>
#include <stdexcept>
#include "config_file.h"

namespace agent_rover {

static cardio::promise<void> WriteTemplate(HANDLE file, bool* completed, std::wstring* error) {
  // Defaults and placeholders only. Never copy the running CLI or generated token.
  const std::wstring text =
      L"; agent-rover Windows agent configuration\r\n"
      L"; Save this file, then use Exit and start the agent again to apply it.\r\n"
      L"; Command-line options override these settings.\r\n"
      L"; UTF-8 or UTF-16LE with BOM is supported (maximum 1 MiB).\r\n"
      L"[agent]\r\n"
      L"; host=0.0.0.0\r\n"
      L"; port=39397\r\n"
      L"; connection-mode=single\r\n"
      L"; max-transfer-size=512\r\n"
      L"; no-auth=false\r\n"
      L"; unsafe-token=replace-with-your-own-token\r\n"
      L"; safe-base-dir=C:\\RoverWork\r\n"
      L"; safe-base-dir must be an existing absolute Windows directory.\r\n"
      L"; It does not configure Microsoft Defender exclusions.\r\n";
  std::vector<unsigned char> bytes = {255, 254};
  for (const auto unit : text) { bytes.push_back(unit & 255); bytes.push_back(unit >> 8); }
  try {
    auto deadline = cardio::cancellations::timeout(10000);
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto written = co_await cardio::win32::write(file,
          std::as_bytes(std::span(bytes).subspan(offset)), offset, deadline.get_cancellation());
      if (!written) throw std::runtime_error("Empty template write");
      offset += written;
    }
    *completed = true;
  } catch (const std::system_error& failure) {
    *error = L"Cannot write the configuration template (Win32=" + std::to_wstring(failure.code().value()) + L").";
  } catch (const std::exception&) { *error = L"Cannot finish writing the configuration template."; }
}

static bool EnsureTemplate(const std::wstring& path, std::wstring* error) {
  const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const auto code = GetLastError();
    if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) {
      const auto attributes = GetFileAttributesW(path.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    *error = L"Cannot create the configuration file (Win32=" + std::to_wstring(code) + L").";
    return false;
  }
  bool completed = false;
  try {
    cardio::dispatcher_host_win32_auto dispatcher;
    auto writing = WriteTemplate(file, &completed, error);
    dispatcher.park();
  } catch (const std::exception&) { *error = L"Cannot initialize configuration file I/O."; }
  CloseHandle(file);
  return completed;
}

bool OpenAgentConfigurationFile(const std::wstring& path, std::wstring* error) {
  const auto instructions = L"\nOpen this file in Notepad to edit it:\n" + path;
  if (path.empty() || path.find(L'\0') != std::wstring::npos || !EnsureTemplate(path, error)) {
    *error += instructions;
    return false;
  }
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(initialized)) {
    *error = L"Cannot initialize the configuration editor (HRESULT=" +
        std::to_wstring(static_cast<uint32_t>(initialized)) + L")." + instructions;
    return false;
  }
  SHELLEXECUTEINFOW open = {};
  open.cbSize = sizeof(open);
  open.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  open.lpVerb = L"open";
  open.lpFile = path.c_str();
  open.nShow = SW_SHOWNORMAL;
  const bool accepted = ShellExecuteExW(&open) != FALSE;
  const auto code = GetLastError();
  if (open.hProcess) CloseHandle(open.hProcess);
  CoUninitialize();
  if (!accepted) {
    *error = (code == ERROR_NO_ASSOCIATION ? L"No application is associated with INI files." :
        L"The configuration editor could not be opened.") +
        std::wstring(L" (Win32=") + std::to_wstring(code) + L")" + instructions;
  }
  return accepted;
}
}
