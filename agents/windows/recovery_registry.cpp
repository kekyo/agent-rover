// agent-rover - Capture ownership retained across host restarts
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "recovery_registry.h"
#include "operation_worker.h"
#include "win32_cleanup.h"
#include "agent_log.h"
#include <cstring>
#include <array>
#include <stdexcept>

namespace agent_rover {

static constexpr uint32_t kRecoverySlots = 64;
struct RecoveryRecord {
  // Interlocked state publishes the complete record across processes. Only
  // the owning generation writes an active entry; the monitor reclaims it
  // after that generation's host and operation workers have stopped.
  LONG state; // 0 free, 1 being committed, 2 committed, 3 monitor recovery.
  uint32_t generation, process_id, size;
  FILETIME created;
  unsigned char ownership[32768];
};
struct SharedRecovery {
  LONG requested_exit;
  struct ProcessRecord {
    LONG state;
    uint32_t generation, process_id;
    FILETIME created;
  } workers[8];
  RecoveryRecord records[kRecoverySlots];
};
struct RecoveryRegistry {
  HANDLE mapping = nullptr;
  SharedRecovery* shared = nullptr;
  uint32_t generation = 0, retired = 0;
  ~RecoveryRegistry() {
    if (shared) UnmapViewOfFile(shared);
    if (mapping) CloseHandle(mapping);
  }
};
static RecoveryRegistryHandle host_registry;

static std::runtime_error Failure(const char* api) {
  return std::runtime_error(std::string(api) + " Win32=" + std::to_string(GetLastError()));
}

RecoveryRegistryHandle AttachRecoveryRegistry(HANDLE mapping, uint32_t generation) {
  auto registry = std::make_shared<RecoveryRegistry>();
  registry->mapping = mapping;
  registry->generation = generation;
  registry->shared = static_cast<SharedRecovery*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedRecovery)));
  if (!registry->shared) throw Failure("MapViewOfFile(recovery)");
  return registry;
}

RecoveryRegistryHandle CreateRecoveryRegistry() {
  const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedRecovery), nullptr);
  if (!mapping) throw Failure("CreateFileMappingW(recovery)");
  return AttachRecoveryRegistry(mapping, 0);
}

HANDLE RecoveryMapping(const RecoveryRegistryHandle& registry) { return registry->mapping; }
void SetHostRecoveryRegistry(RecoveryRegistryHandle registry) { host_registry = std::move(registry); }

uint32_t RegisterRecoveryWorker(HANDLE process) {
  if (!host_registry) return kNoRecoverySlot;
  FILETIME created = {}, exited = {}, kernel = {}, user = {};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) throw Failure("GetProcessTimes(worker)");
  const auto pid = GetProcessId(process);
  if (!pid) throw Failure("GetProcessId(worker)");
  for (uint32_t i = 0; i < 8; ++i) {
    auto& record = host_registry->shared->workers[i];
    if (InterlockedCompareExchange(&record.state, 1, 0) != 0) continue;
    record.generation = host_registry->generation;
    record.process_id = pid;
    record.created = created;
    InterlockedExchange(&record.state, 2);
    return i;
  }
  throw std::runtime_error("Operation ownership capacity reached.");
}

void ReleaseRecoveryWorker(uint32_t slot) {
  if (!host_registry || slot == kNoRecoverySlot) return;
  auto& record = host_registry->shared->workers[slot];
  if (record.generation != host_registry->generation || InterlockedCompareExchange(&record.state, 0, 2) != 2)
    throw std::runtime_error("Worker ownership generation mismatch.");
}

uint32_t ReserveRecoveryRoot(HANDLE process) {
  if (!host_registry) return kNoRecoverySlot;
  FILETIME created = {}, exited = {}, kernel = {}, user = {};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) throw Failure("GetProcessTimes(recovery)");
  const auto pid = GetProcessId(process);
  if (!pid) throw Failure("GetProcessId(recovery)");
  for (uint32_t i = 0; i < kRecoverySlots; ++i) {
    auto& record = host_registry->shared->records[i];
    if (InterlockedCompareExchange(&record.state, 1, 0) != 0) continue;
    record.generation = host_registry->generation;
    record.process_id = pid;
    record.created = created;
    record.size = 0;
    return i;
  }
  throw std::runtime_error("Capture recovery capacity reached; control operations remain available.");
}

void CommitRecoveryRoot(uint32_t slot, const std::vector<unsigned char>& ownership) {
  if (!host_registry) return;
  if (slot >= kRecoverySlots || ownership.size() <= sizeof(CaptureIdentity) || ownership.size() > 32768)
    throw std::runtime_error("Invalid recovery ownership.");
  auto& record = host_registry->shared->records[slot];
  if (record.generation != host_registry->generation || InterlockedCompareExchange(&record.state, 0, 0) != 1)
    throw std::runtime_error("Invalid capture reservation.");
  record.size = ownership.size();
  std::memcpy(record.ownership, ownership.data(), ownership.size());
  InterlockedExchange(&record.state, 2);
}

void ReleaseRecoveryRoot(uint32_t slot) {
  if (!host_registry || slot == kNoRecoverySlot) return;
  auto& record = host_registry->shared->records[slot];
  const auto state = InterlockedCompareExchange(&record.state, 0, 0);
  if (record.generation != host_registry->generation || (state != 1 && state != 2))
    throw std::runtime_error("Capture ownership generation mismatch.");
  InterlockedExchange(&record.state, 0);
}

void RequestSupervisorExit() {
  if (host_registry) InterlockedExchange(&host_registry->shared->requested_exit, host_registry->generation);
}
bool SupervisorExitRequested(const RecoveryRegistryHandle& registry, uint32_t generation) {
  return static_cast<uint32_t>(InterlockedCompareExchange(&registry->shared->requested_exit, 0, 0)) == generation;
}

cardio::promise<bool> StopOrphanWorkers(RecoveryRegistryHandle registry, uint32_t generation) {
  auto deadline = cardio::cancellations::timeout(10000);
  for (auto& record : registry->shared->workers) {
    if (InterlockedCompareExchange(&record.state, 0, 0) != 2 || record.generation != generation) continue;
    const auto process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, record.process_id);
    if (!process) {
      if (GetLastError() == ERROR_INVALID_PARAMETER) { InterlockedExchange(&record.state, 0); continue; }
      co_return false;
    }
    FILETIME created = {}, exited = {}, kernel = {}, user = {};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) { CloseHandle(process); co_return false; }
    if (CompareFileTime(&created, &record.created) != 0) { CloseHandle(process); InterlockedExchange(&record.state, 0); continue; }
    bool stopped = false;
    try {
      if (!TerminateProcess(process, ERROR_PROCESS_ABORTED) && WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
        throw Failure("TerminateProcess(orphan worker)");
      co_await cardio::from_win32_handle(process, deadline.get_cancellation());
      stopped = true;
    } catch (const std::exception& error) { PrintAgentLogEvent(std::string("phase=orphan-stop-failed reason=") + error.what()); }
    CloseHandle(process);
    if (!stopped) co_return false;
    InterlockedExchange(&record.state, 0);
  }
  co_return true;
}

void RetireRecoveryGeneration(const RecoveryRegistryHandle& registry, uint32_t generation) {
  registry->retired = generation;
  // An interrupted commit cannot have acknowledged capture ownership. It has
  // no output files and must not consume a slot across all future restarts.
  for (auto& record : registry->shared->workers) InterlockedCompareExchange(&record.state, 0, 1);
  for (auto& record : registry->shared->records) {
    if (InterlockedCompareExchange(&record.state, 0, 1) == 1) {
      PrintAgentLogEvent("phase=recovery-incomplete-registration no capture operation was acknowledged");
    } else if (record.generation <= generation) {
      // Claim before starting the next host. A retry retains state 3, so that
      // an active host can never reuse a record while the monitor reads it.
      InterlockedCompareExchange(&record.state, 3, 2);
    }
  }
}

cardio::promise<void> RecoverRetiredRoots(RecoveryRegistryHandle registry, cardio::cancellation stop) {
  // Preserve a failed attempt's helper until its exit is confirmed. A stuck
  // native teardown must never spawn an unbounded succession of replacements.
  std::array<Worker, kRecoverySlots> attempts;
  while (!stop.is_cancellation_requested()) {
    for (uint32_t i = 0; i < kRecoverySlots; ++i) {
      auto& record = registry->shared->records[i];
      if (stop.is_cancellation_requested()) break;
      if (InterlockedCompareExchange(&record.state, 0, 0) != 3 || record.generation > registry->retired) continue;
      if (!attempts[i]) {
        const std::vector<unsigned char> ownership(record.ownership, record.ownership + record.size);
        attempts[i] = AdoptRecoveryRoot(ownership);
      }
      const auto recovered = co_await RecoverOperationWorker(attempts[i]);
      if (recovered) { attempts[i].reset(); InterlockedExchange(&record.state, 0); }
      PrintAgentLogEvent(recovered ? "phase=supervisor-capture-recovered" : "phase=supervisor-capture-pending");
    }
    try { co_await cardio::promises::delay(1000, stop); }
    catch (const cardio::canceled_exception&) { break; }
  }
}

void ReportPendingRecovery(const RecoveryRegistryHandle& registry) {
  for (auto& record : registry->shared->records) {
    const auto state = InterlockedCompareExchange(&record.state, 0, 0);
    if ((state != 2 && state != 3) || record.size <= sizeof(CaptureIdentity) || record.size > 32768) continue;
    PrintAgentLogEvent("phase=supervisor-exit-capture-pending path=" + SanitizeAgentLogField(
        std::string(record.ownership + sizeof(CaptureIdentity), record.ownership + record.size)));
  }
}
}
