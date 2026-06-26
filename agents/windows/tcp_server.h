// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_TCP_SERVER_H
#define AGENT_ROVER_WINDOWS_AGENT_TCP_SERVER_H

#include <cstdint>
#include <string>

namespace agent_rover {

/** Native TCP agent listen options. */
struct ServerOptions {
  /** Host interface to bind. */
  std::string host;
  /** TCP port to listen on. */
  uint16_t port;
  /** Whether clients must answer an auth challenge before using the session. */
  bool auth_required;
  /** Token required when auth_required is true. */
  std::string auth_token;
};

/**
 * Runs the native TCP frame server until the process is terminated.
 *
 * @param options Listen options.
 * @param error Receives a human-readable startup error.
 * @return 0 on normal startup, otherwise a non-zero process exit code.
 */
int RunTcpServer(const ServerOptions& options, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_TCP_SERVER_H
