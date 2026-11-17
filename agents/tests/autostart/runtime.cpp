// agent-rover - Current-user startup registration and tray behavior
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "auto_start.h"
#include "agent_gui.h"

static constexpr auto run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static decltype(&RegSetValueExW) set_value;
static bool deny_write;
static UINT menu_state;
static std::wstring menu_text, message;

static void Require(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
static LSTATUS WINAPI SetValue(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size) {
  return deny_write ? ERROR_ACCESS_DENIED : set_value(key, name, reserved, type, data, size);
}
static BOOL WINAPI ObserveMenu(HMENU menu, UINT, int, int, int, HWND, const RECT*) {
  menu_state = GetMenuState(menu, 5, MF_BYCOMMAND);
  menu_text.clear();
  for (int index = 0; index < GetMenuItemCount(menu); ++index) {
    wchar_t text[1024] = {};
    GetMenuStringW(menu, index, text, 1024, MF_BYPOSITION);
    menu_text += text;
  }
  return 0;
}
static int WINAPI ObserveMessage(HWND, LPCWSTR text, LPCWSTR, UINT) { message = text; return IDOK; }
#ifdef _WIN64
#define SET_IMPORT "__imp_RegSetValueExW"
#define MENU_IMPORT "__imp_TrackPopupMenu"
#define MESSAGE_IMPORT "__imp_MessageBoxW"
#else
#define SET_IMPORT "__imp__RegSetValueExW@24"
#define MENU_IMPORT "__imp__TrackPopupMenu@28"
#define MESSAGE_IMPORT "__imp__MessageBoxW@16"
#endif
extern "C" {
decltype(&RegSetValueExW) TestSetValue asm(SET_IMPORT) = SetValue;
decltype(&TrackPopupMenu) TestMenu asm(MENU_IMPORT) = ObserveMenu;
decltype(&MessageBoxW) TestMessage asm(MESSAGE_IMPORT) = ObserveMessage;
}

int wmain() {
  using namespace agent_rover;
  auto symbol = GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegSetValueExW");
  static_assert(sizeof(symbol) == sizeof(set_value));
  std::memcpy(&set_value, &symbol, sizeof(symbol));
  if (!set_value) return 2;
  const auto sandbox = L"Software\\AgentRoverTests\\autostart-" + std::to_wstring(GetCurrentProcessId());
  HKEY root = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &root, nullptr) != ERROR_SUCCESS) return 3;
  if (RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS) { RegCloseKey(root); return 4; }
  int result = 0;
  HKEY key = nullptr;
  try {
    std::wstring executable, error;
    Require(GetAutoStartExecutablePath(&executable, &error), "Could not read executable path");
    Require(executable.find(L"日本語") != std::wstring::npos && executable.find(L' ') != std::wstring::npos, "Test requires a Unicode path containing spaces");
    AutoStartState state;
    Require(ReadAutoStartRegistration(executable, &state, &error) && !state.registered, "Missing registration was not absent");
    Require(RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) == ERROR_FILE_NOT_FOUND,
        "Reading startup state created a Run key");
    ServerOptions options = {};
    options.host = "127.0.0.1"; options.port = 39751;
    bool exited = false;
    const auto gui = CreateAgentGui(options, [&] { exited = true; });
    const auto window = FindWindowW(L"AgentRoverLogViewer", L"agent-rover logs (39751)");
    Require(window != nullptr, "Missing viewer");
    SendMessageW(window, WM_APP + 41, 0, WM_CONTEXTMENU);
    Require(menu_state != static_cast<UINT>(-1) && !(menu_state & MF_CHECKED), "Missing unchecked Start at sign-in menu");
    Require(ReadAutoStartRegistration(executable, &state, &error) && !state.registered, "GUI startup registered itself");
    SendMessageW(window, WM_COMMAND, 5, 0);
    Require(ReadAutoStartRegistration(executable, &state, &error) && state.current_copy, "Menu did not enable this copy");
    Require(message.find(L"INI") != std::wstring::npos && message.find(L"command-line") != std::wstring::npos,
        "Menu did not explain settings on next sign-in");
    Require(RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS, "Missing Run key");
    wchar_t command[1024] = {}; DWORD size = sizeof(command), type = 0;
    Require(RegQueryValueExW(key, L"AgentRover", nullptr, &type, reinterpret_cast<BYTE*>(command), &size) == ERROR_SUCCESS &&
        type == REG_SZ && std::wstring(command) == L"\"" + executable + L"\"", "Run command was not just a quoted executable path");
    SendMessageW(window, WM_APP + 41, 0, WM_CONTEXTMENU);
    Require(menu_state & MF_CHECKED, "Menu did not reread enabled registration");
    SendMessageW(window, WM_COMMAND, 5, 0);
    Require(ReadAutoStartRegistration(executable, &state, &error) && !state.registered, "Menu did not disable this copy");

    const std::wstring other = L"C:\\別のコピー\\agent.exe";
    const auto other_command = L"\"" + other + L"\" --unsafe-token keep-private-token";
    Require(set_value(key, L"AgentRover", 0, REG_SZ, reinterpret_cast<const BYTE*>(other_command.c_str()),
        (other_command.size() + 1) * sizeof(wchar_t)) == ERROR_SUCCESS, "Could not set external registration");
    Require(ReadAutoStartRegistration(executable, &state, &error) && state.registered && !state.current_copy && state.registered_path == other,
        "External copy was not identified without its arguments");
    Require(WriteAutoStartRegistration(executable, false, &state, &error) && state.registered && !state.current_copy, "OFF removed another copy");
    SendMessageW(window, WM_APP + 41, 0, WM_CONTEXTMENU);
    Require(!(menu_state & MF_CHECKED) && menu_text.find(other) != std::wstring::npos && menu_text.find(L"keep-private-token") == std::wstring::npos,
        "Menu did not reflect the external path safely");
    deny_write = true;
    SendMessageW(window, WM_COMMAND, 5, 0);
    Require(message.find(L"5") != std::wstring::npos && message.find(L"keep-private-token") == std::wstring::npos, "Write failure was hidden or leaked arguments");
    Require(ReadAutoStartRegistration(executable, &state, &error) && !state.current_copy && state.registered_path == other, "Failed write changed state");
    deny_write = false;
    SendMessageW(window, WM_COMMAND, 5, 0);
    Require(ReadAutoStartRegistration(executable, &state, &error) && state.current_copy, "ON did not replace the other copy");
    Require(!WriteAutoStartRegistration(L"C:\\" + std::wstring(260, L'x') + L".exe", true, &state, &error) &&
        error.find(L"260") != std::wstring::npos, "Overlong Run command was accepted");
    Require(ReadAutoStartRegistration(executable, &state, &error) && state.current_copy, "Length rejection changed registration");
    Require(WriteAutoStartRegistration(executable, false, &state, &error) && !state.registered, "Final disable failed");
    SendMessageW(window, WM_COMMAND, 3, 0);
    Require(exited, "Exit callback was lost");
    std::puts("Current-user registration, literal path, external changes, menu state and failures passed");
  } catch (const std::exception& error) { std::puts(error.what()); result = 1; }
  if (key) RegCloseKey(key);
  for (const auto path : {run_key, L"Software\\Microsoft\\Windows\\CurrentVersion", L"Software\\Microsoft\\Windows", L"Software\\Microsoft", L"Software"})
    RegDeleteKeyW(HKEY_CURRENT_USER, path);
  RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
  RegCloseKey(root);
  RegDeleteKeyW(HKEY_CURRENT_USER, sandbox.c_str());
  return result;
}
