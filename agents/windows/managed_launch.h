// agent-rover - Parent-owned application launch protocol
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_MANAGED_LAUNCH_H
#define AGENT_ROVER_MANAGED_LAUNCH_H
#include "win32_process.h"

namespace agent_rover {
/** Serializes a launch for the private pipe; never sent over the network.
 * @param options Launch settings. @return Bounded launch payload. */
std::vector<unsigned char> EncodeManagedLaunch(const ManagedProcessLaunchOptions& options);
/** Decodes a private launch, rejecting malformed or trailing bytes.
 * @param bytes Private payload. @return Launch settings. */
ManagedProcessLaunchOptions DecodeManagedLaunch(const std::vector<unsigned char>& bytes);
/** Installs synchronous IPC delegates only inside the isolated operation worker. */
void InstallManagedLaunchBroker();
/** Runs one launch inside a Job already owned by the host.
 * @return Native helper exit code. */
int RunManagedLaunchWorker();
}
#endif
