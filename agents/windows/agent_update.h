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
  /** UUID used for the handoff and the durable outcome. */
  std::string id;
  /** SHA-256 of the executable selected before the download. */
  std::string source_hash;
  /** Exact three-part release tag. */
  std::string version;
  /** CPU of the selected Windows executable. */
  std::string architecture;
  /** Expected SHA-256 of the release executable. */
  std::string hash;
  /** Identity of the server that initially answered the client. */
  std::string source_instance;
  /** Digest authorization derived from the existing connection token. */
  std::string authorization;
  /** Size of the release executable in bytes. */
  uint64_t bytes = 0;
  /** Update protocol supported by the release, or zero for a cataloged legacy release. */
  uint32_t protocol = 0;
  /** Compares every field bound to a prepared transaction. */
  bool operator==(const AgentUpdateSpec&) const = default;
};
/** @return SHA-256 of the executable that owns this server. */
std::string RunningAgentHash();
/** Checks the requested build and its compatibility with the active settings.
 * @param spec Client's verified release selection. @param options Current listener and authentication settings.
 * @throws std::runtime_error if the selection is stale, incompatible or unauthorized. */
void ValidateAgentUpdate(const AgentUpdateSpec& spec, const ServerOptions& options);
/** Starts a one-shot helper and waits for it to acquire the result connection.
 * @param spec Previously validated build. @param image Verified binary transfer.
 * @param options Effective startup configuration. @param socket Authenticated session.
 * @param request_id Request to complete after the restart. @param stop Cancels before handoff.
 * @return Completion after which the server must shut down normally. */
cardio::promise<void> HandOffAgentUpdate(AgentUpdateSpec spec, std::vector<unsigned char> image,
                                         ServerOptions options, SocketConnection socket,
                                         std::string request_id, cardio::cancellation stop);
/** @param id Transaction UUID. @return Recorded result JSON for this transaction, or JSON null. */
cardio::promise<std::string> AgentUpdateStatus(std::string id);
/** Reconciles an interrupted transaction after this instance has acquired its listener. */
cardio::promise<void> RecoverAgentUpdate();
/** Runs the temporary updater from a private inherited mapping.
 * @param handle Decimal handle from the running agent. @return Process exit code. */
int RunAgentUpdateHelper(const wchar_t* handle);
/** Waits for the specific updater process so its executable can be deleted.
 * @param pid Decimal process ID. @param created Decimal creation time to distinguish reused IDs.
 * @return Zero once this process identity has exited. */
int WaitForAgentUpdateExit(const wchar_t* pid, const wchar_t* created);
/** Restarts the supervisor with the effective settings of the verified transaction.
 * @param handle Decimal handle from the update helper. @return Supervisor exit code. */
int RunUpdatedAgent(const wchar_t* handle);
}  // namespace agent_rover
#endif
