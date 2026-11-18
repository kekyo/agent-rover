// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_job.h"
#include <cstddef>

namespace agent_rover {
bool ListJobProcessIds(HANDLE job, std::vector<uint32_t>* ids, OperationError* error) {
  ids->clear();
  for (size_t capacity = 128; capacity <= 65536; capacity *= 2) {
    const size_t bytes = offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList) + capacity * sizeof(ULONG_PTR);
    // ULONG_PTR storage preserves alignment on both Windows architectures.
    std::vector<ULONG_PTR> storage((bytes + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
    const auto list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(bytes), nullptr)) {
      const auto code = GetLastError();
      if (code == ERROR_MORE_DATA) continue;
      *error = MakeOperationError("QueryInformationJobObject(process ids)", "", code);
      return false;
    }
    if (list->NumberOfProcessIdsInList < list->NumberOfAssignedProcesses) continue;
    if (list->NumberOfProcessIdsInList > capacity) {
      *error = MakeOperationError("QueryInformationJobObject(process ids)", "", ERROR_INVALID_DATA);
      return false;
    }
    ids->assign(list->ProcessIdList, list->ProcessIdList + list->NumberOfProcessIdsInList);
    return true;
  }
  *error = MakeOperationError("QueryInformationJobObject(process ids)", "", ERROR_MORE_DATA);
  return false;
}

bool OpenJobProcess(HANDLE job, uint32_t id, DWORD access, HANDLE* process, OperationError* error) {
  *process = nullptr;
  const auto candidate = OpenProcess(access, FALSE, id);
  if (!candidate) {
    const auto code = GetLastError();
    if (code == ERROR_INVALID_PARAMETER) return true; // Exited during enumeration.
    *error = MakeOperationError("OpenProcess(Job member)", "", code);
    return false;
  }
  BOOL member = FALSE;
  if (!IsProcessInJob(candidate, job, &member)) {
    const auto code = GetLastError();
    CloseHandle(candidate);
    *error = MakeOperationError("IsProcessInJob", "", code);
    return false;
  }
  if (!member) { CloseHandle(candidate); return true; } // Recycled PID, not a member.
  *process = candidate;
  return true;
}
}
