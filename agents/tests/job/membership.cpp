#include "win32_job.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <set>

static DWORD last_error = 0;
static DWORD query_error = 0;
static DWORD open_error = 0;
static DWORD membership_error = 0;
static bool recycled = false;
static bool incomplete_success = false;
static size_t count = 0;
static unsigned queries = 0;
static std::set<HANDLE> open_handles;
static const auto job = reinterpret_cast<HANDLE>(1);
static const auto opened_process = reinterpret_cast<HANDLE>(2);

DWORD GetLastError() { return last_error; }
BOOL QueryInformationJobObject(HANDLE actual_job, JOBOBJECTINFOCLASS, void* buffer, DWORD size, DWORD*) {
  assert(actual_job == job);
  ++queries;
  if (query_error) { last_error = query_error; return FALSE; }
  auto* list = static_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer);
  const size_t capacity = (size - offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList)) / sizeof(ULONG_PTR);
  list->NumberOfAssignedProcesses = count;
  list->NumberOfProcessIdsInList = std::min(count, capacity);
  for (size_t i = 0; i < list->NumberOfProcessIdsInList; ++i) list->ProcessIdList[i] = i + 10;
  if (capacity < count) { last_error = ERROR_MORE_DATA; return incomplete_success; }
  return 1;
}
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD id) {
  assert(access == 123 && !inherit && id == 42);
  if (open_error) { last_error = open_error; return nullptr; }
  open_handles.insert(opened_process);
  return opened_process;
}
BOOL IsProcessInJob(HANDLE process, HANDLE actual_job, BOOL* member) {
  assert(process == opened_process && actual_job == job && open_handles.contains(process));
  if (membership_error) { last_error = membership_error; return FALSE; }
  *member = !recycled;
  return 1;
}
BOOL CloseHandle(HANDLE handle) { assert(open_handles.erase(handle) == 1); return 1; }
agent_rover::OperationError agent_rover::MakeOperationError(const std::string& operation, const std::string& path, uint32_t code) {
  OperationError error;
  error.native_operation = operation;
  error.path = path;
  error.os_code = code;
  return error;
}

int main() {
  using namespace agent_rover;
  OperationError error;
  std::vector<uint32_t> ids;
  assert(ListJobProcessIds(job, &ids, &error) && ids.empty());
  for (const bool success : {false, true}) {
    incomplete_success = success;
    count = 1000;
    queries = 0;
    assert(ListJobProcessIds(job, &ids, &error) && ids.size() == count);
    assert(ids.front() == 10 && ids.back() == 1009 && queries > 1 && queries < 20);
  }
  query_error = 5;
  assert(!ListJobProcessIds(job, &ids, &error) && error.os_code == 5 && ids.empty());
  query_error = 0;
  count = 1000000;
  queries = 0;
  assert(!ListJobProcessIds(job, &ids, &error) && error.os_code == ERROR_MORE_DATA && queries < 20);
  HANDLE process = nullptr;
  assert(OpenJobProcess(job, 42, 123, &process, &error) && process == opened_process);
  // Consumers receive the exact handle whose identity was checked, not a PID to reopen.
  CloseHandle(process);
  recycled = true;
  assert(OpenJobProcess(job, 42, 123, &process, &error) && !process && open_handles.empty());
  recycled = false;
  open_error = ERROR_INVALID_PARAMETER;
  assert(OpenJobProcess(job, 42, 123, &process, &error) && !process);
  open_error = 5;
  assert(!OpenJobProcess(job, 42, 123, &process, &error) && error.os_code == 5);
  open_error = 0;
  membership_error = 5;
  assert(!OpenJobProcess(job, 42, 123, &process, &error) && !process && error.os_code == 5 && open_handles.empty());
  std::cout << "Job membership races, buffer growth and failures passed\n";
}
