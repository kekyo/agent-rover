// agent-rover - Maintenance transactions for the running executable
// Copyright (c) Kouji Matsui. Under MIT.
#ifndef AGENT_ROVER_AGENT_UPDATE_H
#define AGENT_ROVER_AGENT_UPDATE_H
#include "async_io.h"
#include "tcp_server.h"
#include <vector>
namespace agent_rover {
/** The client may select a build, but never a destination path or launch command. */
struct AgentUpdateSpec {
  std::string id, source_hash, version, architecture, hash;
  uint64_t bytes = 0;
  uint32_t protocol = 0;
};
/** @return SHA-256 of the executable that owns this server. */
std::string RunningAgentHash();
/** Checks the requested build and its compatibility with the active settings. */
void ValidateAgentUpdate(const AgentUpdateSpec& spec, const ServerOptions& options);
/** Starts a one-shot helper and waits for it to acquire the result connection.
 * @param spec Previously validated build. @param image Verified binary transfer.
 * @param options Effective startup configuration. @param socket Authenticated session.
 * @param request_id Request to complete after the restart. @param stop Cancels before handoff.
 * @return Completion after which the server must shut down normally. */
cardio::promise<void> HandOffAgentUpdate(AgentUpdateSpec spec, std::vector<unsigned char> image,
                                         ServerOptions options, SocketConnection socket,
                                         std::string request_id, cardio::cancellation stop);
/** @return Recorded result JSON for this transaction, or null. */
cardio::promise<std::string> AgentUpdateStatus(std::string id);
/** Runs the temporary updater from a private inherited mapping. */
int RunAgentUpdateHelper(const wchar_t* handle);
/** Restarts the supervisor with the effective settings of the verified transaction. */
int RunUpdatedAgent(const wchar_t* handle);
}  // namespace agent_rover
#endif
