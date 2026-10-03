// agent-rover - Independent host supervision
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_SUPERVISOR_H
#define AGENT_ROVER_SUPERVISOR_H
#include "tcp_server.h"
namespace agent_rover {
/** Runs the independent monitor, retaining authentication and startup options.
 * @param options Validated options. @param error Receives unrecoverable startup failure.
 * @return Zero after explicit Exit, otherwise a native failure code. */
int RunSupervisor(const ServerOptions& options, std::string* error);
/** Reads private startup configuration and runs one monitored host.
 * @return Host exit code; no modal error UI is used by this child. */
int RunSupervisedServer();
/** Publishes event-loop progress only while operation/recovery deadlines hold.
 * @param healthy Whether active operations can still complete within their deadlines. */
void PulseSupervisor(bool healthy);
}
#endif
