// agent-rover - Windows startup configuration test driver
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include "agent_configuration.h"
#include "win32_files.h"
#include "win32_util.h"

static void Require(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

static int Matrix(const std::wstring& home) {
  using namespace agent_rover;
  const auto path = home + L"\\.agent-rover-agent.ini";
  const auto inspect = [](const std::wstring& location, const std::vector<std::wstring>& arguments) {
    Require(SetEnvironmentVariableW(L"HOME", location == L"<unset>" ? nullptr : location.c_str()), "Set HOME failed");
    std::vector<const wchar_t*> argv = {L"agent.exe"};
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    return LoadAgentConfiguration(argv.size(), argv.data());
  };
  const auto save = [&](const std::vector<unsigned char>& bytes) {
    OperationError error;
    Require(WriteFileBytes(WideToUtf8(path), bytes, &error), "Cannot save test INI");
  };
  const auto save_text = [&](const std::string& text) { save({text.begin(), text.end()}); };
  try {
    auto result = inspect(home, {});
    Require(result.ok && result.options.config_path == path && result.options.port == 39397, "Missing INI failed");
    const auto fallback = inspect(L"<unset>", {});
    Require(fallback.ok && fallback.options.config_path != path &&
        fallback.options.config_path.ends_with(L"\\.agent-rover-agent.ini"), "Missing HOME did not use user profile");
    result = inspect(L"", {});
    Require(result.ok && result.options.config_path == fallback.options.config_path, "Empty HOME changed fallback");
    for (const auto invalid : {L"relative", L"C:relative", L"\\rooted", L"C:\\does-not-exist-rover", L"C:\\invalid?name"}) {
      result = inspect(invalid, {});
      Require(!result.ok && result.error.find(L"HOME") != std::wstring::npos, "Invalid HOME was accepted");
    }
    Require(CreateDirectoryW(path.c_str(), nullptr), "Cannot create unreadable INI fixture");
    result = inspect(home, {});
    Require(!result.ok && result.error.find(path) != std::wstring::npos, "Unreadable INI did not stop startup");
    Require(RemoveDirectoryW(path.c_str()), "Cannot remove unreadable INI fixture");
    save_text("[agent]\nport=43901\nno-auth=true\nconnection-mode=multiple\nmax-transfer-size=1\n");
    result = inspect(home, {});
    Require(result.ok && result.options.port == 43901 && !result.options.auth_required &&
        result.options.multiple_connections && result.options.max_transfer_bytes == 1048576, "INI settings were ignored");
    Require(inspect(home, {L"--port", L"43902"}).options.port == 43902, "CLI override failed");
    const auto text = L"[agent]\r\nport=43903\r\nsafe-base-dir=" + home + L"\r\n";
    std::vector<unsigned char> utf16 = {255, 254};
    for (const auto unit : text) { utf16.push_back(unit & 255); utf16.push_back(unit >> 8); }
    save(utf16);
    result = inspect(home, {});
    Require(result.ok && result.options.safe_base_dir == home && result.options.port == 43903, "UTF-16 Unicode path failed");
    save_text("[agent]\nport=secret-token\n");
    result = inspect(home, {});
    Require(!result.ok && result.error.find(L".agent-rover-agent.ini:2 [port]") != std::wstring::npos &&
        result.error.find(L"secret-token") == std::wstring::npos, "INI error was not redacted and located");
    result = inspect(home, {L"--help"});
    Require(result.ok && result.help_requested, "Invalid INI prevented help");
    result = inspect(L"relative", {L"--help"});
    Require(result.ok && result.help_requested, "Invalid HOME prevented help");
    Require(DeleteFileW(path.c_str()), "Cannot remove test INI");
    std::puts("Windows configuration requirements passed");
    return 0;
  } catch (const std::exception& error) { std::puts(error.what()); return 1; }
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  if (!SetEnvironmentVariableW(L"HOME", std::wstring(argv[2]) == L"<unset>" ? nullptr : argv[2])) return 3;
  if (std::wstring(argv[1]) == L"matrix") return Matrix(argv[2]);
  if (std::wstring(argv[1]) == L"launch") {
    if (argc < 4) return 2;
    std::vector<std::wstring> arguments;
    for (int index = 4; index < argc; ++index) arguments.push_back(argv[index]);
    auto command = agent_rover::BuildCommandLine(argv[3], arguments);
    STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(argv[3], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 4;
    std::printf("pid=%lu\n", process.dwProcessId);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 0;
  }
  const auto result = agent_rover::LoadAgentConfiguration(argc - 2, argv + 2);
  if (!result.ok) { std::puts(agent_rover::WideToUtf8(result.error).c_str()); return 1; }
  std::printf("help=%d\nport=%u\nauth=%d\nmultiple=%d\nmax=%llu\nconfig=%s\nsafe=%s\n",
      result.help_requested, result.options.port, result.options.auth_required,
      result.options.multiple_connections, static_cast<unsigned long long>(result.options.max_transfer_bytes),
      agent_rover::WideToUtf8(result.options.config_path).c_str(),
      agent_rover::WideToUtf8(result.options.safe_base_dir).c_str());
  return 0;
}
