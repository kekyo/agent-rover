#ifndef AGENT_ROVER_TEST_JOB_WINDOWS_H
#define AGENT_ROVER_TEST_JOB_WINDOWS_H
#include <cstdint>
using DWORD = uint32_t;
using ULONG_PTR = uintptr_t;
using HANDLE = void*;
using BOOL = int;
constexpr BOOL FALSE = 0;
constexpr DWORD ERROR_INVALID_PARAMETER = 87;
constexpr DWORD ERROR_MORE_DATA = 234;
constexpr DWORD ERROR_INVALID_DATA = 13;
struct JOBOBJECT_BASIC_PROCESS_ID_LIST {
  DWORD NumberOfAssignedProcesses;
  DWORD NumberOfProcessIdsInList;
  ULONG_PTR ProcessIdList[1];
};
enum JOBOBJECTINFOCLASS { JobObjectBasicProcessIdList };
BOOL QueryInformationJobObject(HANDLE, JOBOBJECTINFOCLASS, void*, DWORD, DWORD*);
HANDLE OpenProcess(DWORD, BOOL, DWORD);
BOOL IsProcessInJob(HANDLE, HANDLE, BOOL*);
BOOL CloseHandle(HANDLE);
DWORD GetLastError();
#endif
