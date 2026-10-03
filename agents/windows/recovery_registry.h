// agent-rover - Capture ownership retained across host restarts
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_RECOVERY_REGISTRY_H
#define AGENT_ROVER_RECOVERY_REGISTRY_H
#include "async_io.h"

namespace agent_rover {
/** Bounded registry shared only with a trusted host instance. */
struct RecoveryRegistry;
/** Owned mapping and view. */
using RecoveryRegistryHandle = std::shared_ptr<RecoveryRegistry>;
/** Sentinel for workers without an owned capture directory. */
inline constexpr uint32_t kNoRecoverySlot = UINT32_MAX;
/** Creates the monitor's anonymous, zero-initialized registry.
 * @return Owned mapping. */
RecoveryRegistryHandle CreateRecoveryRegistry();
/** Attaches the host to a duplicated registry handle.
 * @param mapping Handle transferred to this function. @param generation Host generation.
 * @return Owned mapping and view. */
RecoveryRegistryHandle AttachRecoveryRegistry(HANDLE mapping, uint32_t generation);
/** Gets the mapping handle for an explicit, non-inheritable duplication.
 * @param registry Owner. @return Borrowed mapping handle. */
HANDLE RecoveryMapping(const RecoveryRegistryHandle& registry);
/** Selects this host's registry; the supervisor never calls this.
 * @param registry Shared ownership, or null at shutdown. */
void SetHostRecoveryRegistry(RecoveryRegistryHandle registry);
/** Commits an operation worker before any command is sent.
 * @param process Owned worker handle. @return Worker registry slot. */
uint32_t RegisterRecoveryWorker(HANDLE process);
/** Removes a worker record only after its native process has stopped.
 * @param slot Worker registry entry. */
void ReleaseRecoveryWorker(uint32_t slot);
/** Reserves capacity before even creating a capture directory.
 * @param process Owned worker. @return Slot, or throws when capture capacity is exhausted. */
uint32_t ReserveRecoveryRoot(HANDLE process);
/** Commits ownership before a worker may create output or start an application.
 * @param slot Reserved entry. @param ownership Native directory identity and UTF-8 path. */
void CommitRecoveryRoot(uint32_t slot, const std::vector<unsigned char>& ownership);
/** Frees a host-owned record only after verified cleanup.
 * @param slot Previously committed registry entry. */
void ReleaseRecoveryRoot(uint32_t slot);
/** Records an explicit tray Exit so shutdown failures never cause a restart. */
void RequestSupervisorExit();
/** Tests whether Exit was requested for the current instance.
 * @param registry Monitor registry. @param generation Instance identifier.
 * @return Whether the monitor should also exit. */
bool SupervisorExitRequested(const RecoveryRegistryHandle& registry, uint32_t generation);
/** Confirms that recorded operation workers from a dead host have stopped.
 * @param registry Monitor registry. @param generation Dead host instance.
 * @return Whether every matching native process has exited. */
cardio::promise<bool> StopOrphanWorkers(RecoveryRegistryHandle registry, uint32_t generation);
/** Makes a stopped host's capture records eligible for independent recovery.
 * @param registry Monitor registry. @param generation Last stopped host. */
void RetireRecoveryGeneration(const RecoveryRegistryHandle& registry, uint32_t generation);
/** Retries old capture records without blocking the new host.
 * @param registry Monitor registry. @param stop Monitor shutdown signal.
 * @return Completion after the current bounded attempt finishes. */
cardio::promise<void> RecoverRetiredRoots(RecoveryRegistryHandle registry, cardio::cancellation stop);
/** Reports paths still owned when the monitor exits.
 * @param registry Monitor registry. */
void ReportPendingRecovery(const RecoveryRegistryHandle& registry);
}
#endif
