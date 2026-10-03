#include "operation_limits.h"
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
}
