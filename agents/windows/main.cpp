// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <cstdio>

#include "auth.h"
#include "command_line.h"
#include "tcp_server.h"
#include "version_banner.h"

namespace agent_rover {

static void PrintUsage() {
  std::wprintf(
      L"agent-rover native windows agent\n\n"
      L"Usage:\n"
      L"  agent-rover-agent.exe [--host <host>] [--port <port>] [--unsafe-token <token>] [-n|--no-auth]\n"
      L"  agent-rover-agent.exe --help\n");
}

}  // namespace agent_rover

int wmain(int argc, wchar_t** argv) {
  agent_rover::PrintAgentVersionBanner();

  const agent_rover::AgentCommandLineParseResult parsed =
      agent_rover::ParseAgentCommandLine(argc, argv);
  if (parsed.help_requested) {
    agent_rover::PrintUsage();
    return 0;
  }
  if (!parsed.ok) {
    std::fwprintf(stderr, L"%ls\n", parsed.error.c_str());
    agent_rover::PrintUsage();
    return 1;
  }

  agent_rover::ServerOptions options = {
      parsed.options.host,
      parsed.options.port,
      parsed.options.auth_required,
      parsed.options.auth_token};

  if (options.auth_required && options.auth_token.empty()) {
    std::string error;
    if (!agent_rover::GenerateAuthToken(&options.auth_token, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
  }

  std::printf("agent-rover native agent listening on %s:%u\n",
              options.host.c_str(), static_cast<unsigned int>(options.port));
  if (options.auth_required) {
    std::printf("agent-rover agent token: %s\n", options.auth_token.c_str());
  }
  std::fflush(stdout);

  std::string error;
  const int exit_code = agent_rover::RunTcpServer(options, &error);
  if (exit_code != 0) {
    std::fprintf(stderr, "%s\n", error.c_str());
  }
  return exit_code;
}
