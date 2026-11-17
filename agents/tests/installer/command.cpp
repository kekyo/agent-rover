// agent-rover - NSIS command-line and shortcut integration fixture
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <cstdio>
#include <string>

static bool RegistryString(HKEY key, const wchar_t* name, const std::wstring& expected) {
  wchar_t value[32768] = {}; DWORD bytes = sizeof(value), type = 0;
  return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
      type == REG_SZ && bytes == (expected.size() + 1) * sizeof(wchar_t) && std::wstring(value) == expected;
}

static int Shortcut(const std::wstring& mode, const wchar_t* path, const wchar_t* target) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 20;
  IShellLinkW* link = nullptr;
  IPersistFile* file = nullptr;
  auto result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
  if (SUCCEEDED(result)) result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
  if (SUCCEEDED(result)) {
    if (mode == L"link") {
      result = link->SetPath(target);
      if (SUCCEEDED(result)) result = file->Save(path, TRUE);
    } else {
      result = file->Load(path, STGM_READ);
      wchar_t actual[MAX_PATH] = {};
      if (SUCCEEDED(result)) result = link->GetPath(actual, MAX_PATH, nullptr, SLGP_RAWPATH);
      if (SUCCEEDED(result) && _wcsicmp(actual, target) != 0) result = E_FAIL;
    }
  }
  if (file) file->Release();
  if (link) link->Release();
  CoUninitialize();
  return result == S_OK ? 0 : 21;
}

static BOOL CALLBACK FindDialog(HWND window, LPARAM context) {
  wchar_t name[32] = {};
  GetClassNameW(window, name, 32);
  if (std::wstring(name) != L"#32770" || !IsWindowVisible(window)) return TRUE;
  *reinterpret_cast<HWND*>(context) = window;
  return FALSE;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 4) return 2;
  const std::wstring mode = argv[1];
  if (mode == L"link" || mode == L"link-target") return Shortcut(mode, argv[2], argv[3]);
  if (mode == L"tray" || mode == L"dialog") {
    const auto viewer = FindWindowW(L"AgentRoverLogViewer", (L"agent-rover logs (" + std::wstring(argv[2]) + L")").c_str());
    if (!viewer) return 22;
    if (mode == L"tray") return PostMessageW(viewer, WM_COMMAND, 5, 0) ? 0 : 23;
    HWND dialog = nullptr;
    EnumThreadWindows(GetWindowThreadProcessId(viewer, nullptr), FindDialog, reinterpret_cast<LPARAM>(&dialog));
    if (!dialog) return 24;
    wchar_t text[4096] = {};
    DWORD_PTR ignored = 0;
    if (!SendMessageTimeoutW(GetDlgItem(dialog, 65535), WM_GETTEXT, 4096, reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 5000, &ignored)) return 25;
    if (std::wstring(text).find(L"Start at sign-in") == std::wstring::npos) return 25;
    if (std::wstring(argv[3]) != L"close") return 0;
    const auto button = FindWindowExW(dialog, nullptr, L"Button", nullptr);
    if (!button) return 26;
    wchar_t label[64] = {};
    if (!SendMessageTimeoutW(button, WM_GETTEXT, 64, reinterpret_cast<LPARAM>(label), SMTO_ABORTIFHUNG, 5000, &ignored) ||
        (std::wstring(label) != L"OK" && std::wstring(label) != L"&OK")) return 26;
    // Windows can assign IDCANCEL to the only button of an MB_OK message box.
    return PostMessageW(button, BM_CLICK, 0, 0) ? 0 : 26;
  }
  if (mode == L"set-startup" || mode == L"startup-is" || mode == L"clear-startup") {
    HKEY key = nullptr;
    const auto access = KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY;
    const auto code = mode == L"set-startup" ? RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
        nullptr, 0, access, nullptr, &key, nullptr) : RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, access, &key);
    const std::wstring expected = argv[2];
    if (mode == L"startup-is" && expected == L"<absent>" && code == ERROR_FILE_NOT_FOUND) return 0;
    if (code != ERROR_SUCCESS) return 27;
    bool valid;
    if (mode == L"set-startup") valid = RegSetValueExW(key, L"AgentRover", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(expected.c_str()), (expected.size() + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
    else if (mode == L"clear-startup") valid = RegistryString(key, L"AgentRover", expected) && RegDeleteValueW(key, L"AgentRover") == ERROR_SUCCESS;
    else if (expected == L"<absent>") valid = RegQueryValueExW(key, L"AgentRover", nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND;
    else valid = RegistryString(key, L"AgentRover", expected);
    RegCloseKey(key);
    return valid ? 0 : 28;
  }
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
    const auto waited = ok && info.hProcess ? WaitForSingleObject(info.hProcess, INFINITE) : WAIT_FAILED;
    if (info.hProcess) CloseHandle(info.hProcess);
    CoUninitialize();
    return waited == WAIT_OBJECT_0 ? 0 : 5;
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
