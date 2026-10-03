// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "operation_worker.h"
#include "file_logger.h"
#include "managed_launch.h"
#include <shellapi.h>

#include "auth.h"
#include "command_line.h"
#include "tcp_server.h"
#include "version_banner.h"
#include "win32_util.h"

namespace agent_rover {

static void ShowUsage() {
  MessageBoxW(nullptr,
      L"agent-rover native windows agent\n\n"
      L"Usage:\n"
      L"  agent-rover-agent.exe [--host <host>] [--port <port>] [--unsafe-token <token>] [-n|--no-auth] [--max-transfer-size <MiB>]\n"
      L"  agent-rover-agent.exe --help\n\n"
      L"--max-transfer-size: positive integer in MiB (default: 64).\n", L"agent-rover", MB_OK);
}

}  // namespace agent_rover

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 1;
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
  if (argc >= 2 && (std::wstring(argv[1]) == L"--agent-worker" || std::wstring(argv[1]) == L"--agent-probe")) {
    const bool probe = std::wstring(argv[1]) == L"--agent-probe";
    const auto parsed = agent_rover::ParseAgentCommandLine(argc - 1, argv + 1);
    LocalFree(argv);
    if (!parsed.ok || parsed.help_requested) return 1;
    return agent_rover::RunOperationWorker(probe, parsed.options.max_transfer_bytes);
  }

  const agent_rover::AgentCommandLineParseResult parsed =
      agent_rover::ParseAgentCommandLine(argc, argv);
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
      parsed.options.max_transfer_bytes};

  if (options.auth_required && options.auth_token.empty()) {
    std::string error;
    if (!agent_rover::GenerateAuthToken(&options.auth_token, &error)) {
      MessageBoxW(nullptr, agent_rover::Utf8ToWide(error).c_str(), L"agent-rover", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  std::string error;
  const int exit_code = agent_rover::RunTcpServer(options, &error);
  if (exit_code != 0) {
    MessageBoxW(nullptr, agent_rover::Utf8ToWide(error).c_str(), L"agent-rover", MB_OK | MB_ICONERROR);
  }
  return exit_code;
}
