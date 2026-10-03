// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <condition_variable>
#include <mutex>
#include <thread>

int main() {
  std::mutex mutex;
  std::condition_variable condition;
  bool completed = false;
  std::thread worker([&]() {
    {
      const auto lock = std::lock_guard<std::mutex>(mutex);
      completed = true;
    }
    condition.notify_one();
  });
  {
    auto lock = std::unique_lock<std::mutex>(mutex);
    condition.wait(lock, [&]() { return completed; });
  }
  worker.join();
  return completed ? 0 : 1;
}
