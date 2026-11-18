// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "agent_update.h"
#include "operation_worker.h"
#include "file_logger.h"
#include "managed_launch.h"
#include "supervisor.h"
#include <shellapi.h>

#include "auth.h"
#include "command_line.h"
#include "agent_configuration.h"
#include "config_editor.h"
#include "tcp_server.h"
#include "version_banner.h"
#include "win32_util.h"

namespace agent_rover {

static void ShowUsage() {
  MessageBoxW(nullptr,
      L"agent-rover native windows agent\n\n"
      L"Usage:\n"
      L"  agent-rover-agent.exe [--host <host>] [--port <port>] [--unsafe-token <token>] [-n|--no-auth] [--connection-mode <single|multiple>] [--max-transfer-size <MiB>] [--safe-base-dir <path>]\n"
      L"  agent-rover-agent.exe --help\n\n"
      L"--connection-mode: single or multiple (default: single).\n"
      L"--max-transfer-size: positive integer in MiB (default: 512).\n"
      L"--safe-base-dir: existing absolute directory (default: OS temporary directory).\n"
      L"Settings: CLI overrides $HOME/.agent-rover-agent.ini, then built-in defaults.\n"
      L"When HOME is empty or unset, the current user's profile directory is used.\n", L"agent-rover", MB_OK);
}

}  // namespace agent_rover

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 1;
  if (argc == 4 && std::wstring(argv[1]) == L"--agent-update-wait") {
    const auto result = agent_rover::WaitForAgentUpdateExit(argv[2], argv[3]);
    LocalFree(argv);
    return result;
  }
  if (argc == 3 && (std::wstring(argv[1]) == L"--agent-update-helper" || std::wstring(argv[1]) == L"--agent-update-restart")) {
    const bool helper = std::wstring(argv[1]) == L"--agent-update-helper";
    const std::wstring handle = argv[2];
    LocalFree(argv);
    return helper ? agent_rover::RunAgentUpdateHelper(handle.c_str()) : agent_rover::RunUpdatedAgent(handle.c_str());
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--agent-server") {
    LocalFree(argv);
    return agent_rover::RunSupervisedServer();
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--agent-launch-worker") {
    LocalFree(argv);
    return agent_rover::RunManagedLaunchWorker();
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--agent-cleanup-worker") {
    LocalFree(argv);
    return agent_rover::RunCleanupWorker();
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--agent-log-worker") {
    LocalFree(argv);
    return agent_rover::RunFileLogWorker();
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--agent-config-editor") {
    LocalFree(argv);
    return agent_rover::RunConfigEditorWorker();
  }
  if (argc >= 2 && (std::wstring(argv[1]) == L"--agent-worker" || std::wstring(argv[1]) == L"--agent-probe")) {
    const bool probe = std::wstring(argv[1]) == L"--agent-probe";
    const auto parsed = agent_rover::ParseAgentCommandLine(argc - 1, argv + 1);
    LocalFree(argv);
    if (!parsed.ok || parsed.help_requested) return 1;
    return agent_rover::RunOperationWorker(probe, parsed.options.max_transfer_bytes);
  }

  const agent_rover::AgentCommandLineParseResult parsed =
      agent_rover::LoadAgentConfiguration(argc, argv);
  LocalFree(argv);
  if (parsed.help_requested) {
    agent_rover::ShowUsage();
    return 0;
  }
  if (!parsed.ok) {
    MessageBoxW(nullptr, parsed.error.c_str(), L"agent-rover", MB_OK | MB_ICONERROR);
    return 1;
  }

  agent_rover::ServerOptions options = {
      parsed.options.host,
      parsed.options.port,
      parsed.options.auth_required,
      parsed.options.auth_token,
      parsed.options.max_transfer_bytes,
      parsed.options.multiple_connections,
      parsed.options.auth_token.empty()};
  options.config_path = agent_rover::WideToUtf8(parsed.options.config_path);
  options.safe_base_dir = agent_rover::WideToUtf8(parsed.options.safe_base_dir);

  if (options.auth_required && options.auth_token.empty()) {
    std::string error;
    if (!agent_rover::GenerateAuthToken(&options.auth_token, &error)) {
      MessageBoxW(nullptr, agent_rover::Utf8ToWide(error).c_str(), L"agent-rover", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  std::string error;
  const int exit_code = agent_rover::RunSupervisor(options, &error);
  if (exit_code != 0) {
    MessageBoxW(nullptr, agent_rover::Utf8ToWide(error).c_str(), L"agent-rover", MB_OK | MB_ICONERROR);
  }
  return exit_code;
}
