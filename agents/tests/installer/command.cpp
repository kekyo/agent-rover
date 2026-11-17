// agent-rover - NSIS command-line and shortcut integration fixture
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

static bool RegistryString(HKEY key, const wchar_t* name, const std::wstring& expected) {
  wchar_t value[32768] = {}; DWORD bytes = sizeof(value), type = 0;
  return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
      type == REG_SZ && bytes == (expected.size() + 1) * sizeof(wchar_t) && std::wstring(value) == expected;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 4) return 2;
  if (std::wstring(argv[1]) == L"installed") {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AgentRover", 0,
        KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return 8;
    const std::wstring directory = argv[2];
    const bool valid = RegistryString(key, L"InstallLocation", directory) && RegistryString(key, L"DisplayVersion", argv[3]) &&
        RegistryString(key, L"UninstallString", L"\"" + directory + L"\\uninstall.exe\"") &&
        RegistryString(key, L"QuietUninstallString", L"\"" + directory + L"\\uninstall.exe\" /S") &&
        RegistryString(key, L"DisplayIcon", directory + L"\\agent-rover-agent.exe");
    RegCloseKey(key);
    return valid ? 0 : 9;
  }
  if (std::wstring(argv[1]) == L"shortcut") {
    if (!SetEnvironmentVariableW(L"HOME", argv[3])) return 3;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 4;
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open"; info.lpFile = argv[2]; info.nShow = SW_SHOWNORMAL;
    const auto ok = ShellExecuteExW(&info);
    if (info.hProcess) CloseHandle(info.hProcess);
    CoUninitialize();
    return ok ? 0 : 5;
  }
  if (std::wstring(argv[1]) != L"run") return 2;
  // NSIS /D and _? consume the remaining unquoted command line, including spaces.
  auto command = L"\"" + std::wstring(argv[2]) + L"\" " + argv[3];
  STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(argv[2], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 6;
  CloseHandle(process.hThread);
  const auto waited = WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  const auto queried = GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return waited == WAIT_OBJECT_0 && queried ? static_cast<int>(code) : 7;
}
