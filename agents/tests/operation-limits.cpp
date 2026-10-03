#include "operation_limits.h"
#include "supervisor_limits.h"
#include <cassert>
int main() {
  using namespace agent_rover;
  assert(OperationBudgetMs("window.setBounds", 0) == 30000);
  assert(OperationBudgetMs("file.read", 0) == 120000);
  assert(OperationBudgetMs("video.result", 600000) == 720000);
  assert(OperationBudgetMs("video.result", 0xffffffff) == 720000);
  assert(RemainingOperationMs(30000, 29999) == 1);
  assert(RemainingOperationMs(30000, 30000) == 0);
  assert(RemainingOperationMs(30000, 0xffffffffffffffffull) == 0);
  std::deque<uint64_t> starts;
  assert(ReserveSupervisorStart(&starts, 0) == 0);
  assert(ReserveSupervisorStart(&starts, 1) == 0);
  assert(ReserveSupervisorStart(&starts, 2) == 0);
  for (uint64_t time = 3; time < 60000; time += 997) {
    assert(ReserveSupervisorStart(&starts, time) == 60000 - time);
    assert(starts.size() == 3);
  }
  assert(ReserveSupervisorStart(&starts, 60000) == 0);
  assert(ReserveSupervisorStart(&starts, 60000) == 1);
  assert(ReserveSupervisorStart(&starts, 60001) == 0);
  assert(ReserveSupervisorStart(&starts, 60001) == 1);
  assert(ReserveSupervisorStart(&starts, 120002) == 0);
  assert(starts.size() == 1);
}
