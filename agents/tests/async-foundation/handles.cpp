// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <cardio.h>
#include <cstdlib>

static cardio::promise<void> CheckIndependentWaits(
    HANDLE held_event,
    HANDLE ready_event,
    int* exit_code) {
  auto held = cardio::from_win32_handle(held_event);
  auto ready = cardio::from_win32_handle(ready_event);
  if (!SetEvent(ready_event)) {
    // A failed signal cannot release the borrowed handle safely. Fail this
    // standalone probe immediately; process teardown reclaims its resources.
    std::abort();
  }
  const auto ready_result = co_await ready;
  const bool independent =
      ready_result == cardio::win32_handle_event::signaled && !held.is_ready();
  if (!SetEvent(held_event)) std::abort();
  const auto held_result = co_await held;
  if (independent && held_result == cardio::win32_handle_event::signaled) {
    *exit_code = 0;
  }
}

int main() {
  cardio::dispatcher_host_win32 dispatcher;
  const auto held_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  const auto ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (held_event == nullptr || ready_event == nullptr) {
    if (held_event != nullptr) CloseHandle(held_event);
    if (ready_event != nullptr) CloseHandle(ready_event);
    return 2;
  }
  int exit_code = 1;
  // Keep the coroutine, its output, and both borrowed handles alive until drain.
  auto check = CheckIndependentWaits(held_event, ready_event, &exit_code);
  dispatcher.park();
  CloseHandle(ready_event);
  CloseHandle(held_event);
  return exit_code;
}
