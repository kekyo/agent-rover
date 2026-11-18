// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WIN32_JOB_H
#define AGENT_ROVER_WIN32_JOB_H
#include <windows.h>
#include <cstdint>
#include <vector>
#include "operation_error.h"

namespace agent_rover {
/**
 * Enumerates current Job process IDs with bounded buffer growth.
 * @param job Job handle with query access.
 * @param ids Receives the complete snapshot, empty on failure.
 * @param error Receives a native query or capacity failure.
 * @return Whether enumeration succeeded. IDs must be revalidated before use.
 */
bool ListJobProcessIds(HANDLE job, std::vector<uint32_t>* ids, OperationError* error);

/**
 * Opens and verifies one candidate against the retained Job.
 * @param job Job handle with query access.
 * @param id Candidate process ID from enumeration.
 * @param access Requested access, including PROCESS_QUERY_INFORMATION.
 * @param process Receives the verified handle owned by the caller, or null if
 * the process disappeared or its PID was reused outside this Job.
 * @param error Receives errors other than a process disappearing.
 * @return Whether verification succeeded. Query state using the returned handle.
 */
bool OpenJobProcess(HANDLE job, uint32_t id, DWORD access, HANDLE* process, OperationError* error);
}
#endif
