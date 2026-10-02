// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#pragma once

#ifndef CARDIO_H
#define CARDIO_H

//-----------------------------------------------------------------

#ifndef CARDIO_WITH_LINUX_IO_URING
#define CARDIO_WITH_LINUX_IO_URING 0
#endif

#ifndef CARDIO_WITH_GLIB
#define CARDIO_WITH_GLIB 0
#endif

#ifndef CARDIO_WITH_GIO
#define CARDIO_WITH_GIO 0
#endif

#ifndef CARDIO_WITH_SUPPLEMENTAL
#define CARDIO_WITH_SUPPLEMENTAL 1
#endif

#ifndef CARDIO_WITH_PRIMITIVES
#define CARDIO_WITH_PRIMITIVES 1
#endif

#ifndef CARDIO_SHARED_LIB
#define CARDIO_SHARED_LIB 0
#endif

#ifndef CARDIO_BUILD_SHARED_LIB
#define CARDIO_BUILD_SHARED_LIB 0
#endif

#if CARDIO_BUILD_SHARED_LIB && !CARDIO_SHARED_LIB
#error "CARDIO_BUILD_SHARED_LIB requires CARDIO_SHARED_LIB"
#endif

#ifndef CARDIO_API
#if CARDIO_SHARED_LIB
#if defined(_WIN32)
#if CARDIO_BUILD_SHARED_LIB
#define CARDIO_API __declspec(dllexport)
#else
#define CARDIO_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CARDIO_API __attribute__((visibility("default")))
#else
#define CARDIO_API
#endif
#else
#define CARDIO_API
#endif
#endif

//-----------------------------------------------------------------

#ifndef CARDIO_HAS_WIN32_HANDLE
#if defined(_WIN32)
#define CARDIO_HAS_WIN32_HANDLE 1
#else
#define CARDIO_HAS_WIN32_HANDLE 0
#endif
#endif

#if defined(_WIN32) || CARDIO_HAS_WIN32_HANDLE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#endif

#include <stdlib.h>

#ifndef CARDIO_HAS_POSIX_FD
#if defined(_POSIX_C_SOURCE) || defined(__ANDROID__)
#define CARDIO_HAS_POSIX_FD 1
#else
#define CARDIO_HAS_POSIX_FD 0
#endif
#endif

#ifndef CARDIO_HAS_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define CARDIO_HAS_EXCEPTIONS 1
#else
#define CARDIO_HAS_EXCEPTIONS 0
#endif
#endif

#if CARDIO_WITH_GLIB && !CARDIO_HAS_POSIX_FD
#error "CARDIO_WITH_GLIB requires CARDIO_HAS_POSIX_FD"
#endif

#if defined(__ANDROID__) && !CARDIO_HAS_POSIX_FD
#error "Android requires CARDIO_HAS_POSIX_FD"
#endif

#if CARDIO_WITH_GIO && !CARDIO_WITH_GLIB
#error "CARDIO_WITH_GIO requires CARDIO_WITH_GLIB"
#endif

#if CARDIO_WITH_LINUX_IO_URING && !defined(__linux__)
#error "CARDIO_WITH_LINUX_IO_URING requires Linux"
#endif

#if defined(__ANDROID__) && CARDIO_WITH_LINUX_IO_URING
#error "CARDIO_WITH_LINUX_IO_URING is not supported on Android"
#endif

#if CARDIO_WITH_LINUX_IO_URING && !CARDIO_HAS_POSIX_FD
#error "CARDIO_WITH_LINUX_IO_URING requires CARDIO_HAS_POSIX_FD"
#endif

#if CARDIO_HAS_POSIX_FD && CARDIO_HAS_WIN32_HANDLE
#error "CARDIO_HAS_POSIX_FD and CARDIO_HAS_WIN32_HANDLE cannot be enabled together"
#endif

#if CARDIO_HAS_POSIX_FD || CARDIO_HAS_WIN32_HANDLE
#define CARDIO_HAS_NATIVE_WAIT 1
#else
#define CARDIO_HAS_NATIVE_WAIT 0
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <thread>

#if CARDIO_WITH_SUPPLEMENTAL
#include <chrono>
#endif

#if CARDIO_HAS_POSIX_FD
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <android/looper.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

#if CARDIO_WITH_LINUX_IO_URING
#include <liburing.h>
#include <sys/eventfd.h>
#endif

#if CARDIO_WITH_GIO
#include <gio/gio.h>
#elif CARDIO_WITH_GLIB
#include <glib.h>
#endif

#if CARDIO_WITH_GIO
#if !GLIB_CHECK_VERSION(2, 44, 0)
#error "CARDIO_WITH_GIO requires GLib/GIO 2.44 or newer"
#endif
#endif

namespace cardio {

//-----------------------------------------------------------------------------------------------

namespace internal {
#if defined(_WIN32)
  // MinGW's win32 thread model before GCC 13 has no C++ threading classes.
  // Use XP primitives even when native handle waits are disabled.
  [[noreturn]] inline void fail_thread_error(DWORD error) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::system_error(
        static_cast<int>(error), std::system_category(),
        "cardio: Windows threading operation failed");
#else
    (void)error;
    std::terminate();
#endif
  }

  class mutex {
  private:
    CRITICAL_SECTION section_{};

  public:
    inline mutex() {
      if (::InitializeCriticalSectionAndSpinCount(&section_, 0) == 0) {
        fail_thread_error(::GetLastError());
      }
    }
    inline ~mutex() { ::DeleteCriticalSection(&section_); }
    inline void lock() noexcept { ::EnterCriticalSection(&section_); }
    inline void unlock() noexcept { ::LeaveCriticalSection(&section_); }
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
  };

  class condition_variable {
  private:
    struct waiter {
      HANDLE event;
      waiter* next;
    };
    mutex mutex_;
    waiter* waiters_ = nullptr;

    inline void wait_for(std::unique_lock<mutex>& lock, DWORD timeout) {
      auto entry = waiter{::CreateEventW(nullptr, FALSE, FALSE, nullptr), nullptr};
      if (entry.event == nullptr) {
        fail_thread_error(::GetLastError());
      }
      {
        auto guard = std::lock_guard<mutex>(mutex_);
        entry.next = waiters_;
        waiters_ = &entry;
      }
      lock.unlock();
      const auto result = ::WaitForSingleObject(entry.event, timeout);
      const auto error = result == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;
      {
        // Unlink before closing the event, including when a timeout races a
        // notifier. SetEvent also runs under this lock, so it cannot use a
        // closed event. Each waiter has its own notification to avoid stealing
        // wakeups or losing a broadcast during mutex reacquisition.
        auto guard = std::lock_guard<mutex>(mutex_);
        auto** current = &waiters_;
        while (*current != nullptr && *current != &entry) {
          current = &(*current)->next;
        }
        if (*current == &entry) {
          *current = entry.next;
        }
        (void)::CloseHandle(entry.event);
      }
      lock.lock();
      if (error != ERROR_SUCCESS) {
        fail_thread_error(error);
      }
    }

  public:
    inline void notify_one() noexcept {
      auto guard = std::lock_guard<mutex>(mutex_);
      if (waiters_ != nullptr) {
        auto* entry = waiters_;
        waiters_ = entry->next;
        if (::SetEvent(entry->event) == 0) {
          std::terminate();
        }
      }
    }
    inline void notify_all() noexcept {
      auto guard = std::lock_guard<mutex>(mutex_);
      while (waiters_ != nullptr) {
        auto* entry = waiters_;
        waiters_ = entry->next;
        if (::SetEvent(entry->event) == 0) {
          std::terminate();
        }
      }
    }
    template <typename Predicate>
    inline void wait(std::unique_lock<mutex>& lock, Predicate predicate) {
      while (!predicate()) {
        wait_for(lock, INFINITE);
      }
    }
    inline void wait_until(
        std::unique_lock<mutex>& lock,
        std::chrono::steady_clock::time_point deadline) {
      const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()).count();
      const auto timeout = remaining <= 0 ? DWORD{0} :
          remaining >= INFINITE ? INFINITE - 1 : static_cast<DWORD>(remaining);
      wait_for(lock, timeout);
    }
  };

  class thread {
  private:
    HANDLE handle_ = nullptr;
    unsigned id_ = 0;

    template <typename Function>
    static unsigned __stdcall run(void* argument) noexcept {
      auto function = std::unique_ptr<Function>(static_cast<Function*>(argument));
      std::invoke(*function);
      return 0;
    }

  public:
    using id = DWORD;
    inline thread() noexcept = default;
    template <typename Function>
    inline explicit thread(Function&& function) {
      using function_type = std::decay_t<Function>;
      auto owned = std::make_unique<function_type>(std::forward<Function>(function));
      handle_ = reinterpret_cast<HANDLE>(::_beginthreadex(
          nullptr, 0, &run<function_type>, owned.get(), 0, &id_));
      if (handle_ == nullptr) {
        fail_thread_error(ERROR_NOT_ENOUGH_MEMORY);
      }
      (void)owned.release();
    }
    inline thread(thread&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          id_(std::exchange(other.id_, 0)) {}
    inline thread& operator=(thread&& other) noexcept {
      if (joinable()) {
        std::terminate();
      }
      handle_ = std::exchange(other.handle_, nullptr);
      id_ = std::exchange(other.id_, 0);
      return *this;
    }
    inline ~thread() {
      if (joinable()) {
        std::terminate();
      }
    }
    inline bool joinable() const noexcept { return handle_ != nullptr; }
    inline void join() {
      if (!joinable() || id_ == ::GetCurrentThreadId()) {
        fail_thread_error(ERROR_INVALID_PARAMETER);
      }
      if (::WaitForSingleObject(handle_, INFINITE) != WAIT_OBJECT_0) {
        fail_thread_error(::GetLastError());
      }
      detach();
    }
    inline void detach() {
      if (!joinable()) {
        fail_thread_error(ERROR_INVALID_HANDLE);
      }
      (void)::CloseHandle(handle_);
      handle_ = nullptr;
      id_ = 0;
    }
    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
  };

  inline thread::id current_thread_id() noexcept {
    return ::GetCurrentThreadId();
  }
#else
  using mutex = std::mutex;
  using condition_variable = std::condition_variable;
  using thread = std::thread;
  inline thread::id current_thread_id() noexcept {
    return std::this_thread::get_id();
  }
#endif
}  // namespace internal

//-----------------------------------------------------------------------------------------------

class cancellation_registration;
class cancellation_source;
class dispatcher_group;
class dispatcher;
class dispatcher_host;
#if defined(__ANDROID__)
class dispatcher_host_android;
class dispatcher_host_android_auto;
#endif
#if CARDIO_HAS_WIN32_HANDLE
class dispatcher_host_win32_auto;
#endif
#if CARDIO_WITH_GLIB
class dispatcher_group_glib;
class dispatcher_host_glib;
class dispatcher_host_glib_auto;
#endif

namespace internal {
#if defined(__ANDROID__)
  class dispatcher_host_android_base;
#endif
  struct cancellation_callback_entry;
  struct cancellation_state;
  struct dispatcher_lifetime;
  struct timer_awaiter;
  struct timer_wait_state;
  void configure_timeout_cancellation(
      cancellation_source& source,
      std::chrono::steady_clock::time_point deadline,
      dispatcher* target);
  bool request_cancellation(
      const std::shared_ptr<cancellation_state>& state) noexcept;
  void arm_timeout_cancellation(
      const std::shared_ptr<cancellation_state>& state);
  void disarm_timeout_cancellation(
      const std::shared_ptr<cancellation_state>& state) noexcept;
  void retain_cancellation_registration(
      cancellation_source& source,
      cancellation_registration registration);
  void dangerous_schedule_later__(
      dispatcher* target,
      std::function<void()> callback);
#if CARDIO_HAS_POSIX_FD
  struct fd_awaiter;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  struct win32_handle_awaiter;
  struct win32_overlapped_awaiter;
#endif
}

#if CARDIO_WITH_LINUX_IO_URING
class io_uring;

/**
 * io_uring completion result copied from a CQE.
 */
struct io_uring_completion {
  /**
   * CQE result value.
   *
   * @remarks
   * Negative values represent errno values returned by io_uring operations.
   */
  int result;

  /**
   * CQE flags.
   */
  unsigned flags;
};
#endif

template <typename T> class promise;
template <typename T> class promise_source;

/**
 * Controls how dispatcher host park functions order external message pumping
 * and cardio continuation work.
 */
enum class park_policy {
  /**
   * Pump GLib sources or Win32 messages before running queued or inline
   * continuations.
   */
  external_pump_first,

  /**
   * Run queued and inline continuations before pumping GLib sources or Win32
   * messages.
   */
  continuation_first,
};

/**
 * Controls which threads may execute a manual dispatcher host.
 */
enum class dispatcher_thread_policy {
  /** Allow any thread to park the dispatcher. */
  any_thread,
  /** Only the constructing thread may park the dispatcher. */
  current_thread,
};

/**
 * Controls how parked dispatcher hosts react to a shutdown request.
 */
enum class shutdown_mode {
  /**
   * Drain already queued or ready work before park() exits.
   */
  gentle,

  /**
   * Force park() to exit without draining queued or waiting work.
   */
  unsafe_immediate,
};

/**
 * Dispatcher capability flags.
 */
enum class dispatcher_feature : unsigned {
  /**
   * No capability.
   */
  none = 0,

  /**
   * POSIX file descriptor waits are available.
   */
  posix = 1u << 0,

  /**
   * Win32 handle waits are available.
   */
  win32 = 1u << 1,

  /**
   * Linux io_uring integration is available.
   */
  io_uring = 1u << 2,

  /**
   * GLib main context integration is available.
   */
  glib = 1u << 3,

  /**
   * GIO helper integration is available.
   */
  gio = 1u << 4,

  /**
   * C++ exception based failure propagation is available.
   */
  exceptions = 1u << 5,

  /**
   * Android Looper integration is available.
   */
  android = 1u << 6,
};

/**
 * Combines dispatcher feature flags.
 *
 * @param left Left feature flags.
 * @param right Feature flags to add.
 * @return Combined feature flags.
 */
inline constexpr dispatcher_feature operator|(
    dispatcher_feature left,
    dispatcher_feature right) noexcept {
  return static_cast<dispatcher_feature>(
      static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

/**
 * Intersects dispatcher feature flags.
 *
 * @param left Left feature flags.
 * @param right Feature flags to keep.
 * @return Intersected feature flags.
 */
inline constexpr dispatcher_feature operator&(
    dispatcher_feature left,
    dispatcher_feature right) noexcept {
  return static_cast<dispatcher_feature>(
      static_cast<unsigned>(left) & static_cast<unsigned>(right));
}

/**
 * Combines dispatcher feature flags in-place.
 *
 * @param left Target feature flags.
 * @param right Feature flags to add.
 * @return Target feature flags.
 */
inline constexpr dispatcher_feature& operator|=(
    dispatcher_feature& left,
    dispatcher_feature right) noexcept {
  left = left | right;
  return left;
}

/**
 * Intersects dispatcher feature flags in-place.
 *
 * @param left Target feature flags.
 * @param right Feature flags to keep.
 * @return Target feature flags.
 */
inline constexpr dispatcher_feature& operator&=(
    dispatcher_feature& left,
    dispatcher_feature right) noexcept {
  left = left & right;
  return left;
}

#if CARDIO_HAS_POSIX_FD
/**
 * File descriptor readiness events.
 */
enum class fd_event : unsigned {
  /**
   * No event.
   */
  none = 0,

  /**
   * The file descriptor can be read.
   */
  read = 1,

  /**
   * The file descriptor can be written.
   */
  write = 2,

  /**
   * The file descriptor reported an error.
   */
  error = 4,

  /**
   * The file descriptor reported a hangup.
   */
  hangup = 8,
};

/**
 * Combines file descriptor readiness events.
 *
 * @param left Left events.
 * @param right Right events.
 * @return Combined events.
 */
inline constexpr fd_event operator|(fd_event left, fd_event right) noexcept {
  return static_cast<fd_event>(
      static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

/**
 * Intersects file descriptor readiness events.
 *
 * @param left Left events.
 * @param right Right events.
 * @return Intersected events.
 */
inline constexpr fd_event operator&(fd_event left, fd_event right) noexcept {
  return static_cast<fd_event>(
      static_cast<unsigned>(left) & static_cast<unsigned>(right));
}

/**
 * Combines file descriptor readiness events in-place.
 *
 * @param left Target events.
 * @param right Events to add.
 * @return Target events.
 */
inline constexpr fd_event& operator|=(fd_event& left, fd_event right) noexcept {
  left = left | right;
  return left;
}

/**
 * Intersects file descriptor readiness events in-place.
 *
 * @param left Target events.
 * @param right Events to keep.
 * @return Target events.
 */
inline constexpr fd_event& operator&=(fd_event& left, fd_event right) noexcept {
  left = left & right;
  return left;
}
#endif

#if CARDIO_HAS_WIN32_HANDLE
class io_completion_port;

/**
 * Win32 handle wait events.
 */
enum class win32_handle_event : unsigned {
  /**
   * No event.
   */
  none = 0,

  /**
   * The handle was signaled.
   */
  signaled = 1,

  /**
   * The mutex handle was abandoned while being waited on.
   */
  abandoned = 2,
};

/**
 * Win32 OVERLAPPED operation completion result.
 */
struct win32_overlapped_result {
  /**
   * Number of bytes transferred by the completed operation.
   */
  DWORD bytes_transferred;
};

/**
 * Win32 I/O completion port operation completion result.
 */
struct win32_iocp_completion {
  /**
   * Win32 error code reported for the completed operation.
   */
  DWORD error;

  /**
   * Number of bytes transferred by the completed operation.
   */
  DWORD bytes_transferred;

  /**
   * Completion key associated with the handle.
   */
  ULONG_PTR completion_key;
};
#endif

//-----------------------------------------------------------------

/**
 * Exception used when an operation is canceled.
 */
class canceled_exception : public std::runtime_error {
public:
  /**
   * Creates a cancellation exception.
   *
   * @param message Diagnostic message.
   */
  inline explicit canceled_exception(
      const char* message = "cardio: operation canceled")
      : std::runtime_error(message) {}
};

/**
 * RAII registration for a cancellation callback.
 *
 * @remarks
 * Destroying or resetting the registration unregisters the callback when it has
 * not started yet. If cancellation is already dispatching callbacks, a racing
 * callback may still run.
 */
class cancellation_registration {
private:
  friend class cancellation;

  std::shared_ptr<internal::cancellation_state> state_;
  std::shared_ptr<internal::cancellation_callback_entry> entry_;

  inline cancellation_registration(
      std::shared_ptr<internal::cancellation_state> state,
      std::shared_ptr<internal::cancellation_callback_entry> entry) noexcept
      : state_(std::move(state)),
        entry_(std::move(entry)) {}

public:
  /**
   * Creates an empty registration.
   */
  inline cancellation_registration() noexcept = default;

  /**
   * Destroys the registration and unregisters the callback.
   */
  inline ~cancellation_registration() {
    reset();
  }

  cancellation_registration(const cancellation_registration&) = delete;
  cancellation_registration& operator=(const cancellation_registration&) = delete;

  /**
   * Moves a registration.
   *
   * @param other Source registration.
   */
  inline cancellation_registration(cancellation_registration&& other) noexcept
      : state_(std::move(other.state_)),
        entry_(std::move(other.entry_)) {}

  /**
   * Moves a registration.
   *
   * @param other Source registration.
   * @return This registration.
   */
  inline cancellation_registration& operator=(
      cancellation_registration&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();
    state_ = std::move(other.state_);
    entry_ = std::move(other.entry_);
    return *this;
  }

  /**
   * Unregisters the callback if it is still pending.
   */
  inline void reset() noexcept;
};

/**
 * Read-only cancellation signal.
 *
 * @remarks
 * The default constructed value is never canceled.
 */
class cancellation {
private:
  friend class cancellation_source;
  friend struct internal::timer_awaiter;

#if CARDIO_HAS_POSIX_FD
  friend struct internal::fd_awaiter;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  friend struct internal::win32_handle_awaiter;
  friend struct internal::win32_overlapped_awaiter;
#endif
#if CARDIO_WITH_LINUX_IO_URING
  friend class io_uring;
#endif
  friend void internal::arm_timeout_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state);
  friend void internal::disarm_timeout_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state) noexcept;
  friend bool internal::request_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state) noexcept;

  std::shared_ptr<internal::cancellation_state> state_;

  inline explicit cancellation(
      std::shared_ptr<internal::cancellation_state> state) noexcept
      : state_(std::move(state)) {}

  inline bool try_register_callback(
      std::function<void()> callback,
      cancellation_registration& registration) const;

public:
  /**
   * Creates a non-cancelable signal.
   */
  inline cancellation() noexcept = default;

  /**
   * Returns whether cancellation has been requested.
   *
   * @return True when cancellation has been requested.
   */
  inline bool is_cancellation_requested() const noexcept;

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Throws canceled_exception when cancellation has been requested.
   *
   * @throws canceled_exception Thrown when cancellation has been requested.
   */
  inline void throw_if_cancellation_requested() const;
#endif

  /**
   * Registers a callback invoked when cancellation is requested.
   *
   * @param callback Callback to invoke.
   * @return Registration controlling the callback lifetime.
   *
   * @remarks
   * The current dispatcher is captured when the callback is registered, and
   * the callback is queued to that dispatcher when cancellation is requested.
   * If cancellation has already been requested, the callback is queued before
   * this function returns and the returned registration is empty. The captured
   * dispatcher must outlive the registration and queued callback.
   */
  inline cancellation_registration on_cancellation_requested(
      std::function<void()> callback) const;
};

/**
 * Source that requests cancellation for its associated cancellation signal.
 */
class cancellation_source {
private:
  friend void internal::configure_timeout_cancellation(
      cancellation_source& source,
      std::chrono::steady_clock::time_point deadline,
      dispatcher* target);
  friend void internal::retain_cancellation_registration(
      cancellation_source& source,
      cancellation_registration registration);

  std::shared_ptr<internal::cancellation_state> state_;

public:
  /**
   * Creates a cancellation source.
   */
  inline cancellation_source();

  /**
   * Returns the read-only cancellation signal.
   *
   * @return Associated cancellation signal.
   */
  inline cancellation get_cancellation() const noexcept;

  /**
   * Requests cancellation.
   *
   * @return True when this call requested cancellation, or false when
   * cancellation had already been requested.
   */
  inline bool cancel() noexcept;
};

//-----------------------------------------------------------------

namespace internal {
  struct switch_to_awaiter;
#if CARDIO_HAS_POSIX_FD
  struct fd_awaiter;
#endif
#if CARDIO_WITH_LINUX_IO_URING
  struct io_uring_operation_state;
  struct io_uring_ring_state;
#endif
  template <typename T> promise<T> make_resolved_promise(T value);
#if CARDIO_HAS_EXCEPTIONS
  template <typename T>
  promise<T> make_rejected_promise(std::exception_ptr exception);
#endif
  promise<void> make_resolved_void_promise();
#if CARDIO_HAS_EXCEPTIONS
  promise<void> make_rejected_void_promise(std::exception_ptr exception);
#endif

  inline constexpr auto max_inline_continuation_depth = std::size_t{64};

  struct runtime_thread_state {
    dispatcher* current_dispatcher = nullptr;
    std::size_t inline_continuation_depth = 0;
    std::size_t dispatcher_execution_depth = 0;
    std::size_t final_suspend_depth = 0;
    bool inline_continuation_draining = false;
    std::size_t inline_continuation_count = 0;
    void* inline_continuations[max_inline_continuation_depth] = {};
  };

#if CARDIO_SHARED_LIB
  CARDIO_API runtime_thread_state& runtime_state() noexcept;
#else
  inline runtime_thread_state& runtime_state() noexcept {
    thread_local runtime_thread_state state;
    return state;
  }
#endif

  struct cancellation_callback_entry {
    std::function<void()> callback;
    dispatcher* target = nullptr;
    cancellation_state* owner = nullptr;
    cancellation_callback_entry* previous = nullptr;
    cancellation_callback_entry* next = nullptr;
    bool registered = false;
  };

  struct scheduled_cancellation_callback {
    std::function<void()> callback;
    dispatcher* target = nullptr;
  };

  struct cancellation_state {
    ::cardio::internal::mutex mutex;
    bool cancellation_requested = false;
    cancellation_callback_entry* callbacks = nullptr;
    std::vector<cancellation_registration> retained_registrations;
    std::optional<std::chrono::steady_clock::time_point> timeout_deadline;
    std::shared_ptr<dispatcher_lifetime> timeout_dispatcher_lifetime;
    std::shared_ptr<timer_wait_state> timeout_wait;
    bool timeout_active = false;

    ~cancellation_state();
  };

  inline void invoke_cancellation_callback(
      const std::function<void()>& callback) noexcept {
    if (!callback) {
      return;
    }

#if CARDIO_HAS_EXCEPTIONS
    try {
      callback();
    } catch (...) {
    }
#else
    callback();
#endif
  }

  inline void enqueue_cancellation_callback(
      std::shared_ptr<cancellation_callback_entry> entry) noexcept;
  inline void enqueue_cancellation_callback(
      scheduled_cancellation_callback callback) noexcept;

#if CARDIO_HAS_EXCEPTIONS
#ifdef CARDIO_SILENCE_UNHANDLED_EXCEPTION
  inline void log_ignored_unhandled_exception(std::exception_ptr) noexcept {}
#else
  inline void log_ignored_unhandled_exception(
      std::exception_ptr exception) noexcept {
    try {
      if (exception == nullptr) {
        std::clog << "cardio: ignored non-standard unhandled exception\n";
        return;
      }

      try {
        std::rethrow_exception(exception);
      } catch (const std::exception& caught) {
        std::clog << "cardio: ignored unhandled exception: "
                  << caught.what() << '\n';
      } catch (...) {
        std::clog << "cardio: ignored non-standard unhandled exception\n";
      }
    } catch (...) {
    }
  }
#endif
#endif

  struct dispatcher_group_lifetime {
    ::cardio::internal::mutex mutex;
    dispatcher_group* group;

    inline explicit dispatcher_group_lifetime(
        dispatcher_group* group) noexcept: group(group) {}
  };

  struct dispatcher_lifetime {
    // Timeout cancellation states keep this token after the dispatcher dies.
    // Holding mutex makes a non-null target safe to use, while group_lifetime
    // lets an outstanding activity be finished independently of target.
    ::cardio::internal::mutex mutex;
    dispatcher* target;
    std::shared_ptr<dispatcher_group_lifetime> group_lifetime;

    inline dispatcher_lifetime(
        dispatcher* target,
        std::shared_ptr<dispatcher_group_lifetime> group_lifetime) noexcept
        : target(target), group_lifetime(std::move(group_lifetime)) {}

    bool add_timeout_activity() noexcept;
    void finish_timeout_activity() noexcept;
  };

  struct promise_state_base {
    ::cardio::internal::mutex mutex;
    std::coroutine_handle<> coroutine;
    dispatcher_group* group = nullptr;
    std::shared_ptr<dispatcher_group_lifetime> group_lifetime;
#if CARDIO_HAS_EXCEPTIONS
    std::exception_ptr exception;
#endif
    std::atomic<bool> completed = false;
    std::atomic<bool> has_suspended = false;
    std::atomic<bool> active_promise = false;
    std::coroutine_handle<> continuation;
    dispatcher* continuation_dispatcher = nullptr;
    bool continuation_scheduled = false;
  };

  dispatcher& require_current_dispatcher();
  void activate_current_promise(promise_state_base& state);
  void finish_promise(promise_state_base& state) noexcept;

  struct scheduled_continuation {
    dispatcher* target = nullptr;
    std::coroutine_handle<> continuation;

    inline explicit operator bool() const noexcept {
      return target != nullptr && continuation;
    }
  };

  void enqueue_continuation(scheduled_continuation continuation);
  scheduled_continuation finish_completed_promise(
      promise_state_base& state) noexcept;

  inline scheduled_continuation try_schedule_continuation(
      promise_state_base& state) noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
    if (!state.completed.load(std::memory_order_acquire) ||
        !state.continuation || state.continuation_scheduled) {
      return {};
    }

    if (state.continuation_dispatcher == nullptr) {
      std::terminate();
    }

    state.continuation_scheduled = true;
    return {state.continuation_dispatcher, state.continuation};
  }

  inline scheduled_continuation register_continuation(
      promise_state_base& state,
      dispatcher* target,
      std::coroutine_handle<> continuation) noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
    state.continuation = continuation;
    state.continuation_dispatcher = target;
    state.continuation_scheduled = false;

    if (!state.completed.load(std::memory_order_acquire)) {
      return {};
    }

    if (state.continuation_dispatcher == nullptr) {
      std::terminate();
    }

    state.continuation_scheduled = true;
    return {state.continuation_dispatcher, state.continuation};
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool should_propagate_unhandled_exception(
      promise_state_base& state) noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
    if (state.continuation) {
      return false;
    }

    if (!state.has_suspended.load(std::memory_order_acquire)) {
      state.coroutine = {};
    }

    return true;
  }
#endif

  class promise_type_base {
  private:
    promise_state_base* state_ = nullptr;

  protected:
    inline void set_state(promise_state_base* state) noexcept {
      state_ = state;
    }

  public:
    inline void mark_suspended() noexcept {
      if (state_ != nullptr) {
        state_->has_suspended.store(true, std::memory_order_release);
      }
    }
  };

#if CARDIO_HAS_EXCEPTIONS
  inline std::exception_ptr normalize_rejection_exception(
      std::exception_ptr exception) {
    if (exception) {
      return exception;
    }

    return std::make_exception_ptr(
        std::runtime_error("cardio: rejected promise"));
  }

  inline std::exception_ptr make_canceled_exception() {
    return std::make_exception_ptr(canceled_exception());
  }

  inline bool try_complete_exception(
      promise_state_base& state,
      std::exception_ptr exception,
      scheduled_continuation& scheduled) {
    auto normalized =
        internal::normalize_rejection_exception(std::move(exception));
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
      if (state.completed.load(std::memory_order_acquire)) {
        return false;
      }

      state.exception = std::move(normalized);
      state.completed.store(true, std::memory_order_release);
    }

    scheduled = internal::finish_completed_promise(state);
    return true;
  }

  inline bool try_complete_canceled(
      promise_state_base& state,
      scheduled_continuation& scheduled) {
    return internal::try_complete_exception(
        state, internal::make_canceled_exception(), scheduled);
  }
#endif

#if !CARDIO_HAS_EXCEPTIONS
  inline void log_terminating_failure(const char* message) noexcept {
    std::clog << "cardio: terminating: " << message << '\n';
    std::clog.flush();
  }

  inline void log_terminating_failure(
      const char* message, int value) noexcept {
    std::clog << "cardio: terminating: " << message
              << " (" << value << ")\n";
    std::clog.flush();
  }
#endif

  [[noreturn]] inline void fail_system_error(int error, const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::system_error(error, std::generic_category(), message);
#else
    log_terminating_failure(message, error);
    std::terminate();
#endif
  }

#if CARDIO_HAS_WIN32_HANDLE
  [[noreturn]] inline void fail_win32_error(DWORD error, const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::system_error(
        static_cast<int>(error), std::system_category(), message);
#else
    log_terminating_failure(message, static_cast<int>(error));
    std::terminate();
#endif
  }

  using win32_cancel_io_ex_function = BOOL (WINAPI*)(HANDLE, LPOVERLAPPED);

  inline win32_cancel_io_ex_function find_win32_cancel_io_ex() noexcept {
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
    return &::CancelIoEx;
#else
    // Resolve by name: an XP-targeted executable must not import CancelIoEx.
    const auto address = ::GetProcAddress(
        ::GetModuleHandleW(L"kernel32.dll"), "CancelIoEx");
    auto function = win32_cancel_io_ex_function{};
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
#endif
  }

  struct win32_legacy_io_registry {
    mutex guard;
    std::vector<HANDLE> handles;
  };

#if CARDIO_SHARED_LIB
  // A host and its plugins must reserve handles in the same registry.
  CARDIO_API win32_legacy_io_registry& legacy_io_registry();
#else
  inline win32_legacy_io_registry& legacy_io_registry() {
    static win32_legacy_io_registry registry;
    return registry;
  }
#endif

  // CancelIo affects every operation for an issuing thread and HANDLE. Reserve
  // the handle until native completion, and serialize retirement with cancel
  // callbacks that may already have been queued when a registration is reset.
  class win32_io_lease {
  private:
    mutex guard_;
    HANDLE handle_ = nullptr;
    win32_cancel_io_ex_function cancel_io_ex_ = find_win32_cancel_io_ex();

  public:
    inline bool is_legacy() const noexcept { return cancel_io_ex_ == nullptr; }

    inline DWORD acquire(HANDLE handle) {
      if (is_legacy()) {
        auto& registry = legacy_io_registry();
        auto lock = std::lock_guard<mutex>(registry.guard);
        for (auto active : registry.handles) {
          if (active == handle) {
            return ERROR_BUSY;
          }
        }
        registry.handles.push_back(handle);
      }
      handle_ = handle;
      return ERROR_SUCCESS;
    }

    inline void release() noexcept {
      auto lock = std::lock_guard<mutex>(guard_);
      if (handle_ == nullptr) {
        return;
      }
      if (is_legacy()) {
        auto& registry = legacy_io_registry();
        auto registry_lock = std::lock_guard<mutex>(registry.guard);
        for (auto current = registry.handles.begin();
             current != registry.handles.end(); ++current) {
          if (*current == handle_) {
            registry.handles.erase(current);
            break;
          }
        }
      }
      handle_ = nullptr;
    }

    inline void cancel(OVERLAPPED& overlapped, DWORD owner_thread) noexcept {
      auto lock = std::lock_guard<mutex>(guard_);
      if (handle_ == nullptr) {
        return;
      }
      if (cancel_io_ex_ != nullptr) {
        (void)cancel_io_ex_(handle_, &overlapped);
      } else {
        if (::GetCurrentThreadId() != owner_thread) {
          // Destruction of an unfinished legacy helper is also an owner-thread
          // operation. Do not free its OVERLAPPED or cancel unrelated I/O.
          std::terminate();
        }
        (void)::CancelIo(handle_);
      }
    }

    inline ~win32_io_lease() { release(); }
  };
#endif

  [[noreturn]] inline void fail_invalid_argument(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::invalid_argument(message);
#else
    log_terminating_failure(message);
    std::terminate();
#endif
  }

  [[noreturn]] inline void fail_logic_error(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::logic_error(message);
#else
    log_terminating_failure(message);
    std::terminate();
#endif
  }

  [[noreturn]] inline void fail_runtime_error(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw std::runtime_error(message);
#else
    log_terminating_failure(message);
    std::terminate();
#endif
  }

  inline dispatcher& require_current_dispatcher() {
    auto* current = internal::runtime_state().current_dispatcher;
    if (current == nullptr) {
      internal::fail_runtime_error("cardio: no active dispatcher");
    }
    return *current;
  }

  struct timer_wait_state {
    std::chrono::steady_clock::time_point deadline;
    std::coroutine_handle<> continuation;
    std::function<void()> callback;
    dispatcher* owner = nullptr;
    std::size_t heap_index = std::numeric_limits<std::size_t>::max();
    bool registered = false;
#if CARDIO_HAS_EXCEPTIONS
    bool canceled = false;
    cancellation_registration cancellation_registration_;
#endif
  };

#if CARDIO_HAS_POSIX_FD
  struct fd_wait_state {
    int fd = -1;
    fd_event interests = fd_event::none;
    fd_event result = fd_event::none;
    std::coroutine_handle<> continuation;
    dispatcher* owner = nullptr;
    bool registered = false;
#if CARDIO_HAS_EXCEPTIONS
    bool canceled = false;
    cancellation_registration cancellation_registration_;
#endif
  };

  inline bool has_fd_io_interest(fd_event interests) noexcept {
    return (interests & (fd_event::read | fd_event::write)) != fd_event::none;
  }

  inline short to_poll_events(fd_event interests) noexcept {
    auto events = short{};
    if ((interests & fd_event::read) != fd_event::none) {
      events |= POLLIN;
    }
    if ((interests & fd_event::write) != fd_event::none) {
      events |= POLLOUT;
    }
    return events;
  }

  inline fd_event from_poll_events(short revents) noexcept {
    auto events = fd_event::none;
    if ((revents & (POLLIN | POLLPRI)) != 0) {
      events |= fd_event::read;
    }
    if ((revents & POLLOUT) != 0) {
      events |= fd_event::write;
    }
    if ((revents & (POLLERR | POLLNVAL)) != 0) {
      events |= fd_event::error;
    }
    if ((revents & POLLHUP) != 0) {
      events |= fd_event::hangup;
    }
    return events;
  }

  class posix_fd_wait_backend {
  private:
    std::deque<std::shared_ptr<fd_wait_state>> waits_;
    int wakeup_read_fd_ = -1;
    int wakeup_write_fd_ = -1;

    static inline void close_fd(int& fd) noexcept {
      if (fd < 0) {
        return;
      }

      (void)::close(fd);
      fd = -1;
    }

    static inline void set_fd_flags(int fd) {
      auto flags = ::fcntl(fd, F_GETFL, 0);
      if (flags == -1) {
        fail_system_error(errno, "cardio: fcntl F_GETFL failed");
      }

      if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fail_system_error(errno, "cardio: fcntl F_SETFL failed");
      }

      flags = ::fcntl(fd, F_GETFD, 0);
      if (flags == -1) {
        fail_system_error(errno, "cardio: fcntl F_GETFD failed");
      }

      if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        fail_system_error(errno, "cardio: fcntl F_SETFD failed");
      }
    }

    inline void drain_wakeup() noexcept {
      char buffer[64]{};
      while (true) {
        const auto result = ::read(wakeup_read_fd_, buffer, sizeof(buffer));
        if (result > 0) {
          continue;
        }
        if (result == 0) {
          return;
        }
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return;
        }
        return;
      }
    }

  public:
    struct snapshot {
      std::vector<pollfd> poll_fds;
      std::vector<std::shared_ptr<fd_wait_state>> waits;

      inline bool empty() const noexcept {
        return poll_fds.empty();
      }
    };

    inline posix_fd_wait_backend() {
      int fds[2]{-1, -1};
      if (::pipe(fds) == -1) {
        fail_system_error(errno, "cardio: pipe failed");
      }

#if CARDIO_HAS_EXCEPTIONS
      try {
        set_fd_flags(fds[0]);
        set_fd_flags(fds[1]);
        wakeup_read_fd_ = fds[0];
        wakeup_write_fd_ = fds[1];
        fds[0] = -1;
        fds[1] = -1;
      } catch (...) {
        close_fd(fds[0]);
        close_fd(fds[1]);
        throw;
      }
#else
      set_fd_flags(fds[0]);
      set_fd_flags(fds[1]);
      wakeup_read_fd_ = fds[0];
      wakeup_write_fd_ = fds[1];
      fds[0] = -1;
      fds[1] = -1;
#endif
    }

    inline ~posix_fd_wait_backend() {
      close_fd(wakeup_read_fd_);
      close_fd(wakeup_write_fd_);
    }

    posix_fd_wait_backend(const posix_fd_wait_backend&) = delete;
    posix_fd_wait_backend& operator=(const posix_fd_wait_backend&) = delete;

    inline bool empty() const noexcept {
      return waits_.empty();
    }

    inline void notify() noexcept {
      const auto value = char{1};
      while (true) {
        const auto result = ::write(wakeup_write_fd_, &value, 1);
        if (result == 1) {
          return;
        }
        if (result == -1 && errno == EINTR) {
          continue;
        }
        return;
      }
    }

    inline void register_wait(
        dispatcher* owner,
        std::shared_ptr<fd_wait_state> wait,
        std::coroutine_handle<> continuation) {
      wait->continuation = continuation;
      wait->owner = owner;
      wait->registered = true;
      waits_.push_back(std::move(wait));
    }

    inline bool unregister_wait(
        dispatcher* owner,
        const std::shared_ptr<fd_wait_state>& wait) noexcept {
      if (!wait || wait->owner != owner || !wait->registered) {
        return false;
      }

      wait->owner = nullptr;
      wait->registered = false;
      for (auto iterator = waits_.begin(); iterator != waits_.end(); ++iterator) {
        if (iterator->get() == wait.get()) {
          waits_.erase(iterator);
          break;
        }
      }
      return true;
    }

    inline void clear(dispatcher* owner) noexcept {
      for (auto& wait : waits_) {
        if (wait && wait->owner == owner) {
          wait->owner = nullptr;
          wait->registered = false;
        }
      }
      waits_.clear();
    }

    inline snapshot make_snapshot(
        dispatcher* owner,
        bool include_wakeup_when_empty = false) {
      auto result = snapshot{};
      result.poll_fds.reserve(waits_.size() + 1);
      result.waits.reserve(waits_.size());

      result.poll_fds.push_back(pollfd{wakeup_read_fd_, POLLIN, 0});

      for (auto iterator = waits_.begin(); iterator != waits_.end();) {
        auto& wait = *iterator;
        if (!wait || !wait->registered || wait->owner != owner) {
          iterator = waits_.erase(iterator);
          continue;
        }

        result.poll_fds.push_back(
            pollfd{wait->fd, internal::to_poll_events(wait->interests), 0});
        result.waits.push_back(wait);
        ++iterator;
      }

      if (result.waits.empty() && !include_wakeup_when_empty) {
        result.poll_fds.clear();
      }
      return result;
    }

    static inline void wait(snapshot& snapshot) {
      if (snapshot.empty()) {
        return;
      }

      auto poll_result = int{};
      do {
        poll_result = ::poll(
            snapshot.poll_fds.data(),
            static_cast<nfds_t>(snapshot.poll_fds.size()),
            -1);
      } while (poll_result == -1 && errno == EINTR);

      if (poll_result == -1) {
        fail_system_error(errno, "cardio: poll failed");
      }
    }

    inline std::vector<std::coroutine_handle<>> collect_ready(
        dispatcher* owner,
        const snapshot& snapshot) {
      auto continuations = std::vector<std::coroutine_handle<>>{};

      if (!snapshot.poll_fds.empty() && snapshot.poll_fds[0].revents != 0) {
        drain_wakeup();
      }

      for (auto index = std::size_t{1}; index < snapshot.poll_fds.size(); ++index) {
        if (snapshot.poll_fds[index].revents == 0) {
          continue;
        }

        auto events = internal::from_poll_events(snapshot.poll_fds[index].revents);
        if (events == fd_event::none) {
          continue;
        }

        auto& wait = snapshot.waits[index - 1];
        if (!wait || !wait->registered || wait->owner != owner) {
          continue;
        }

        wait->result = events;
        wait->owner = nullptr;
        wait->registered = false;
        if (wait->continuation) {
          continuations.push_back(wait->continuation);
        }
      }

      for (auto iterator = waits_.begin(); iterator != waits_.end();) {
        if (!*iterator || !(*iterator)->registered || (*iterator)->owner != owner) {
          iterator = waits_.erase(iterator);
        } else {
          ++iterator;
        }
      }

      return continuations;
    }
  };

  using fd_wait_backend = posix_fd_wait_backend;
#endif  // CARDIO_HAS_POSIX_FD

#if CARDIO_HAS_WIN32_HANDLE
  struct win32_wait_state {
    HANDLE wait_handle = nullptr;
    HANDLE operation_handle = nullptr;
    OVERLAPPED* overlapped = nullptr;
    win32_handle_event result = win32_handle_event::none;
    DWORD bytes_transferred = 0;
    DWORD error = ERROR_SUCCESS;
    std::coroutine_handle<> continuation;
    dispatcher* owner = nullptr;
    bool registered = false;
    bool failed = false;
#if CARDIO_HAS_EXCEPTIONS
    bool canceled = false;
    cancellation_registration cancellation_registration_;
#endif
  };

  inline bool is_invalid_win32_handle(HANDLE handle) noexcept {
    return handle == nullptr || handle == INVALID_HANDLE_VALUE;
  }

  class win32_handle_wait_backend {
  private:
    std::deque<std::shared_ptr<win32_wait_state>> waits_;
    HANDLE wake_event_ = nullptr;

    static inline constexpr std::size_t max_snapshot_handles() noexcept {
      return static_cast<std::size_t>(MAXIMUM_WAIT_OBJECTS - 1);
    }

    static inline constexpr std::size_t max_user_waits() noexcept {
      return max_snapshot_handles() - 1;
    }

    static inline void close_handle(HANDLE& handle) noexcept {
      if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
      }

      (void)::CloseHandle(handle);
      handle = nullptr;
    }

    inline void reset_wakeup() noexcept {
      if (wake_event_ != nullptr) {
        (void)::ResetEvent(wake_event_);
      }
    }

  public:
    struct snapshot {
      std::vector<HANDLE> handles;
      std::vector<std::shared_ptr<win32_wait_state>> waits;
      std::size_t ready_index = std::numeric_limits<std::size_t>::max();
      bool ready_abandoned = false;
      bool message_ready = false;
      bool timed_out = false;

      inline bool empty() const noexcept {
        return handles.empty();
      }
    };

    inline win32_handle_wait_backend() {
      wake_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (wake_event_ == nullptr) {
        fail_win32_error(
            ::GetLastError(), "cardio: CreateEventW failed");
      }
    }

    inline ~win32_handle_wait_backend() {
      close_handle(wake_event_);
    }

    win32_handle_wait_backend(const win32_handle_wait_backend&) = delete;
    win32_handle_wait_backend& operator=(const win32_handle_wait_backend&) =
        delete;

    inline bool empty() const noexcept {
      return waits_.empty();
    }

    inline void notify() noexcept {
      if (wake_event_ != nullptr) {
        (void)::SetEvent(wake_event_);
      }
    }

    inline void register_wait(
        dispatcher* owner,
        std::shared_ptr<win32_wait_state> wait,
        std::coroutine_handle<> continuation) {
      if (waits_.size() >= max_user_waits()) {
        fail_runtime_error("cardio: too many Win32 handle waits");
      }

      wait->continuation = continuation;
      wait->owner = owner;
      wait->registered = true;
      waits_.push_back(std::move(wait));
    }

    inline bool unregister_wait(
        dispatcher* owner,
        const std::shared_ptr<win32_wait_state>& wait) noexcept {
      if (!wait || wait->owner != owner || !wait->registered) {
        return false;
      }

      wait->owner = nullptr;
      wait->registered = false;
      for (auto iterator = waits_.begin(); iterator != waits_.end(); ++iterator) {
        if (iterator->get() == wait.get()) {
          waits_.erase(iterator);
          break;
        }
      }
      return true;
    }

    inline void clear(dispatcher* owner) noexcept {
      for (auto& wait : waits_) {
        if (wait && wait->owner == owner) {
          wait->owner = nullptr;
          wait->registered = false;
        }
      }
      waits_.clear();
    }

    inline snapshot make_snapshot(
        dispatcher* owner,
        bool include_wakeup_when_empty = false) {
      auto result = snapshot{};
      result.handles.reserve(waits_.size() + 1);
      result.waits.reserve(waits_.size());

      result.handles.push_back(wake_event_);

      for (auto iterator = waits_.begin(); iterator != waits_.end();) {
        auto& wait = *iterator;
        if (!wait || !wait->registered || wait->owner != owner) {
          iterator = waits_.erase(iterator);
          continue;
        }

        if (result.handles.size() >= max_snapshot_handles()) {
          fail_runtime_error("cardio: too many Win32 handle waits");
        }

        result.handles.push_back(wait->wait_handle);
        result.waits.push_back(wait);
        ++iterator;
      }

      if (result.waits.empty() && !include_wakeup_when_empty) {
        result.handles.clear();
      }
      return result;
    }

    static inline void wait(snapshot& snapshot, int timeout_milliseconds = -1) {
      if (snapshot.empty()) {
        return;
      }

      snapshot.ready_index = std::numeric_limits<std::size_t>::max();
      snapshot.ready_abandoned = false;
      snapshot.message_ready = false;
      snapshot.timed_out = false;

      const auto timeout = timeout_milliseconds < 0
          ? INFINITE
          : static_cast<DWORD>(timeout_milliseconds);
      const auto count = static_cast<DWORD>(snapshot.handles.size());
      const auto result = ::MsgWaitForMultipleObjectsEx(
          count,
          snapshot.handles.data(),
          timeout,
          QS_ALLINPUT,
          MWMO_INPUTAVAILABLE);

      if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) {
        snapshot.ready_index =
            static_cast<std::size_t>(result - WAIT_OBJECT_0);
        return;
      }
      if (result == WAIT_OBJECT_0 + count) {
        snapshot.message_ready = true;
        return;
      }
      if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) {
        snapshot.ready_index =
            static_cast<std::size_t>(result - WAIT_ABANDONED_0);
        snapshot.ready_abandoned = true;
        return;
      }
      if (result == WAIT_TIMEOUT) {
        snapshot.timed_out = true;
        return;
      }
      if (result == WAIT_FAILED) {
        fail_win32_error(
            ::GetLastError(), "cardio: MsgWaitForMultipleObjectsEx failed");
      }

      fail_runtime_error("cardio: unexpected Win32 wait result");
    }

    inline std::vector<std::coroutine_handle<>> collect_ready(
        dispatcher* owner,
        const snapshot& snapshot) {
      auto continuations = std::vector<std::coroutine_handle<>>{};

      if (snapshot.ready_index == 0) {
        reset_wakeup();
        return continuations;
      }

      if (snapshot.ready_index == std::numeric_limits<std::size_t>::max()) {
        return continuations;
      }

      const auto wait_index = snapshot.ready_index - 1;
      if (wait_index >= snapshot.waits.size()) {
        return continuations;
      }

      auto& wait = snapshot.waits[wait_index];
      if (!wait || !wait->registered || wait->owner != owner) {
        return continuations;
      }

      wait->result = snapshot.ready_abandoned
          ? win32_handle_event::abandoned
          : win32_handle_event::signaled;

      if (wait->overlapped != nullptr) {
        auto transferred = DWORD{};
        if (::GetOverlappedResult(
                wait->operation_handle, wait->overlapped, &transferred,
                FALSE) == 0) {
          wait->failed = true;
          wait->error = ::GetLastError();
        } else {
          wait->bytes_transferred = transferred;
          wait->error = ERROR_SUCCESS;
          wait->failed = false;
        }
      }

      wait->owner = nullptr;
      wait->registered = false;
      if (wait->continuation) {
        continuations.push_back(wait->continuation);
      }

      for (auto iterator = waits_.begin(); iterator != waits_.end();) {
        if (!*iterator || !(*iterator)->registered || (*iterator)->owner != owner) {
          iterator = waits_.erase(iterator);
        } else {
          ++iterator;
        }
      }

      return continuations;
    }
  };
#endif  // CARDIO_HAS_WIN32_HANDLE

#if CARDIO_WITH_LINUX_IO_URING
#if CARDIO_HAS_EXCEPTIONS
  inline std::system_error make_liburing_error(
      int result, const char* message) {
    return std::system_error(-result, std::generic_category(), message);
  }
#endif

  [[noreturn]] inline void fail_liburing_error(
      int result, const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    throw make_liburing_error(result, message);
#else
    log_terminating_failure(message, result);
    std::terminate();
#endif
  }

  inline void fail_if_liburing_failed(int result, const char* message) {
    if (result < 0) {
      fail_liburing_error(result, message);
    }
  }

  inline bool is_valid_io_uring_buffer_size(std::size_t size) noexcept {
    return size <= static_cast<std::size_t>(
        std::numeric_limits<unsigned>::max());
  }

  inline bool is_valid_io_uring_dfd(int dfd) noexcept {
    return dfd >= 0 || dfd == AT_FDCWD;
  }

  inline void fail_if_io_uring_completion_failed(
      io_uring_completion completion,
      const char* message) {
    if (completion.result < 0) {
      fail_system_error(-completion.result, message);
    }
  }

  inline std::size_t io_uring_size_result(
      io_uring_completion completion) {
    fail_if_io_uring_completion_failed(
        completion, "cardio: io_uring operation failed");
    return static_cast<std::size_t>(completion.result);
  }

  inline int io_uring_int_result(io_uring_completion completion) {
    fail_if_io_uring_completion_failed(
        completion, "cardio: io_uring operation failed");
    return completion.result;
  }

  inline void io_uring_void_result(io_uring_completion completion) {
    fail_if_io_uring_completion_failed(
        completion, "cardio: io_uring operation failed");
  }

  inline void close_fd_noexcept(int& fd) noexcept {
    if (fd < 0) {
      return;
    }

    (void)::close(fd);
    fd = -1;
  }

  struct io_uring_operation_state {
    std::function<scheduled_continuation(io_uring_completion)> complete;
    std::shared_ptr<io_uring_ring_state> ring;
    dispatcher* owner = nullptr;
#if CARDIO_HAS_EXCEPTIONS
    cancellation_registration cancellation_registration_;
#endif
  };

  struct io_uring_ring_state {
    ::io_uring ring{};
    int event_fd = -1;
    dispatcher* owner = nullptr;
    bool initialized = false;
    std::deque<std::shared_ptr<io_uring_operation_state>> operations;

    inline explicit io_uring_ring_state(unsigned entries) {
      auto result = ::io_uring_queue_init(entries, &ring, 0);
      if (result < 0) {
        fail_liburing_error(result, "cardio: io_uring_queue_init failed");
      }
      initialized = true;

#if CARDIO_HAS_EXCEPTIONS
      try {
        event_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (event_fd == -1) {
          fail_system_error(errno, "cardio: eventfd failed");
        }

        result = ::io_uring_register_eventfd(&ring, event_fd);
        if (result < 0) {
          fail_liburing_error(
              result, "cardio: io_uring_register_eventfd failed");
        }
      } catch (...) {
        close_fd_noexcept(event_fd);
        ::io_uring_queue_exit(&ring);
        initialized = false;
        throw;
      }
#else
      event_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
      if (event_fd == -1) {
        fail_system_error(errno, "cardio: eventfd failed");
      }

      result = ::io_uring_register_eventfd(&ring, event_fd);
      if (result < 0) {
        fail_liburing_error(
            result, "cardio: io_uring_register_eventfd failed");
      }
#endif
    }

    inline ~io_uring_ring_state() {
      if (initialized) {
        (void)::io_uring_unregister_eventfd(&ring);
        ::io_uring_queue_exit(&ring);
      }
      close_fd_noexcept(event_fd);
    }

    io_uring_ring_state(const io_uring_ring_state&) = delete;
    io_uring_ring_state& operator=(const io_uring_ring_state&) = delete;

    inline void drain_event_fd() noexcept {
      auto value = std::uint64_t{};
      while (true) {
        const auto result = ::read(event_fd, &value, sizeof(value));
        if (result == static_cast<ssize_t>(sizeof(value))) {
          continue;
        }
        if (result == -1 && errno == EINTR) {
          continue;
        }
        if (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          return;
        }
        return;
      }
    }
  };

  class linux_io_uring_wait_backend {
  private:
    std::deque<std::shared_ptr<io_uring_ring_state>> rings_;

    static inline bool contains_ring(
        const std::deque<std::shared_ptr<io_uring_ring_state>>& rings,
        const std::shared_ptr<io_uring_ring_state>& ring) noexcept {
      for (const auto& current : rings) {
        if (current.get() == ring.get()) {
          return true;
        }
      }
      return false;
    }

    static inline std::shared_ptr<io_uring_operation_state> take_operation(
        const std::shared_ptr<io_uring_ring_state>& ring,
        io_uring_operation_state* operation) noexcept {
      for (auto iterator = ring->operations.begin();
           iterator != ring->operations.end(); ++iterator) {
        if (iterator->get() == operation) {
          auto result = *iterator;
          ring->operations.erase(iterator);
          result->ring.reset();
          result->owner = nullptr;
          if (ring->operations.empty()) {
            ring->owner = nullptr;
          }
          return result;
        }
      }

      return {};
    }

    static inline bool contains_operation(
        const std::shared_ptr<io_uring_ring_state>& ring,
        const io_uring_operation_state* operation) noexcept {
      for (const auto& current : ring->operations) {
        if (current.get() == operation) {
          return true;
        }
      }
      return false;
    }

    inline void remove_empty_rings() noexcept {
      for (auto iterator = rings_.begin(); iterator != rings_.end();) {
        if (!*iterator || (*iterator)->operations.empty()) {
          if (*iterator) {
            (*iterator)->owner = nullptr;
          }
          iterator = rings_.erase(iterator);
        } else {
          ++iterator;
        }
      }
    }

  public:
    struct snapshot {
      std::vector<pollfd> poll_fds;
      std::vector<std::shared_ptr<io_uring_ring_state>> rings;

      inline bool empty() const noexcept {
        return poll_fds.empty();
      }
    };

    inline bool empty() const noexcept {
      return rings_.empty();
    }

    template <typename Prepare>
    inline void submit_operation(
        dispatcher* owner,
        std::shared_ptr<io_uring_ring_state> ring,
        std::shared_ptr<io_uring_operation_state> operation,
        Prepare prepare) {
      if (ring->owner != nullptr && ring->owner != owner) {
        fail_logic_error(
            "cardio: io_uring is already attached to another dispatcher");
      }

      auto* sqe = ::io_uring_get_sqe(&ring->ring);
      if (sqe == nullptr) {
        fail_if_liburing_failed(
            ::io_uring_submit(&ring->ring), "cardio: io_uring_submit failed");
        sqe = ::io_uring_get_sqe(&ring->ring);
      }
      if (sqe == nullptr) {
        fail_system_error(EBUSY, "cardio: io_uring SQ is full");
      }

      prepare(sqe);
      ::io_uring_sqe_set_data(sqe, operation.get());

      ring->owner = owner;
      operation->owner = owner;
      operation->ring = ring;
      ring->operations.push_back(operation);
      if (!contains_ring(rings_, ring)) {
        rings_.push_back(ring);
      }

      const auto result = ::io_uring_submit(&ring->ring);
      if (result < 0) {
        (void)take_operation(ring, operation.get());
        remove_empty_rings();
        fail_liburing_error(result, "cardio: io_uring_submit failed");
      }
    }

#if CARDIO_HAS_EXCEPTIONS
    inline bool submit_cancel_operation(
        dispatcher* owner,
        const std::shared_ptr<io_uring_operation_state>& operation) {
      if (!operation || operation->owner != owner || !operation->ring) {
        return false;
      }

      auto ring = operation->ring;
      if (ring->owner != owner || !contains_operation(ring, operation.get())) {
        return false;
      }

      auto cancel_operation =
          std::make_shared<internal::io_uring_operation_state>();
      cancel_operation->complete =
          [](io_uring_completion) noexcept -> scheduled_continuation {
        return {};
      };

      auto* sqe = ::io_uring_get_sqe(&ring->ring);
      if (sqe == nullptr) {
        const auto submit_result = ::io_uring_submit(&ring->ring);
        if (submit_result < 0) {
          return false;
        }
        sqe = ::io_uring_get_sqe(&ring->ring);
      }
      if (sqe == nullptr) {
        return false;
      }

      ::io_uring_prep_cancel(sqe, operation.get(), 0);
      ::io_uring_sqe_set_data(sqe, cancel_operation.get());

      cancel_operation->owner = owner;
      cancel_operation->ring = ring;
      ring->operations.push_back(cancel_operation);
      if (!contains_ring(rings_, ring)) {
        rings_.push_back(ring);
      }

      const auto result = ::io_uring_submit(&ring->ring);
      if (result < 0) {
        (void)take_operation(ring, cancel_operation.get());
        remove_empty_rings();
        return false;
      }

      return true;
    }
#endif

    inline void clear(dispatcher* owner) noexcept {
      for (auto& ring : rings_) {
        if (ring && ring->owner == owner) {
          ring->owner = nullptr;
        }
      }
      rings_.clear();
    }

    inline snapshot make_snapshot(dispatcher* owner) {
      auto result = snapshot{};
      result.poll_fds.reserve(rings_.size());
      result.rings.reserve(rings_.size());

      for (auto iterator = rings_.begin(); iterator != rings_.end();) {
        auto& ring = *iterator;
        if (!ring || ring->owner != owner || ring->operations.empty()) {
          if (ring && ring->operations.empty()) {
            ring->owner = nullptr;
          }
          iterator = rings_.erase(iterator);
          continue;
        }

        result.poll_fds.push_back(pollfd{ring->event_fd, POLLIN, 0});
        result.rings.push_back(ring);
        ++iterator;
      }

      return result;
    }

    inline std::vector<scheduled_continuation> collect_ready(
        dispatcher* owner,
        const snapshot& snapshot) {
      auto continuations = std::vector<scheduled_continuation>{};

      for (auto index = std::size_t{0}; index < snapshot.poll_fds.size();
           ++index) {
        if (snapshot.poll_fds[index].revents == 0) {
          continue;
        }

        auto& ring = snapshot.rings[index];
        if (!ring || ring->owner != owner) {
          continue;
        }

        ring->drain_event_fd();

        while (true) {
          auto* cqe = static_cast<::io_uring_cqe*>(nullptr);
          const auto result = ::io_uring_peek_cqe(&ring->ring, &cqe);
          if (result == -EAGAIN) {
            break;
          }
          fail_if_liburing_failed(
              result, "cardio: io_uring_peek_cqe failed");
          if (cqe == nullptr) {
            break;
          }

          auto* operation = static_cast<io_uring_operation_state*>(
              ::io_uring_cqe_get_data(cqe));
          auto operation_owner = take_operation(ring, operation);
          if (operation_owner && operation_owner->complete) {
#if CARDIO_HAS_EXCEPTIONS
            operation_owner->cancellation_registration_.reset();
#endif
            auto scheduled = operation_owner->complete(
                io_uring_completion{cqe->res, cqe->flags});
            if (scheduled) {
              continuations.push_back(scheduled);
            }
          }
          ::io_uring_cqe_seen(&ring->ring, cqe);
        }
      }

      remove_empty_rings();
      return continuations;
    }
  };
#endif

}  // namespace internal

//-----------------------------------------------------------------------------------------------

inline void cancellation_registration::reset() noexcept {
  auto state = state_;
  if (state && entry_) {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (entry_->registered && entry_->owner == state.get()) {
      if (entry_->previous != nullptr) {
        entry_->previous->next = entry_->next;
      } else {
        state->callbacks = entry_->next;
      }
      if (entry_->next != nullptr) {
        entry_->next->previous = entry_->previous;
      }
      entry_->owner = nullptr;
      entry_->previous = nullptr;
      entry_->next = nullptr;
      entry_->registered = false;
    }
  }

  state_.reset();
  entry_.reset();
  internal::disarm_timeout_cancellation(state);
}

inline bool cancellation::try_register_callback(
    std::function<void()> callback,
    cancellation_registration& registration) const {
  registration.reset();
  if (!state_ || !callback) {
    return true;
  }

  auto& target = internal::require_current_dispatcher();
  auto entry = std::make_shared<internal::cancellation_callback_entry>();
  entry->callback = std::move(callback);
  entry->target = &target;

  auto already_requested = false;
  auto registered = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    if (state_->cancellation_requested) {
      already_requested = true;
    } else {
      registration = cancellation_registration(state_, entry);
      entry->owner = state_.get();
      entry->previous = nullptr;
      entry->next = state_->callbacks;
      if (state_->callbacks != nullptr) {
        state_->callbacks->previous = entry.get();
      }
      entry->registered = true;
      state_->callbacks = entry.get();
      registered = true;
    }
  }

  if (already_requested) {
    internal::enqueue_cancellation_callback(std::move(entry));
    return false;
  }

  if (registered) {
    internal::arm_timeout_cancellation(state_);
  }

  return true;
}

inline bool cancellation::is_cancellation_requested() const noexcept {
  if (!state_) {
    return false;
  }

  auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
  return state_->cancellation_requested ||
      (state_->timeout_deadline &&
       *state_->timeout_deadline <= std::chrono::steady_clock::now());
}

#if CARDIO_HAS_EXCEPTIONS
inline void cancellation::throw_if_cancellation_requested() const {
  if (is_cancellation_requested()) {
    throw canceled_exception();
  }
}
#endif

inline cancellation_registration cancellation::on_cancellation_requested(
    std::function<void()> callback) const {
  auto registration = cancellation_registration{};
  (void)try_register_callback(std::move(callback), registration);
  return registration;
}

inline cancellation_source::cancellation_source()
    : state_(std::make_shared<internal::cancellation_state>()) {}

inline cancellation cancellation_source::get_cancellation() const noexcept {
  return cancellation(state_);
}

inline bool cancellation_source::cancel() noexcept {
  return internal::request_cancellation(state_);
}

namespace internal {
  inline void retain_cancellation_registration(
      cancellation_source& source,
      cancellation_registration registration) {
    if (!source.state_) {
      return;
    }

    auto lock = std::lock_guard<::cardio::internal::mutex>(source.state_->mutex);
    if (source.state_->cancellation_requested) {
      return;
    }

    source.state_->retained_registrations.push_back(std::move(registration));
  }
}  // namespace internal

//-----------------------------------------------------------------

/**
 * Group exit condition.
 */
enum class exit_condition {
  /**
   * Exit when the whole group becomes empty.
   */
  exit_by_empty,

  /**
   * Exit only when shutdown() is called.
   */
  exit_by_manual,
};

/**
 * Controls the lifetime policy shared by one or more dispatchers.
 *
 * @remarks
 * Dispatchers in the same group share the same exit condition. A group is empty
 * when no active promise, queued work, or running work remains in the group.
 * exit_condition::exit_by_manual keeps parked dispatchers alive while the group
 * is empty until shutdown() is called.
 */
class dispatcher_group {
private:
  friend class dispatcher;
  friend struct internal::dispatcher_lifetime;
  friend class dispatcher_host;
#if defined(__ANDROID__)
  friend class internal::dispatcher_host_android_base;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  friend class dispatcher_host_win32_auto;
#endif
#if CARDIO_WITH_GLIB
  friend class dispatcher_host_glib;
  friend class dispatcher_host_glib_auto;
#endif
  friend void internal::activate_current_promise(internal::promise_state_base& state);
  friend void internal::finish_promise(internal::promise_state_base& state) noexcept;

  ::cardio::internal::mutex mutex_;
  std::vector<dispatcher*> dispatchers_;
  std::atomic<std::size_t> active_promises_ = 0;
  std::atomic<std::size_t> queued_continuations_ = 0;
  const exit_condition condition_;
  std::atomic<std::size_t> active_continuations_ = 0;
  std::atomic<bool> shutdown_requested_ = false;
  std::shared_ptr<internal::dispatcher_group_lifetime> lifetime_;

  inline bool empty() const noexcept {
    return active_promises_.load(std::memory_order_acquire) == 0 &&
           queued_continuations_.load(std::memory_order_acquire) == 0 &&
           active_continuations_.load(std::memory_order_acquire) == 0;
  }

  inline bool work_drained() const noexcept {
    return queued_continuations_.load(std::memory_order_acquire) == 0 &&
           active_continuations_.load(std::memory_order_acquire) == 0;
  }

  inline bool is_shutdown_requested() const noexcept {
    return shutdown_requested_.load(std::memory_order_acquire);
  }

  inline bool should_exit_immediately(shutdown_mode mode) const noexcept {
    return is_shutdown_requested() && mode == shutdown_mode::unsafe_immediate;
  }

  inline bool should_exit(shutdown_mode mode) const noexcept {
    if (is_shutdown_requested()) {
      return mode == shutdown_mode::unsafe_immediate || work_drained();
    }

    return condition_ == exit_condition::exit_by_empty && empty();
  }

  inline bool should_notify_exit() const noexcept {
    return (is_shutdown_requested() && work_drained()) ||
           (condition_ == exit_condition::exit_by_empty && empty());
  }

  void notify_all() noexcept;
  void notify_if_empty() noexcept;
  void register_dispatcher(dispatcher* target);
  void unregister_dispatcher(dispatcher* target) noexcept;

  inline void add_active_promise() noexcept {
    active_promises_.fetch_add(1, std::memory_order_acq_rel);
  }

  inline void finish_active_promise() noexcept {
    if (active_promises_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      notify_if_empty();
    }
  }

  inline void add_queued_continuation() noexcept {
    queued_continuations_.fetch_add(1, std::memory_order_acq_rel);
  }

  inline void start_continuation() noexcept {
    queued_continuations_.fetch_sub(1, std::memory_order_acq_rel);
    active_continuations_.fetch_add(1, std::memory_order_acq_rel);
  }

  inline void finish_continuation() noexcept {
    if (active_continuations_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      notify_if_empty();
    }
  }

public:
  /**
   * Creates a dispatcher group.
   *
   * @param condition Group exit condition.
   */
  inline explicit dispatcher_group(
      exit_condition condition = exit_condition::exit_by_empty)
      : condition_(condition),
        lifetime_(
            std::make_shared<internal::dispatcher_group_lifetime>(this)) {
  }

  inline virtual ~dispatcher_group() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(lifetime_->mutex);
    lifetime_->group = nullptr;
  }

  /**
   * Requests all parked dispatchers in the group to exit.
   *
   * @remarks
   * This wakes threads currently running a dispatcher host park function.
   * park() controls whether shutdown drains queued or ready work.
   */
  inline void shutdown() noexcept {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      shutdown_requested_.store(true, std::memory_order_release);
    }
    notify_all();
  }

  dispatcher_group(const dispatcher_group&) = delete;
  dispatcher_group& operator=(const dispatcher_group&) = delete;
};

namespace internal {
  inline bool dispatcher_lifetime::add_timeout_activity() noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(group_lifetime->mutex);
    if (group_lifetime->group == nullptr) {
      return false;
    }

    group_lifetime->group->add_active_promise();
    return true;
  }

  inline void dispatcher_lifetime::finish_timeout_activity() noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(group_lifetime->mutex);
    if (group_lifetime->group != nullptr) {
      group_lifetime->group->finish_active_promise();
    }
  }
}  // namespace internal

//-----------------------------------------------------------------

#if CARDIO_WITH_GLIB
/**
 * Dispatcher group that hosts dispatchers with a GLib main context.
 *
 * @remarks
 * This group always uses exit_condition::exit_by_manual. Call shutdown() to
 * let parked dispatchers exit. Regular dispatcher host instances can also be
 * created with this group and share the same shutdown state.
 */
class dispatcher_group_glib final : public dispatcher_group {
private:
  GMainContext* context_ = nullptr;

public:
  /**
   * Creates a GLib dispatcher group.
   *
   * @param context GLib main context to drive, or nullptr for the global default
   * context.
   */
  inline explicit dispatcher_group_glib(GMainContext* context = nullptr)
      : dispatcher_group(exit_condition::exit_by_manual),
        context_(g_main_context_ref(
            context != nullptr ? context : g_main_context_default())) {
  }

  /**
   * Destroys the GLib dispatcher group.
   */
  inline ~dispatcher_group_glib() {
    g_main_context_unref(context_);
  }

  /**
   * Gets the hosted GLib main context.
   *
   * @return Referenced GLib main context.
   */
  inline GMainContext* context() const noexcept {
    return context_;
  }

  dispatcher_group_glib(const dispatcher_group_glib&) = delete;
  dispatcher_group_glib& operator=(const dispatcher_group_glib&) = delete;
};

//-----------------------------------------------------------------

#endif

/**
 * Base class for dispatcher hosts that queue continuations and posted callbacks.
 *
 * @remarks
 * The first dispatcher registered in a dispatcher_group is installed as the
 * current dispatcher on the constructing thread. Use set_current_dispatcher()
 * when assigning another dispatcher or installing a dispatcher on another
 * thread.
 */
class dispatcher {
private:
  friend class dispatcher_group;
  friend class dispatcher_host;
#if defined(__ANDROID__)
  friend class internal::dispatcher_host_android_base;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  friend class dispatcher_host_win32_auto;
#endif
#if CARDIO_WITH_GLIB
  friend class dispatcher_host_glib;
  friend class dispatcher_host_glib_auto;
#endif
  friend void internal::activate_current_promise(internal::promise_state_base& state);
  template <typename T> friend class promise;
  template <typename T> friend class promise_source;
  friend void internal::enqueue_continuation(
      internal::scheduled_continuation continuation);
  friend void internal::dangerous_schedule_later__(
      dispatcher* target,
      std::function<void()> callback);
  friend struct internal::switch_to_awaiter;
  friend struct internal::timer_awaiter;
#if CARDIO_HAS_POSIX_FD
  friend struct internal::fd_awaiter;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  friend struct internal::win32_handle_awaiter;
  friend struct internal::win32_overlapped_awaiter;
#endif
#if CARDIO_WITH_LINUX_IO_URING
  friend class io_uring;
#endif
  friend void internal::arm_timeout_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state);
  friend void internal::disarm_timeout_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state) noexcept;
  friend void internal::configure_timeout_cancellation(
      cancellation_source& source,
      std::chrono::steady_clock::time_point deadline,
      dispatcher* target);
  friend bool internal::request_cancellation(
      const std::shared_ptr<internal::cancellation_state>& state) noexcept;
  friend struct internal::cancellation_state;

  dispatcher_feature features_ = dispatcher_feature::none;
  internal::thread::id owner_thread_id_{};
  std::unique_ptr<dispatcher_group> owned_group_;
  dispatcher_group* group_ = nullptr;
  std::shared_ptr<internal::dispatcher_lifetime> lifetime_;
  ::cardio::internal::mutex mutex_;
  ::cardio::internal::condition_variable condition_;
  struct work_item {
    std::coroutine_handle<> continuation;
    std::function<void()> callback;

    inline work_item() noexcept = default;

    inline explicit work_item(
        std::coroutine_handle<> continuation) noexcept
        : continuation(continuation) {}

    inline explicit work_item(std::function<void()> callback)
        : callback(std::move(callback)) {}

    inline explicit operator bool() const noexcept {
      return continuation || callback;
    }
  };
  std::deque<work_item> queue_;
  std::vector<internal::timer_wait_state*> timer_waits_;
#if CARDIO_HAS_POSIX_FD
  internal::fd_wait_backend fd_waits_;
#endif
#if CARDIO_HAS_WIN32_HANDLE
  internal::win32_handle_wait_backend win32_handle_waits_;
#endif
#if CARDIO_WITH_LINUX_IO_URING
  internal::linux_io_uring_wait_backend io_uring_waits_;
#endif
  std::size_t active_continuations_ = 0;
#if CARDIO_HAS_NATIVE_WAIT
  bool wait_polling_ = false;
  ::cardio::internal::thread::id wait_polling_thread_id_{};
#endif
#if CARDIO_HAS_EXCEPTIONS
  std::function<void(std::exception_ptr)> unhandled_exception_;
#endif

  static inline constexpr dispatcher_feature default_features() noexcept {
    auto result = dispatcher_feature::none;
#if CARDIO_HAS_POSIX_FD
    result |= dispatcher_feature::posix;
#endif
#if CARDIO_HAS_WIN32_HANDLE
    result |= dispatcher_feature::win32;
#endif
#if CARDIO_WITH_LINUX_IO_URING
    result |= dispatcher_feature::io_uring;
#endif
#if CARDIO_HAS_EXCEPTIONS
    result |= dispatcher_feature::exceptions;
#endif
#if defined(__ANDROID__)
    result |= dispatcher_feature::android;
#endif
    return result;
  }

  inline bool waits_empty() const noexcept {
#if CARDIO_WITH_LINUX_IO_URING
    return timer_waits_.empty() && fd_waits_.empty() &&
           io_uring_waits_.empty();
#elif CARDIO_HAS_POSIX_FD
    return timer_waits_.empty() && fd_waits_.empty();
#elif CARDIO_HAS_WIN32_HANDLE
    return timer_waits_.empty() && win32_handle_waits_.empty();
#else
    return timer_waits_.empty();
#endif
  }

  inline dispatcher_group& group() const noexcept {
    return *group_;
  }

  inline void notify() noexcept {
    condition_.notify_all();
#if CARDIO_HAS_POSIX_FD
    fd_waits_.notify();
#endif
#if CARDIO_HAS_WIN32_HANDLE
    win32_handle_waits_.notify();
#endif
    notify_external_event();
  }

  inline virtual void notify_external_event() noexcept {
  }

#if CARDIO_HAS_NATIVE_WAIT
  inline void mark_wait_polling(bool polling) noexcept {
    wait_polling_ = polling;
    wait_polling_thread_id_ =
        polling ? ::cardio::internal::current_thread_id() : ::cardio::internal::thread::id{};
  }

  inline bool should_notify_native_wait_from_current_thread() const noexcept {
#if CARDIO_HAS_POSIX_FD
    // The self-pipe is only needed when another thread must interrupt a parked
    // poll/g_poll snapshot. Work queued by the polling thread itself will be
    // observed before entering the native wait again.
    return wait_polling_ &&
           wait_polling_thread_id_ != ::cardio::internal::current_thread_id();
#else
    return wait_polling_;
#endif
  }

#endif

  inline std::optional<std::chrono::steady_clock::time_point>
  next_timer_deadline() const noexcept {
    if (timer_waits_.empty()) {
      return std::nullopt;
    }
    return timer_waits_.front()->deadline;
  }

  static inline bool timer_wait_precedes(
      const internal::timer_wait_state* left,
      const internal::timer_wait_state* right) noexcept {
    return left->deadline < right->deadline;
  }

  inline void swap_timer_waits(std::size_t left, std::size_t right) noexcept {
    using std::swap;
    swap(timer_waits_[left], timer_waits_[right]);
    timer_waits_[left]->heap_index = left;
    timer_waits_[right]->heap_index = right;
  }

  inline void sift_timer_wait_up(std::size_t index) noexcept {
    while (index > 0) {
      const auto parent = (index - 1) / 2;
      if (!timer_wait_precedes(timer_waits_[index], timer_waits_[parent])) {
        break;
      }
      swap_timer_waits(index, parent);
      index = parent;
    }
  }

  inline void sift_timer_wait_down(std::size_t index) noexcept {
    while (true) {
      const auto left = index * 2 + 1;
      const auto right = left + 1;
      auto smallest = index;
      if (left < timer_waits_.size() &&
          timer_wait_precedes(timer_waits_[left], timer_waits_[smallest])) {
        smallest = left;
      }
      if (right < timer_waits_.size() &&
          timer_wait_precedes(timer_waits_[right], timer_waits_[smallest])) {
        smallest = right;
      }
      if (smallest == index) {
        break;
      }
      swap_timer_waits(index, smallest);
      index = smallest;
    }
  }

  inline void push_timer_wait(internal::timer_wait_state* wait) {
    wait->heap_index = timer_waits_.size();
    timer_waits_.push_back(wait);
    sift_timer_wait_up(wait->heap_index);
  }

  inline void remove_timer_wait(internal::timer_wait_state* wait) noexcept {
    if (!wait || wait->heap_index >= timer_waits_.size() ||
        timer_waits_[wait->heap_index] != wait) {
      return;
    }

    const auto index = wait->heap_index;
    const auto last = timer_waits_.size() - 1;
    if (index != last) {
      swap_timer_waits(index, last);
    }
    timer_waits_.pop_back();
    wait->heap_index = std::numeric_limits<std::size_t>::max();
    if (index < timer_waits_.size()) {
      auto* moved = timer_waits_[index];
      sift_timer_wait_up(moved->heap_index);
      sift_timer_wait_down(moved->heap_index);
    }
  }

  inline internal::timer_wait_state* pop_timer_wait() noexcept {
    if (timer_waits_.empty()) {
      return nullptr;
    }

    auto* wait = timer_waits_.front();
    remove_timer_wait(wait);
    return wait;
  }

  static inline int timeout_until(
      std::optional<std::chrono::steady_clock::time_point> deadline) noexcept {
    if (!deadline) {
      return -1;
    }

    const auto now = std::chrono::steady_clock::now();
    if (*deadline <= now) {
      return 0;
    }

    const auto remaining = *deadline - now;
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
    if (milliseconds < remaining) {
      ++milliseconds;
    }

    const auto max_timeout =
        std::chrono::milliseconds{std::numeric_limits<int>::max()};
    if (milliseconds > max_timeout) {
      return std::numeric_limits<int>::max();
    }

    return static_cast<int>(milliseconds.count());
  }

  static inline int min_wait_timeout(int left, int right) noexcept {
    if (left < 0) {
      return right;
    }
    if (right < 0) {
      return left;
    }
    return left < right ? left : right;
  }

  inline std::vector<work_item> collect_expired_timer_waits(
      std::chrono::steady_clock::time_point now) {
    auto work_items = std::vector<work_item>{};
    while (!timer_waits_.empty()) {
      auto wait = timer_waits_.front();
      if (!wait || !wait->registered || wait->owner != this) {
        (void)pop_timer_wait();
        continue;
      }
      if (wait->deadline > now) {
        break;
      }

      (void)pop_timer_wait();
      wait->owner = nullptr;
      wait->registered = false;
      if (wait->continuation) {
        work_items.emplace_back(wait->continuation);
        wait->continuation = {};
      } else if (wait->callback) {
        work_items.emplace_back(std::move(wait->callback));
      }
    }
    return work_items;
  }

  inline bool collect_ready_timer_waits() {
    auto work_items = std::vector<work_item>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      work_items =
          collect_expired_timer_waits(std::chrono::steady_clock::now());
      for (auto& work : work_items) {
        group_->add_queued_continuation();
        queue_.emplace_back(std::move(work));
      }
    }
    if (!work_items.empty()) {
      condition_.notify_all();
    }
    return !work_items.empty();
  }

  inline void clear_timer_waits() noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
    for (auto& wait : timer_waits_) {
      if (wait && wait->owner == this) {
        wait->owner = nullptr;
        wait->heap_index = std::numeric_limits<std::size_t>::max();
        wait->registered = false;
      }
    }
    timer_waits_.clear();
  }

#if CARDIO_HAS_NATIVE_WAIT
  struct wait_snapshot {
#if CARDIO_HAS_POSIX_FD
    internal::fd_wait_backend::snapshot fd_snapshot;
#if CARDIO_WITH_LINUX_IO_URING
    internal::linux_io_uring_wait_backend::snapshot io_uring_snapshot;
#endif
    std::vector<pollfd> poll_fds;
    std::size_t io_uring_offset = 0;
#endif
#if CARDIO_HAS_WIN32_HANDLE
    internal::win32_handle_wait_backend::snapshot win32_handle_snapshot;
#endif
    std::optional<std::chrono::steady_clock::time_point> deadline;

    inline bool empty() const noexcept {
#if CARDIO_HAS_POSIX_FD
      return poll_fds.empty();
#elif CARDIO_HAS_WIN32_HANDLE
      return win32_handle_snapshot.empty();
#else
      return true;
#endif
    }
  };

  inline wait_snapshot make_wait_snapshot() {
    auto result = wait_snapshot{};
    result.deadline = next_timer_deadline();
#if CARDIO_HAS_POSIX_FD
    result.fd_snapshot = fd_waits_.make_snapshot(this, true);
    result.poll_fds = result.fd_snapshot.poll_fds;

#if CARDIO_WITH_LINUX_IO_URING
    result.io_uring_snapshot = io_uring_waits_.make_snapshot(this);
    result.io_uring_offset = result.poll_fds.size();
    result.poll_fds.insert(
        result.poll_fds.end(),
        result.io_uring_snapshot.poll_fds.begin(),
        result.io_uring_snapshot.poll_fds.end());
#endif
#elif CARDIO_HAS_WIN32_HANDLE
    result.win32_handle_snapshot =
        win32_handle_waits_.make_snapshot(this, true);
#endif

    return result;
  }

  static inline void wait_for_snapshot(
      wait_snapshot& snapshot,
      int timeout_milliseconds = -1) {
    if (snapshot.empty()) {
      return;
    }

#if CARDIO_HAS_POSIX_FD
    auto poll_result = int{};
    do {
      poll_result = ::poll(
          snapshot.poll_fds.data(),
          static_cast<nfds_t>(snapshot.poll_fds.size()),
          timeout_milliseconds);
    } while (poll_result == -1 && errno == EINTR);

    if (poll_result == -1) {
      internal::fail_system_error(errno, "cardio: poll failed");
    }

    for (auto index = std::size_t{0};
         index < snapshot.fd_snapshot.poll_fds.size(); ++index) {
      snapshot.fd_snapshot.poll_fds[index].revents =
          snapshot.poll_fds[index].revents;
    }

#if CARDIO_WITH_LINUX_IO_URING
    for (auto index = std::size_t{0};
         index < snapshot.io_uring_snapshot.poll_fds.size(); ++index) {
      snapshot.io_uring_snapshot.poll_fds[index].revents =
          snapshot.poll_fds[snapshot.io_uring_offset + index].revents;
    }
#endif
#elif CARDIO_HAS_WIN32_HANDLE
    internal::win32_handle_wait_backend::wait(
        snapshot.win32_handle_snapshot, timeout_milliseconds);
#endif
  }
#endif

  inline void finish_continuation() noexcept {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      --active_continuations_;
    }
    group_->finish_continuation();
  }

  inline void start_inline_continuation() {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      ++active_continuations_;
    }
    group_->active_continuations_.fetch_add(1, std::memory_order_acq_rel);
  }

  inline void add_feature(dispatcher_feature feature) noexcept {
    features_ |= feature;
  }

  class active_continuation_guard {
  private:
    dispatcher& owner_;

  public:
    inline explicit active_continuation_guard(dispatcher& owner) noexcept
        : owner_(owner) {}

    inline ~active_continuation_guard() noexcept {
      owner_.finish_continuation();
    }

    active_continuation_guard(const active_continuation_guard&) = delete;
    active_continuation_guard& operator=(const active_continuation_guard&) = delete;
  };

#if CARDIO_HAS_NATIVE_WAIT
  inline void clear_wait_polling() noexcept {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      mark_wait_polling(false);
    }
    condition_.notify_all();
  }

#if CARDIO_HAS_WIN32_HANDLE
  inline bool dispatch_win32_messages() {
    auto dispatched = false;
    auto message = MSG{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
      dispatched = true;
      if (message.message == WM_QUIT) {
        group_->shutdown();
        ::PostQuitMessage(static_cast<int>(message.wParam));
        break;
      }

      (void)::TranslateMessage(&message);
      (void)::DispatchMessageW(&message);
    }
    return dispatched;
  }
#endif

  inline bool collect_ready_wait_snapshot(wait_snapshot& wait_snapshot) {
#if CARDIO_WITH_LINUX_IO_URING
    auto external_continuations =
        std::vector<internal::scheduled_continuation>{};
#endif
    auto collected = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      auto timer_work_items =
          collect_expired_timer_waits(std::chrono::steady_clock::now());
#if CARDIO_HAS_POSIX_FD
      auto ready_continuations =
          fd_waits_.collect_ready(this, wait_snapshot.fd_snapshot);
#if CARDIO_WITH_LINUX_IO_URING
      auto io_uring_continuations =
          io_uring_waits_.collect_ready(this, wait_snapshot.io_uring_snapshot);
#endif
#elif CARDIO_HAS_WIN32_HANDLE
      auto ready_continuations =
          win32_handle_waits_.collect_ready(
              this, wait_snapshot.win32_handle_snapshot);
#endif
      for (auto& timer_work : timer_work_items) {
        group_->add_queued_continuation();
        queue_.emplace_back(std::move(timer_work));
        collected = true;
      }
      for (auto ready_continuation : ready_continuations) {
        group_->add_queued_continuation();
        queue_.emplace_back(ready_continuation);
        collected = true;
      }
#if CARDIO_WITH_LINUX_IO_URING
      for (auto scheduled : io_uring_continuations) {
        if (scheduled.target == this) {
          group_->add_queued_continuation();
          queue_.emplace_back(scheduled.continuation);
        } else {
          external_continuations.push_back(scheduled);
        }
        collected = true;
      }
#endif
      mark_wait_polling(false);
    }
    condition_.notify_all();
#if CARDIO_WITH_LINUX_IO_URING
    for (auto scheduled : external_continuations) {
      scheduled.target->enqueue(scheduled.continuation);
    }
#endif

    return collected;
  }
#endif

  inline void execute_work_body(work_item& work) {
    struct dispatcher_execution_guard {
      inline dispatcher_execution_guard() noexcept {
        ++internal::runtime_state().dispatcher_execution_depth;
      }
      inline ~dispatcher_execution_guard() noexcept {
        --internal::runtime_state().dispatcher_execution_depth;
      }
    } execution_guard;
    (void)execution_guard;

#if CARDIO_HAS_EXCEPTIONS
    try {
      if (work.continuation) {
        work.continuation.resume();
      } else {
        work.callback();
      }
    } catch (...) {
      handle_unhandled_exception(std::current_exception());
    }
#else
    if (work.continuation) {
      work.continuation.resume();
    } else {
      work.callback();
    }
#endif
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void handle_unhandled_exception(std::exception_ptr exception) noexcept {
    if (unhandled_exception_) {
      try {
        unhandled_exception_(std::move(exception));
      } catch (...) {
      }
    } else {
      internal::log_ignored_unhandled_exception(std::move(exception));
    }
  }
#endif

  inline void execute_dequeued_work(
      work_item& work,
      const std::function<void()>& pump_external = {},
      shutdown_mode mode = shutdown_mode::gentle) {
    group_->start_continuation();
    auto active_scope = active_continuation_guard(*this);
    execute_work_body(work);
    drain_inline_continuations(pump_external, mode);
  }

  inline void execute_inline_work(work_item& work) {
    start_inline_continuation();
    auto active_scope = active_continuation_guard(*this);
    execute_work_body(work);
  }

  inline bool try_schedule_inline(work_item& item) {
    auto& runtime = internal::runtime_state();
    if (!item.continuation || item.callback ||
        runtime.current_dispatcher != this ||
        runtime.dispatcher_execution_depth == 0 ||
        runtime.final_suspend_depth != 0 ||
        group_->is_shutdown_requested() ||
        runtime.inline_continuation_count >= internal::max_inline_continuation_depth) {
      return false;
    }

    runtime.inline_continuations[runtime.inline_continuation_count] =
        item.continuation.address();
    ++runtime.inline_continuation_count;
    item.continuation = {};
    return true;
  }

  inline void enqueue_slow(work_item item) {
    if (!item) {
      return;
    }

    group_->add_queued_continuation();
#if CARDIO_HAS_NATIVE_WAIT
    auto notify_native_wait = false;
#endif
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      queue_.push_back(std::move(item));
#if CARDIO_HAS_NATIVE_WAIT
      notify_native_wait = should_notify_native_wait_from_current_thread();
#endif
    }
    condition_.notify_all();
#if CARDIO_HAS_POSIX_FD
    if (notify_native_wait) {
      fd_waits_.notify();
    }
#endif
#if CARDIO_HAS_WIN32_HANDLE
    if (notify_native_wait) {
      win32_handle_waits_.notify();
    }
#endif
    notify_external_event();
  }

  inline void drain_inline_continuations(
      const std::function<void()>& pump_external = {},
      shutdown_mode mode = shutdown_mode::gentle) {
    auto& runtime = internal::runtime_state();
    if (runtime.inline_continuation_draining) {
      return;
    }

    struct drain_guard {
      internal::runtime_thread_state& runtime;

      inline explicit drain_guard(
          internal::runtime_thread_state& runtime) noexcept
          : runtime(runtime) {
        runtime.inline_continuation_draining = true;
      }
      inline ~drain_guard() noexcept {
        runtime.inline_continuation_draining = false;
      }
    } guard(runtime);
    (void)guard;

    auto processed = std::size_t{0};
    while (runtime.inline_continuation_count != 0 &&
           processed < internal::max_inline_continuation_depth) {
      if (pump_external) {
        pump_external();
        if (group_->should_exit_immediately(mode)) {
          break;
        }
      }

      --runtime.inline_continuation_count;
      auto continuation = std::coroutine_handle<>::from_address(
          runtime.inline_continuations[runtime.inline_continuation_count]);
      runtime.inline_continuations[runtime.inline_continuation_count] = nullptr;
      ++processed;

      struct inline_depth_guard {
        internal::runtime_thread_state& runtime;

        inline explicit inline_depth_guard(
            internal::runtime_thread_state& runtime) noexcept
            : runtime(runtime) {
          ++runtime.inline_continuation_depth;
        }
        inline ~inline_depth_guard() noexcept {
          --runtime.inline_continuation_depth;
        }
      } depth_guard(runtime);
      (void)depth_guard;

      auto work = work_item{continuation};
      execute_inline_work(work);
    }

    while (runtime.inline_continuation_count != 0) {
      --runtime.inline_continuation_count;
      auto continuation = std::coroutine_handle<>::from_address(
          runtime.inline_continuations[runtime.inline_continuation_count]);
      runtime.inline_continuations[runtime.inline_continuation_count] = nullptr;
      enqueue_slow(work_item{continuation});
    }
  }

  inline void enqueue(work_item item) {
    if (!item) {
      return;
    }

    if (try_schedule_inline(item)) {
      return;
    }

    enqueue_slow(std::move(item));
  }

  inline void enqueue(std::coroutine_handle<> continuation) {
    enqueue(work_item{continuation});
  }

  inline void enqueue_callback(std::function<void()> callback) {
    if (!callback) {
      return;
    }

    enqueue(work_item{std::move(callback)});
  }

  inline void register_timer_wait(
      std::shared_ptr<internal::timer_wait_state> wait,
      std::coroutine_handle<> continuation) {
#if CARDIO_HAS_NATIVE_WAIT
    auto notify_native_wait = false;
#endif
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
#if CARDIO_HAS_NATIVE_WAIT
      const auto previous_deadline = next_timer_deadline();
#endif
      wait->continuation = continuation;
      wait->callback = {};
      wait->owner = this;
      wait->registered = true;
      push_timer_wait(wait.get());
#if CARDIO_HAS_NATIVE_WAIT
      const auto current_deadline = next_timer_deadline();
      notify_native_wait = should_notify_native_wait_from_current_thread() &&
          current_deadline &&
          (!previous_deadline || *current_deadline < *previous_deadline);
#endif
    }
    condition_.notify_one();
#if CARDIO_HAS_POSIX_FD
    if (notify_native_wait) {
      fd_waits_.notify();
    }
#endif
#if CARDIO_HAS_WIN32_HANDLE
    if (notify_native_wait) {
      win32_handle_waits_.notify();
    }
#endif
    notify_external_event();
  }

  inline void register_timer_callback_wait(
      std::shared_ptr<internal::timer_wait_state> wait,
      std::function<void()> callback) {
#if CARDIO_HAS_NATIVE_WAIT
    auto notify_native_wait = false;
#endif
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
#if CARDIO_HAS_NATIVE_WAIT
      const auto previous_deadline = next_timer_deadline();
#endif
      wait->continuation = {};
      wait->callback = std::move(callback);
      wait->owner = this;
      wait->registered = true;
      push_timer_wait(wait.get());
#if CARDIO_HAS_NATIVE_WAIT
      const auto current_deadline = next_timer_deadline();
      notify_native_wait = should_notify_native_wait_from_current_thread() &&
          current_deadline &&
          (!previous_deadline || *current_deadline < *previous_deadline);
#endif
    }
    condition_.notify_one();
#if CARDIO_HAS_POSIX_FD
    if (notify_native_wait) {
      fd_waits_.notify();
    }
#endif
#if CARDIO_HAS_WIN32_HANDLE
    if (notify_native_wait) {
      win32_handle_waits_.notify();
    }
#endif
    notify_external_event();
  }

  inline bool unregister_timer_wait(
      const std::shared_ptr<internal::timer_wait_state>& wait) noexcept {
    auto unregistered = false;
#if CARDIO_HAS_NATIVE_WAIT
    auto notify_native_wait = false;
#endif
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (wait && wait->owner == this && wait->registered) {
        wait->owner = nullptr;
        wait->registered = false;
        wait->continuation = {};
        wait->callback = {};
        remove_timer_wait(wait.get());
        unregistered = true;
#if CARDIO_HAS_NATIVE_WAIT
        notify_native_wait = should_notify_native_wait_from_current_thread();
#endif
      }
    }

    if (unregistered) {
      condition_.notify_one();
#if CARDIO_HAS_POSIX_FD
      if (notify_native_wait) {
        fd_waits_.notify();
      }
#endif
#if CARDIO_HAS_WIN32_HANDLE
      if (notify_native_wait) {
        win32_handle_waits_.notify();
      }
#endif
      notify_external_event();
    }
    return unregistered;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void cancel_timer_wait(
      const std::shared_ptr<internal::timer_wait_state>& wait) noexcept {
    auto continuation = std::coroutine_handle<>{};
    auto unregistered = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (wait && wait->owner == this && wait->registered) {
        wait->canceled = true;
        continuation = wait->continuation;
        wait->owner = nullptr;
        wait->registered = false;
        wait->continuation = {};
        wait->callback = {};
        remove_timer_wait(wait.get());
        unregistered = true;
      }
    }

    if (unregistered) {
      condition_.notify_one();
      if (continuation) {
        enqueue(continuation);
      }
      notify_external_event();
    }
  }
#endif

#if CARDIO_HAS_POSIX_FD
  inline void register_fd_wait(
      std::shared_ptr<internal::fd_wait_state> wait,
      std::coroutine_handle<> continuation) {
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      fd_waits_.register_wait(this, std::move(wait), continuation);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    }
    condition_.notify_one();
    if (notify_native_wait) {
      fd_waits_.notify();
    }
    notify_external_event();
  }

  inline void unregister_fd_wait(
      const std::shared_ptr<internal::fd_wait_state>& wait) noexcept {
    auto unregistered = false;
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      unregistered = fd_waits_.unregister_wait(this, wait);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    }

    if (unregistered) {
      condition_.notify_one();
      if (notify_native_wait) {
        fd_waits_.notify();
      }
      notify_external_event();
    }
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void cancel_fd_wait(
      const std::shared_ptr<internal::fd_wait_state>& wait) noexcept {
    auto continuation = std::coroutine_handle<>{};
    auto unregistered = false;
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (wait && wait->owner == this && wait->registered) {
        wait->canceled = true;
        continuation = wait->continuation;
        unregistered = fd_waits_.unregister_wait(this, wait);
        notify_native_wait = should_notify_native_wait_from_current_thread();
      }
    }

    if (unregistered) {
      condition_.notify_one();
      if (notify_native_wait) {
        fd_waits_.notify();
      }
      if (continuation) {
        enqueue(continuation);
      }
      notify_external_event();
    }
  }
#endif

  inline void clear_fd_waits() noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
    fd_waits_.clear(this);
#if CARDIO_WITH_LINUX_IO_URING
    io_uring_waits_.clear(this);
#endif
  }
#endif

#if CARDIO_HAS_WIN32_HANDLE
  inline void register_win32_handle_wait(
      std::shared_ptr<internal::win32_wait_state> wait,
      std::coroutine_handle<> continuation) {
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      win32_handle_waits_.register_wait(this, std::move(wait), continuation);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    }
    condition_.notify_one();
    if (notify_native_wait) {
      win32_handle_waits_.notify();
    }
    notify_external_event();
  }

  inline void unregister_win32_handle_wait(
      const std::shared_ptr<internal::win32_wait_state>& wait) noexcept {
    auto unregistered = false;
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      unregistered = win32_handle_waits_.unregister_wait(this, wait);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    }

    if (unregistered) {
      condition_.notify_one();
      if (notify_native_wait) {
        win32_handle_waits_.notify();
      }
      notify_external_event();
    }
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void cancel_win32_handle_wait(
      const std::shared_ptr<internal::win32_wait_state>& wait) noexcept {
    auto continuation = std::coroutine_handle<>{};
    auto unregistered = false;
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (wait && wait->owner == this && wait->registered) {
        wait->canceled = true;
        continuation = wait->continuation;
        unregistered = win32_handle_waits_.unregister_wait(this, wait);
        notify_native_wait = should_notify_native_wait_from_current_thread();
      }
    }

    if (unregistered) {
      condition_.notify_one();
      if (notify_native_wait) {
        win32_handle_waits_.notify();
      }
      if (continuation) {
        enqueue(continuation);
      }
      notify_external_event();
    }
  }
#endif

  inline void clear_win32_handle_waits() noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
    win32_handle_waits_.clear(this);
  }
#endif

#if CARDIO_WITH_LINUX_IO_URING
  template <typename Prepare>
  inline void submit_io_uring_operation(
      std::shared_ptr<internal::io_uring_ring_state> ring,
      std::shared_ptr<internal::io_uring_operation_state> operation,
      Prepare prepare) {
    auto notify_native_wait = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      io_uring_waits_.submit_operation(
          this, std::move(ring), std::move(operation), prepare);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    }
    condition_.notify_one();
    if (notify_native_wait) {
      fd_waits_.notify();
    }
    notify_external_event();
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void cancel_io_uring_operation(
      const std::shared_ptr<internal::io_uring_operation_state>& operation) noexcept {
    auto submitted = false;
    auto notify_native_wait = false;
    try {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      submitted = io_uring_waits_.submit_cancel_operation(this, operation);
      notify_native_wait = should_notify_native_wait_from_current_thread();
    } catch (...) {
      submitted = false;
    }

    if (submitted) {
      condition_.notify_one();
      if (notify_native_wait) {
        fd_waits_.notify();
      }
      notify_external_event();
    }
  }
#endif
#endif

protected:
  /**
   * Creates a dispatcher base.
   *
   * @throws std::system_error Thrown if the fd wakeup backend cannot be
   * initialized.
   *
   * @remarks
   * This overload creates an implicit dispatcher_group for single-dispatcher
   * use.
   */
  inline dispatcher()
      : features_(default_features()),
        owned_group_(std::make_unique<dispatcher_group>()),
        group_(owned_group_.get()),
        lifetime_(std::make_shared<internal::dispatcher_lifetime>(
            this, group_->lifetime_)) {
    group_->register_dispatcher(this);
  }

  /**
   * Creates a dispatcher base in an existing dispatcher group.
   *
   * @param group Dispatcher group shared with other dispatchers.
   *
   * @throws std::system_error Thrown if the fd wakeup backend cannot be
   * initialized.
   */
  inline explicit dispatcher(dispatcher_group& group)
      : group_(&group),
        lifetime_(std::make_shared<internal::dispatcher_lifetime>(
            this, group_->lifetime_)) {
    features_ = default_features();
    group_->register_dispatcher(this);
  }

public:
  /**
   * Destroys the dispatcher.
   */
  virtual ~dispatcher() = 0;

  /**
   * Gets capabilities supported by this dispatcher.
   *
   * @return Dispatcher feature flags.
   */
  inline dispatcher_feature get_feature() const noexcept {
    return features_;
  }

  /**
   * Reports whether this dispatcher is bound to the calling OS thread.
   *
   * @return True for a thread-bound dispatcher on its owner thread; false for
   * an unbound dispatcher or a call from another thread.
   */
  inline bool is_current_thread_owner() const noexcept {
    return owner_thread_id_ != internal::thread::id{} &&
           owner_thread_id_ == internal::current_thread_id();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Installs a callback for exceptions escaping resumed continuations or posted
   * callbacks.
   *
   * @param callback Callback that receives the escaping exception.
   *
   * @remarks
   * If the callback throws, that exception is ignored.
   */
  inline void unhandled_exception(
      std::function<void(std::exception_ptr)> callback) {
    unhandled_exception_ = std::move(callback);
  }
#endif

  dispatcher(const dispatcher&) = delete;
  dispatcher& operator=(const dispatcher&) = delete;
};

inline dispatcher::~dispatcher() {
  // Invalidate target while excluding timeout registration and cleanup paths.
  auto lifetime_lock = std::lock_guard<::cardio::internal::mutex>(lifetime_->mutex);
  lifetime_->target = nullptr;
  clear_timer_waits();
#if CARDIO_HAS_POSIX_FD
  clear_fd_waits();
#endif
#if CARDIO_HAS_WIN32_HANDLE
  clear_win32_handle_waits();
#endif
  group_->unregister_dispatcher(this);
  if (internal::runtime_state().current_dispatcher == this) {
    internal::runtime_state().current_dispatcher = nullptr;
  }
}

namespace internal {
  /* DANGER: DO NOT USE this function. USE fire_and_forget() instead. */
  inline void dangerous_schedule_later__(
      dispatcher* target,
      std::function<void()> callback) {
    target->enqueue_callback(std::move(callback));
  }
}  // namespace internal

//-----------------------------------------------------------------------------------------------

/**
 * Dispatches continuations and posted callbacks on the current thread.
 *
 * @remarks
 * The first dispatcher registered in a dispatcher_group is installed as the
 * current dispatcher on the constructing thread. Use set_current_dispatcher()
 * when assigning another dispatcher or installing a dispatcher on another
 * thread.
 */
class dispatcher_host final : public dispatcher {
public:
  /**
   * Creates a dispatcher host.
   *
   * @param policy Whether to bind execution to the constructing thread.
   *
   * @throws std::system_error Thrown if the fd wakeup backend cannot be
   * initialized.
   *
   * @remarks
   * This overload creates an implicit dispatcher_group for single-dispatcher
   * use.
   */
  inline explicit dispatcher_host(
      dispatcher_thread_policy policy = dispatcher_thread_policy::any_thread)
      : dispatcher() {
    if (policy == dispatcher_thread_policy::current_thread) {
      owner_thread_id_ = internal::current_thread_id();
    }
  }

  /**
   * Creates a dispatcher host in an existing dispatcher group.
   *
   * @param group Dispatcher group shared with other dispatchers.
   * @param policy Whether to bind execution to the constructing thread.
   *
   * @throws std::system_error Thrown if the fd wakeup backend cannot be
   * initialized.
   */
  inline explicit dispatcher_host(
      dispatcher_group& group,
      dispatcher_thread_policy policy = dispatcher_thread_policy::any_thread)
      : dispatcher(group) {
    if (policy == dispatcher_thread_policy::current_thread) {
      owner_thread_id_ = internal::current_thread_id();
    }
  }

  /**
   * Park the current thread and distribute the continuations.
   *
   * @remarks
   * The exit condition is controlled by the dispatcher's group.
   * This function does not install the dispatcher as the current dispatcher.
   *
   * When exception support is enabled, escaping exceptions are passed to the
   * installed unhandled_exception() callback. If no callback is installed, they
   * are logged to std::clog unless CARDIO_SILENCE_UNHANDLED_EXCEPTION is
   * defined. In builds without exception support, continuations and callbacks
   * are executed directly.
   *
   * @param mode Shutdown mode for this park call.
   * @param policy Ordering policy for external message pumping and queued or
   * inline continuation work.
   */
  inline void park(
      shutdown_mode mode = shutdown_mode::gentle,
      park_policy policy = park_policy::external_pump_first) {
    if (owner_thread_id_ != internal::thread::id{} &&
        !is_current_thread_owner()) {
      internal::fail_runtime_error(
          "cardio: thread-bound dispatcher must be parked on its owner thread");
    }
#if !CARDIO_HAS_WIN32_HANDLE
    (void)policy;
#endif
#if CARDIO_HAS_NATIVE_WAIT
    auto shutdown_wait_probe_done = false;
#endif
    while (true) {
#if CARDIO_HAS_WIN32_HANDLE
      if (policy == park_policy::external_pump_first &&
          !group_->is_shutdown_requested()) {
        (void)dispatch_win32_messages();
        if (group_->should_exit_immediately(mode)) {
          return;
        }
      }
#endif

      if (collect_ready_timer_waits()) {
        continue;
      }

      auto work = work_item{};
#if CARDIO_HAS_NATIVE_WAIT
      auto wait_snapshot = dispatcher::wait_snapshot{};
      auto wait_timeout_milliseconds = -1;
#endif
      {
        auto lock = std::unique_lock<::cardio::internal::mutex>(mutex_);
        if (group_->should_exit_immediately(mode)) {
          return;
        }
        while (queue_.empty()) {
#if CARDIO_HAS_NATIVE_WAIT
          if (waits_empty()) {
            if (group_->should_exit(mode)) {
              return;
            }

#if CARDIO_HAS_WIN32_HANDLE
            if (!wait_polling_) {
              break;
            }

            condition_.wait(lock, [this, mode] {
              return !queue_.empty() || !waits_empty() || !wait_polling_ ||
                     group_->should_exit(mode);
            });
#else
            condition_.wait(lock, [this, mode] {
              return !queue_.empty() || !waits_empty() ||
                     group_->should_exit(mode);
            });
#endif
            continue;
          }

          if (group_->is_shutdown_requested()) {
            if (group_->work_drained()) {
              if (!shutdown_wait_probe_done && !wait_polling_) {
                break;
              }
              return;
            }

            condition_.wait(lock, [this, mode] {
              return !queue_.empty() || waits_empty() || group_->should_exit(mode);
            });
            continue;
          }

          if (!wait_polling_) {
            break;
          }

          condition_.wait(lock, [this, mode] {
            return !queue_.empty() || waits_empty() || !wait_polling_ ||
                   group_->should_exit(mode);
          });
#else
          if (group_->should_exit(mode)) {
            return;
          }

          const auto timer_deadline = next_timer_deadline();
          if (timer_deadline) {
            if (*timer_deadline <= std::chrono::steady_clock::now()) {
              break;
            }
            condition_.wait_until(lock, *timer_deadline);
          } else {
            condition_.wait(lock, [this, mode] {
              return !queue_.empty() || group_->should_exit(mode);
            });
          }
#endif
        }

        if (group_->should_exit_immediately(mode)) {
          return;
        }

#if CARDIO_HAS_NATIVE_WAIT
#if CARDIO_HAS_WIN32_HANDLE
        if (queue_.empty() && !wait_polling_) {
#else
        if (queue_.empty() && !waits_empty() && !wait_polling_) {
#endif
          wait_snapshot = make_wait_snapshot();
          if (!wait_snapshot.empty()) {
            mark_wait_polling(true);
            wait_timeout_milliseconds =
                timeout_until(wait_snapshot.deadline);
            if (group_->is_shutdown_requested()) {
              wait_timeout_milliseconds =
                  min_wait_timeout(wait_timeout_milliseconds, 0);
            }
          }
        }
#endif

        if (!queue_.empty()) {
#if CARDIO_HAS_NATIVE_WAIT
          shutdown_wait_probe_done = false;
          mark_wait_polling(false);
#endif
          work = std::move(queue_.front());
          queue_.pop_front();
          ++active_continuations_;
        }
      }

#if CARDIO_HAS_NATIVE_WAIT
      if (!work && !wait_snapshot.empty()) {
#if CARDIO_HAS_EXCEPTIONS
        try {
          wait_for_snapshot(wait_snapshot, wait_timeout_milliseconds);
        } catch (...) {
          clear_wait_polling();
          throw;
        }
#else
        wait_for_snapshot(wait_snapshot, wait_timeout_milliseconds);
#endif

        if (group_->should_exit_immediately(mode)) {
          clear_wait_polling();
          return;
        }

#if CARDIO_HAS_WIN32_HANDLE
        if (wait_snapshot.win32_handle_snapshot.message_ready) {
          (void)dispatch_win32_messages();
        }

        if (group_->should_exit_immediately(mode)) {
          clear_wait_polling();
          return;
        }
#endif

        (void)collect_ready_wait_snapshot(wait_snapshot);
        if (wait_timeout_milliseconds == 0) {
          shutdown_wait_probe_done = true;
        }

        continue;
      }
#endif

      if (!work) {
        continue;
      }

#if CARDIO_HAS_WIN32_HANDLE
      if (policy == park_policy::external_pump_first) {
        auto pump_external = [this] {
          if (!group_->is_shutdown_requested()) {
            (void)dispatch_win32_messages();
          }
        };
        execute_dequeued_work(work, pump_external, mode);
      } else
#endif
      {
        execute_dequeued_work(work, {}, mode);
      }
    }
  }

  dispatcher_host(const dispatcher_host&) = delete;
  dispatcher_host& operator=(const dispatcher_host&) = delete;
};

#if CARDIO_HAS_WIN32_HANDLE
/**
 * Alias for dispatcher_host in Win32 HANDLE builds.
 */
using dispatcher_host_win32 = dispatcher_host;
#endif

//-----------------------------------------------------------------------------------------------

#if defined(__ANDROID__)
namespace internal {
  class dispatcher_host_android_base;

  struct android_looper_callback_entry {
    std::uintptr_t token = 0;
    dispatcher_host_android_base* owner = nullptr;
  };

  struct android_looper_callback_registry {
    ::cardio::internal::mutex mutex;
    std::vector<android_looper_callback_entry> entries;
    std::atomic<std::uintptr_t> next_token = 1;
  };

  inline android_looper_callback_registry&
  get_android_looper_callback_registry() noexcept {
    static android_looper_callback_registry registry;
    return registry;
  }

  inline std::uintptr_t register_android_looper_callback(
      dispatcher_host_android_base* owner) {
    auto& registry = get_android_looper_callback_registry();
    auto token = registry.next_token.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) {
      token = registry.next_token.fetch_add(1, std::memory_order_relaxed);
    }

    auto lock = std::lock_guard<::cardio::internal::mutex>(registry.mutex);
    registry.entries.push_back(android_looper_callback_entry{token, owner});
    return token;
  }

  inline void unregister_android_looper_callback(
      std::uintptr_t token) noexcept {
    if (token == 0) {
      return;
    }

    auto& registry = get_android_looper_callback_registry();
    auto lock = std::lock_guard<::cardio::internal::mutex>(registry.mutex);
    for (auto iterator = registry.entries.begin();
         iterator != registry.entries.end(); ++iterator) {
      if (iterator->token == token) {
        registry.entries.erase(iterator);
        return;
      }
    }
  }

  inline dispatcher_host_android_base* find_android_looper_callback(
      std::uintptr_t token) noexcept {
    auto& registry = get_android_looper_callback_registry();
    auto lock = std::lock_guard<::cardio::internal::mutex>(registry.mutex);
    for (const auto& entry : registry.entries) {
      if (entry.token == token) {
        return entry.owner;
      }
    }
    return nullptr;
  }

  class dispatcher_host_android_base : public dispatcher {
  private:
    struct looper_registration {
      int fd = -1;
      int events = 0;
      std::uintptr_t token = 0;
    };

    struct desired_registration {
      int fd = -1;
      int events = 0;
    };

    ALooper* looper_ = nullptr;
    int event_fd_ = -1;
    int timer_fd_ = -1;
    looper_registration event_registration_;
    looper_registration timer_registration_;
    std::vector<looper_registration> wait_registrations_;
    dispatcher::wait_snapshot looper_wait_snapshot_;
    bool looper_wait_snapshot_valid_ = false;
    ::cardio::internal::thread::id owner_thread_;
    shutdown_mode active_shutdown_mode_ = shutdown_mode::gentle;
    bool parked_ = false;

    class execution_scope {
    private:
      dispatcher* previous_dispatcher_ = nullptr;
      bool installed_dispatcher_ = false;

    public:
      inline explicit execution_scope(dispatcher_host_android_base& owner)
          : previous_dispatcher_(
                internal::runtime_state().current_dispatcher) {
        if (previous_dispatcher_ != nullptr && previous_dispatcher_ != &owner) {
          internal::fail_runtime_error(
              "cardio: active dispatcher does not match Android Looper callback");
        }
        if (previous_dispatcher_ == nullptr) {
          internal::runtime_state().current_dispatcher = &owner;
          installed_dispatcher_ = true;
        }
      }

      inline ~execution_scope() {
        if (installed_dispatcher_) {
          internal::runtime_state().current_dispatcher = previous_dispatcher_;
        }
      }

      execution_scope(const execution_scope&) = delete;
      execution_scope& operator=(const execution_scope&) = delete;
    };

    static inline int to_looper_events(short events) noexcept {
      auto result = 0;
      if ((events & (POLLIN | POLLPRI)) != 0) {
        result |= ALOOPER_EVENT_INPUT;
      }
      if ((events & POLLOUT) != 0) {
        result |= ALOOPER_EVENT_OUTPUT;
      }
      return result;
    }

    static inline short from_looper_events(int events) noexcept {
      auto result = short{};
      if ((events & ALOOPER_EVENT_INPUT) != 0) {
        result |= POLLIN;
      }
      if ((events & ALOOPER_EVENT_OUTPUT) != 0) {
        result |= POLLOUT;
      }
      if ((events & ALOOPER_EVENT_ERROR) != 0) {
        result |= POLLERR;
      }
      if ((events & ALOOPER_EVENT_HANGUP) != 0) {
        result |= POLLHUP;
      }
      if ((events & ALOOPER_EVENT_INVALID) != 0) {
        result |= POLLNVAL;
      }
      return result;
    }

    inline void ensure_owner_thread() const {
      if (::cardio::internal::current_thread_id() != owner_thread_) {
        internal::fail_runtime_error(
            "cardio: Android dispatcher must be used on its owner thread");
      }
    }

    inline looper_registration add_looper_registration(
        int fd,
        int events) {
      auto registration = looper_registration{fd, events, 0};
      registration.token =
          internal::register_android_looper_callback(this);
      if (ALooper_addFd(
              looper_,
              fd,
              ALOOPER_POLL_CALLBACK,
              events,
              &dispatcher_host_android_base::looper_callback,
              reinterpret_cast<void*>(registration.token)) != 1) {
        internal::unregister_android_looper_callback(registration.token);
        internal::fail_runtime_error(
            "cardio: ALooper_addFd failed for Android dispatcher");
      }
      return registration;
    }

    inline void remove_looper_registration(
        looper_registration& registration) noexcept {
      if (registration.token == 0) {
        return;
      }

      internal::unregister_android_looper_callback(registration.token);
      if (looper_ != nullptr && registration.fd >= 0) {
        (void)ALooper_removeFd(looper_, registration.fd);
      }
      registration = looper_registration{};
    }

    static inline bool desired_contains(
        const std::vector<desired_registration>& desired,
        int fd,
        int events) noexcept {
      for (const auto& registration : desired) {
        if (registration.fd == fd && registration.events == events) {
          return true;
        }
      }
      return false;
    }

    static inline bool installed_contains(
        const std::vector<looper_registration>& installed,
        int fd,
        int events) noexcept {
      for (const auto& registration : installed) {
        if (registration.fd == fd && registration.events == events) {
          return true;
        }
      }
      return false;
    }

    static inline std::vector<desired_registration>
    make_desired_registrations(const dispatcher::wait_snapshot& snapshot) {
      auto desired = std::vector<desired_registration>{};
      desired.reserve(snapshot.poll_fds.size());
      for (const auto& fd : snapshot.poll_fds) {
        const auto events = to_looper_events(fd.events);
        auto* existing = static_cast<desired_registration*>(nullptr);
        for (auto& registration : desired) {
          if (registration.fd == fd.fd) {
            existing = &registration;
            break;
          }
        }
        if (existing != nullptr) {
          existing->events |= events;
        } else {
          desired.push_back(desired_registration{fd.fd, events});
        }
      }
      return desired;
    }

    inline void install_wait_registrations(
        const dispatcher::wait_snapshot& snapshot) {
      const auto desired = make_desired_registrations(snapshot);

      for (auto index = std::size_t{0}; index < wait_registrations_.size();) {
        auto& registration = wait_registrations_[index];
        if (desired_contains(desired, registration.fd, registration.events)) {
          ++index;
          continue;
        }

        remove_looper_registration(registration);
        wait_registrations_.erase(wait_registrations_.begin() + index);
      }

      for (const auto& registration : desired) {
        if (!installed_contains(
                wait_registrations_, registration.fd, registration.events)) {
          wait_registrations_.push_back(
              add_looper_registration(registration.fd, registration.events));
        }
      }
    }

    inline void arm_timer(
        std::optional<std::chrono::steady_clock::time_point> deadline) {
      auto specification = itimerspec{};
      if (deadline) {
        auto remaining = *deadline - std::chrono::steady_clock::now();
        auto nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(remaining);
        if (nanoseconds.count() <= 0) {
          nanoseconds = std::chrono::nanoseconds{1};
        }
        specification.it_value.tv_sec = static_cast<time_t>(
            nanoseconds.count() / 1000000000LL);
        specification.it_value.tv_nsec = static_cast<long>(
            nanoseconds.count() % 1000000000LL);
      }

      if (::timerfd_settime(timer_fd_, 0, &specification, nullptr) == -1) {
        internal::fail_system_error(
            errno, "cardio: timerfd_settime failed for Android dispatcher");
      }
    }

    inline void refresh_looper_wait_snapshot() {
      auto snapshot = dispatcher::wait_snapshot{};
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
        mark_wait_polling(false);
        if (!group_->should_exit_immediately(active_shutdown_mode_)) {
          snapshot = make_wait_snapshot();
          if (!snapshot.empty()) {
            mark_wait_polling(true);
          }
        }
      }

      install_wait_registrations(snapshot);
      arm_timer(snapshot.deadline);
      looper_wait_snapshot_ = std::move(snapshot);
      looper_wait_snapshot_valid_ = !looper_wait_snapshot_.empty();
    }

    inline void drain_event_fd() noexcept {
      auto value = eventfd_t{};
      while (eventfd_read(event_fd_, &value) == 0) {
      }
    }

    inline void drain_timer_fd() noexcept {
      auto expirations = std::uint64_t{};
      while (true) {
        const auto result = ::read(
            timer_fd_, &expirations, sizeof(expirations));
        if (result == static_cast<ssize_t>(sizeof(expirations))) {
          continue;
        }
        if (result == -1 && errno == EINTR) {
          continue;
        }
        return;
      }
    }

    inline void copy_looper_revents(int fd, int events) noexcept {
      if (!looper_wait_snapshot_valid_) {
        return;
      }

      const auto revents = from_looper_events(events);
      for (auto& current : looper_wait_snapshot_.poll_fds) {
        if (current.fd == fd) {
          current.revents |= revents;
        }
      }
      for (auto index = std::size_t{0};
           index < looper_wait_snapshot_.fd_snapshot.poll_fds.size(); ++index) {
        looper_wait_snapshot_.fd_snapshot.poll_fds[index].revents =
            looper_wait_snapshot_.poll_fds[index].revents;
      }
    }

    inline work_item take_next_work() {
      auto work = work_item{};
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (!group_->should_exit_immediately(active_shutdown_mode_) &&
          !queue_.empty()) {
        mark_wait_polling(false);
        work = std::move(queue_.front());
        queue_.pop_front();
        ++active_continuations_;
      }
      return work;
    }

    inline bool has_queued_work() noexcept {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      return !queue_.empty();
    }

    inline void execute_android_work(work_item& work) {
      group_->start_continuation();
      auto active_scope = active_continuation_guard(*this);
      auto execution = execution_scope(*this);
      (void)execution;
      execute_work_body(work);
      drain_inline_continuations({}, active_shutdown_mode_);
    }

    inline int dispatch_looper_event_impl(
        std::uintptr_t token,
        int fd,
        int events) {
      ensure_owner_thread();

      if (token == event_registration_.token && fd == event_fd_) {
        drain_event_fd();
      } else if (token == timer_registration_.token && fd == timer_fd_) {
        drain_timer_fd();
      } else {
        auto matched = false;
        for (const auto& registration : wait_registrations_) {
          if (registration.token == token && registration.fd == fd) {
            matched = true;
            break;
          }
        }
        if (!matched) {
          return 0;
        }
        copy_looper_revents(fd, events);
      }

      if (looper_wait_snapshot_valid_) {
        auto snapshot = std::move(looper_wait_snapshot_);
        looper_wait_snapshot_valid_ = false;
        (void)collect_ready_wait_snapshot(snapshot);
      } else {
        (void)collect_ready_timer_waits();
      }

      auto work = take_next_work();
      if (work) {
        execute_android_work(work);
      }

      refresh_looper_wait_snapshot();
      if (!group_->should_exit_immediately(active_shutdown_mode_) &&
          has_queued_work()) {
        notify_external_event();
      }
      return 1;
    }

    inline int dispatch_looper_event_noexcept(
        std::uintptr_t token,
        int fd,
        int events) noexcept {
#if CARDIO_HAS_EXCEPTIONS
      try {
        return dispatch_looper_event_impl(token, fd, events);
      } catch (...) {
        handle_unhandled_exception(std::current_exception());
        return 1;
      }
#else
      return dispatch_looper_event_impl(token, fd, events);
#endif
    }

    static inline int looper_callback(
        int fd,
        int events,
        void* data) noexcept {
      const auto token = reinterpret_cast<std::uintptr_t>(data);
      auto* owner = internal::find_android_looper_callback(token);
      if (owner == nullptr) {
        return 0;
      }
      return owner->dispatch_looper_event_noexcept(token, fd, events);
    }

    inline void detach_looper() noexcept {
      for (auto& registration : wait_registrations_) {
        remove_looper_registration(registration);
      }
      wait_registrations_.clear();
      remove_looper_registration(event_registration_);
      remove_looper_registration(timer_registration_);
      looper_wait_snapshot_ = dispatcher::wait_snapshot{};
      looper_wait_snapshot_valid_ = false;

      if (event_fd_ >= 0) {
        (void)::close(event_fd_);
        event_fd_ = -1;
      }
      if (timer_fd_ >= 0) {
        (void)::close(timer_fd_);
        timer_fd_ = -1;
      }
      if (looper_ != nullptr) {
        ALooper_release(looper_);
        looper_ = nullptr;
      }
    }

    inline void initialize_looper(bool prepare_if_missing) {
      owner_thread_ = ::cardio::internal::current_thread_id();
      looper_ = ALooper_forThread();
      if (looper_ == nullptr && prepare_if_missing) {
        looper_ = ALooper_prepare(0);
      }
      if (looper_ == nullptr) {
        internal::fail_runtime_error(
            "cardio: current thread does not have an Android Looper");
      }
      ALooper_acquire(looper_);

#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (event_fd_ == -1) {
          internal::fail_system_error(
              errno, "cardio: eventfd failed for Android dispatcher");
        }
        timer_fd_ = timerfd_create(
            CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fd_ == -1) {
          internal::fail_system_error(
              errno, "cardio: timerfd_create failed for Android dispatcher");
        }

        event_registration_ = add_looper_registration(
            event_fd_, ALOOPER_EVENT_INPUT);
        timer_registration_ = add_looper_registration(
            timer_fd_, ALOOPER_EVENT_INPUT);
        refresh_looper_wait_snapshot();
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        detach_looper();
        throw;
      }
#endif
    }

  protected:
    inline explicit dispatcher_host_android_base(bool prepare_if_missing)
        : dispatcher() {
      initialize_looper(prepare_if_missing);
    }

    inline dispatcher_host_android_base(
        dispatcher_group& group,
        bool prepare_if_missing)
        : dispatcher(group) {
      initialize_looper(prepare_if_missing);
    }

    inline ~dispatcher_host_android_base() override {
      detach_looper();
    }

    inline void park_looper(
        shutdown_mode mode = shutdown_mode::gentle) {
      ensure_owner_thread();
      if (parked_) {
        internal::fail_runtime_error(
            "cardio: Android dispatcher is already parked");
      }

      struct park_scope {
        dispatcher_host_android_base& owner;

        inline explicit park_scope(
            dispatcher_host_android_base& owner) noexcept
            : owner(owner) {
          owner.parked_ = true;
        }

        inline ~park_scope() noexcept {
          owner.parked_ = false;
          owner.active_shutdown_mode_ = shutdown_mode::gentle;
        }
      } scope(*this);
      (void)scope;

      active_shutdown_mode_ = mode;
      while (true) {
        auto timeout_milliseconds = -1;
        if (group_->is_shutdown_requested()) {
          if (mode == shutdown_mode::unsafe_immediate) {
            return;
          }
          if (group_->work_drained()) {
            timeout_milliseconds = 0;
          }
        } else if (group_->should_exit(mode)) {
          return;
        }

        const auto result = ALooper_pollOnce(
            timeout_milliseconds, nullptr, nullptr, nullptr);
        if (result == ALOOPER_POLL_ERROR) {
          internal::fail_runtime_error(
              "cardio: ALooper_pollOnce failed for Android dispatcher");
        }
        if (timeout_milliseconds == 0 &&
            result == ALOOPER_POLL_TIMEOUT && group_->work_drained()) {
          return;
        }
      }
    }

    inline void notify_external_event() noexcept override {
      if (event_fd_ >= 0) {
        while (eventfd_write(event_fd_, 1) == -1 && errno == EINTR) {
        }
      }
      if (looper_ != nullptr) {
        ALooper_wake(looper_);
      }
    }

    dispatcher_host_android_base(const dispatcher_host_android_base&) = delete;
    dispatcher_host_android_base& operator=(
        const dispatcher_host_android_base&) = delete;
  };
}  // namespace internal

/**
 * Dispatches continuations together with an Android native Looper.
 *
 * @remarks
 * The host attaches to the current thread's ALooper or prepares one when the
 * thread has none. Construction, park(), and destruction must all occur on the
 * same native loop-owning thread. Do not call park() on a Java UI thread; use
 * dispatcher_host_android_auto there instead. Existing callback-based ALooper
 * file descriptor registrations continue to be dispatched by park().
 */
class dispatcher_host_android final
    : public internal::dispatcher_host_android_base {
public:
  /**
   * Creates an Android dispatcher host with an implicit dispatcher group.
   *
   * @throws std::system_error Thrown when eventfd or timerfd initialization
   * fails.
   * @throws std::runtime_error Thrown when ALooper initialization fails.
   */
  inline dispatcher_host_android()
      : internal::dispatcher_host_android_base(true) {
  }

  /**
   * Creates an Android dispatcher host in an existing dispatcher group.
   *
   * @param group Dispatcher group shared with other dispatchers.
   * @throws std::system_error Thrown when eventfd or timerfd initialization
   * fails.
   * @throws std::runtime_error Thrown when ALooper initialization fails.
   */
  inline explicit dispatcher_host_android(dispatcher_group& group)
      : internal::dispatcher_host_android_base(group, true) {
  }

  /**
   * Destroys the Android dispatcher host on its owner thread.
   */
  inline ~dispatcher_host_android() override = default;

  /**
   * Parks the owner thread and dispatches ALooper and cardio work.
   *
   * @param mode Shutdown mode for this park call.
   * @throws std::runtime_error Thrown when called from a non-owner thread or
   * while the host is already parked.
   */
  inline void park(shutdown_mode mode = shutdown_mode::gentle) {
    park_looper(mode);
  }

  dispatcher_host_android(const dispatcher_host_android&) = delete;
  dispatcher_host_android& operator=(const dispatcher_host_android&) = delete;
};

//-----------------------------------------------------------------------------------------------

/**
 * Dispatches continuations automatically from an existing Android Looper.
 *
 * @remarks
 * The current thread must already own an ALooper, as Java UI threads do. The
 * host attaches callback-based file descriptor registrations to that Looper
 * and does not expose park(), so the application keeps ownership of its message
 * loop. Construction and destruction must occur on the same Looper thread.
 */
class dispatcher_host_android_auto final
    : public internal::dispatcher_host_android_base {
public:
  /**
   * Creates an automatic Android dispatcher host with an implicit group.
   *
   * @throws std::system_error Thrown when eventfd or timerfd initialization
   * fails.
   * @throws std::runtime_error Thrown when the current thread has no ALooper.
   */
  inline dispatcher_host_android_auto()
      : internal::dispatcher_host_android_base(false) {
  }

  /**
   * Creates an automatic Android dispatcher host in an existing group.
   *
   * @param group Dispatcher group shared with other dispatchers.
   * @throws std::system_error Thrown when eventfd or timerfd initialization
   * fails.
   * @throws std::runtime_error Thrown when the current thread has no ALooper.
   */
  inline explicit dispatcher_host_android_auto(dispatcher_group& group)
      : internal::dispatcher_host_android_base(group, false) {
  }

  /**
   * Detaches the automatic Android dispatcher host on its owner thread.
   */
  inline ~dispatcher_host_android_auto() override = default;

  dispatcher_host_android_auto(const dispatcher_host_android_auto&) = delete;
  dispatcher_host_android_auto& operator=(
      const dispatcher_host_android_auto&) = delete;
};

//-----------------------------------------------------------------------------------------------

#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_WIN32_HANDLE
/**
 * Dispatches continuations from Win32 messages.
 *
 * @remarks
 * This host creates a message-only window on the constructing thread. Posted
 * callbacks, continuations, timers, and Win32 handle waits are represented as
 * private window messages so an application-owned Win32 message pump can execute
 * cardio work. park() must be called on the constructing thread.
 */
class dispatcher_host_win32_auto final : public dispatcher {
private:
  HWND message_window_ = nullptr;
  ::cardio::internal::thread wait_worker_;
  bool stop_wait_worker_ = false;
  bool shutdown_wait_probe_done_ = false;
  std::atomic<bool> dispatch_message_posted_ = false;

  class execution_scope {
  private:
    dispatcher* previous_dispatcher_ = nullptr;
    bool installed_dispatcher_ = false;

  public:
    inline explicit execution_scope(dispatcher_host_win32_auto& owner)
        : previous_dispatcher_(internal::runtime_state().current_dispatcher) {
      if (previous_dispatcher_ != nullptr && previous_dispatcher_ != &owner) {
        internal::fail_runtime_error(
            "cardio: active dispatcher does not match Win32 dispatcher message");
      }

      if (previous_dispatcher_ == nullptr) {
        internal::runtime_state().current_dispatcher = &owner;
        installed_dispatcher_ = true;
      }
    }

    inline ~execution_scope() {
      if (installed_dispatcher_) {
        internal::runtime_state().current_dispatcher = previous_dispatcher_;
      }
    }

    execution_scope(const execution_scope&) = delete;
    execution_scope& operator=(const execution_scope&) = delete;
  };

  static inline UINT dispatch_message_id() noexcept {
    return WM_APP + 0x4c3;
  }

  static inline const wchar_t* window_class_name() noexcept {
    return L"cardio_dispatcher_host_win32_auto";
  }

  static inline void register_window_class() {
    auto window_class = WNDCLASSW{};
    window_class.lpfnWndProc = &dispatcher_host_win32_auto::window_proc;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = window_class_name();

    const auto atom = ::RegisterClassW(&window_class);
    if (atom == 0 &&
        ::GetLastError() != static_cast<DWORD>(ERROR_CLASS_ALREADY_EXISTS)) {
      internal::fail_win32_error(
          ::GetLastError(), "cardio: RegisterClassW failed");
    }
  }

  static inline dispatcher_host_win32_auto* window_owner(HWND window) noexcept {
    return reinterpret_cast<dispatcher_host_win32_auto*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
  }

  static LRESULT CALLBACK window_proc(
      HWND window,
      UINT message,
      WPARAM wparam,
      LPARAM lparam) {
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      (void)::SetWindowLongPtrW(
          window,
          GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      return TRUE;
    }

    auto* owner = window_owner(window);
    if (owner != nullptr && message == dispatch_message_id()) {
      owner->dispatch_window_message_noexcept();
      return 0;
    }

    if (message == WM_NCDESTROY) {
      (void)::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }

    return ::DefWindowProcW(window, message, wparam, lparam);
  }

  inline void create_message_window() {
    register_window_class();
    message_window_ = ::CreateWindowExW(
        0,
        window_class_name(),
        L"",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        ::GetModuleHandleW(nullptr),
        this);
    if (message_window_ == nullptr) {
      internal::fail_win32_error(
          ::GetLastError(), "cardio: CreateWindowExW failed");
    }
  }

  inline void notify_external_event() noexcept override {
    if (message_window_ == nullptr) {
      return;
    }

    auto expected = false;
    if (!dispatch_message_posted_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
      return;
    }

    if (::PostMessageW(message_window_, dispatch_message_id(), 0, 0) == 0) {
      dispatch_message_posted_.store(false, std::memory_order_release);
    }
  }

  inline work_item take_message_work() {
    auto work = work_item{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (!group_->should_exit_immediately(shutdown_mode::gentle) &&
          !queue_.empty()) {
        work = std::move(queue_.front());
        queue_.pop_front();
        ++active_continuations_;
      }
    }
    return work;
  }

  inline void execute_message_work(work_item& work) {
    group_->start_continuation();
    auto active_scope = active_continuation_guard(*this);
    auto execution = execution_scope(*this);
    (void)execution;
    execute_work_body(work);
    drain_inline_continuations();
  }

  inline void dispatch_window_message_impl() {
    if (::GetCurrentThreadId() != owner_thread_id_) {
      internal::fail_runtime_error(
          "cardio: Win32 auto dispatcher message ran on an unexpected thread");
    }

    while (!group_->should_exit_immediately(shutdown_mode::gentle)) {
      auto work = take_message_work();
      if (!work) {
        break;
      }
      execute_message_work(work);
    }
  }

  inline void dispatch_window_message_noexcept() noexcept {
    dispatch_message_posted_.store(false, std::memory_order_release);
#if CARDIO_HAS_EXCEPTIONS
    try {
      dispatch_window_message_impl();
    } catch (...) {
      handle_unhandled_exception(std::current_exception());
    }
#else
    dispatch_window_message_impl();
#endif

    auto has_more_work = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      has_more_work =
          !group_->should_exit_immediately(shutdown_mode::gentle) &&
          !queue_.empty();
    }
    if (has_more_work) {
      notify_external_event();
    }
  }

  inline bool make_worker_wait_snapshot(
      wait_snapshot& snapshot,
      int& timeout_milliseconds) {
    auto lock = std::unique_lock<::cardio::internal::mutex>(mutex_);
    condition_.wait(lock, [this] {
      return stop_wait_worker_ ||
             (!wait_polling_ && !waits_empty() &&
              (!group_->is_shutdown_requested() || !shutdown_wait_probe_done_));
    });
    if (stop_wait_worker_) {
      return false;
    }
    if (group_->should_exit_immediately(shutdown_mode::gentle)) {
      shutdown_wait_probe_done_ = true;
      return true;
    }

    snapshot = make_wait_snapshot();
    if (snapshot.empty()) {
      return true;
    }

    mark_wait_polling(true);
    timeout_milliseconds = timeout_until(snapshot.deadline);
    if (group_->is_shutdown_requested()) {
      timeout_milliseconds = min_wait_timeout(timeout_milliseconds, 0);
    } else {
      shutdown_wait_probe_done_ = false;
    }
    return true;
  }

  inline void wait_worker_loop_impl() {
    while (true) {
      auto snapshot = wait_snapshot{};
      auto timeout_milliseconds = -1;
      if (!make_worker_wait_snapshot(snapshot, timeout_milliseconds)) {
        return;
      }
      if (snapshot.empty()) {
        continue;
      }

#if CARDIO_HAS_EXCEPTIONS
      try {
        wait_for_snapshot(snapshot, timeout_milliseconds);
      } catch (...) {
        clear_wait_polling();
        throw;
      }
#else
      wait_for_snapshot(snapshot, timeout_milliseconds);
#endif

      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
        if (stop_wait_worker_) {
          mark_wait_polling(false);
          return;
        }
      }

      if (group_->should_exit_immediately(shutdown_mode::gentle)) {
        clear_wait_polling();
        notify_external_event();
        continue;
      }

      const auto collected = collect_ready_wait_snapshot(snapshot);
      if (group_->is_shutdown_requested() && timeout_milliseconds == 0) {
        auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
        shutdown_wait_probe_done_ = true;
      }
      if (collected || group_->should_exit(shutdown_mode::gentle)) {
        notify_external_event();
      }
    }
  }

  inline void wait_worker_loop() noexcept {
#if CARDIO_HAS_EXCEPTIONS
    try {
      wait_worker_loop_impl();
    } catch (...) {
      handle_unhandled_exception(std::current_exception());
      notify_external_event();
    }
#else
    wait_worker_loop_impl();
#endif
  }

public:
  /**
   * Creates a Win32 auto dispatcher host.
   *
   * @throws std::system_error Thrown if the message window cannot be created.
   *
   * @remarks
   * This overload creates an implicit dispatcher_group for single-dispatcher
   * use.
   */
  inline dispatcher_host_win32_auto()
      : dispatcher() {
    owner_thread_id_ = ::GetCurrentThreadId();
    create_message_window();
    wait_worker_ = ::cardio::internal::thread([this] { wait_worker_loop(); });
  }

  /**
   * Creates a Win32 auto dispatcher host in an existing dispatcher group.
   *
   * @param group Dispatcher group shared with other dispatchers.
   *
   * @throws std::system_error Thrown if the message window cannot be created.
   */
  inline explicit dispatcher_host_win32_auto(dispatcher_group& group)
      : dispatcher(group) {
    owner_thread_id_ = ::GetCurrentThreadId();
    create_message_window();
    wait_worker_ = ::cardio::internal::thread([this] { wait_worker_loop(); });
  }

  /**
   * Destroys the Win32 auto dispatcher host.
   */
  inline ~dispatcher_host_win32_auto() override {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      stop_wait_worker_ = true;
    }
    condition_.notify_all();
    win32_handle_waits_.notify();
    notify_external_event();
    if (wait_worker_.joinable()) {
      wait_worker_.join();
    }
    if (message_window_ != nullptr) {
      (void)::DestroyWindow(message_window_);
      message_window_ = nullptr;
    }
  }

  /**
   * Parks the constructing thread and dispatches Win32 messages.
   *
   * @remarks
   * Win32 message queue ordering determines when cardio work runs.
   */
  inline void park() {
    if (::GetCurrentThreadId() != owner_thread_id_) {
      internal::fail_runtime_error(
          "cardio: Win32 auto dispatcher must be parked on its owner thread");
    }

    notify_external_event();
    while (true) {
      if (group_->should_exit(shutdown_mode::gentle)) {
        return;
      }

      auto message = MSG{};
      const auto result = ::GetMessageW(&message, nullptr, 0, 0);
      if (result == -1) {
        internal::fail_win32_error(
            ::GetLastError(), "cardio: GetMessageW failed");
      }
      if (result == 0) {
        group_->shutdown();
        ::PostQuitMessage(static_cast<int>(message.wParam));
        return;
      }

      (void)::TranslateMessage(&message);
      (void)::DispatchMessageW(&message);
    }
  }

  dispatcher_host_win32_auto(const dispatcher_host_win32_auto&) = delete;
  dispatcher_host_win32_auto& operator=(const dispatcher_host_win32_auto&) =
      delete;
};

//-----------------------------------------------------------------------------------------------

#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_WITH_GLIB
/**
 * Dispatcher host that drives continuations together with a GLib main context.
 *
 * @remarks
 * The dispatcher uses g_main_context_prepare(), g_main_context_query(),
 * g_poll(), g_main_context_check(), and g_main_context_dispatch() to wait for
 * GLib sources and dispatcher work in one blocking wait. The associated
 * dispatcher_group_glib must be shut down explicitly.
 */
class dispatcher_host_glib final : public dispatcher {
private:
  dispatcher_group_glib& glib_group_;

  class context_scope {
  private:
    GMainContext* context_ = nullptr;

  public:
    inline explicit context_scope(GMainContext* context): context_(context) {
      if (!g_main_context_acquire(context_)) {
        internal::fail_runtime_error(
            "cardio: GLib main context is already owned");
      }
      g_main_context_push_thread_default(context_);
    }

    inline ~context_scope() {
      g_main_context_pop_thread_default(context_);
      g_main_context_release(context_);
    }

    context_scope(const context_scope&) = delete;
    context_scope& operator=(const context_scope&) = delete;
  };

  struct glib_wait_snapshot {
    std::vector<GPollFD> poll_fds;
    gint priority = G_PRIORITY_DEFAULT;
    gint timeout_milliseconds = -1;
    bool prepared = false;
  };

  struct combined_wait_result {
    bool glib_dispatched = false;
    bool dispatcher_collected = false;
  };

  inline glib_wait_snapshot make_glib_wait_snapshot(
      bool force_nonblocking) const {
    auto result = glib_wait_snapshot{};
    auto* context = glib_group_.context();
    result.prepared = g_main_context_prepare(context, &result.priority) != 0;

    result.poll_fds.resize(8);
    while (true) {
      result.timeout_milliseconds = -1;
      const auto count = g_main_context_query(
          context,
          result.priority,
          &result.timeout_milliseconds,
          result.poll_fds.data(),
          static_cast<gint>(result.poll_fds.size()));
      if (count < 0) {
        internal::fail_runtime_error("cardio: g_main_context_query failed");
      }
      if (static_cast<std::size_t>(count) <= result.poll_fds.size()) {
        result.poll_fds.resize(static_cast<std::size_t>(count));
        break;
      }
      result.poll_fds.resize(static_cast<std::size_t>(count));
    }

    if (result.prepared || force_nonblocking) {
      result.timeout_milliseconds = 0;
    }

    return result;
  }

  static inline GPollFD to_glib_poll_fd(const pollfd& fd) noexcept {
    return GPollFD{
        static_cast<gint>(fd.fd),
        static_cast<gushort>(fd.events),
        static_cast<gushort>(fd.revents)};
  }

  static inline void copy_dispatcher_revents(
      wait_snapshot& target,
      const std::vector<GPollFD>& source,
      std::size_t offset) noexcept {
    for (auto index = std::size_t{0}; index < target.poll_fds.size(); ++index) {
      target.poll_fds[index].revents =
          static_cast<short>(source[offset + index].revents);
    }

    for (auto index = std::size_t{0};
         index < target.fd_snapshot.poll_fds.size(); ++index) {
      target.fd_snapshot.poll_fds[index].revents =
          target.poll_fds[index].revents;
    }

#if CARDIO_WITH_LINUX_IO_URING
    for (auto index = std::size_t{0};
         index < target.io_uring_snapshot.poll_fds.size(); ++index) {
      target.io_uring_snapshot.poll_fds[index].revents =
          target.poll_fds[target.io_uring_offset + index].revents;
    }
#endif
  }

  inline combined_wait_result wait_and_dispatch(
      glib_wait_snapshot& glib_snapshot,
      wait_snapshot* dispatcher_snapshot,
      bool collect_dispatcher_ready,
      shutdown_mode mode) {
    auto combined_fds = glib_snapshot.poll_fds;
    const auto dispatcher_offset = combined_fds.size();
    if (dispatcher_snapshot != nullptr) {
      combined_fds.reserve(
          combined_fds.size() + dispatcher_snapshot->poll_fds.size());
      for (const auto& fd : dispatcher_snapshot->poll_fds) {
        combined_fds.push_back(to_glib_poll_fd(fd));
      }
    }

    auto timeout_milliseconds = glib_snapshot.timeout_milliseconds;
    if (dispatcher_snapshot != nullptr) {
      timeout_milliseconds = min_wait_timeout(
          timeout_milliseconds,
          timeout_until(dispatcher_snapshot->deadline));
    }

    auto poll_result = gint{};
    do {
      poll_result = combined_fds.empty()
          ? 0
          : g_poll(
                combined_fds.data(),
                static_cast<guint>(combined_fds.size()),
                timeout_milliseconds);
    } while (poll_result == -1 && errno == EINTR);

    if (poll_result == -1) {
      if (dispatcher_snapshot != nullptr) {
        clear_wait_polling();
      }
      internal::fail_system_error(errno, "cardio: g_poll failed");
    }

    for (auto index = std::size_t{0}; index < glib_snapshot.poll_fds.size();
         ++index) {
      glib_snapshot.poll_fds[index].revents = combined_fds[index].revents;
    }
    if (dispatcher_snapshot != nullptr) {
      copy_dispatcher_revents(
          *dispatcher_snapshot, combined_fds, dispatcher_offset);
    }

    auto result = combined_wait_result{};
    if (g_main_context_check(
            glib_group_.context(),
            glib_snapshot.priority,
            glib_snapshot.poll_fds.empty() ? nullptr : glib_snapshot.poll_fds.data(),
            static_cast<gint>(glib_snapshot.poll_fds.size())) != 0) {
      result.glib_dispatched = true;
    }

    if (dispatcher_snapshot != nullptr) {
      if (collect_dispatcher_ready && !group_->should_exit_immediately(mode)) {
        result.dispatcher_collected =
            collect_ready_wait_snapshot(*dispatcher_snapshot);
      } else {
        clear_wait_polling();
      }
    }

    if (result.glib_dispatched) {
      g_main_context_dispatch(glib_group_.context());
    }

    return result;
  }

  inline bool dispatch_glib_nonblocking(shutdown_mode mode = shutdown_mode::gentle) {
    auto glib_snapshot = make_glib_wait_snapshot(true);
    auto wait_result = wait_and_dispatch(glib_snapshot, nullptr, false, mode);
    return wait_result.glib_dispatched;
  }

public:
  /**
   * Creates a GLib dispatcher host in a GLib dispatcher group.
   *
   * @param group GLib dispatcher group that owns the main context.
   */
  inline explicit dispatcher_host_glib(dispatcher_group_glib& group)
      : dispatcher(group),
        glib_group_(group) {
    add_feature(dispatcher_feature::glib);
#if CARDIO_WITH_GIO
    add_feature(dispatcher_feature::gio);
#endif
  }

  /**
   * Parks the current thread and drives dispatcher work with GLib sources.
   *
   * @remarks
   * The associated dispatcher_group_glib must be shut down explicitly. After
   * shutdown, immediately ready GLib sources are drained without blocking before
   * park() returns. shutdown_mode::unsafe_immediate mode skips non-GLib
   * dispatcher work.
   *
   * @param mode Shutdown mode for this park call.
   * @param policy Ordering policy for GLib source pumping and queued or inline
   * continuation work.
   */
  inline void park(
      shutdown_mode mode = shutdown_mode::gentle,
      park_policy policy = park_policy::external_pump_first) {
    auto context_scope = dispatcher_host_glib::context_scope(glib_group_.context());

    while (true) {
      if (policy == park_policy::external_pump_first) {
        (void)dispatch_glib_nonblocking(mode);
      }

      auto work = work_item{};
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
        if (!group_->should_exit_immediately(mode) && !queue_.empty()) {
          mark_wait_polling(false);
          work = std::move(queue_.front());
          queue_.pop_front();
          ++active_continuations_;
        }
      }

      if (work) {
        if (policy == park_policy::external_pump_first) {
          auto pump_external = [this, mode] {
            (void)dispatch_glib_nonblocking(mode);
          };
          execute_dequeued_work(work, pump_external, mode);
        } else {
          execute_dequeued_work(work, {}, mode);
        }
        continue;
      }

      const auto shutdown_requested = group_->is_shutdown_requested();
      const auto immediate_shutdown = group_->should_exit_immediately(mode);
      auto wait_snapshot = dispatcher::wait_snapshot{};
      auto has_dispatcher_snapshot = false;
      if (!immediate_shutdown) {
        auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
        if (!wait_polling_) {
          wait_snapshot = make_wait_snapshot();
          if (!wait_snapshot.empty()) {
            mark_wait_polling(true);
            has_dispatcher_snapshot = true;
          }
        }
      }

      auto glib_snapshot = make_glib_wait_snapshot(shutdown_requested);
      auto wait_result = wait_and_dispatch(
          glib_snapshot,
          has_dispatcher_snapshot ? &wait_snapshot : nullptr,
          !immediate_shutdown,
          mode);

      if (group_->is_shutdown_requested()) {
        if (group_->should_exit_immediately(mode)) {
          if (!wait_result.glib_dispatched) {
            return;
          }
        } else if (
            group_->work_drained() && !wait_result.glib_dispatched &&
            !wait_result.dispatcher_collected) {
          return;
        }
      }
    }
  }

  dispatcher_host_glib(const dispatcher_host_glib&) = delete;
  dispatcher_host_glib& operator=(const dispatcher_host_glib&) = delete;
};

//-----------------------------------------------------------------------------------------------

/**
 * Dispatcher host that exposes continuations as a GLib source.
 *
 * @remarks
 * The dispatcher attaches a source to the GLib main context, so an application
 * owned GLib main loop can execute cardio continuations. The associated
 * dispatcher_group_glib must be shut down explicitly.
 */
class dispatcher_host_glib_auto final : public dispatcher {
private:
  dispatcher_group_glib& glib_group_;
  GSource* source_ = nullptr;
  GMainLoop* park_loop_ = nullptr;
  std::vector<GPollFD> glib_poll_fds_;
  wait_snapshot glib_wait_snapshot_{};
  bool glib_wait_snapshot_valid_ = false;

  struct glib_source_state {
    GSource source;
    dispatcher_host_glib_auto* owner = nullptr;
  };

  class execution_scope {
  private:
    dispatcher* previous_dispatcher_ = nullptr;
    GMainContext* context_ = nullptr;
    bool pushed_context_ = false;
    bool installed_dispatcher_ = false;

  public:
    inline execution_scope(dispatcher_host_glib_auto& owner)
        : previous_dispatcher_(internal::runtime_state().current_dispatcher),
          context_(owner.glib_group_.context()) {
      if (previous_dispatcher_ != nullptr && previous_dispatcher_ != &owner) {
        internal::fail_runtime_error(
            "cardio: active dispatcher does not match GLib source");
      }

      auto* thread_default = g_main_context_get_thread_default();
      if (thread_default != nullptr && thread_default != context_) {
        internal::fail_runtime_error(
            "cardio: active GLib thread-default context does not match GLib source");
      }
      if (thread_default == nullptr) {
        g_main_context_push_thread_default(context_);
        pushed_context_ = true;
      }

      if (previous_dispatcher_ == nullptr) {
        internal::runtime_state().current_dispatcher = &owner;
        installed_dispatcher_ = true;
      }
    }

    inline ~execution_scope() {
      if (installed_dispatcher_) {
        internal::runtime_state().current_dispatcher = previous_dispatcher_;
      }
      if (pushed_context_) {
        g_main_context_pop_thread_default(context_);
      }
    }

    execution_scope(const execution_scope&) = delete;
    execution_scope& operator=(const execution_scope&) = delete;
  };

  static inline GPollFD to_glib_poll_fd(const pollfd& fd) noexcept {
    return GPollFD{
        static_cast<gint>(fd.fd),
        static_cast<gushort>(fd.events),
        static_cast<gushort>(fd.revents)};
  }

  static inline void copy_glib_revents(
      wait_snapshot& target,
      const std::vector<GPollFD>& source) noexcept {
    for (auto index = std::size_t{0}; index < target.poll_fds.size(); ++index) {
      target.poll_fds[index].revents =
          static_cast<short>(source[index].revents);
    }

    for (auto index = std::size_t{0};
         index < target.fd_snapshot.poll_fds.size(); ++index) {
      target.fd_snapshot.poll_fds[index].revents =
          target.poll_fds[index].revents;
    }

#if CARDIO_WITH_LINUX_IO_URING
    for (auto index = std::size_t{0};
         index < target.io_uring_snapshot.poll_fds.size(); ++index) {
      target.io_uring_snapshot.poll_fds[index].revents =
          target.poll_fds[target.io_uring_offset + index].revents;
    }
#endif
  }

  static inline dispatcher_host_glib_auto* source_owner(GSource* source) noexcept {
    return static_cast<glib_source_state*>(static_cast<void*>(source))->owner;
  }

  inline void clear_glib_poll_fds() noexcept {
    if (source_ != nullptr) {
      for (auto& fd : glib_poll_fds_) {
        g_source_remove_poll(source_, &fd);
      }
    }
    glib_poll_fds_.clear();
    glib_wait_snapshot_ = wait_snapshot{};
    glib_wait_snapshot_valid_ = false;
  }

  inline bool glib_poll_fds_match(
      const wait_snapshot& snapshot) const noexcept {
    if (glib_poll_fds_.size() != snapshot.poll_fds.size()) {
      return false;
    }
    for (auto index = std::size_t{0};
         index < glib_poll_fds_.size(); ++index) {
      const auto expected = to_glib_poll_fd(snapshot.poll_fds[index]);
      if (glib_poll_fds_[index].fd != expected.fd ||
          glib_poll_fds_[index].events != expected.events) {
        return false;
      }
    }
    return true;
  }

  inline void install_glib_wait_snapshot(wait_snapshot snapshot) {
    if (snapshot.empty()) {
      clear_glib_poll_fds();
      return;
    }

    // Replacing poll registrations wakes an attached GMainContext. Preserve
    // stable registrations so an unchanged fd wait can remain blocked.
    if (!glib_poll_fds_match(snapshot)) {
      clear_glib_poll_fds();
      glib_poll_fds_.reserve(snapshot.poll_fds.size());
      for (const auto& fd : snapshot.poll_fds) {
        glib_poll_fds_.push_back(to_glib_poll_fd(fd));
      }
      for (auto& fd : glib_poll_fds_) {
        g_source_add_poll(source_, &fd);
      }
    }

    glib_wait_snapshot_ = std::move(snapshot);
    glib_wait_snapshot_valid_ = true;
    for (auto& fd : glib_poll_fds_) {
      fd.revents = 0;
    }
  }

  inline gboolean prepare_source(gint* timeout) {
    auto snapshot = wait_snapshot{};
    auto has_snapshot = false;
    auto ready = false;
    auto timeout_milliseconds = -1;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      mark_wait_polling(false);

      const auto immediate_shutdown =
          group_->should_exit_immediately(shutdown_mode::gentle);
      if (!immediate_shutdown && !waits_empty()) {
        snapshot = make_wait_snapshot();
        has_snapshot = !snapshot.empty();
        if (has_snapshot) {
          mark_wait_polling(true);
          timeout_milliseconds =
              min_wait_timeout(
                  timeout_milliseconds, timeout_until(snapshot.deadline));
        }
      }

      if (!immediate_shutdown && !queue_.empty()) {
        ready = true;
      }

      const auto timer_timeout = timeout_until(next_timer_deadline());
      timeout_milliseconds =
          min_wait_timeout(timeout_milliseconds, timer_timeout);
      if (!immediate_shutdown && timer_timeout == 0) {
        ready = true;
      }

      if (park_loop_ != nullptr && group_->is_shutdown_requested()) {
        timeout_milliseconds = min_wait_timeout(timeout_milliseconds, 0);
        if (!has_snapshot && (immediate_shutdown || group_->work_drained())) {
          ready = true;
        }
      }
    }

    if (has_snapshot) {
      install_glib_wait_snapshot(std::move(snapshot));
    } else {
      clear_glib_poll_fds();
    }

    if (timeout != nullptr) {
      *timeout = ready ? 0 : timeout_milliseconds;
    }
    return ready ? TRUE : FALSE;
  }

  inline gboolean check_source() {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      const auto immediate_shutdown =
          group_->should_exit_immediately(shutdown_mode::gentle);
      if (!immediate_shutdown && !queue_.empty()) {
        return TRUE;
      }

      const auto timer_deadline = next_timer_deadline();
      if (!immediate_shutdown && timer_deadline &&
          *timer_deadline <= std::chrono::steady_clock::now()) {
        return TRUE;
      }

      if (park_loop_ != nullptr && group_->is_shutdown_requested() &&
          (immediate_shutdown || group_->work_drained())) {
        return TRUE;
      }
    }

    for (const auto& fd : glib_poll_fds_) {
      if (fd.revents != 0) {
        return TRUE;
      }
    }

    return FALSE;
  }

  inline bool collect_glib_ready_work() {
    auto snapshot = wait_snapshot{};
    if (glib_wait_snapshot_valid_) {
      copy_glib_revents(glib_wait_snapshot_, glib_poll_fds_);
      snapshot = std::move(glib_wait_snapshot_);
      glib_wait_snapshot_valid_ = false;
    }
    clear_glib_poll_fds();
    return collect_ready_wait_snapshot(snapshot);
  }

  inline work_item take_next_work() {
    auto work = work_item{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (!group_->should_exit_immediately(shutdown_mode::gentle) &&
          !queue_.empty()) {
        mark_wait_polling(false);
        work = std::move(queue_.front());
        queue_.pop_front();
        ++active_continuations_;
      }
    }
    return work;
  }

  inline void execute_glib_dequeued_work(work_item& work) {
    group_->start_continuation();
    auto active_scope = active_continuation_guard(*this);
    auto execution = execution_scope(*this);
    (void)execution;
    execute_work_body(work);
    drain_inline_continuations();
  }

  inline void maybe_quit_park_loop() noexcept {
    if (park_loop_ != nullptr && group_->should_exit(shutdown_mode::gentle)) {
      g_main_loop_quit(park_loop_);
    }
  }

#if CARDIO_HAS_EXCEPTIONS
  inline void dispatch_source_impl() noexcept {
    try {
      if (!group_->should_exit_immediately(shutdown_mode::gentle)) {
        (void)collect_glib_ready_work();
      } else {
        clear_glib_poll_fds();
        clear_wait_polling();
      }

      auto work = take_next_work();
      if (work) {
        try {
          execute_glib_dequeued_work(work);
        } catch (...) {
          handle_unhandled_exception(std::current_exception());
        }
      }
      maybe_quit_park_loop();
    } catch (...) {
      handle_unhandled_exception(std::current_exception());
      maybe_quit_park_loop();
    }
  }
#else
  inline void dispatch_source_impl() {
    if (!group_->should_exit_immediately(shutdown_mode::gentle)) {
      (void)collect_glib_ready_work();
    } else {
      clear_glib_poll_fds();
      clear_wait_polling();
    }

    auto work = take_next_work();
    if (work) {
      execute_glib_dequeued_work(work);
    }
    maybe_quit_park_loop();
  }
#endif

  static inline gboolean source_prepare(GSource* source, gint* timeout) {
    auto* owner = source_owner(source);
#if CARDIO_HAS_EXCEPTIONS
    try {
      return owner->prepare_source(timeout);
    } catch (...) {
      owner->handle_unhandled_exception(std::current_exception());
      if (timeout != nullptr) {
        *timeout = -1;
      }
      return FALSE;
    }
#else
    return owner->prepare_source(timeout);
#endif
  }

  static inline gboolean source_check(GSource* source) {
    auto* owner = source_owner(source);
#if CARDIO_HAS_EXCEPTIONS
    try {
      return owner->check_source();
    } catch (...) {
      owner->handle_unhandled_exception(std::current_exception());
      return FALSE;
    }
#else
    return owner->check_source();
#endif
  }

  static inline gboolean source_dispatch(
      GSource* source,
      GSourceFunc callback,
      gpointer user_data) {
    (void)callback;
    (void)user_data;
    auto* owner = source_owner(source);
    owner->dispatch_source_impl();
    return G_SOURCE_CONTINUE;
  }

  static inline void source_finalize(GSource* source) {
    source_owner(source)->source_ = nullptr;
  }

  inline void create_source() {
    static GSourceFuncs funcs{
      &dispatcher_host_glib_auto::source_prepare,
      &dispatcher_host_glib_auto::source_check,
      &dispatcher_host_glib_auto::source_dispatch,
      &dispatcher_host_glib_auto::source_finalize,
      nullptr,
      nullptr,
    };

    source_ = g_source_new(&funcs, sizeof(glib_source_state));
    static_cast<glib_source_state*>(static_cast<void*>(source_))->owner = this;
    g_source_set_name(source_, "cardio dispatcher");
    g_source_set_priority(source_, G_PRIORITY_DEFAULT);
    (void)g_source_attach(source_, glib_group_.context());
  }

  inline void notify_external_event() noexcept override {
    g_main_context_wakeup(glib_group_.context());
  }

public:
  /**
   * Creates a GLib dispatcher host in a GLib dispatcher group.
   *
   * @param group GLib dispatcher group that owns the main context.
   */
  inline explicit dispatcher_host_glib_auto(dispatcher_group_glib& group)
      : dispatcher(group),
        glib_group_(group) {
    add_feature(dispatcher_feature::glib);
#if CARDIO_WITH_GIO
    add_feature(dispatcher_feature::gio);
#endif
    create_source();
  }

  /**
   * Destroys the GLib dispatcher host.
   */
  inline ~dispatcher_host_glib_auto() override {
    if (source_ != nullptr) {
      clear_glib_poll_fds();
      g_source_destroy(source_);
      g_source_unref(source_);
      source_ = nullptr;
    }
  }

  /**
   * Parks the current thread with the associated GLib main context.
   *
   * @remarks
   * The associated dispatcher_group_glib must be shut down explicitly.
   * dispatcher_host_glib_auto work is dispatched by the source attached at
   * construction time. GLib source priority determines ordering.
   */
  inline void park() {
    auto* loop = g_main_loop_new(glib_group_.context(), FALSE);
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (park_loop_ != nullptr) {
        g_main_loop_unref(loop);
        internal::fail_runtime_error("cardio: GLib dispatcher is already parked");
      }
      park_loop_ = loop;
    }
    notify_external_event();

#if CARDIO_HAS_EXCEPTIONS
    try {
      g_main_loop_run(loop);
    } catch (...) {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (park_loop_ == loop) {
        park_loop_ = nullptr;
      }
      g_main_loop_unref(loop);
      throw;
    }
#else
    g_main_loop_run(loop);
#endif

    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
      if (park_loop_ == loop) {
        park_loop_ = nullptr;
      }
    }
    g_main_loop_unref(loop);
  }

  dispatcher_host_glib_auto(const dispatcher_host_glib_auto&) = delete;
  dispatcher_host_glib_auto& operator=(const dispatcher_host_glib_auto&) = delete;
};

//-----------------------------------------------------------------------------------------------

#endif

namespace internal {
  inline cancellation_state::~cancellation_state() {
    if (!timeout_dispatcher_lifetime) {
      return;
    }

    auto lifetime_lock = std::lock_guard<::cardio::internal::mutex>(
        timeout_dispatcher_lifetime->mutex);
    if (timeout_dispatcher_lifetime->target != nullptr && timeout_wait) {
      timeout_dispatcher_lifetime->target->unregister_timer_wait(timeout_wait);
    }
    if (timeout_active) {
      timeout_dispatcher_lifetime->finish_timeout_activity();
    }
  }

  inline void configure_timeout_cancellation(
      cancellation_source& source,
      std::chrono::steady_clock::time_point deadline,
      dispatcher* target) {
    if (!source.state_) {
      return;
    }

    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(source.state_->mutex);
      source.state_->timeout_deadline = deadline;
      source.state_->timeout_dispatcher_lifetime =
          target != nullptr ? target->lifetime_ : nullptr;
    }
    arm_timeout_cancellation(source.state_);
  }

  inline bool request_cancellation(
      const std::shared_ptr<cancellation_state>& state) noexcept {
    if (!state) {
      return false;
    }

    auto callbacks = std::vector<scheduled_cancellation_callback>{};
    auto retained_registrations =
        std::vector<cancellation_registration>{};
    auto timeout_wait = std::shared_ptr<timer_wait_state>{};
    auto timeout_dispatcher_lifetime =
        std::shared_ptr<dispatcher_lifetime>{};
    auto finish_timeout_activity = false;

    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (state->cancellation_requested) {
        return false;
      }

      state->cancellation_requested = true;
      auto* entry = state->callbacks;
      while (entry != nullptr) {
        auto* next = entry->next;
        callbacks.push_back(scheduled_cancellation_callback{
            std::move(entry->callback),
            entry->target});
        entry->owner = nullptr;
        entry->previous = nullptr;
        entry->next = nullptr;
        entry->registered = false;
        entry = next;
      }
      state->callbacks = nullptr;
      retained_registrations = std::move(state->retained_registrations);
      timeout_wait = std::move(state->timeout_wait);
      timeout_dispatcher_lifetime = state->timeout_dispatcher_lifetime;
      finish_timeout_activity = state->timeout_active;
      state->timeout_active = false;
    }

    if (timeout_dispatcher_lifetime) {
      auto lifetime_lock = std::lock_guard<::cardio::internal::mutex>(
          timeout_dispatcher_lifetime->mutex);
      if (timeout_dispatcher_lifetime->target != nullptr && timeout_wait) {
        timeout_dispatcher_lifetime->target->unregister_timer_wait(
            timeout_wait);
      }
      if (finish_timeout_activity) {
        timeout_dispatcher_lifetime->finish_timeout_activity();
      }
    }

    for (auto& callback : callbacks) {
      internal::enqueue_cancellation_callback(std::move(callback));
    }
    retained_registrations.clear();

    return true;
  }

  inline void arm_timeout_cancellation(
      const std::shared_ptr<cancellation_state>& state) {
    if (!state) {
      return;
    }

    auto cancel_now = false;
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (state->cancellation_requested || !state->timeout_deadline ||
          state->timeout_wait) {
        return;
      }
      if (*state->timeout_deadline <= std::chrono::steady_clock::now()) {
        cancel_now = true;
      } else {
        auto lifetime = state->timeout_dispatcher_lifetime;
        if (!lifetime) {
          return;
        }

        auto wait = std::make_shared<timer_wait_state>();
        wait->deadline = *state->timeout_deadline;
        auto weak_state = std::weak_ptr<cancellation_state>(state);

        auto lifetime_lock = std::lock_guard<::cardio::internal::mutex>(lifetime->mutex);
        auto* target = lifetime->target;
        if (target == nullptr || !lifetime->add_timeout_activity()) {
          return;
        }

        state->timeout_wait = wait;
        state->timeout_active = true;
#if CARDIO_HAS_EXCEPTIONS
        try {
#endif
          target->register_timer_callback_wait(
              wait,
              [weak_state = std::move(weak_state)] {
                if (auto locked = weak_state.lock()) {
                  (void)request_cancellation(locked);
                }
              });
#if CARDIO_HAS_EXCEPTIONS
        } catch (...) {
          state->timeout_wait.reset();
          state->timeout_active = false;
          target->unregister_timer_wait(wait);
          lifetime->finish_timeout_activity();
          throw;
        }
#endif
      }
    }

    if (cancel_now) {
      (void)request_cancellation(state);
    }
  }

  inline void disarm_timeout_cancellation(
      const std::shared_ptr<cancellation_state>& state) noexcept {
    if (!state) {
      return;
    }

    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (state->cancellation_requested || state->callbacks != nullptr ||
        !state->timeout_wait) {
      return;
    }

    auto wait = std::move(state->timeout_wait);
    auto lifetime = state->timeout_dispatcher_lifetime;
    const auto finish_activity = state->timeout_active;
    state->timeout_active = false;
    if (lifetime) {
      auto lifetime_lock = std::lock_guard<::cardio::internal::mutex>(lifetime->mutex);
      if (lifetime->target != nullptr) {
        lifetime->target->unregister_timer_wait(wait);
      }
      if (finish_activity) {
        lifetime->finish_timeout_activity();
      }
    }
  }

  inline void enqueue_cancellation_callback(
      std::shared_ptr<cancellation_callback_entry> entry) noexcept {
    if (!entry || entry->target == nullptr) {
      return;
    }

#if CARDIO_HAS_EXCEPTIONS
    try {
#endif
      auto* target = entry->target;
      internal::dangerous_schedule_later__(target, [entry = std::move(entry)] {
        internal::invoke_cancellation_callback(entry->callback);
      });
#if CARDIO_HAS_EXCEPTIONS
    } catch (...) {
    }
#endif
  }

  inline void enqueue_cancellation_callback(
      scheduled_cancellation_callback callback) noexcept {
    if (!callback.callback || callback.target == nullptr) {
      return;
    }

#if CARDIO_HAS_EXCEPTIONS
    try {
#endif
      auto* target = callback.target;
      internal::dangerous_schedule_later__(
          target,
          [callback = std::move(callback.callback)] {
            internal::invoke_cancellation_callback(callback);
          });
#if CARDIO_HAS_EXCEPTIONS
    } catch (...) {
    }
#endif
  }
}  // namespace internal

inline void dispatcher_group::notify_all() noexcept {
  auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
  for (auto* target : dispatchers_) {
    if (target != nullptr) {
      target->notify();
    }
  }
}

inline void dispatcher_group::notify_if_empty() noexcept {
  if (should_notify_exit()) {
    notify_all();
  }
}

inline void dispatcher_group::register_dispatcher(dispatcher* target) {
  auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
  const auto set_current = dispatchers_.empty();
  dispatchers_.push_back(target);
  if (set_current) {
    internal::runtime_state().current_dispatcher = target;
  }
}

inline void dispatcher_group::unregister_dispatcher(
    dispatcher* target) noexcept {
  auto lock = std::lock_guard<::cardio::internal::mutex>(mutex_);
  for (auto iterator = dispatchers_.begin();
       iterator != dispatchers_.end(); ++iterator) {
    if (*iterator == target) {
      dispatchers_.erase(iterator);
      return;
    }
  }
}

namespace internal {
  inline void activate_current_promise(promise_state_base& state) {
    auto& current = internal::require_current_dispatcher();
    state.group = &current.group();
    state.group_lifetime = state.group->lifetime_;
    state.active_promise.store(true, std::memory_order_release);
    state.group->add_active_promise();
  }

  inline void finish_promise(promise_state_base& state) noexcept {
    if (state.active_promise.exchange(false, std::memory_order_acq_rel)) {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state.group_lifetime->mutex);
      if (state.group_lifetime->group != nullptr) {
        state.group_lifetime->group->finish_active_promise();
      }
    }
  }

  inline void enqueue_continuation(scheduled_continuation continuation) {
    if (continuation) {
      continuation.target->enqueue(continuation.continuation);
    }
  }

  inline scheduled_continuation finish_completed_promise(
      promise_state_base& state) noexcept {
    internal::finish_promise(state);
    return internal::try_schedule_continuation(state);
  }

  struct switch_to_awaiter {
    dispatcher* target;

    inline explicit switch_to_awaiter(dispatcher& target) noexcept
        : target(&target) {}

    inline bool await_ready() const {
      return target == &internal::require_current_dispatcher();
    }

    template <typename ContinuationPromise>
    inline bool await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      auto& current = internal::require_current_dispatcher();
      if (target == &current) {
        return false;
      }
      if (&target->group() != &current.group()) {
        internal::fail_runtime_error(
            "cardio: cannot switch to a dispatcher in another group");
      }

      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      target->enqueue(
          std::coroutine_handle<>::from_address(continuation.address()));
      return true;
    }

    inline void await_resume() const noexcept {}
  };

  struct timer_awaiter {
    std::shared_ptr<timer_wait_state> state;
#if CARDIO_HAS_EXCEPTIONS
    cancellation cancellation_;
#endif

    inline explicit timer_awaiter(
        std::chrono::steady_clock::time_point deadline)
        : state(std::make_shared<timer_wait_state>()) {
      state->deadline = deadline;
    }

#if CARDIO_HAS_EXCEPTIONS
    inline timer_awaiter(
        std::chrono::steady_clock::time_point deadline,
        cancellation cancellation)
        : timer_awaiter(deadline) {
      cancellation_ = std::move(cancellation);
    }
#endif

    timer_awaiter(const timer_awaiter&) = delete;
    timer_awaiter& operator=(const timer_awaiter&) = delete;

    inline timer_awaiter(timer_awaiter&& other) noexcept
        : state(std::move(other.state))
#if CARDIO_HAS_EXCEPTIONS
        , cancellation_(std::move(other.cancellation_))
#endif
    {}

    inline timer_awaiter& operator=(timer_awaiter&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      if (state && state->owner) {
        state->owner->unregister_timer_wait(state);
      }
      state = std::move(other.state);
#if CARDIO_HAS_EXCEPTIONS
      cancellation_ = std::move(other.cancellation_);
#endif
      return *this;
    }

    inline ~timer_awaiter() {
#if CARDIO_HAS_EXCEPTIONS
      if (state) {
        state->cancellation_registration_.reset();
      }
#endif
      if (state && state->owner) {
        state->owner->unregister_timer_wait(state);
      }
    }

    inline bool await_ready() const noexcept {
      return state &&
             state->deadline <= std::chrono::steady_clock::now();
    }

    template <typename ContinuationPromise>
    inline bool await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto& current = internal::require_current_dispatcher();

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());

#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_.is_cancellation_requested()) {
        state->canceled = true;
        current.enqueue(continuation_handle);
        return true;
      }
#endif

      current.register_timer_wait(state, continuation_handle);

#if CARDIO_HAS_EXCEPTIONS
      (void)cancellation_.try_register_callback(
          [target = &current, wait = state] {
            wait->cancellation_registration_.reset();
            target->cancel_timer_wait(wait);
          },
          state->cancellation_registration_);
#endif

      return true;
    }

    inline void await_resume() {
#if CARDIO_HAS_EXCEPTIONS
      if (state->canceled) {
        throw canceled_exception();
      }
#endif
    }
  };

#if CARDIO_HAS_POSIX_FD
  struct fd_awaiter {
    std::shared_ptr<fd_wait_state> state;
#if CARDIO_HAS_EXCEPTIONS
    cancellation cancellation_;
#endif

    inline fd_awaiter(int fd, fd_event interests)
        : state(std::make_shared<fd_wait_state>()) {
      state->fd = fd;
      state->interests = interests;
    }

#if CARDIO_HAS_EXCEPTIONS
    inline fd_awaiter(
        int fd,
        fd_event interests,
        cancellation cancellation)
        : fd_awaiter(fd, interests) {
      cancellation_ = std::move(cancellation);
    }
#endif

    fd_awaiter(const fd_awaiter&) = delete;
    fd_awaiter& operator=(const fd_awaiter&) = delete;

    inline fd_awaiter(fd_awaiter&& other) noexcept
        : state(std::move(other.state))
#if CARDIO_HAS_EXCEPTIONS
        , cancellation_(std::move(other.cancellation_))
#endif
    {}

    inline fd_awaiter& operator=(fd_awaiter&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      if (state && state->owner) {
        state->owner->unregister_fd_wait(state);
      }
      state = std::move(other.state);
#if CARDIO_HAS_EXCEPTIONS
      cancellation_ = std::move(other.cancellation_);
#endif
      return *this;
    }

    inline ~fd_awaiter() {
#if CARDIO_HAS_EXCEPTIONS
      if (state) {
        state->cancellation_registration_.reset();
      }
#endif
      if (state && state->owner) {
        state->owner->unregister_fd_wait(state);
      }
    }

    inline bool await_ready() noexcept {
      return false;
    }

    template <typename ContinuationPromise>
    inline bool await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto& current = internal::require_current_dispatcher();

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());

#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_.is_cancellation_requested()) {
        state->canceled = true;
        current.enqueue(continuation_handle);
        return true;
      }
#endif

      current.register_fd_wait(state, continuation_handle);

#if CARDIO_HAS_EXCEPTIONS
      (void)cancellation_.try_register_callback(
          [target = &current, wait = state] {
            wait->cancellation_registration_.reset();
            target->cancel_fd_wait(wait);
          },
          state->cancellation_registration_);
#endif

      return true;
    }

    inline fd_event await_resume() {
#if CARDIO_HAS_EXCEPTIONS
      if (state->canceled) {
        throw canceled_exception();
      }
#endif
      return state->result;
    }
  };
#endif

#if CARDIO_HAS_WIN32_HANDLE
  struct win32_handle_awaiter {
    std::shared_ptr<win32_wait_state> state;
#if CARDIO_HAS_EXCEPTIONS
    cancellation cancellation_;
#endif

    inline explicit win32_handle_awaiter(HANDLE handle)
        : state(std::make_shared<win32_wait_state>()) {
      state->wait_handle = handle;
    }

#if CARDIO_HAS_EXCEPTIONS
    inline win32_handle_awaiter(HANDLE handle, cancellation cancellation)
        : win32_handle_awaiter(handle) {
      cancellation_ = std::move(cancellation);
    }
#endif

    win32_handle_awaiter(const win32_handle_awaiter&) = delete;
    win32_handle_awaiter& operator=(const win32_handle_awaiter&) = delete;

    inline win32_handle_awaiter(win32_handle_awaiter&& other) noexcept
        : state(std::move(other.state))
#if CARDIO_HAS_EXCEPTIONS
        , cancellation_(std::move(other.cancellation_))
#endif
    {}

    inline win32_handle_awaiter& operator=(
        win32_handle_awaiter&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      if (state && state->owner) {
        state->owner->unregister_win32_handle_wait(state);
      }
      state = std::move(other.state);
#if CARDIO_HAS_EXCEPTIONS
      cancellation_ = std::move(other.cancellation_);
#endif
      return *this;
    }

    inline ~win32_handle_awaiter() {
#if CARDIO_HAS_EXCEPTIONS
      if (state) {
        state->cancellation_registration_.reset();
      }
#endif
      if (state && state->owner) {
        state->owner->unregister_win32_handle_wait(state);
      }
    }

    inline bool await_ready() noexcept {
      return false;
    }

    template <typename ContinuationPromise>
    inline bool await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto& current = internal::require_current_dispatcher();

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());

#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_.is_cancellation_requested()) {
        state->canceled = true;
        current.enqueue(continuation_handle);
        return true;
      }
#endif

      current.register_win32_handle_wait(state, continuation_handle);

#if CARDIO_HAS_EXCEPTIONS
      (void)cancellation_.try_register_callback(
          [target = &current, wait = state] {
            wait->cancellation_registration_.reset();
            target->cancel_win32_handle_wait(wait);
          },
          state->cancellation_registration_);
#endif

      return true;
    }

    inline win32_handle_event await_resume() {
#if CARDIO_HAS_EXCEPTIONS
      if (state->canceled) {
        throw canceled_exception();
      }
#endif
      return state->result;
    }
  };

  struct win32_overlapped_awaiter {
    std::shared_ptr<win32_wait_state> state;
#if CARDIO_HAS_EXCEPTIONS
    cancellation cancellation_;
#endif

    inline win32_overlapped_awaiter(HANDLE handle, OVERLAPPED& overlapped)
        : state(std::make_shared<win32_wait_state>()) {
      state->wait_handle =
          overlapped.hEvent != nullptr ? overlapped.hEvent : handle;
      state->operation_handle = handle;
      state->overlapped = &overlapped;
    }

#if CARDIO_HAS_EXCEPTIONS
    inline win32_overlapped_awaiter(
        HANDLE handle,
        OVERLAPPED& overlapped,
        cancellation cancellation)
        : win32_overlapped_awaiter(handle, overlapped) {
      cancellation_ = std::move(cancellation);
    }
#endif

    win32_overlapped_awaiter(const win32_overlapped_awaiter&) = delete;
    win32_overlapped_awaiter& operator=(const win32_overlapped_awaiter&) =
        delete;

    inline win32_overlapped_awaiter(
        win32_overlapped_awaiter&& other) noexcept
        : state(std::move(other.state))
#if CARDIO_HAS_EXCEPTIONS
        , cancellation_(std::move(other.cancellation_))
#endif
    {}

    inline win32_overlapped_awaiter& operator=(
        win32_overlapped_awaiter&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      if (state && state->owner) {
        state->owner->unregister_win32_handle_wait(state);
      }
      state = std::move(other.state);
#if CARDIO_HAS_EXCEPTIONS
      cancellation_ = std::move(other.cancellation_);
#endif
      return *this;
    }

    inline ~win32_overlapped_awaiter() {
#if CARDIO_HAS_EXCEPTIONS
      if (state) {
        state->cancellation_registration_.reset();
      }
#endif
      if (state && state->owner) {
        state->owner->unregister_win32_handle_wait(state);
      }
    }

    inline bool await_ready() noexcept {
      return false;
    }

    template <typename ContinuationPromise>
    inline bool await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto& current = internal::require_current_dispatcher();

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());

#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_.is_cancellation_requested()) {
        state->canceled = true;
        current.enqueue(continuation_handle);
        return true;
      }
#endif

      current.register_win32_handle_wait(state, continuation_handle);

#if CARDIO_HAS_EXCEPTIONS
      (void)cancellation_.try_register_callback(
          [target = &current, wait = state] {
            wait->cancellation_registration_.reset();
            target->cancel_win32_handle_wait(wait);
          },
          state->cancellation_registration_);
#endif

      return true;
    }

    inline win32_overlapped_result await_resume() {
#if CARDIO_HAS_EXCEPTIONS
      if (state->canceled) {
        throw canceled_exception();
      }
#endif
      if (state->failed) {
        fail_win32_error(
            state->error, "cardio: GetOverlappedResult failed");
      }

      return win32_overlapped_result{state->bytes_transferred};
    }
  };
#endif
}  // namespace internal

//-----------------------------------------------------------------------------------------------

/**
 * Gets the dispatcher used by asynchronous operations on the current thread.
 *
 * @returns Dispatcher reference installed for the current thread.
 */
inline dispatcher& get_current_dispatcher() {
  return internal::require_current_dispatcher();
}

/**
 * Gets the dispatcher used by asynchronous operations on the current thread.
 *
 * @returns Dispatcher pointer installed for the current thread.
 */
inline dispatcher *unsafe_get_current_dispatcher() noexcept {
  return internal::runtime_state().current_dispatcher;
}

/**
 * Sets the dispatcher used by asynchronous operations on the current thread.
 *
 * @param dispatcher Dispatcher pointer to install for the current thread.
 *
 * @remarks
 * The dispatcher must outlive promises and continuations created while it is
 * current on the thread.
 */
inline void set_current_dispatcher(dispatcher* dispatcher) noexcept {
  internal::runtime_state().current_dispatcher = dispatcher;
}

/**
 * Creates an awaiter that resumes the current coroutine on another dispatcher.
 *
 * @param target Dispatcher that will resume the awaiting coroutine.
 * @return Awaiter object for dispatcher switching.
 *
 * @remarks
 * If the target dispatcher is already active on the current thread, the awaiter
 * does not suspend. Otherwise, the awaiting coroutine is enqueued to the target
 * dispatcher. The current and target dispatchers must belong to the same
 * dispatcher group. The target dispatcher must remain alive until the
 * continuation is resumed.
 */
inline auto switch_to(dispatcher& target) noexcept {
  return internal::switch_to_awaiter(target);
}

//-----------------------------------------------------------------

/**
 * Minimal C++20 coroutine promise value or failure.
 *
 * @tparam T Resolved value type.
 */
template <typename T> class promise {
private:
  friend class promise_source<T>;
  friend promise<T> internal::make_resolved_promise<T>(T value);
#if CARDIO_HAS_EXCEPTIONS
  friend promise<T> internal::make_rejected_promise<T>(
      std::exception_ptr exception);
#endif
#if CARDIO_WITH_LINUX_IO_URING
  friend class io_uring;
#endif

  struct shared_state : internal::promise_state_base {
    std::optional<T> value;
  };

public:
  /**
   * Coroutine promise type used by the C++20 coroutine machinery.
   */
  struct promise_type : internal::promise_type_base {
  private:
    friend class promise;

    std::shared_ptr<shared_state> state_ = std::make_shared<shared_state>();

  public:
    inline promise_type() {
      set_state(state_.get());
      internal::activate_current_promise(*state_);
    }

    inline ~promise_type() noexcept {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      state_->coroutine = {};
    }

    /**
     * Creates the user-facing promise that owns this coroutine.
     *
     * @return Promise object for the coroutine.
     */
    inline promise get_return_object() noexcept {
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
        state_->coroutine =
            std::coroutine_handle<promise_type>::from_promise(*this);
      }
      return promise(state_);
    }

    /**
     * Starts the coroutine immediately.
     *
     * @return A never-suspending awaiter.
     */
    inline std::suspend_never initial_suspend() noexcept {
      return {};
    }

    /**
     * Schedules the awaiting coroutine, if any, after this coroutine finishes.
     *
     * @return Final suspend awaiter.
     */
    inline auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() noexcept {
          return false;
        }

        void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
          struct final_suspend_guard {
            inline final_suspend_guard() noexcept {
              ++internal::runtime_state().final_suspend_depth;
            }
            inline ~final_suspend_guard() noexcept {
              --internal::runtime_state().final_suspend_depth;
            }
          } guard;
          (void)guard;

          auto& self = handle.promise();
          auto continuation =
              internal::try_schedule_continuation(*self.state_);
          if (continuation) {
            continuation.target->enqueue(continuation.continuation);
          }
        }

        void await_resume() noexcept {}
      };

      return final_awaiter{};
    }

    /**
     * Stores the coroutine return value.
     *
     * @param value Returned value.
     */
    inline void return_value(T value) {
      state_->value.emplace(std::move(value));
      state_->completed.store(true, std::memory_order_release);
      internal::finish_promise(*state_);
    }

    /**
     * Stores an exception that escapes the coroutine.
     */
    inline void unhandled_exception() {
#if CARDIO_HAS_EXCEPTIONS
      state_->exception = std::current_exception();
      state_->completed.store(true, std::memory_order_release);
      internal::finish_promise(*state_);

      if (internal::should_propagate_unhandled_exception(*state_)) {
        throw;
      }
#else
      std::terminate();
#endif
    }
  };

private:
  std::shared_ptr<shared_state> state_;
  bool owns_active_lifetime_ = true;

  inline explicit promise(
      std::shared_ptr<shared_state> state,
      bool owns_active_lifetime = true) noexcept
    : state_(std::move(state)),
      owns_active_lifetime_(owns_active_lifetime) {}

  inline void reset() noexcept {
    auto coroutine = std::coroutine_handle<>{};
    if (state_) {
      if (owns_active_lifetime_) {
        internal::finish_promise(*state_);
      }
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      state_->continuation = {};
      state_->continuation_dispatcher = nullptr;
      state_->continuation_scheduled = false;
      if (owns_active_lifetime_) {
        coroutine = std::exchange(state_->coroutine, {});
      }
    }

    if (coroutine) {
      coroutine.destroy();
    }
  }

  inline void rethrow_if_failed() const {
    if (state_) {
      state_->completed.load(std::memory_order_acquire);
    }
#if CARDIO_HAS_EXCEPTIONS
    if (state_ && state_->exception) {
      std::rethrow_exception(state_->exception);
    }
#endif
  }

  inline T& value() {
    rethrow_if_failed();
    return *state_->value;
  }

  inline const T& value() const {
    rethrow_if_failed();
    return *state_->value;
  }

  struct awaiter {
    promise& target;

    inline bool await_ready() noexcept {
      return false;
    }

    template <typename ContinuationPromise>
    inline void await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());
      auto& current = internal::require_current_dispatcher();
      if (target.state_->group != &current.group()) {
        internal::fail_runtime_error(
            "cardio: cannot await a promise from another group");
      }

      auto scheduled = internal::register_continuation(
          *target.state_, &current, continuation_handle);
      if (scheduled) {
        scheduled.target->enqueue(scheduled.continuation);
      }
    }

    inline T& await_resume() {
      return target.unsafe_result();
    }
  };

public:
  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  /**
   * Moves a promise.
   *
   * @param other Source promise.
   */
  inline promise(promise&& other) noexcept
      : state_(std::move(other.state_)),
        owns_active_lifetime_(other.owns_active_lifetime_) {
    other.owns_active_lifetime_ = true;
  }

  /**
   * Moves a promise.
   *
   * @param other Source promise.
   * @return This promise.
   */
  inline promise& operator=(promise&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();
    state_ = std::move(other.state_);
    owns_active_lifetime_ = other.owns_active_lifetime_;
    other.owns_active_lifetime_ = true;
    return *this;
  }

  /**
   * Destroys the owned coroutine, if present.
   */
  inline ~promise() {
    reset();
  }

  /**
   * Returns whether the promise has resolved or failed.
   *
   * @return True when the promise has completed.
   */
  inline bool is_ready() const noexcept {
    return state_ && state_->completed.load(std::memory_order_acquire);
  }

  /**
   * Returns the resolved value when it is available.
   *
   * @return Pointer to the resolved value, or null when the promise has not completed.
   *
   * @remarks
   * When exception support is enabled, rethrows the stored exception when the
   * promise has failed.
   */
  inline T* try_result() & {
    if (!is_ready()) {
      return nullptr;
    }

    return &value();
  }

  /**
   * Returns the resolved value when it is available.
   *
   * @return Pointer to the resolved value, or null when the promise has not completed.
   *
   * @remarks
   * When exception support is enabled, rethrows the stored exception when the
   * promise has failed.
   */
  inline const T* try_result() const& {
    if (!is_ready()) {
      return nullptr;
    }

    return &value();
  }

  /**
   * Returns the resolved value without checking completion.
   *
   * @return Resolved value.
   *
   * @remarks
   * The caller must know that the promise has completed. When exception
   * support is enabled, rethrows the stored exception when the promise has
   * failed.
   */
  inline T& unsafe_result() & {
    return value();
  }

  /**
   * Returns the resolved value without checking completion.
   *
   * @return Resolved value.
   *
   * @remarks
   * The caller must know that the promise has completed. When exception
   * support is enabled, rethrows the stored exception when the promise has
   * failed.
   */
  inline const T& unsafe_result() const& {
    return value();
  }

  /**
   * Returns the resolved value without checking completion.
   *
   * @return Resolved value.
   *
   * @remarks
   * The caller must know that the promise has completed. When exception
   * support is enabled, rethrows the stored exception when the promise has
   * failed.
   */
  inline T&& unsafe_result() && {
    return std::move(value());
  }

  /**
   * Returns an awaiter for this promise.
   *
   * @return Awaiter object.
   */
  inline auto operator co_await() & noexcept {
    return awaiter{*this};
  }

  /**
   * Returns an awaiter for this promise.
   *
   * @return Awaiter object.
   */
  inline auto operator co_await() && noexcept {
    return operator co_await();
  }

  inline promise(): state_(std::make_shared<shared_state>()) {
    internal::activate_current_promise(*state_);
  }
};

//-----------------------------------------------------------------------------------------------

/**
 * Minimal C++20 coroutine promise without a resolved value.
 */
template <> class promise<void> {
private:
  friend class promise_source<void>;
  friend promise<void> internal::make_resolved_void_promise();
#if CARDIO_HAS_EXCEPTIONS
  friend promise<void> internal::make_rejected_void_promise(
      std::exception_ptr exception);
#endif
#if CARDIO_WITH_LINUX_IO_URING
  friend class io_uring;
#endif

  struct shared_state : internal::promise_state_base {};

public:
  /**
   * Coroutine promise type used by the C++20 coroutine machinery.
   */
  struct promise_type : internal::promise_type_base {
  private:
    friend class promise;

    std::shared_ptr<shared_state> state_ = std::make_shared<shared_state>();

  public:
    inline promise_type() {
      set_state(state_.get());
      internal::activate_current_promise(*state_);
    }

    inline ~promise_type() noexcept {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      state_->coroutine = {};
    }

    /**
     * Creates the user-facing promise that owns this coroutine.
     *
     * @return Promise object for the coroutine.
     */
    inline promise get_return_object() noexcept {
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
        state_->coroutine =
            std::coroutine_handle<promise_type>::from_promise(*this);
      }
      return promise(state_);
    }

    /**
     * Starts the coroutine immediately.
     *
     * @return A never-suspending awaiter.
     */
    inline std::suspend_never initial_suspend() noexcept {
      return {};
    }

    /**
     * Schedules the awaiting coroutine, if any, after this coroutine finishes.
     *
     * @return Final suspend awaiter.
     */
    inline auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() noexcept {
          return false;
        }

        void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
          struct final_suspend_guard {
            inline final_suspend_guard() noexcept {
              ++internal::runtime_state().final_suspend_depth;
            }
            inline ~final_suspend_guard() noexcept {
              --internal::runtime_state().final_suspend_depth;
            }
          } guard;
          (void)guard;

          auto& self = handle.promise();
          auto continuation =
              internal::try_schedule_continuation(*self.state_);
          if (continuation) {
            continuation.target->enqueue(continuation.continuation);
          }
        }

        void await_resume() noexcept {}
      };

      return final_awaiter{};
    }

    /**
     * Completes the coroutine without a return value.
     */
    inline void return_void() noexcept {
      state_->completed.store(true, std::memory_order_release);
      internal::finish_promise(*state_);
    }

    /**
     * Stores an exception that escapes the coroutine.
     */
    inline void unhandled_exception() {
#if CARDIO_HAS_EXCEPTIONS
      state_->exception = std::current_exception();
      state_->completed.store(true, std::memory_order_release);
      internal::finish_promise(*state_);

      if (internal::should_propagate_unhandled_exception(*state_)) {
        throw;
      }
#else
      std::terminate();
#endif
    }
  };

private:
  std::shared_ptr<shared_state> state_;
  bool owns_active_lifetime_ = true;

  inline explicit promise(
      std::shared_ptr<shared_state> state,
      bool owns_active_lifetime = true) noexcept
    : state_(std::move(state)),
      owns_active_lifetime_(owns_active_lifetime) {}

  inline void reset() noexcept {
    auto coroutine = std::coroutine_handle<>{};
    if (state_) {
      if (owns_active_lifetime_) {
        internal::finish_promise(*state_);
      }
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      state_->continuation = {};
      state_->continuation_dispatcher = nullptr;
      state_->continuation_scheduled = false;
      if (owns_active_lifetime_) {
        coroutine = std::exchange(state_->coroutine, {});
      }
    }

    if (coroutine) {
      coroutine.destroy();
    }
  }

  inline void rethrow_if_failed() const {
    if (state_) {
      state_->completed.load(std::memory_order_acquire);
    }
#if CARDIO_HAS_EXCEPTIONS
    if (state_ && state_->exception) {
      std::rethrow_exception(state_->exception);
    }
#endif
  }

  struct awaiter {
    promise& target;

    inline bool await_ready() noexcept {
      return false;
    }

    template <typename ContinuationPromise>
    inline void await_suspend(
        std::coroutine_handle<ContinuationPromise> continuation) {
      if constexpr (std::is_base_of_v<internal::promise_type_base,
                                      ContinuationPromise>) {
        continuation.promise().mark_suspended();
      }

      auto continuation_handle =
          std::coroutine_handle<>::from_address(continuation.address());
      auto& current = internal::require_current_dispatcher();
      if (target.state_->group != &current.group()) {
        internal::fail_runtime_error(
            "cardio: cannot await a promise from another group");
      }

      auto scheduled = internal::register_continuation(
          *target.state_, &current, continuation_handle);
      if (scheduled) {
        scheduled.target->enqueue(scheduled.continuation);
      }
    }

    inline void await_resume() {
      target.unsafe_result();
    }
  };

public:
  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  /**
   * Moves a promise.
   *
   * @param other Source promise.
   */
  inline promise(promise&& other) noexcept
      : state_(std::move(other.state_)),
        owns_active_lifetime_(other.owns_active_lifetime_) {
    other.owns_active_lifetime_ = true;
  }

  /**
   * Moves a promise.
   *
   * @param other Source promise.
   * @return This promise.
   */
  inline promise& operator=(promise&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();
    state_ = std::move(other.state_);
    owns_active_lifetime_ = other.owns_active_lifetime_;
    other.owns_active_lifetime_ = true;
    return *this;
  }

  /**
   * Destroys the owned coroutine, if present.
   */
  inline ~promise() {
    reset();
  }

  /**
   * Returns whether the promise has resolved or failed.
   *
   * @return True when the promise has completed.
   */
  inline bool is_ready() const noexcept {
    return state_ && state_->completed.load(std::memory_order_acquire);
  }

  /**
   * Returns whether the promise has completed.
   *
   * @return True when the promise has completed, or false when it has not completed.
   *
   * @remarks
   * When exception support is enabled, rethrows the stored exception when the
   * promise has failed.
   */
  inline bool try_result() const {
    if (!is_ready()) {
      return false;
    }

    rethrow_if_failed();
    return true;
  }

  /**
   * Checks completion without returning a value.
   *
   * @remarks
   * The caller must know that the promise has completed. When exception
   * support is enabled, rethrows the stored exception when the promise has
   * failed.
   */
  inline void unsafe_result() const {
    rethrow_if_failed();
  }

  /**
   * Returns an awaiter for this promise.
   *
   * @return Awaiter object.
   */
  inline auto operator co_await() & noexcept {
    return awaiter{*this};
  }

  /**
   * Returns an awaiter for this promise.
   *
   * @return Awaiter object.
   */
  inline auto operator co_await() && noexcept {
    return operator co_await();
  }

  inline promise(): state_(std::make_shared<shared_state>()) {
    internal::activate_current_promise(*state_);
  }
};

//-----------------------------------------------------------------------------------------------

/**
 * Explicit completion source for an externally resolved promise.
 *
 * @tparam T Resolved value type.
 *
 * @remarks
 * The source owns the pending lifetime until resolve() or reject() completes
 * it. The promise returned by get_promise() only observes the shared result.
 */
template <typename T> class promise_source {
private:
  using shared_state = typename promise<T>::shared_state;

  std::shared_ptr<shared_state> state_;
  bool promise_requested_ = false;

  inline shared_state& require_state() const {
    if (!state_) {
      internal::fail_logic_error("cardio: promise_source has no state");
    }
    return *state_;
  }

  inline void fail_if_completed(const shared_state& state) const {
    if (state.completed.load(std::memory_order_acquire)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
  }

  inline bool try_complete_value(
      T value,
      internal::scheduled_continuation& scheduled) {
    auto& state = require_state();
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
      if (state.completed.load(std::memory_order_acquire)) {
        return false;
      }
      state.value.emplace(std::move(value));
      state.completed.store(true, std::memory_order_release);
    }
    scheduled = internal::finish_completed_promise(state);
    return true;
  }

  inline internal::scheduled_continuation complete_value(T value) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_value(std::move(value), scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool try_complete_exception(
      std::exception_ptr exception,
      internal::scheduled_continuation& scheduled) {
    auto& state = require_state();
    return internal::try_complete_exception(
        state, std::move(exception), scheduled);
  }

  inline internal::scheduled_continuation complete_exception(
      std::exception_ptr exception) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_exception(std::move(exception), scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

  inline internal::scheduled_continuation complete_canceled() {
    auto scheduled = internal::scheduled_continuation{};
    auto& state = require_state();
    if (!internal::try_complete_canceled(state, scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

  inline void abandon() noexcept {
    if (!state_ || state_->completed.load(std::memory_order_acquire)) {
      return;
    }

    try {
      auto exception = std::make_exception_ptr(
          std::runtime_error("cardio: broken promise_source"));
      auto scheduled = internal::scheduled_continuation{};
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
        if (state_->completed.load(std::memory_order_acquire)) {
          return;
        }
        state_->exception = std::move(exception);
        state_->completed.store(true, std::memory_order_release);
      }
      scheduled = internal::finish_completed_promise(*state_);
      internal::enqueue_continuation(scheduled);
    } catch (...) {
      std::terminate();
    }
  }
#else
  inline void abandon() noexcept {
    if (!state_ || state_->completed.load(std::memory_order_acquire)) {
      return;
    }

    internal::log_terminating_failure("cardio: broken promise_source");
    std::terminate();
  }
#endif

public:
  /**
   * Creates a pending promise source on the current dispatcher.
   */
  inline promise_source()
      : state_(std::make_shared<shared_state>()) {
    internal::activate_current_promise(*state_);
  }

  /**
   * Destroys the source.
   *
   * @remarks
   * If the source is still pending, it completes the promise with a
   * broken-promise failure when exception support is enabled. Without
   * exception support, destroying a pending source terminates the process.
   */
  inline ~promise_source() {
    abandon();
  }

  promise_source(const promise_source&) = delete;
  promise_source& operator=(const promise_source&) = delete;

  /**
   * Moves a promise source.
   *
   * @param other Source promise source.
   */
  inline promise_source(promise_source&& other) noexcept
      : state_(std::move(other.state_)),
        promise_requested_(other.promise_requested_) {
    other.promise_requested_ = false;
  }

  /**
   * Moves a promise source.
   *
   * @param other Source promise source.
   * @return This promise source.
   */
  inline promise_source& operator=(promise_source&& other) {
    if (this == &other) {
      return *this;
    }

    abandon();
    state_ = std::move(other.state_);
    promise_requested_ = other.promise_requested_;
    other.promise_requested_ = false;
    return *this;
  }

  /**
   * Returns the promise controlled by this source.
   *
   * @return Promise that observes this source result.
   *
   * @remarks
   * This function may be called only once for a source.
   */
  inline promise<T> get_promise() {
    (void)require_state();
    if (promise_requested_) {
      internal::fail_logic_error(
          "cardio: promise_source promise already requested");
    }

    promise_requested_ = true;
    return promise<T>(state_, false);
  }

  /**
   * Resolves the controlled promise with a value.
   *
   * @param value Resolved value.
   *
   * @remarks
   * A promise source may be completed only once.
   */
  inline void resolve(T value) {
    internal::enqueue_continuation(complete_value(std::move(value)));
  }

  /**
   * Tries to resolve the controlled promise with a value.
   *
   * @param value Resolved value.
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   */
  inline bool try_resolve(T value) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_value(std::move(value), scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Rejects the controlled promise with an exception.
   *
   * @param exception Stored exception. Defaults to the currently handled exception.
   *
   * @remarks
   * If the exception pointer is null, a runtime_error is stored instead.
   */
  inline void reject(
      std::exception_ptr exception = std::current_exception()) {
    internal::enqueue_continuation(complete_exception(std::move(exception)));
  }

  /**
   * Tries to reject the controlled promise with an exception.
   *
   * @param exception Stored exception. Defaults to the currently handled exception.
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   *
   * @remarks
   * If the exception pointer is null, a runtime_error is stored instead.
   */
  inline bool try_reject(
      std::exception_ptr exception = std::current_exception()) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_exception(std::move(exception), scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }

  /**
   * Rejects the controlled promise from an exception object.
   *
   * @tparam Exception Exception object type.
   * @param exception Stored exception object.
   *
   * @remarks
   * The exception is stored through std::make_exception_ptr().
   */
  template <
      typename Exception,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
          !std::is_null_pointer_v<std::decay_t<Exception>>>>
  inline void reject(Exception exception) {
    reject(std::make_exception_ptr(std::move(exception)));
  }

  /**
   * Tries to reject the controlled promise from an exception object.
   *
   * @tparam Exception Exception object type.
   * @param exception Stored exception object.
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   *
   * @remarks
   * The exception is stored through std::make_exception_ptr().
   */
  template <
      typename Exception,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
          !std::is_null_pointer_v<std::decay_t<Exception>>>>
  inline bool try_reject(Exception exception) {
    return try_reject(std::make_exception_ptr(std::move(exception)));
  }

  /**
   * Cancels the controlled promise.
   *
   * @remarks
   * A promise source may be completed only once.
   */
  inline void cancel() {
    internal::enqueue_continuation(complete_canceled());
  }

  /**
   * Tries to cancel the controlled promise.
   *
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   */
  inline bool try_cancel() {
    auto scheduled = internal::scheduled_continuation{};
    auto& state = require_state();
    if (!internal::try_complete_canceled(state, scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }
#endif
};

//-----------------------------------------------------------------------------------------------

/**
 * Explicit completion source for an externally resolved void promise.
 *
 * @remarks
 * The source owns the pending lifetime until resolve() or reject() completes
 * it. The promise returned by get_promise() only observes the shared result.
 */
template <> class promise_source<void> {
private:
  using shared_state = promise<void>::shared_state;

  std::shared_ptr<shared_state> state_;
  bool promise_requested_ = false;

  inline shared_state& require_state() const {
    if (!state_) {
      internal::fail_logic_error("cardio: promise_source has no state");
    }
    return *state_;
  }

  inline void fail_if_completed(const shared_state& state) const {
    if (state.completed.load(std::memory_order_acquire)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
  }

  inline bool try_complete_void(internal::scheduled_continuation& scheduled) {
    auto& state = require_state();
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state.mutex);
      if (state.completed.load(std::memory_order_acquire)) {
        return false;
      }
      state.completed.store(true, std::memory_order_release);
    }
    scheduled = internal::finish_completed_promise(state);
    return true;
  }

  inline internal::scheduled_continuation complete_void() {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_void(scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool try_complete_exception(
      std::exception_ptr exception,
      internal::scheduled_continuation& scheduled) {
    auto& state = require_state();
    return internal::try_complete_exception(
        state, std::move(exception), scheduled);
  }

  inline internal::scheduled_continuation complete_exception(
      std::exception_ptr exception) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_exception(std::move(exception), scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

  inline internal::scheduled_continuation complete_canceled() {
    auto scheduled = internal::scheduled_continuation{};
    auto& state = require_state();
    if (!internal::try_complete_canceled(state, scheduled)) {
      internal::fail_logic_error("cardio: promise_source already completed");
    }
    return scheduled;
  }

  inline void abandon() noexcept {
    if (!state_ || state_->completed.load(std::memory_order_acquire)) {
      return;
    }

    try {
      auto exception = std::make_exception_ptr(
          std::runtime_error("cardio: broken promise_source"));
      auto scheduled = internal::scheduled_continuation{};
      {
        auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
        if (state_->completed.load(std::memory_order_acquire)) {
          return;
        }
        state_->exception = std::move(exception);
        state_->completed.store(true, std::memory_order_release);
      }
      scheduled = internal::finish_completed_promise(*state_);
      internal::enqueue_continuation(scheduled);
    } catch (...) {
      std::terminate();
    }
  }
#else
  inline void abandon() noexcept {
    if (!state_ || state_->completed.load(std::memory_order_acquire)) {
      return;
    }

    internal::log_terminating_failure("cardio: broken promise_source");
    std::terminate();
  }
#endif

public:
  /**
   * Creates a pending promise source on the current dispatcher.
   */
  inline promise_source()
      : state_(std::make_shared<shared_state>()) {
    internal::activate_current_promise(*state_);
  }

  /**
   * Destroys the source.
   *
   * @remarks
   * If the source is still pending, it completes the promise with a
   * broken-promise failure when exception support is enabled. Without
   * exception support, destroying a pending source terminates the process.
   */
  inline ~promise_source() {
    abandon();
  }

  promise_source(const promise_source&) = delete;
  promise_source& operator=(const promise_source&) = delete;

  /**
   * Moves a promise source.
   *
   * @param other Source promise source.
   */
  inline promise_source(promise_source&& other) noexcept
      : state_(std::move(other.state_)),
        promise_requested_(other.promise_requested_) {
    other.promise_requested_ = false;
  }

  /**
   * Moves a promise source.
   *
   * @param other Source promise source.
   * @return This promise source.
   */
  inline promise_source& operator=(promise_source&& other) {
    if (this == &other) {
      return *this;
    }

    abandon();
    state_ = std::move(other.state_);
    promise_requested_ = other.promise_requested_;
    other.promise_requested_ = false;
    return *this;
  }

  /**
   * Returns the promise controlled by this source.
   *
   * @return Promise that observes this source result.
   *
   * @remarks
   * This function may be called only once for a source.
   */
  inline promise<void> get_promise() {
    (void)require_state();
    if (promise_requested_) {
      internal::fail_logic_error(
          "cardio: promise_source promise already requested");
    }

    promise_requested_ = true;
    return promise<void>(state_, false);
  }

  /**
   * Resolves the controlled promise.
   *
   * @remarks
   * A promise source may be completed only once.
   */
  inline void resolve() {
    internal::enqueue_continuation(complete_void());
  }

  /**
   * Tries to resolve the controlled promise.
   *
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   */
  inline bool try_resolve() {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_void(scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Rejects the controlled promise with an exception.
   *
   * @param exception Stored exception. Defaults to the currently handled exception.
   *
   * @remarks
   * If the exception pointer is null, a runtime_error is stored instead.
   */
  inline void reject(
      std::exception_ptr exception = std::current_exception()) {
    internal::enqueue_continuation(complete_exception(std::move(exception)));
  }

  /**
   * Tries to reject the controlled promise with an exception.
   *
   * @param exception Stored exception. Defaults to the currently handled exception.
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   *
   * @remarks
   * If the exception pointer is null, a runtime_error is stored instead.
   */
  inline bool try_reject(
      std::exception_ptr exception = std::current_exception()) {
    auto scheduled = internal::scheduled_continuation{};
    if (!try_complete_exception(std::move(exception), scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }

  /**
   * Rejects the controlled promise from an exception object.
   *
   * @tparam Exception Exception object type.
   * @param exception Stored exception object.
   *
   * @remarks
   * The exception is stored through std::make_exception_ptr().
   */
  template <
      typename Exception,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
          !std::is_null_pointer_v<std::decay_t<Exception>>>>
  inline void reject(Exception exception) {
    reject(std::make_exception_ptr(std::move(exception)));
  }

  /**
   * Tries to reject the controlled promise from an exception object.
   *
   * @tparam Exception Exception object type.
   * @param exception Stored exception object.
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   *
   * @remarks
   * The exception is stored through std::make_exception_ptr().
   */
  template <
      typename Exception,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
          !std::is_null_pointer_v<std::decay_t<Exception>>>>
  inline bool try_reject(Exception exception) {
    return try_reject(std::make_exception_ptr(std::move(exception)));
  }

  /**
   * Cancels the controlled promise.
   *
   * @remarks
   * A promise source may be completed only once.
   */
  inline void cancel() {
    internal::enqueue_continuation(complete_canceled());
  }

  /**
   * Tries to cancel the controlled promise.
   *
   * @return True when this call completed the promise, or false when the
   * promise had already completed.
   */
  inline bool try_cancel() {
    auto scheduled = internal::scheduled_continuation{};
    auto& state = require_state();
    if (!internal::try_complete_canceled(state, scheduled)) {
      return false;
    }

    internal::enqueue_continuation(scheduled);
    return true;
  }
#endif
};

//-----------------------------------------------------------------------------------------------

namespace internal {
  struct fire_and_forget_state {
    dispatcher* cleanup_dispatcher = nullptr;
    std::optional<promise<void>> watcher;
  };

  inline void schedule_fire_and_forget_cleanup(
      const std::shared_ptr<fire_and_forget_state>& state) noexcept {
    auto* target = state->cleanup_dispatcher;
    if (target == nullptr) {
      std::terminate();
    }

    internal::dangerous_schedule_later__(target, [state] {
      state->watcher.reset();
    });
  }

  struct fire_and_forget_cleanup_guard {
    std::shared_ptr<fire_and_forget_state> state;

    inline explicit fire_and_forget_cleanup_guard(
        std::shared_ptr<fire_and_forget_state> state) noexcept
      : state(std::move(state)) {}

    inline ~fire_and_forget_cleanup_guard() noexcept {
      if (state) {
        internal::schedule_fire_and_forget_cleanup(state);
      }
    }

    fire_and_forget_cleanup_guard(
        const fire_and_forget_cleanup_guard&) = delete;
    fire_and_forget_cleanup_guard& operator=(
        const fire_and_forget_cleanup_guard&) = delete;
  };

  template <typename T>
  inline promise<void> fire_and_forget_impl(
      std::shared_ptr<fire_and_forget_state> state,
      promise<T> target) {
    auto cleanup = internal::fire_and_forget_cleanup_guard(std::move(state));
    (void)cleanup;

    if constexpr (std::is_void_v<T>) {
      co_await target;
    } else {
      (void)co_await target;
    }
  }

}  // namespace internal

//-----------------------------------------------------------------------------------------------

/**
 * Keeps a root promise alive until it completes and discards its result.
 *
 * @tparam T Promise result type.
 * @param target Promise to run without awaiting its result.
 *
 * @remarks
 * Use this for intentional root asynchronous operations where the caller does
 * not need the result. When exception support is enabled, failures escaping the
 * promise are reported through dispatcher::unhandled_exception(), matching
 * ordinary unhandled root promise behavior. This function does not extend the
 * lifetime of objects referenced by the asynchronous operation.
 */
template <typename T>
inline void fire_and_forget(promise<T> target) {
  auto state = std::make_shared<internal::fire_and_forget_state>();
  state->cleanup_dispatcher = &internal::require_current_dispatcher();
  state->watcher.emplace(
      internal::fire_and_forget_impl(state, std::move(target)));
}

//-----------------------------------------------------------------------------------------------

namespace internal {
  template <typename T>
  inline promise<T> make_resolved_promise(T value) {
    auto result = promise<T>();
    result.state_->value.emplace(std::move(value));
    result.state_->completed.store(true, std::memory_order_release);
    (void)internal::finish_completed_promise(*result.state_);
    return result;
  }

#if CARDIO_HAS_EXCEPTIONS
  template <typename T>
  inline promise<T> make_rejected_promise(std::exception_ptr exception) {
    auto result = promise<T>();
    result.state_->exception =
        internal::normalize_rejection_exception(std::move(exception));
    result.state_->completed.store(true, std::memory_order_release);
    (void)internal::finish_completed_promise(*result.state_);
    return result;
  }
#endif

  inline promise<void> make_resolved_void_promise() {
    auto result = promise<void>();
    result.state_->completed.store(true, std::memory_order_release);
    (void)internal::finish_completed_promise(*result.state_);
    return result;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline promise<void> make_rejected_void_promise(
      std::exception_ptr exception) {
    auto result = promise<void>();
    result.state_->exception =
        internal::normalize_rejection_exception(std::move(exception));
    result.state_->completed.store(true, std::memory_order_release);
    (void)internal::finish_completed_promise(*result.state_);
    return result;
  }
#endif

  inline std::chrono::steady_clock::time_point deadline_after_milliseconds(
      std::uint64_t msec) {
    return std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(
               std::chrono::duration<std::uint64_t, std::milli>(msec));
  }

  inline promise<void> delay_until_impl(
      std::chrono::steady_clock::time_point deadline) {
    co_await internal::timer_awaiter(deadline);
  }

#if CARDIO_HAS_EXCEPTIONS
  inline promise<void> delay_until_impl(
      std::chrono::steady_clock::time_point deadline,
      cancellation cancellation_signal) {
    co_await internal::timer_awaiter(
        deadline, std::move(cancellation_signal));
  }
#endif

#if CARDIO_HAS_POSIX_FD
  inline promise<fd_event> from_fd_impl(int fd, fd_event interests) {
    auto events = co_await internal::fd_awaiter(fd, interests);
    co_return events;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline promise<fd_event> from_fd_impl(
      int fd,
      fd_event interests,
      cancellation cancellation) {
    auto events =
        co_await internal::fd_awaiter(fd, interests, std::move(cancellation));
    co_return events;
  }
#endif
#endif

#if CARDIO_HAS_WIN32_HANDLE
  inline promise<win32_handle_event> from_win32_handle_impl(HANDLE handle) {
    auto event = co_await internal::win32_handle_awaiter(handle);
    co_return event;
  }

  inline promise<win32_overlapped_result> from_win32_overlapped_impl(
      HANDLE handle,
      OVERLAPPED& overlapped) {
    auto result =
        co_await internal::win32_overlapped_awaiter(handle, overlapped);
    co_return result;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline promise<win32_handle_event> from_win32_handle_impl(
      HANDLE handle,
      cancellation cancellation) {
    auto event =
        co_await internal::win32_handle_awaiter(handle, std::move(cancellation));
    co_return event;
  }

  inline promise<win32_overlapped_result> from_win32_overlapped_impl(
      HANDLE handle,
      OVERLAPPED& overlapped,
      cancellation cancellation) {
    auto result = co_await internal::win32_overlapped_awaiter(
        handle, overlapped, std::move(cancellation));
    co_return result;
  }
#endif
#endif
}  // namespace internal

//-----------------------------------------------------------------------------------------------

/**
 * Creates an already resolved void promise.
 *
 * @return Resolved promise.
 */
inline promise<void> resolved() {
  return internal::make_resolved_void_promise();
}

/**
 * Creates an already resolved promise.
 *
 * @tparam T Resolved value type.
 * @param value Resolved value.
 * @return Resolved promise.
 */
template <typename T>
inline promise<std::decay_t<T>> resolved(T value) {
  return internal::make_resolved_promise<std::decay_t<T>>(
      std::forward<T>(value));
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Creates an already failed void promise.
 *
 * @param exception Stored exception. Defaults to the currently handled exception.
 * @return Failed promise.
 *
 * @remarks
 * If the exception pointer is null, a runtime_error is stored instead.
 */
inline promise<void> rejected(
    std::exception_ptr exception = std::current_exception()) {
  return internal::make_rejected_void_promise(std::move(exception));
}

/**
 * Creates an already failed void promise from an exception object.
 *
 * @tparam Exception Exception object type.
 * @param exception Stored exception object.
 * @return Failed promise.
 *
 * @remarks
 * The exception is stored through std::make_exception_ptr().
 */
template <
    typename Exception,
    typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
        !std::is_null_pointer_v<std::decay_t<Exception>>>>
inline promise<void> rejected(Exception exception) {
  return rejected(std::make_exception_ptr(std::move(exception)));
}

/**
 * Creates an already failed promise.
 *
 * @tparam T Resolved value type.
 * @param exception Stored exception. Defaults to the currently handled exception.
 * @return Failed promise.
 *
 * @remarks
 * If the exception pointer is null, a runtime_error is stored instead.
 */
template <typename T>
inline promise<T> rejected(
    std::exception_ptr exception = std::current_exception()) {
  return internal::make_rejected_promise<T>(std::move(exception));
}

/**
 * Creates an already failed promise from an exception object.
 *
 * @tparam T Resolved value type.
 * @tparam Exception Exception object type.
 * @param exception Stored exception object.
 * @return Failed promise.
 *
 * @remarks
 * The exception is stored through std::make_exception_ptr().
 */
template <
    typename T,
    typename Exception,
    typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<Exception>, std::exception_ptr> &&
        !std::is_null_pointer_v<std::decay_t<Exception>>>>
inline promise<T> rejected(Exception exception) {
  return rejected<T>(std::make_exception_ptr(std::move(exception)));
}
#endif

#if CARDIO_HAS_POSIX_FD
/**
 * Creates a promise that resolves when a file descriptor becomes ready.
 *
 * @param fd File descriptor to observe.
 * @param interests Readiness events to observe.
 * @return Promise that resolves with the reported readiness events.
 *
 * @remarks
 * The file descriptor is not owned by the promise and is never closed by it.
 */
inline promise<fd_event> from_fd(int fd, fd_event interests) {
  if (fd < 0) {
#if CARDIO_HAS_EXCEPTIONS
    return rejected<fd_event>(std::invalid_argument(
        "cardio: file descriptor must not be negative"));
#else
    internal::fail_invalid_argument(
        "cardio: file descriptor must not be negative");
#endif
  }

  if (!internal::has_fd_io_interest(interests)) {
#if CARDIO_HAS_EXCEPTIONS
    return rejected<fd_event>(std::invalid_argument(
        "cardio: file descriptor interests must include read or write"));
#else
    internal::fail_invalid_argument(
        "cardio: file descriptor interests must include read or write");
#endif
  }

  return internal::from_fd_impl(fd, interests);
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Creates a cancellable promise that resolves when a file descriptor becomes ready.
 *
 * @param fd File descriptor to observe.
 * @param interests Readiness events to observe.
 * @param cancellation Cancellation signal.
 * @return Promise that resolves with the reported readiness events.
 *
 * @remarks
 * The file descriptor is not owned by the promise and is never closed by it.
 * When cancellation is requested before readiness, the promise fails with
 * canceled_exception.
 */
inline promise<fd_event> from_fd(
    int fd,
    fd_event interests,
    cancellation cancellation) {
  if (fd < 0) {
    return rejected<fd_event>(std::invalid_argument(
        "cardio: file descriptor must not be negative"));
  }

  if (!internal::has_fd_io_interest(interests)) {
    return rejected<fd_event>(std::invalid_argument(
        "cardio: file descriptor interests must include read or write"));
  }

  if (cancellation.is_cancellation_requested()) {
    return rejected<fd_event>(canceled_exception());
  }

  return internal::from_fd_impl(fd, interests, std::move(cancellation));
}
#endif
#endif

#if CARDIO_HAS_WIN32_HANDLE
/**
 * Creates a promise that resolves when a Win32 handle becomes signaled.
 *
 * @param handle Handle to observe.
 * @return Promise that resolves with the reported handle event.
 *
 * @remarks
 * The handle is not owned by the promise and is never closed by it.
 */
inline promise<win32_handle_event> from_win32_handle(HANDLE handle) {
  if (internal::is_invalid_win32_handle(handle)) {
#if CARDIO_HAS_EXCEPTIONS
    return rejected<win32_handle_event>(std::invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
#else
    internal::fail_invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
#endif
  }

  return internal::from_win32_handle_impl(handle);
}

/**
 * Creates a promise that resolves when a Win32 OVERLAPPED operation completes.
 *
 * @param handle Handle used by the overlapped operation.
 * @param overlapped OVERLAPPED structure used by the operation.
 * @return Promise that resolves with the completed byte count.
 *
 * @remarks
 * The handle and OVERLAPPED structure are not owned by the promise. When
 * OVERLAPPED::hEvent is null, the file handle itself is waited on. Use a
 * distinct manual-reset event for each concurrently pending operation on the
 * same handle so completions can be distinguished.
 */
inline promise<win32_overlapped_result> from_win32_overlapped(
    HANDLE handle,
    OVERLAPPED& overlapped) {
  if (internal::is_invalid_win32_handle(handle)) {
#if CARDIO_HAS_EXCEPTIONS
    return rejected<win32_overlapped_result>(std::invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
#else
    internal::fail_invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
#endif
  }

  return internal::from_win32_overlapped_impl(handle, overlapped);
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Creates a cancellable promise that resolves when a Win32 handle becomes signaled.
 *
 * @param handle Handle to observe.
 * @param cancellation Cancellation signal.
 * @return Promise that resolves with the reported handle event.
 *
 * @remarks
 * The handle is not owned by the promise and is never closed by it. When
 * cancellation is requested before the handle is signaled, the promise fails
 * with canceled_exception.
 */
inline promise<win32_handle_event> from_win32_handle(
    HANDLE handle,
    cancellation cancellation) {
  if (internal::is_invalid_win32_handle(handle)) {
    return rejected<win32_handle_event>(std::invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
  }

  if (cancellation.is_cancellation_requested()) {
    return rejected<win32_handle_event>(canceled_exception());
  }

  return internal::from_win32_handle_impl(handle, std::move(cancellation));
}

/**
 * Creates a cancellable promise that resolves when a Win32 OVERLAPPED operation completes.
 *
 * @param handle Handle used by the overlapped operation.
 * @param overlapped OVERLAPPED structure used by the operation.
 * @param cancellation Cancellation signal.
 * @return Promise that resolves with the completed byte count.
 *
 * @remarks
 * The handle and OVERLAPPED structure are not owned by the promise. Cancellation
 * only cancels the promise wait; it does not call CancelIoEx() or close the
 * handle.
 */
inline promise<win32_overlapped_result> from_win32_overlapped(
    HANDLE handle,
    OVERLAPPED& overlapped,
    cancellation cancellation) {
  if (internal::is_invalid_win32_handle(handle)) {
    return rejected<win32_overlapped_result>(std::invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
  }

  if (cancellation.is_cancellation_requested()) {
    return rejected<win32_overlapped_result>(canceled_exception());
  }

  return internal::from_win32_overlapped_impl(
      handle, overlapped, std::move(cancellation));
}
#endif
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_WIN32_HANDLE
namespace internal {

  struct win32_iocp_operation_state {
    OVERLAPPED overlapped{};
    HANDLE handle = nullptr;
    std::function<DWORD(HANDLE, OVERLAPPED&)> start;
    std::function<void(win32_iocp_completion, bool)> complete;
    win32_io_lease lease;
    bool started = false;
    bool cancellation_sent = false;
    std::atomic<bool> retired = false;
    std::atomic<bool> cancellation_requested = false;
#if CARDIO_HAS_EXCEPTIONS
    std::function<void(std::exception_ptr)> reject;
    cancellation_registration cancellation_registration_;
#endif
  };

  class win32_iocp_port_state {
  private:
    HANDLE port_ = nullptr;
    thread pump_thread_;
    mutex mutex_;
    const std::size_t capacity_;
    std::vector<HANDLE> associated_handles_;
    std::deque<std::shared_ptr<win32_iocp_operation_state>> operations_;
    std::atomic<bool> shutdown_requested_ = false;
    bool control_packet_posted_ = false;
    bool shutdown_packet_seen_ = false;
    static constexpr ULONG_PTR control_key = 1;

    inline bool associate_handle(HANDLE handle) {
      for (auto associated : associated_handles_) {
        if (associated == handle) {
          return true;
        }
      }
      if (::CreateIoCompletionPort(handle, port_,
              reinterpret_cast<ULONG_PTR>(handle), 0) != port_) {
        return false;
      }
      associated_handles_.push_back(handle);
      return true;
    }

    inline DWORD post_control_locked() noexcept {
      if (control_packet_posted_) {
        return ERROR_SUCCESS;
      }
      if (::PostQueuedCompletionStatus(port_, 0, control_key, nullptr) == 0) {
        return ::GetLastError();
      }
      control_packet_posted_ = true;
      return ERROR_SUCCESS;
    }

    inline void retire(
        const std::shared_ptr<win32_iocp_operation_state>& operation) noexcept {
      {
        auto lock = std::lock_guard<mutex>(mutex_);
        operation->retired.store(true, std::memory_order_release);
        for (auto current = operations_.begin(); current != operations_.end(); ++current) {
          if (current->get() == operation.get()) {
            operations_.erase(current);
            break;
          }
        }
      }
      // Release ownership before resolving a promise or running its mapper.
      // Already queued cancellation callbacks only carry weak references and
      // cannot apply CancelIo to the next operation on the same handle.
      operation->lease.release();
#if CARDIO_HAS_EXCEPTIONS
      operation->cancellation_registration_.reset();
#endif
      operation->start = {};
    }

    inline void complete_operation(
        const std::shared_ptr<win32_iocp_operation_state>& operation,
        win32_iocp_completion completion) noexcept {
      retire(operation);
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        operation->complete(completion, operation->cancellation_requested.load(
            std::memory_order_acquire));
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        operation->reject(std::current_exception());
      }
#endif
    }

    inline void fail_start(
        const std::shared_ptr<win32_iocp_operation_state>& operation,
        DWORD error) noexcept {
      retire(operation);
#if CARDIO_HAS_EXCEPTIONS
      operation->reject(std::make_exception_ptr(std::system_error(
          static_cast<int>(error), std::system_category(),
          "cardio: Win32 IOCP operation failed")));
#else
      fail_win32_error(error, "cardio: Win32 IOCP operation failed");
#endif
    }

    inline void cancel_on_pump(
        const std::shared_ptr<win32_iocp_operation_state>& operation) noexcept {
      if (!operation->cancellation_sent) {
        operation->cancellation_sent = true;
        operation->lease.cancel(operation->overlapped, ::GetCurrentThreadId());
      }
    }

    inline void process_control() noexcept {
      auto operations = std::vector<std::shared_ptr<win32_iocp_operation_state>>{};
      {
        auto lock = std::lock_guard<mutex>(mutex_);
        control_packet_posted_ = false;
        shutdown_packet_seen_ = shutdown_requested_.load(std::memory_order_acquire);
        operations.assign(operations_.begin(), operations_.end());
      }
      for (const auto& operation : operations) {
        if (shutdown_requested_.load(std::memory_order_acquire)) {
          operation->cancellation_requested.store(true, std::memory_order_release);
        }
        if (operation->cancellation_requested.load(std::memory_order_acquire)) {
          if (operation->started) {
            cancel_on_pump(operation);
          } else {
            complete_operation(operation, win32_iocp_completion{
                ERROR_OPERATION_ABORTED, 0, reinterpret_cast<ULONG_PTR>(operation->handle)});
          }
          continue;
        }
        if (operation->started) {
          continue;
        }
#if CARDIO_HAS_EXCEPTIONS
        try {
#endif
          const auto lease_error = operation->lease.acquire(operation->handle);
          if (lease_error != ERROR_SUCCESS) {
            fail_start(operation, lease_error);
            continue;
          }
          if (!associate_handle(operation->handle)) {
            fail_start(operation, ::GetLastError());
            continue;
          }
          auto start = std::move(operation->start);
          const auto error = start(operation->handle, operation->overlapped);
          if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING) {
            if (error == ERROR_OPERATION_ABORTED &&
                operation->cancellation_requested.load(std::memory_order_acquire)) {
              complete_operation(operation, win32_iocp_completion{
                  error, 0, reinterpret_cast<ULONG_PTR>(operation->handle)});
            } else {
              fail_start(operation, error);
            }
            continue;
          }
          operation->started = true;
          if (shutdown_requested_.load(std::memory_order_acquire)) {
            operation->cancellation_requested.store(true, std::memory_order_release);
          }
          if (operation->cancellation_requested.load(std::memory_order_acquire)) {
            cancel_on_pump(operation);
          }
#if CARDIO_HAS_EXCEPTIONS
        } catch (...) {
          retire(operation);
          operation->reject(std::current_exception());
        }
#endif
      }
    }

    inline bool process_completion(
        OVERLAPPED* overlapped,
        ULONG_PTR completion_key,
        DWORD packet_bytes,
        DWORD packet_error) noexcept {
      if (overlapped == nullptr) {
        if (completion_key == control_key) {
          process_control();
        }
      } else {
        auto operation = std::shared_ptr<win32_iocp_operation_state>{};
        {
          auto lock = std::lock_guard<mutex>(mutex_);
          for (const auto& current : operations_) {
            if (&current->overlapped == overlapped) {
              operation = current;
              break;
            }
          }
        }
        if (operation) {
          auto bytes = packet_bytes;
          auto error = packet_error;
          if (::GetOverlappedResult(operation->handle, overlapped, &bytes, FALSE)) {
            error = ERROR_SUCCESS;
          } else {
            error = ::GetLastError();
          }
          complete_operation(operation, win32_iocp_completion{error, bytes, completion_key});
        }
      }
      auto lock = std::lock_guard<mutex>(mutex_);
      return !(shutdown_packet_seen_ && operations_.empty());
    }

    inline bool pump_one() noexcept {
      auto bytes = DWORD{};
      auto completion_key = ULONG_PTR{};
      auto* overlapped = static_cast<OVERLAPPED*>(nullptr);
      const auto succeeded = ::GetQueuedCompletionStatus(
          port_, &bytes, &completion_key, &overlapped, INFINITE);
      const auto error = succeeded ? ERROR_SUCCESS : ::GetLastError();
      if (overlapped == nullptr && !succeeded) {
        std::terminate();
      }
      return process_completion(overlapped, completion_key, bytes, error);
    }

#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
    inline bool pump_many() noexcept {
      OVERLAPPED_ENTRY entries[16]{};
      auto removed = ULONG{};
      if (::GetQueuedCompletionStatusEx(port_, entries, 16, &removed, INFINITE, FALSE) == 0) {
        return pump_one();
      }
      for (auto index = ULONG{0}; index < removed; ++index) {
        if (!process_completion(entries[index].lpOverlapped,
                entries[index].lpCompletionKey,
                entries[index].dwNumberOfBytesTransferred, ERROR_SUCCESS)) {
          return false;
        }
      }
      return true;
    }
#endif

    inline void pump_loop() noexcept {
      while (true) {
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
        if (!pump_many()) {
          return;
        }
#else
        if (!pump_one()) {
          return;
        }
#endif
      }
    }

  public:
    inline explicit win32_iocp_port_state(std::size_t capacity)
        : capacity_(capacity) {
      if (capacity == 0) {
        fail_invalid_argument("cardio: IOCP capacity must be greater than zero");
      }
      port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
      if (port_ == nullptr) {
        fail_win32_error(::GetLastError(), "cardio: CreateIoCompletionPort failed");
      }
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        pump_thread_ = thread([this] { pump_loop(); });
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        (void)::CloseHandle(port_);
        throw;
      }
#endif
    }

    inline ~win32_iocp_port_state() {
      request_shutdown();
      pump_thread_.join();
      (void)::CloseHandle(port_);
    }

    win32_iocp_port_state(const win32_iocp_port_state&) = delete;
    win32_iocp_port_state& operator=(const win32_iocp_port_state&) = delete;

    inline DWORD start_operation(
        const std::shared_ptr<win32_iocp_operation_state>& operation) {
      auto lock = std::lock_guard<mutex>(mutex_);
      if (shutdown_requested_.load(std::memory_order_acquire)) {
        return ERROR_OPERATION_ABORTED;
      }
      if (operations_.size() >= capacity_) {
        return ERROR_NOT_ENOUGH_QUOTA;
      }
      operations_.push_back(operation);
      const auto error = post_control_locked();
      if (error != ERROR_SUCCESS) {
        operations_.pop_back();
      }
      return error;
    }

    inline void request_cancel(
        const std::shared_ptr<win32_iocp_operation_state>& operation) noexcept {
      auto lock = std::lock_guard<mutex>(mutex_);
      if (operation->retired.load(std::memory_order_acquire)) {
        return;
      }
      operation->cancellation_requested.store(true, std::memory_order_release);
      if (post_control_locked() != ERROR_SUCCESS) {
        std::terminate();
      }
    }

    inline void request_shutdown() noexcept {
      auto lock = std::lock_guard<mutex>(mutex_);
      if (!shutdown_requested_.exchange(true, std::memory_order_acq_rel) &&
          post_control_locked() != ERROR_SUCCESS) {
        std::terminate();
      }
    }
  };

}  // namespace internal

/**
 * Win32 I/O completion port for helper-owned OVERLAPPED operations.
 *
 * @remarks
 * The completion port owns one pump thread for starting, canceling, and
 * completing I/O. Only operations submitted through this object are completed
 * by it; do not mix the same handle with external I/O, from_win32_overlapped()
 * waits, or another completion port. Destroy the port outside its callbacks.
 * Destruction cancels outstanding work and waits for native completion; drivers
 * that do not finish cancellation can delay destruction indefinitely.
 *
 * XP targets resolve CancelIoEx at runtime. Without it, CancelIo runs on the
 * pump thread and only one operation per HANDLE may be outstanding (ERROR_BUSY
 * otherwise). Keep associated handles open while using this port, since handle
 * associations are cached. All targets bound queued plus in-flight operations
 * by the constructor capacity (ERROR_NOT_ENOUGH_QUOTA when full).
 */
class io_completion_port {
private:
  std::shared_ptr<internal::win32_iocp_port_state> state_;

  template <typename T>
  static inline promise<T> invalid_handle_promise() {
#if CARDIO_HAS_EXCEPTIONS
    if constexpr (std::is_void_v<T>) {
      return rejected(std::invalid_argument(
          "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
    } else {
      return rejected<T>(std::invalid_argument(
          "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE"));
    }
#else
    internal::fail_invalid_argument(
        "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
#endif
  }

#if CARDIO_HAS_EXCEPTIONS
  template <typename T>
  static inline promise<T> canceled_promise() {
    if constexpr (std::is_void_v<T>) {
      return rejected(canceled_exception());
    } else {
      return rejected<T>(canceled_exception());
    }
  }

  template <typename T>
  static inline promise<T> reject_current_exception() {
    if constexpr (std::is_void_v<T>) {
      return rejected();
    } else {
      return rejected<T>();
    }
  }
#endif

  template <typename T, typename Complete>
  static inline void complete_source(
      const std::shared_ptr<promise_source<T>>& source,
      const std::shared_ptr<std::decay_t<Complete>>& complete,
      win32_iocp_completion completion,
      bool cancellation_requested) {
#if CARDIO_HAS_EXCEPTIONS
    try {
      if (cancellation_requested &&
          completion.error == ERROR_OPERATION_ABORTED) {
        source->cancel();
        return;
      }

      if constexpr (std::is_void_v<T>) {
        (void)std::invoke(*complete, completion);
        source->resolve();
      } else {
        source->resolve(std::invoke(*complete, completion));
      }
    } catch (...) {
      source->reject();
    }
#else
    (void)cancellation_requested;
    if constexpr (std::is_void_v<T>) {
      (void)std::invoke(*complete, completion);
      source->resolve();
    } else {
      source->resolve(std::invoke(*complete, completion));
    }
#endif
  }

  template <typename T, typename Start, typename Complete>
  inline promise<T> submit_impl(
      HANDLE handle,
      Start start,
      Complete complete,
#if CARDIO_HAS_EXCEPTIONS
      const cancellation* cancellation_signal
#else
      const void* cancellation_signal
#endif
      ) {
    if (internal::is_invalid_win32_handle(handle)) {
      return invalid_handle_promise<T>();
    }

    if (!state_) {
      internal::fail_logic_error("cardio: IO completion port has no state");
    }

#if CARDIO_HAS_EXCEPTIONS
    if (cancellation_signal != nullptr &&
        cancellation_signal->is_cancellation_requested()) {
      return canceled_promise<T>();
    }
#else
    (void)cancellation_signal;
#endif

    auto source = std::make_shared<promise_source<T>>();
    auto result = source->get_promise();
    auto operation = std::make_shared<internal::win32_iocp_operation_state>();
    operation->handle = handle;
    auto start_handler = std::make_shared<std::decay_t<Start>>(std::move(start));
    operation->start = [start_handler](HANDLE target, OVERLAPPED& overlapped) {
      return static_cast<DWORD>(std::invoke(*start_handler, target, overlapped));
    };
    auto complete_handler =
        std::make_shared<std::decay_t<Complete>>(std::move(complete));
    operation->complete =
        [source, complete_handler](
            win32_iocp_completion completion,
            bool cancellation_requested) mutable {
      complete_source<T, std::decay_t<Complete>>(
          source, complete_handler, completion, cancellation_requested);
    };

#if CARDIO_HAS_EXCEPTIONS
    operation->reject = [source](std::exception_ptr exception) {
      source->reject(std::move(exception));
    };
    if (cancellation_signal != nullptr) {
      operation->cancellation_registration_ =
          cancellation_signal->on_cancellation_requested(
              [weak_operation = std::weak_ptr<internal::win32_iocp_operation_state>(operation),
               weak_port = std::weak_ptr<internal::win32_iocp_port_state>(state_)] {
            if (auto current = weak_operation.lock()) {
              if (auto port = weak_port.lock()) {
                port->request_cancel(current);
              }
            }
          });
    }
#endif

    auto start_error = DWORD{ERROR_SUCCESS};
#if CARDIO_HAS_EXCEPTIONS
    try {
#endif
      start_error = state_->start_operation(operation);
#if CARDIO_HAS_EXCEPTIONS
    } catch (...) {
      operation->cancellation_registration_.reset();
      source->reject();
      return result;
    }
#endif

    if (start_error != ERROR_SUCCESS && start_error != ERROR_IO_PENDING) {
#if CARDIO_HAS_EXCEPTIONS
      operation->cancellation_registration_.reset();
      if (operation->cancellation_requested.load(std::memory_order_acquire) &&
          start_error == ERROR_OPERATION_ABORTED) {
        source->cancel();
      } else {
        source->reject(std::make_exception_ptr(std::system_error(
            static_cast<int>(start_error),
            std::system_category(),
            "cardio: Win32 IOCP operation failed")));
      }
#else
      internal::fail_win32_error(
          start_error, "cardio: Win32 IOCP operation failed");
#endif
    }

    return result;
  }

public:
  /**
   * Creates an I/O completion port and starts its pump thread.
   *
   * @param capacity Maximum number of queued and in-flight operations.
   * @throws std::invalid_argument Thrown when capacity is zero.
   * @throws std::system_error Thrown when CreateIoCompletionPort fails.
   */
  inline explicit io_completion_port(std::size_t capacity = 256)
      : state_(std::make_shared<internal::win32_iocp_port_state>(capacity)) {
  }

  io_completion_port(const io_completion_port&) = delete;
  io_completion_port& operator=(const io_completion_port&) = delete;

  /**
   * Moves an I/O completion port.
   *
   * @param other Source completion port.
   */
  inline io_completion_port(io_completion_port&& other) noexcept
      : state_(std::move(other.state_)) {
  }

  /**
   * Moves an I/O completion port.
   *
   * @param other Source completion port.
   * @return This completion port.
   */
  inline io_completion_port& operator=(io_completion_port&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    state_ = std::move(other.state_);
    return *this;
  }

  /**
   * Submits a helper-owned single-shot OVERLAPPED operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param handle Handle associated with this completion port.
   * @param start Callable that receives HANDLE and OVERLAPPED&, starts the
   *   native operation, and returns ERROR_SUCCESS, ERROR_IO_PENDING, or another
   *   Win32 error code.
   * @param complete Callable that receives win32_iocp_completion and returns
   *   the resolved value.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * The handle and any buffers referenced by start are not owned and must remain
   * valid until completion. The operation must produce an IOCP completion packet.
   * start and complete execute on the port's pump thread and must not block it.
   * start must return promptly after issuing asynchronous I/O and must not throw
   * after successfully issuing I/O. Start failures can complete asynchronously.
   * Excess submissions fail with ERROR_NOT_ENOUGH_QUOTA before start is called.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(HANDLE handle, Start start, Complete complete) {
    return submit_impl<T>(
        handle, std::move(start), std::move(complete), nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Submits a cancellable helper-owned single-shot OVERLAPPED operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param handle Handle associated with this completion port.
   * @param start Callable that starts the native operation.
   * @param complete Callable that maps the IOCP completion.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * Uses the same pump-thread and lifetime contract as the non-cancellable
   * overload. Cancellation uses CancelIoEx when available, or CancelIo on the
   * issuing pump thread. Keep the registering dispatcher running to deliver
   * cancellation callbacks. After I/O starts, the promise completes only after
   * its native completion packet has been dequeued. Queued cancellation skips
   * the starter entirely.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(
      HANDLE handle,
      Start start,
      Complete complete,
      cancellation cancellation) {
    return submit_impl<T>(
        handle, std::move(start), std::move(complete), &cancellation);
  }
#endif
};
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_WITH_LINUX_IO_URING
/**
 * Linux io_uring based asynchronous I/O queue.
 *
 * @remarks
 * This type requires liburing and Linux io_uring support. Submitted operations
 * are completed while an active dispatcher is parked.
 */
class io_uring {
private:
  std::shared_ptr<internal::io_uring_ring_state> state_;

#if CARDIO_HAS_EXCEPTIONS
  template <typename T>
  static inline promise<T> reject_io_uring_current_exception() {
    if constexpr (std::is_void_v<T>) {
      return rejected();
    } else {
      return rejected<T>();
    }
  }
#endif

  template <typename T, typename Prepare, typename Complete>
  inline promise<T> submit_impl(
      Prepare prepare,
      Complete complete,
      const cancellation* cancellation_signal) {
    auto& current = internal::require_current_dispatcher();
    auto operation = std::shared_ptr<internal::io_uring_operation_state>{};

#if CARDIO_HAS_EXCEPTIONS
    try {
      if (cancellation_signal != nullptr &&
          cancellation_signal->is_cancellation_requested()) {
        if constexpr (std::is_void_v<T>) {
          return rejected(canceled_exception());
        } else {
          return rejected<T>(canceled_exception());
        }
      }
#else
    (void)cancellation_signal;
#endif
      auto result = promise<T>();
      operation = std::make_shared<internal::io_uring_operation_state>();
      auto promise_state = result.state_;
      auto complete_handler =
          std::make_shared<std::decay_t<Complete>>(std::move(complete));
      operation->complete =
          [promise_state, complete_handler](
              io_uring_completion completion) mutable
              -> internal::scheduled_continuation {
        if (promise_state->completed.load(std::memory_order_acquire)) {
          return {};
        }

#if CARDIO_HAS_EXCEPTIONS
        auto exception = std::exception_ptr{};
#endif

        if constexpr (std::is_void_v<T>) {
#if CARDIO_HAS_EXCEPTIONS
          try {
#endif
            (void)std::invoke(*complete_handler, completion);
#if CARDIO_HAS_EXCEPTIONS
          } catch (...) {
            exception = std::current_exception();
          }
#endif

          {
            auto lock = std::lock_guard<::cardio::internal::mutex>(promise_state->mutex);
            if (promise_state->completed.load(std::memory_order_acquire)) {
              return {};
            }
#if CARDIO_HAS_EXCEPTIONS
            if (exception) {
              promise_state->exception = std::move(exception);
            }
#endif
            promise_state->completed.store(true, std::memory_order_release);
          }
        } else {
          auto value = std::optional<T>{};
#if CARDIO_HAS_EXCEPTIONS
          try {
#endif
            value.emplace(std::invoke(*complete_handler, completion));
#if CARDIO_HAS_EXCEPTIONS
          } catch (...) {
            exception = std::current_exception();
          }
#endif

          {
            auto lock = std::lock_guard<::cardio::internal::mutex>(promise_state->mutex);
            if (promise_state->completed.load(std::memory_order_acquire)) {
              return {};
            }
#if CARDIO_HAS_EXCEPTIONS
            if (exception) {
              promise_state->exception = std::move(exception);
            } else {
              promise_state->value.emplace(std::move(*value));
            }
#else
            promise_state->value.emplace(std::move(*value));
#endif
            promise_state->completed.store(true, std::memory_order_release);
          }
        }

        return internal::finish_completed_promise(*promise_state);
      };

#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_signal != nullptr) {
        (void)cancellation_signal->try_register_callback(
            [target = &current, promise_state, operation] {
              operation->cancellation_registration_.reset();
              auto scheduled = internal::scheduled_continuation{};
              if (internal::try_complete_canceled(*promise_state, scheduled)) {
                internal::enqueue_continuation(scheduled);
                target->cancel_io_uring_operation(operation);
              }
            },
            operation->cancellation_registration_);

        if (promise_state->completed.load(std::memory_order_acquire)) {
          return result;
        }
      }
#endif

      current.submit_io_uring_operation(
          state_, std::move(operation), std::move(prepare));
      return result;
#if CARDIO_HAS_EXCEPTIONS
    } catch (...) {
      if (operation) {
        operation->cancellation_registration_.reset();
      }
      return reject_io_uring_current_exception<T>();
    }
#endif
  }

public:
  /**
   * Creates an io_uring queue.
   *
   * @param entries Submission queue entry count.
   *
   * @throws std::invalid_argument Thrown when entries is zero.
   * @throws std::system_error Thrown when liburing initialization fails.
   */
  inline explicit io_uring(unsigned entries = 256) {
    if (entries == 0) {
      internal::fail_invalid_argument(
          "cardio: io_uring entries must not be zero");
    }

    state_ = std::make_shared<internal::io_uring_ring_state>(entries);
  }

  io_uring(const io_uring&) = delete;
  io_uring& operator=(const io_uring&) = delete;

  /**
   * Moves an io_uring queue.
   *
   * @param other Source queue.
   */
  inline io_uring(io_uring&& other) noexcept
      : state_(std::move(other.state_)) {}

  /**
   * Moves an io_uring queue.
   *
   * @param other Source queue.
   * @return This queue.
   */
  inline io_uring& operator=(io_uring&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    state_ = std::move(other.state_);
    return *this;
  }

  /**
   * Submits a single-shot io_uring operation.
   *
   * @tparam T Resolved value type.
   * @tparam Prepare SQE preparation callable type.
   * @tparam Complete CQE completion callable type.
   * @param prepare Callable that receives an io_uring_sqe pointer and prepares
   *   the submission.
   * @param complete Callable that receives io_uring_completion and returns the
   *   resolved value.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * The submitted operation is completed while an active dispatcher is parked.
   * The operation is single-shot; multishot completions require a stream-like
   * API and are not represented by this promise.
   */
  template <typename T, typename Prepare, typename Complete>
  inline promise<T> submit(Prepare prepare, Complete complete) {
    return submit_impl<T>(std::move(prepare), std::move(complete), nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Submits a cancellable single-shot io_uring operation.
   *
   * @tparam T Resolved value type.
   * @tparam Prepare SQE preparation callable type.
   * @tparam Complete CQE completion callable type.
   * @param prepare Callable that receives an io_uring_sqe pointer and prepares
   *   the submission.
   * @param complete Callable that receives io_uring_completion and returns the
   *   resolved value.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * Cancellation completes the returned promise with canceled_exception and
   * submits a best-effort io_uring cancel request for the native operation.
   */
  template <typename T, typename Prepare, typename Complete>
  inline promise<T> submit(
      Prepare prepare,
      Complete complete,
      cancellation cancellation) {
    return submit_impl<T>(
        std::move(prepare), std::move(complete), &cancellation);
  }
#endif
};

#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_WITH_SUPPLEMENTAL
namespace cancellations {

/**
 * Creates a cancellation source canceled when any input cancellation is
 * requested.
 *
 * @param cancellations Input cancellation signals.
 * @return Cancellation source linked to the input signals.
 *
 * @remarks
 * This helper is similar to JavaScript AbortSignal.any(). If any input signal
 * has already been canceled, the returned source is already canceled. If no
 * input is provided, the returned source is not automatically canceled. The
 * current dispatcher is captured when input cancellation callbacks are
 * registered.
 */
inline cancellation_source any(
    std::vector<cancellation> cancellations) {
  auto source = cancellation_source{};
  for (const auto& cancellation_signal : cancellations) {
    if (cancellation_signal.is_cancellation_requested()) {
      (void)source.cancel();
      return source;
    }
  }

  for (auto& cancellation_signal : cancellations) {
    auto registration =
        cancellation_signal.on_cancellation_requested([source]() mutable {
          (void)source.cancel();
        });
    internal::retain_cancellation_registration(
        source, std::move(registration));
  }

  return source;
}

/**
 * Creates a cancellation source canceled when any input cancellation is
 * requested.
 *
 * @tparam Cancellations Input cancellation signal types.
 * @param cancellations Input cancellation signals.
 * @return Cancellation source linked to the input signals.
 *
 * @remarks
 * This overload accepts zero or more cancellation values.
 */
template <
    typename... Cancellations,
    typename = std::enable_if_t<(
        std::is_same_v<std::decay_t<Cancellations>, cancellation> && ...)>>
inline cancellation_source any(Cancellations... cancellations) {
  return any(
      std::vector<cancellation>{std::move(cancellations)...});
}

}  // namespace cancellations

#if CARDIO_HAS_WIN32_HANDLE
/**
 * Event-based helpers that own a single-shot OVERLAPPED and its wait event.
 *
 * @remarks
 * Handles and buffers are borrowed until native completion. XP targets resolve
 * CancelIoEx at runtime. When it is absent, all helpers require a dispatcher
 * bound to the current OS thread (ERROR_NOT_SUPPORTED otherwise), and a HANDLE
 * may have only one outstanding cardio operation (ERROR_BUSY otherwise).
 * Do not mix external I/O on that HANDLE. Keep the dispatcher alive and destroy
 * unfinished promises on its owner thread; destruction cancels and waits for
 * native completion. Violating owner-thread destruction terminates the process.
 * A cancellation request alone never permits early buffer or OVERLAPPED reuse.
 */
namespace win32 {

  namespace internal {
    static_assert(sizeof(DWORD) <= sizeof(std::uint64_t));

    inline bool is_invalid_handle(HANDLE handle) noexcept {
      return handle == nullptr || handle == INVALID_HANDLE_VALUE;
    }

    inline bool is_valid_buffer_size(std::size_t size) noexcept {
      return size <=
             static_cast<std::size_t>((std::numeric_limits<DWORD>::max)());
    }

    inline void set_overlapped_offset(
        OVERLAPPED& overlapped,
        std::uint64_t offset) noexcept {
      overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
      overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    }

    inline void close_handle(HANDLE& handle) noexcept {
      if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
      }

      (void)::CloseHandle(handle);
      handle = nullptr;
    }

    struct overlapped_operation_state {
      HANDLE handle = nullptr;
      HANDLE event = nullptr;
      OVERLAPPED overlapped{};
      std::atomic<bool> pending = false;
      std::atomic<bool> cancellation_requested = false;
      ::cardio::internal::win32_io_lease lease;
      DWORD owner_thread = ::GetCurrentThreadId();

      inline overlapped_operation_state(HANDLE handle_value, HANDLE event_value)
          : handle(handle_value), event(event_value) {
        overlapped.hEvent = event;
        if (lease.is_legacy() &&
            !::cardio::internal::require_current_dispatcher().is_current_thread_owner()) {
          ::cardio::internal::fail_win32_error(
              ERROR_NOT_SUPPORTED,
              "cardio: CancelIo requires a dispatcher bound to the issuing thread");
        }
        const auto error = lease.acquire(handle);
        if (error != ERROR_SUCCESS) {
          ::cardio::internal::fail_win32_error(
              error, "cardio: legacy Win32 I/O handle is already in use");
        }
      }

      inline void finish() noexcept {
        if (pending.exchange(false, std::memory_order_acq_rel)) {
          auto ignored = DWORD{};
          if (::GetOverlappedResult(handle, &overlapped, &ignored, FALSE) == 0 &&
              ::GetLastError() == ERROR_IO_INCOMPLETE) {
            lease.cancel(overlapped, owner_thread);
            (void)::GetOverlappedResult(handle, &overlapped, &ignored, TRUE);
          }
        }
        lease.release();
      }

      inline ~overlapped_operation_state() {
        finish();
        close_handle(event);
      }
    };

    struct overlapped_operation_scope {
      overlapped_operation_state& operation;
      inline ~overlapped_operation_scope() { operation.finish(); }
    };

    template <typename T>
    inline promise<T> invalid_argument_promise(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
      if constexpr (std::is_void_v<T>) {
        return rejected(std::invalid_argument(message));
      } else {
        return rejected<T>(std::invalid_argument(message));
      }
#else
      (void)message;
      std::terminate();
#endif
    }

    template <typename T>
    inline promise<T> win32_error_promise(
        DWORD error,
        const char* message) {
#if CARDIO_HAS_EXCEPTIONS
      auto exception = std::system_error(
          static_cast<int>(error), std::system_category(), message);
      if constexpr (std::is_void_v<T>) {
        return rejected(std::move(exception));
      } else {
        return rejected<T>(std::move(exception));
      }
#else
      (void)error;
      (void)message;
      std::terminate();
#endif
    }

    template <typename T>
    inline promise<T> canceled_promise() {
#if CARDIO_HAS_EXCEPTIONS
      if constexpr (std::is_void_v<T>) {
        return rejected(canceled_exception());
      } else {
        return rejected<T>(canceled_exception());
      }
#else
      std::terminate();
#endif
    }

    [[noreturn]] inline void fail_win32_error(
        DWORD error,
        const char* message) {
#if CARDIO_HAS_EXCEPTIONS
      throw std::system_error(
          static_cast<int>(error), std::system_category(), message);
#else
      (void)error;
      (void)message;
      std::terminate();
#endif
    }

    [[noreturn]] inline void fail_canceled() {
#if CARDIO_HAS_EXCEPTIONS
      throw canceled_exception();
#else
      std::terminate();
#endif
    }

    inline bool is_operation_aborted_error(
        const std::system_error& error) noexcept {
      return error.code().category() == std::system_category() &&
             error.code().value() == static_cast<int>(ERROR_OPERATION_ABORTED);
    }

    template <typename T, typename Start, typename Complete>
    inline promise<T> submit_async(
        std::shared_ptr<overlapped_operation_state> operation,
        Start start,
        Complete complete) {
      auto scope = overlapped_operation_scope{*operation};
      auto start_error = static_cast<DWORD>(
          std::invoke(start, operation->handle, operation->overlapped));

      if (start_error == ERROR_SUCCESS) {
        auto transferred = DWORD{};
        if (::GetOverlappedResult(
                operation->handle,
                &operation->overlapped,
                &transferred,
                FALSE) == 0) {
          fail_win32_error(
              ::GetLastError(), "cardio: GetOverlappedResult failed");
        }
        if constexpr (std::is_void_v<T>) {
          (void)std::invoke(
              complete, win32_overlapped_result{transferred});
          co_return;
        } else {
          co_return std::invoke(
              complete, win32_overlapped_result{transferred});
        }
      }

      if (start_error != ERROR_IO_PENDING) {
        fail_win32_error(
            start_error, "cardio: Win32 I/O operation failed");
      }

      operation->pending.store(true, std::memory_order_release);
      auto completion = win32_overlapped_result{};
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        completion =
            co_await from_win32_overlapped(
                operation->handle, operation->overlapped);
        operation->pending.store(false, std::memory_order_release);
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        throw;
      }
#endif

      if constexpr (std::is_void_v<T>) {
        (void)std::invoke(complete, completion);
        co_return;
      } else {
        co_return std::invoke(complete, completion);
      }
    }

#if CARDIO_HAS_EXCEPTIONS
    template <typename T, typename Start, typename Complete>
    inline promise<T> submit_async(
        std::shared_ptr<overlapped_operation_state> operation,
        Start start,
        Complete complete,
        cancellation cancellation_signal) {
      auto scope = overlapped_operation_scope{*operation};
      auto start_error = static_cast<DWORD>(
          std::invoke(start, operation->handle, operation->overlapped));

      if (start_error == ERROR_SUCCESS) {
        auto transferred = DWORD{};
        if (::GetOverlappedResult(
                operation->handle,
                &operation->overlapped,
                &transferred,
                FALSE) == 0) {
          fail_win32_error(
              ::GetLastError(), "cardio: GetOverlappedResult failed");
        }
        if constexpr (std::is_void_v<T>) {
          (void)std::invoke(
              complete, win32_overlapped_result{transferred});
          co_return;
        } else {
          co_return std::invoke(
              complete, win32_overlapped_result{transferred});
        }
      }

      if (start_error != ERROR_IO_PENDING) {
        fail_win32_error(
            start_error, "cardio: Win32 I/O operation failed");
      }

      operation->pending.store(true, std::memory_order_release);
      auto registration = cancellation_signal.on_cancellation_requested(
          [operation] {
            operation->cancellation_requested.store(
                true, std::memory_order_release);
            if (operation->pending.load(std::memory_order_acquire)) {
              operation->lease.cancel(operation->overlapped, operation->owner_thread);
            }
          });

      auto completion = win32_overlapped_result{};
      try {
        completion =
            co_await from_win32_overlapped(
                operation->handle, operation->overlapped);
        operation->pending.store(false, std::memory_order_release);
      } catch (const std::system_error& error) {
        registration.reset();
        if (operation->cancellation_requested.load(
                std::memory_order_acquire) &&
            is_operation_aborted_error(error)) {
          fail_canceled();
        }
        throw;
      } catch (...) {
        registration.reset();
        throw;
      }

      registration.reset();
      if constexpr (std::is_void_v<T>) {
        (void)std::invoke(complete, completion);
        co_return;
      } else {
        co_return std::invoke(complete, completion);
      }
    }
#endif

    inline std::shared_ptr<overlapped_operation_state> create_operation(
        HANDLE handle) {
      auto event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (event == nullptr) {
        fail_win32_error(::GetLastError(), "cardio: CreateEventW failed");
      }
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
      return std::make_shared<overlapped_operation_state>(handle, event);
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        close_handle(event);
        throw;
      }
#endif
    }

    template <typename T, typename Start, typename Complete>
    inline promise<T> submit(
        HANDLE handle,
        Start start,
        Complete complete) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<T>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        return submit_async<T>(
            create_operation(handle), std::move(start), std::move(complete));
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        if constexpr (std::is_void_v<T>) {
          return rejected();
        } else {
          return rejected<T>();
        }
      }
#endif
    }

#if CARDIO_HAS_EXCEPTIONS
    template <typename T, typename Start, typename Complete>
    inline promise<T> submit(
        HANDLE handle,
        Start start,
        Complete complete,
        cancellation cancellation_signal) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<T>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }
      if (cancellation_signal.is_cancellation_requested()) {
        return canceled_promise<T>();
      }

      try {
        return submit_async<T>(
            create_operation(handle),
            std::move(start),
            std::move(complete),
            std::move(cancellation_signal));
      } catch (...) {
        if constexpr (std::is_void_v<T>) {
          return rejected();
        } else {
          return rejected<T>();
        }
      }
    }
#endif

    template <typename T>
    inline promise<T> reject_too_large_size() {
      return invalid_argument_promise<T>(
          "cardio: Win32 I/O buffer is too large");
    }

    inline promise<std::size_t> read_impl(
        HANDLE handle,
        std::span<std::byte> buffer,
        std::uint64_t offset) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!is_valid_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            set_overlapped_offset(overlapped, offset);
            if (::ReadFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_overlapped_result completion) {
            return static_cast<std::size_t>(completion.bytes_transferred);
          });
    }

#if CARDIO_HAS_EXCEPTIONS
    inline promise<std::size_t> read_impl(
        HANDLE handle,
        std::span<std::byte> buffer,
        std::uint64_t offset,
        cancellation cancellation_signal) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!is_valid_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            set_overlapped_offset(overlapped, offset);
            if (::ReadFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_overlapped_result completion) {
            return static_cast<std::size_t>(completion.bytes_transferred);
          },
          std::move(cancellation_signal));
    }
#endif

    inline promise<std::size_t> write_impl(
        HANDLE handle,
        std::span<const std::byte> buffer,
        std::uint64_t offset) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!is_valid_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            set_overlapped_offset(overlapped, offset);
            if (::WriteFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_overlapped_result completion) {
            return static_cast<std::size_t>(completion.bytes_transferred);
          });
    }

#if CARDIO_HAS_EXCEPTIONS
    inline promise<std::size_t> write_impl(
        HANDLE handle,
        std::span<const std::byte> buffer,
        std::uint64_t offset,
        cancellation cancellation_signal) {
      if (is_invalid_handle(handle)) {
        return invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!is_valid_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            set_overlapped_offset(overlapped, offset);
            if (::WriteFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_overlapped_result completion) {
            return static_cast<std::size_t>(completion.bytes_transferred);
          },
          std::move(cancellation_signal));
    }
#endif
  }  // namespace internal

  /**
   * Submits a helper-owned single-shot Win32 OVERLAPPED operation.
   *
   * @tparam Start Operation starter callable type.
   * @param handle Handle used by the operation.
   * @param start Callable that receives HANDLE and OVERLAPPED&, starts the
   *   native operation, and returns ERROR_SUCCESS, ERROR_IO_PENDING, or another
   *   Win32 error code.
   * @return Promise that resolves with the completed byte count.
   *
   * @remarks
   * The helper owns the OVERLAPPED structure and its manual-reset event until
   * native completion. The handle and any buffers referenced by start are not
   * owned and must remain valid until completion.
   */
  template <typename Start>
  inline promise<win32_overlapped_result> submit(
      HANDLE handle,
      Start start) {
    return internal::submit<win32_overlapped_result>(
        handle,
        std::move(start),
        [](win32_overlapped_result completion) noexcept {
          return completion;
        });
  }

  /**
   * Submits a helper-owned single-shot Win32 OVERLAPPED operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param handle Handle used by the operation.
   * @param start Callable that receives HANDLE and OVERLAPPED&, starts the
   *   native operation, and returns ERROR_SUCCESS, ERROR_IO_PENDING, or another
   *   Win32 error code.
   * @param complete Callable that receives win32_overlapped_result and returns
   *   the resolved value.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * The helper owns the OVERLAPPED structure and its manual-reset event until
   * native completion. The handle and buffers are not owned by the promise.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(
      HANDLE handle,
      Start start,
      Complete complete) {
    return internal::submit<T>(
        handle, std::move(start), std::move(complete));
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Submits a cancellable helper-owned single-shot Win32 OVERLAPPED operation.
   *
   * @tparam Start Operation starter callable type.
   * @param handle Handle used by the operation.
   * @param start Callable that starts the native operation.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the completed byte count.
   *
   * @remarks
   * Cancellation uses CancelIoEx when available, or CancelIo on the issuing
   * thread. Without CancelIoEx, use a thread-bound dispatcher on its owner
   * thread and allow only one pending operation on this HANDLE. The promise
   * completes after native completion; see the win32 namespace lifetime rules.
   */
  template <typename Start>
  inline promise<win32_overlapped_result> submit(
      HANDLE handle,
      Start start,
      cancellation cancellation) {
    return internal::submit<win32_overlapped_result>(
        handle,
        std::move(start),
        [](win32_overlapped_result completion) noexcept {
          return completion;
        },
        std::move(cancellation));
  }

  /**
   * Submits a cancellable helper-owned single-shot Win32 OVERLAPPED operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param handle Handle used by the operation.
   * @param start Callable that starts the native operation.
   * @param complete Callable that maps the completed byte count.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the value returned by complete.
   *
   * @remarks
   * Cancellation uses CancelIoEx when available, or CancelIo on the issuing
   * thread. Without CancelIoEx, use a thread-bound dispatcher on its owner
   * thread and allow only one pending operation on this HANDLE. The promise
   * completes after native completion; see the win32 namespace lifetime rules.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(
      HANDLE handle,
      Start start,
      Complete complete,
      cancellation cancellation) {
    return internal::submit<T>(
        handle, std::move(start), std::move(complete), std::move(cancellation));
  }
#endif

  /**
   * Reads into a buffer with a helper-owned OVERLAPPED operation.
   *
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @return Promise that resolves with the number of bytes read.
   *
   * @remarks
   * The handle and buffer are not owned and must remain valid until completion.
   * The helper uses offset zero.
   */
  inline promise<std::size_t> read(
      HANDLE handle,
      std::span<std::byte> buffer) {
    return internal::read_impl(handle, buffer, 0);
  }

  /**
   * Reads into a buffer at an explicit offset with a helper-owned OVERLAPPED operation.
   *
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param offset File offset used in OVERLAPPED.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      HANDLE handle,
      std::span<std::byte> buffer,
      std::uint64_t offset) {
    return internal::read_impl(handle, buffer, offset);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Reads into a buffer with cancellation support.
   *
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes read.
   *
   * @remarks
   * Cancellation uses CancelIoEx when available, or CancelIo on the issuing
   * thread. Without CancelIoEx, use a thread-bound dispatcher on its owner
   * thread and allow only one pending operation on this HANDLE. The promise
   * completes after native completion; see the win32 namespace lifetime rules.
   */
  inline promise<std::size_t> read(
      HANDLE handle,
      std::span<std::byte> buffer,
      cancellation cancellation) {
    return internal::read_impl(handle, buffer, 0, std::move(cancellation));
  }

  /**
   * Reads into a buffer at an explicit offset with cancellation support.
   *
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param offset File offset used in OVERLAPPED.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      HANDLE handle,
      std::span<std::byte> buffer,
      std::uint64_t offset,
      cancellation cancellation) {
    return internal::read_impl(
        handle, buffer, offset, std::move(cancellation));
  }
#endif

  /**
   * Writes a buffer with a helper-owned OVERLAPPED operation.
   *
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @return Promise that resolves with the number of bytes written.
   *
   * @remarks
   * The handle and buffer are not owned and must remain valid until completion.
   * The helper uses offset zero.
   */
  inline promise<std::size_t> write(
      HANDLE handle,
      std::span<const std::byte> buffer) {
    return internal::write_impl(handle, buffer, 0);
  }

  /**
   * Writes a buffer at an explicit offset with a helper-owned OVERLAPPED operation.
   *
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param offset File offset used in OVERLAPPED.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      HANDLE handle,
      std::span<const std::byte> buffer,
      std::uint64_t offset) {
    return internal::write_impl(handle, buffer, offset);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Writes a buffer with cancellation support.
   *
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes written.
   *
   * @remarks
   * Cancellation uses CancelIoEx when available, or CancelIo on the issuing
   * thread. Without CancelIoEx, use a thread-bound dispatcher on its owner
   * thread and allow only one pending operation on this HANDLE. The promise
   * completes after native completion; see the win32 namespace lifetime rules.
   */
  inline promise<std::size_t> write(
      HANDLE handle,
      std::span<const std::byte> buffer,
      cancellation cancellation) {
    return internal::write_impl(handle, buffer, 0, std::move(cancellation));
  }

  /**
   * Writes a buffer at an explicit offset with cancellation support.
   *
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param offset File offset used in OVERLAPPED.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      HANDLE handle,
      std::span<const std::byte> buffer,
      std::uint64_t offset,
      cancellation cancellation) {
    return internal::write_impl(
        handle, buffer, offset, std::move(cancellation));
  }
#endif

}  // namespace win32

namespace iocps {

  namespace internal {
    inline void fail_if_iocp_completion_failed(
        win32_iocp_completion completion,
        const char* message) {
      if (completion.error != ERROR_SUCCESS) {
        ::cardio::internal::fail_win32_error(completion.error, message);
      }
    }

    inline std::size_t iocp_size_result(
        win32_iocp_completion completion) {
      fail_if_iocp_completion_failed(
          completion, "cardio: Win32 IOCP operation failed");
      return static_cast<std::size_t>(completion.bytes_transferred);
    }

    inline promise<std::size_t> read_impl(
        io_completion_port& port,
        HANDLE handle,
        std::span<std::byte> buffer,
        std::uint64_t offset,
#if CARDIO_HAS_EXCEPTIONS
        const cancellation* cancellation_signal
#else
        const void* cancellation_signal
#endif
        ) {
      (void)cancellation_signal;
      if (win32::internal::is_invalid_handle(handle)) {
        return win32::internal::invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!win32::internal::is_valid_buffer_size(size)) {
        return win32::internal::invalid_argument_promise<std::size_t>(
            "cardio: Win32 I/O buffer is too large");
      }

      auto* data = buffer.data();
#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_signal != nullptr) {
        return port.submit<std::size_t>(
            handle,
            [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
              win32::internal::set_overlapped_offset(overlapped, offset);
              if (::ReadFile(
                      target,
                      data,
                      static_cast<DWORD>(size),
                      nullptr,
                      &overlapped) != 0) {
                return static_cast<DWORD>(ERROR_SUCCESS);
              }
              return ::GetLastError();
            },
            [](win32_iocp_completion completion) {
              return iocp_size_result(completion);
            },
            *cancellation_signal);
      }
#endif
      return port.submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            win32::internal::set_overlapped_offset(overlapped, offset);
            if (::ReadFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_iocp_completion completion) {
            return iocp_size_result(completion);
          });
    }

    inline promise<std::size_t> write_impl(
        io_completion_port& port,
        HANDLE handle,
        std::span<const std::byte> buffer,
        std::uint64_t offset,
#if CARDIO_HAS_EXCEPTIONS
        const cancellation* cancellation_signal
#else
        const void* cancellation_signal
#endif
        ) {
      (void)cancellation_signal;
      if (win32::internal::is_invalid_handle(handle)) {
        return win32::internal::invalid_argument_promise<std::size_t>(
            "cardio: Win32 handle must not be null or INVALID_HANDLE_VALUE");
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }
      if (!win32::internal::is_valid_buffer_size(size)) {
        return win32::internal::invalid_argument_promise<std::size_t>(
            "cardio: Win32 I/O buffer is too large");
      }

      auto* data = buffer.data();
#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_signal != nullptr) {
        return port.submit<std::size_t>(
            handle,
            [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
              win32::internal::set_overlapped_offset(overlapped, offset);
              if (::WriteFile(
                      target,
                      data,
                      static_cast<DWORD>(size),
                      nullptr,
                      &overlapped) != 0) {
                return static_cast<DWORD>(ERROR_SUCCESS);
              }
              return ::GetLastError();
            },
            [](win32_iocp_completion completion) {
              return iocp_size_result(completion);
            },
            *cancellation_signal);
      }
#endif
      return port.submit<std::size_t>(
          handle,
          [data, size, offset](HANDLE target, OVERLAPPED& overlapped) {
            win32::internal::set_overlapped_offset(overlapped, offset);
            if (::WriteFile(
                    target,
                    data,
                    static_cast<DWORD>(size),
                    nullptr,
                    &overlapped) != 0) {
              return static_cast<DWORD>(ERROR_SUCCESS);
            }
            return ::GetLastError();
          },
          [](win32_iocp_completion completion) {
            return iocp_size_result(completion);
          });
    }
  }  // namespace internal

  /**
   * Submits a raw single-shot Win32 IOCP operation.
   *
   * @tparam Start Operation starter callable type.
   * @param port Completion port used to submit the operation.
   * @param handle Handle associated with the completion port.
   * @param start Callable that receives HANDLE and OVERLAPPED& and starts the
   *   native operation.
   * @return Promise that resolves with the IOCP completion fields.
   */
  template <typename Start>
  inline promise<win32_iocp_completion> submit(
      io_completion_port& port,
      HANDLE handle,
      Start start) {
    return port.submit<win32_iocp_completion>(
        handle,
        std::move(start),
        [](win32_iocp_completion completion) noexcept {
          return completion;
        });
  }

  /**
   * Submits a raw single-shot Win32 IOCP operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param port Completion port used to submit the operation.
   * @param handle Handle associated with the completion port.
   * @param start Callable that starts the native operation.
   * @param complete Callable that maps the IOCP completion.
   * @return Promise that resolves with the value returned by complete.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(
      io_completion_port& port,
      HANDLE handle,
      Start start,
      Complete complete) {
    return port.submit<T>(
        handle, std::move(start), std::move(complete));
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Submits a cancellable raw single-shot Win32 IOCP operation.
   *
   * @tparam Start Operation starter callable type.
   * @param port Completion port used to submit the operation.
   * @param handle Handle associated with the completion port.
   * @param start Callable that starts the native operation.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the IOCP completion fields.
   */
  template <typename Start>
  inline promise<win32_iocp_completion> submit(
      io_completion_port& port,
      HANDLE handle,
      Start start,
      cancellation cancellation) {
    return port.submit<win32_iocp_completion>(
        handle,
        std::move(start),
        [](win32_iocp_completion completion) noexcept {
          return completion;
        },
        std::move(cancellation));
  }

  /**
   * Submits a cancellable raw single-shot Win32 IOCP operation.
   *
   * @tparam T Resolved value type.
   * @tparam Start Operation starter callable type.
   * @tparam Complete Completion mapping callable type.
   * @param port Completion port used to submit the operation.
   * @param handle Handle associated with the completion port.
   * @param start Callable that starts the native operation.
   * @param complete Callable that maps the IOCP completion.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the value returned by complete.
   */
  template <typename T, typename Start, typename Complete>
  inline promise<T> submit(
      io_completion_port& port,
      HANDLE handle,
      Start start,
      Complete complete,
      cancellation cancellation) {
    return port.submit<T>(
        handle,
        std::move(start),
        std::move(complete),
        std::move(cancellation));
  }
#endif

  /**
   * Reads into a buffer with a helper-owned IOCP OVERLAPPED operation.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      io_completion_port& port,
      HANDLE handle,
      std::span<std::byte> buffer) {
    return internal::read_impl(
        port, handle, buffer, 0, nullptr);
  }

  /**
   * Reads into a buffer at an explicit offset with a helper-owned IOCP operation.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param offset File offset used in OVERLAPPED.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      io_completion_port& port,
      HANDLE handle,
      std::span<std::byte> buffer,
      std::uint64_t offset) {
    return internal::read_impl(
        port, handle, buffer, offset, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Reads into a buffer with cancellation support.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      io_completion_port& port,
      HANDLE handle,
      std::span<std::byte> buffer,
      cancellation cancellation) {
    return internal::read_impl(
        port, handle, buffer, 0, &cancellation);
  }

  /**
   * Reads into a buffer at an explicit offset with cancellation support.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to read from.
   * @param buffer Destination buffer.
   * @param offset File offset used in OVERLAPPED.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes read.
   */
  inline promise<std::size_t> read(
      io_completion_port& port,
      HANDLE handle,
      std::span<std::byte> buffer,
      std::uint64_t offset,
      cancellation cancellation) {
    return internal::read_impl(
        port, handle, buffer, offset, &cancellation);
  }
#endif

  /**
   * Writes a buffer with a helper-owned IOCP OVERLAPPED operation.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      io_completion_port& port,
      HANDLE handle,
      std::span<const std::byte> buffer) {
    return internal::write_impl(
        port, handle, buffer, 0, nullptr);
  }

  /**
   * Writes a buffer at an explicit offset with a helper-owned IOCP operation.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param offset File offset used in OVERLAPPED.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      io_completion_port& port,
      HANDLE handle,
      std::span<const std::byte> buffer,
      std::uint64_t offset) {
    return internal::write_impl(
        port, handle, buffer, offset, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Writes a buffer with cancellation support.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      io_completion_port& port,
      HANDLE handle,
      std::span<const std::byte> buffer,
      cancellation cancellation) {
    return internal::write_impl(
        port, handle, buffer, 0, &cancellation);
  }

  /**
   * Writes a buffer at an explicit offset with cancellation support.
   *
   * @param port Completion port used to submit the operation.
   * @param handle Handle to write to.
   * @param buffer Source buffer.
   * @param offset File offset used in OVERLAPPED.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes written.
   */
  inline promise<std::size_t> write(
      io_completion_port& port,
      HANDLE handle,
      std::span<const std::byte> buffer,
      std::uint64_t offset,
      cancellation cancellation) {
    return internal::write_impl(
        port, handle, buffer, offset, &cancellation);
  }
#endif

}  // namespace iocps
#endif

#if CARDIO_WITH_GIO
namespace gio {

/**
 * Exception converted from a GIO GError.
 */
class gio_error : public std::runtime_error {
private:
  GQuark domain_ = 0;
  int code_ = 0;

public:
  /**
   * Creates a GIO error exception.
   *
   * @param error Source GError. The message is copied.
   */
  inline explicit gio_error(const GError* error)
      : std::runtime_error(
            error != nullptr && error->message != nullptr
                ? error->message
                : "cardio: GIO operation failed"),
        domain_(error != nullptr ? error->domain : 0),
        code_(error != nullptr ? error->code : 0) {
  }

  /**
   * Gets the GError domain.
   *
   * @return GError domain quark.
   */
  inline GQuark domain() const noexcept {
    return domain_;
  }

  /**
   * Gets the GError code.
   *
   * @return GError code.
   */
  inline int code() const noexcept {
    return code_;
  }
};

/**
 * File contents loaded by a GIO helper.
 */
struct file_contents {
  /**
   * Loaded bytes.
   */
  std::vector<std::byte> bytes;

  /**
   * Entity tag returned by GIO, or an empty string when no etag is returned.
   */
  std::string etag;
};

namespace internal {
  inline bool has_gio_feature() {
    return (get_current_dispatcher().get_feature() & dispatcher_feature::gio) !=
           dispatcher_feature::none;
  }

  template <typename T>
  inline promise<T> invalid_argument_promise(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
    if constexpr (std::is_void_v<T>) {
      return rejected(std::invalid_argument(message));
    } else {
      return rejected<T>(std::invalid_argument(message));
    }
#else
    (void)message;
    std::terminate();
#endif
  }

#if CARDIO_HAS_EXCEPTIONS
  template <typename T>
  inline promise<T> canceled_promise() {
    if constexpr (std::is_void_v<T>) {
      return rejected(canceled_exception());
    } else {
      return rejected<T>(canceled_exception());
    }
  }
#endif

  template <typename T>
  inline promise<T> unsupported_dispatcher_promise() {
    return invalid_argument_promise<T>(
        "cardio: current dispatcher does not support GIO");
  }

  template <typename T>
  inline promise<T> null_argument_promise(const char* name) {
    (void)name;
    return invalid_argument_promise<T>("cardio: GIO argument must not be null");
  }

  template <typename T>
  inline promise<T> too_large_size_promise() {
    return invalid_argument_promise<T>("cardio: GIO buffer is too large");
  }

  inline bool is_valid_buffer_size(std::size_t size) noexcept {
    return size <= static_cast<std::size_t>(G_MAXSSIZE);
  }

  inline void ensure_boolean_result(
      gboolean succeeded,
      GError** error,
      const char* message) {
    if (succeeded || error == nullptr || *error != nullptr) {
      return;
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, message);
  }

  inline std::size_t size_result(
      gssize result,
      GError** error,
      const char* message) {
    if (result < 0) {
      if (error != nullptr && *error == nullptr) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, message);
      }
      return std::size_t{};
    }
    return static_cast<std::size_t>(result);
  }

  template <typename T>
  inline void complete_with_error(promise_source<T>& source, GError* error) {
#if CARDIO_HAS_EXCEPTIONS
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      source.cancel();
    } else {
      source.reject(gio_error(error));
    }
    g_error_free(error);
#else
    if (error != nullptr) {
      g_error_free(error);
    }
    std::terminate();
#endif
  }

  template <typename T, typename Finish>
  struct async_operation_state {
    promise_source<T> source;
    Finish finish;
    GCancellable* cancellable = nullptr;
#if CARDIO_HAS_EXCEPTIONS
    cancellation_registration cancellation_registration_;
#endif

    inline explicit async_operation_state(Finish finish_value)
        : finish(std::move(finish_value)) {
    }

    inline ~async_operation_state() {
      if (cancellable != nullptr) {
        g_object_unref(cancellable);
      }
    }
  };

  template <typename T, typename Finish>
  inline void complete_operation(
      const std::shared_ptr<async_operation_state<T, Finish>>& state,
      GObject* source_object,
      GAsyncResult* result) {
#if CARDIO_HAS_EXCEPTIONS
    state->cancellation_registration_.reset();
#endif

    auto* error = static_cast<GError*>(nullptr);
    if constexpr (std::is_void_v<T>) {
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        std::invoke(state->finish, source_object, result, &error);
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        if (error != nullptr) {
          g_error_free(error);
        }
        state->source.reject();
        return;
      }
#endif
      if (error != nullptr) {
        complete_with_error(state->source, error);
        return;
      }
      state->source.resolve();
    } else {
      auto value = std::optional<T>{};
#if CARDIO_HAS_EXCEPTIONS
      try {
#endif
        value.emplace(std::invoke(state->finish, source_object, result, &error));
#if CARDIO_HAS_EXCEPTIONS
      } catch (...) {
        if (error != nullptr) {
          g_error_free(error);
        }
        state->source.reject();
        return;
      }
#endif
      if (error != nullptr) {
        complete_with_error(state->source, error);
        return;
      }
      state->source.resolve(std::move(*value));
    }
  }

  template <typename T, typename Finish>
  inline void async_ready_callback(
      GObject* source_object,
      GAsyncResult* result,
      gpointer user_data) {
    using state_type = async_operation_state<T, Finish>;
    auto holder = std::unique_ptr<std::shared_ptr<state_type>>(
        static_cast<std::shared_ptr<state_type>*>(user_data));
    auto state = *holder;
    complete_operation(state, source_object, result);
  }

  template <typename T, typename Start, typename Finish>
  inline promise<T> submit_impl(
      Start start,
      Finish finish,
#if CARDIO_HAS_EXCEPTIONS
      const cancellation* cancellation_signal
#else
      const void* cancellation_signal
#endif
      ) {
    if (!has_gio_feature()) {
      return unsupported_dispatcher_promise<T>();
    }

#if CARDIO_HAS_EXCEPTIONS
    if (cancellation_signal != nullptr &&
        cancellation_signal->is_cancellation_requested()) {
      return canceled_promise<T>();
    }
#else
    (void)cancellation_signal;
#endif

    using finish_type = std::decay_t<Finish>;
    using state_type = async_operation_state<T, finish_type>;
    auto state = std::make_shared<state_type>(std::move(finish));
    auto promise = state->source.get_promise();

#if CARDIO_HAS_EXCEPTIONS
    if (cancellation_signal != nullptr) {
      state->cancellable = g_cancellable_new();
      state->cancellation_registration_ =
          cancellation_signal->on_cancellation_requested([state] {
            if (state->cancellable != nullptr) {
              g_cancellable_cancel(state->cancellable);
            }
          });
    }
#endif

    auto* holder = new std::shared_ptr<state_type>(state);
#if CARDIO_HAS_EXCEPTIONS
    try {
#endif
      std::invoke(
          std::move(start),
          state->cancellable,
          &async_ready_callback<T, finish_type>,
          static_cast<gpointer>(holder));
#if CARDIO_HAS_EXCEPTIONS
    } catch (...) {
      delete holder;
      state->cancellation_registration_.reset();
      state->source.reject();
    }
#endif

    return promise;
  }

  template <typename T, typename Object, typename Start, typename Finish>
  inline promise<T> submit_checked(
      Object* object,
      Start start,
      Finish finish) {
    if (object == nullptr) {
      return null_argument_promise<T>("object");
    }
    return submit_impl<T>(std::move(start), std::move(finish), nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  template <typename T, typename Object, typename Start, typename Finish>
  inline promise<T> submit_checked(
      Object* object,
      Start start,
      Finish finish,
      const cancellation& cancellation_signal) {
    if (object == nullptr) {
      return null_argument_promise<T>("object");
    }
    return submit_impl<T>(
        std::move(start), std::move(finish), &cancellation_signal);
  }
#endif
}  // namespace internal

/**
 * Submits a raw single-shot GIO asynchronous operation.
 *
 * @tparam T Resolved value type.
 * @tparam Start Async operation starter callable type.
 * @tparam Finish Async operation finisher callable type.
 * @param start Callable that receives GCancellable*, GAsyncReadyCallback, and
 *   gpointer, then starts a GIO *_async() operation.
 * @param finish Callable that receives GObject*, GAsyncResult*, and GError**,
 *   then calls the matching GIO *_finish() operation.
 * @return Promise that resolves with the value returned by finish.
 *
 * @remarks
 * The current dispatcher must report dispatcher_feature::gio. The helper does
 * not push a GLib thread-default context; callers using a non-default
 * GMainContext must start the operation while that context is thread-default.
 */
template <typename T, typename Start, typename Finish>
inline promise<T> submit(Start start, Finish finish) {
  return internal::submit_impl<T>(std::move(start), std::move(finish), nullptr);
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Submits a cancellable raw single-shot GIO asynchronous operation.
 *
 * @tparam T Resolved value type.
 * @tparam Start Async operation starter callable type.
 * @tparam Finish Async operation finisher callable type.
 * @param start Callable that starts a GIO *_async() operation.
 * @param finish Callable that calls the matching GIO *_finish() operation.
 * @param cancellation Cancellation signal.
 * @return Promise that resolves with the value returned by finish.
 *
 * @remarks
 * Cancellation calls g_cancellable_cancel() for the helper-owned
 * GCancellable. The returned promise completes when the GIO operation invokes
 * its callback.
 */
template <typename T, typename Start, typename Finish>
inline promise<T> submit(
    Start start,
    Finish finish,
    cancellation cancellation) {
  return internal::submit_impl<T>(
      std::move(start), std::move(finish), &cancellation);
}
#endif

/**
 * Reads from a GInputStream asynchronously.
 *
 * @param stream Stream to read from.
 * @param buffer Destination buffer. It must remain valid until completion.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes read.
 */
inline promise<std::size_t> read(
    GInputStream* stream,
    std::span<std::byte> buffer,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_input_stream_read_finish(
                G_INPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_input_stream_read_finish failed");
      });
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Reads from a GInputStream asynchronously with cancellation support.
 *
 * @param stream Stream to read from.
 * @param buffer Destination buffer. It must remain valid until completion.
 * @param cancellation Cancellation signal.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes read.
 */
inline promise<std::size_t> read(
    GInputStream* stream,
    std::span<std::byte> buffer,
    cancellation cancellation,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_input_stream_read_finish(
                G_INPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_input_stream_read_finish failed");
      },
      std::move(cancellation));
}
#endif

/**
 * Reads exactly up to the requested size from a GInputStream asynchronously.
 *
 * @param stream Stream to read from.
 * @param buffer Destination buffer. It must remain valid until completion.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes read.
 */
inline promise<std::size_t> read_all(
    GInputStream* stream,
    std::span<std::byte> buffer,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_all_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto bytes_read = gsize{};
        internal::ensure_boolean_result(
            g_input_stream_read_all_finish(
                G_INPUT_STREAM(source_object), result, &bytes_read, error),
            error,
            "cardio: g_input_stream_read_all_finish failed");
        return static_cast<std::size_t>(bytes_read);
      });
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Reads exactly up to the requested size from a GInputStream asynchronously with
 * cancellation support.
 *
 * @param stream Stream to read from.
 * @param buffer Destination buffer. It must remain valid until completion.
 * @param cancellation Cancellation signal.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes read.
 */
inline promise<std::size_t> read_all(
    GInputStream* stream,
    std::span<std::byte> buffer,
    cancellation cancellation,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_all_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto bytes_read = gsize{};
        internal::ensure_boolean_result(
            g_input_stream_read_all_finish(
                G_INPUT_STREAM(source_object), result, &bytes_read, error),
            error,
            "cardio: g_input_stream_read_all_finish failed");
        return static_cast<std::size_t>(bytes_read);
      },
      std::move(cancellation));
}
#endif

/**
 * Reads bytes from a GInputStream asynchronously.
 *
 * @param stream Stream to read from.
 * @param count Maximum byte count.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GBytes pointer.
 *
 * @remarks
 * The caller must release the returned object with g_bytes_unref().
 */
inline promise<GBytes*> read_bytes(
    GInputStream* stream,
    std::size_t count,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<GBytes*>("stream");
  }
  if (!internal::is_valid_buffer_size(count)) {
    return internal::too_large_size_promise<GBytes*>();
  }
  return submit<GBytes*>(
      [stream, count, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_bytes_async(
            stream,
            static_cast<gsize>(count),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_input_stream_read_bytes_finish(
            G_INPUT_STREAM(source_object), result, error);
      });
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Reads bytes from a GInputStream asynchronously with cancellation support.
 *
 * @param stream Stream to read from.
 * @param count Maximum byte count.
 * @param cancellation Cancellation signal.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GBytes pointer.
 */
inline promise<GBytes*> read_bytes(
    GInputStream* stream,
    std::size_t count,
    cancellation cancellation,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<GBytes*>("stream");
  }
  if (!internal::is_valid_buffer_size(count)) {
    return internal::too_large_size_promise<GBytes*>();
  }
  return submit<GBytes*>(
      [stream, count, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_read_bytes_async(
            stream,
            static_cast<gsize>(count),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_input_stream_read_bytes_finish(
            G_INPUT_STREAM(source_object), result, error);
      },
      std::move(cancellation));
}
#endif

/**
 * Skips bytes from a GInputStream asynchronously.
 *
 * @param stream Stream to skip.
 * @param count Maximum byte count.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes skipped.
 */
inline promise<std::size_t> skip(
    GInputStream* stream,
    std::size_t count,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(count)) {
    return internal::too_large_size_promise<std::size_t>();
  }
  return submit<std::size_t>(
      [stream, count, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_skip_async(
            stream,
            static_cast<gsize>(count),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_input_stream_skip_finish(
                G_INPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_input_stream_skip_finish failed");
      });
}

/**
 * Closes a GInputStream asynchronously.
 *
 * @param stream Stream to close.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when close completes.
 */
inline promise<void> close(
    GInputStream* stream,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      stream,
      [stream, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_input_stream_close_async(
            stream, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_input_stream_close_finish(
                G_INPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_input_stream_close_finish failed");
      });
}

/**
 * Writes to a GOutputStream asynchronously.
 *
 * @param stream Stream to write to.
 * @param buffer Source buffer. It must remain valid until completion.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes written.
 */
inline promise<std::size_t> write(
    GOutputStream* stream,
    std::span<const std::byte> buffer,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  const auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_write_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_output_stream_write_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_write_finish failed");
      });
}

#if CARDIO_HAS_EXCEPTIONS
/**
 * Writes to a GOutputStream asynchronously with cancellation support.
 *
 * @param stream Stream to write to.
 * @param buffer Source buffer. It must remain valid until completion.
 * @param cancellation Cancellation signal.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes written.
 */
inline promise<std::size_t> write(
    GOutputStream* stream,
    std::span<const std::byte> buffer,
    cancellation cancellation,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  const auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_write_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_output_stream_write_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_write_finish failed");
      },
      std::move(cancellation));
}
#endif

/**
 * Writes the whole buffer to a GOutputStream asynchronously.
 *
 * @param stream Stream to write to.
 * @param buffer Source buffer. It must remain valid until completion.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes written.
 */
inline promise<std::size_t> write_all(
    GOutputStream* stream,
    std::span<const std::byte> buffer,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr) {
    return internal::null_argument_promise<std::size_t>("stream");
  }
  if (!internal::is_valid_buffer_size(buffer.size())) {
    return internal::too_large_size_promise<std::size_t>();
  }
  const auto* data = buffer.data();
  const auto size = buffer.size();
  return submit<std::size_t>(
      [stream, data, size, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_write_all_async(
            stream,
            data,
            static_cast<gsize>(size),
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto bytes_written = gsize{};
        internal::ensure_boolean_result(
            g_output_stream_write_all_finish(
                G_OUTPUT_STREAM(source_object), result, &bytes_written, error),
            error,
            "cardio: g_output_stream_write_all_finish failed");
        return static_cast<std::size_t>(bytes_written);
      });
}

/**
 * Writes GBytes to a GOutputStream asynchronously.
 *
 * @param stream Stream to write to.
 * @param bytes Source bytes. The GIO operation holds a reference until completion.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes written.
 */
inline promise<std::size_t> write_bytes(
    GOutputStream* stream,
    GBytes* bytes,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr || bytes == nullptr) {
    return internal::null_argument_promise<std::size_t>("object");
  }
  return submit<std::size_t>(
      [stream, bytes, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_write_bytes_async(
            stream, bytes, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_output_stream_write_bytes_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_write_bytes_finish failed");
      });
}

/**
 * Splices an input stream into a GOutputStream asynchronously.
 *
 * @param stream Destination stream.
 * @param source Source stream.
 * @param flags Splice flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with the number of bytes spliced.
 */
inline promise<std::size_t> splice(
    GOutputStream* stream,
    GInputStream* source,
    GOutputStreamSpliceFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream == nullptr || source == nullptr) {
    return internal::null_argument_promise<std::size_t>("object");
  }
  return submit<std::size_t>(
      [stream, source, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_splice_async(
            stream, source, flags, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return internal::size_result(
            g_output_stream_splice_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_splice_finish failed");
      });
}

/**
 * Flushes a GOutputStream asynchronously.
 *
 * @param stream Stream to flush.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when flush completes.
 */
inline promise<void> flush(
    GOutputStream* stream,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      stream,
      [stream, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_flush_async(
            stream, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_output_stream_flush_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_flush_finish failed");
      });
}

/**
 * Closes a GOutputStream asynchronously.
 *
 * @param stream Stream to close.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when close completes.
 */
inline promise<void> close(
    GOutputStream* stream,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      stream,
      [stream, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_output_stream_close_async(
            stream, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_output_stream_close_finish(
                G_OUTPUT_STREAM(source_object), result, error),
            error,
            "cardio: g_output_stream_close_finish failed");
      });
}

/**
 * Splices two GIOStreams asynchronously.
 *
 * @param stream1 First stream.
 * @param stream2 Second stream.
 * @param flags Splice flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when splice completes.
 */
inline promise<void> splice(
    GIOStream* stream1,
    GIOStream* stream2,
    GIOStreamSpliceFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (stream1 == nullptr || stream2 == nullptr) {
    return internal::null_argument_promise<void>("stream");
  }
  return submit<void>(
      [stream1, stream2, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_io_stream_splice_async(
            stream1, stream2, flags, io_priority, cancellable, callback, user_data);
      },
      [](GObject*, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_io_stream_splice_finish(result, error),
            error,
            "cardio: g_io_stream_splice_finish failed");
      });
}

/**
 * Closes a GIOStream asynchronously.
 *
 * @param stream Stream to close.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when close completes.
 */
inline promise<void> close(
    GIOStream* stream,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      stream,
      [stream, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_io_stream_close_async(
            stream, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_io_stream_close_finish(G_IO_STREAM(source_object), result, error),
            error,
            "cardio: g_io_stream_close_finish failed");
      });
}

/**
 * Opens a GFile for reading asynchronously.
 *
 * @param file File to open.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInputStream.
 */
inline promise<GFileInputStream*> read(
    GFile* file,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileInputStream*>(
      file,
      [file, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_read_async(file, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_read_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Opens a GFile for appending asynchronously.
 *
 * @param file File to open.
 * @param flags File create flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileOutputStream.
 */
inline promise<GFileOutputStream*> append_to(
    GFile* file,
    GFileCreateFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileOutputStream*>(
      file,
      [file, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_append_to_async(
            file, flags, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_append_to_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Creates a GFile asynchronously.
 *
 * @param file File to create.
 * @param flags File create flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileOutputStream.
 */
inline promise<GFileOutputStream*> create(
    GFile* file,
    GFileCreateFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileOutputStream*>(
      file,
      [file, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_create_async(
            file, flags, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_create_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Replaces a GFile asynchronously.
 *
 * @param file File to replace.
 * @param etag Entity tag, or nullptr.
 * @param make_backup Whether GIO should make a backup.
 * @param flags File create flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileOutputStream.
 */
inline promise<GFileOutputStream*> replace(
    GFile* file,
    const char* etag,
    bool make_backup,
    GFileCreateFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileOutputStream*>(
      file,
      [file, etag, make_backup, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_replace_async(
            file,
            etag,
            make_backup ? TRUE : FALSE,
            flags,
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_replace_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Opens a GFile for read-write asynchronously.
 *
 * @param file File to open.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileIOStream.
 */
inline promise<GFileIOStream*> open_readwrite(
    GFile* file,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileIOStream*>(
      file,
      [file, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_open_readwrite_async(
            file, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_open_readwrite_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Creates a GFile for read-write asynchronously.
 *
 * @param file File to create.
 * @param flags File create flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileIOStream.
 */
inline promise<GFileIOStream*> create_readwrite(
    GFile* file,
    GFileCreateFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileIOStream*>(
      file,
      [file, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_create_readwrite_async(
            file, flags, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_create_readwrite_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Replaces a GFile for read-write asynchronously.
 *
 * @param file File to replace.
 * @param etag Entity tag, or nullptr.
 * @param make_backup Whether GIO should make a backup.
 * @param flags File create flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileIOStream.
 */
inline promise<GFileIOStream*> replace_readwrite(
    GFile* file,
    const char* etag,
    bool make_backup,
    GFileCreateFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<GFileIOStream*>(
      file,
      [file, etag, make_backup, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_replace_readwrite_async(
            file,
            etag,
            make_backup ? TRUE : FALSE,
            flags,
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_replace_readwrite_finish(
            G_FILE(source_object), result, error);
      });
}

/**
 * Queries file information asynchronously.
 *
 * @param file File to query.
 * @param attributes Attribute query string.
 * @param flags Query flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInfo.
 */
inline promise<GFileInfo*> query_info(
    GFile* file,
    const char* attributes,
    GFileQueryInfoFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileInfo*>("attributes");
  }
  return internal::submit_checked<GFileInfo*>(
      file,
      [file, attributes, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_query_info_async(
            file,
            attributes,
            flags,
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_query_info_finish(G_FILE(source_object), result, error);
      });
}

/**
 * Queries filesystem information asynchronously.
 *
 * @param file File to query.
 * @param attributes Attribute query string.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInfo.
 */
inline promise<GFileInfo*> query_filesystem_info(
    GFile* file,
    const char* attributes,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileInfo*>("attributes");
  }
  return internal::submit_checked<GFileInfo*>(
      file,
      [file, attributes, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_query_filesystem_info_async(
            file, attributes, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_query_filesystem_info_finish(
            G_FILE(source_object), result, error);
      });
}

/**
 * Enumerates children of a GFile asynchronously.
 *
 * @param file Directory file to enumerate.
 * @param attributes Attribute query string.
 * @param flags Query flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileEnumerator.
 */
inline promise<GFileEnumerator*> enumerate_children(
    GFile* file,
    const char* attributes,
    GFileQueryInfoFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileEnumerator*>("attributes");
  }
  return internal::submit_checked<GFileEnumerator*>(
      file,
      [file, attributes, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_enumerate_children_async(
            file,
            attributes,
            flags,
            io_priority,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_enumerate_children_finish(
            G_FILE(source_object), result, error);
      });
}

/**
 * Deletes a GFile asynchronously.
 *
 * @param file File to delete.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when delete completes.
 */
inline promise<void> delete_file(
    GFile* file,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      file,
      [file, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_delete_async(file, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_file_delete_finish(G_FILE(source_object), result, error),
            error,
            "cardio: g_file_delete_finish failed");
      });
}

/**
 * Trashes a GFile asynchronously.
 *
 * @param file File to trash.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when trash completes.
 */
inline promise<void> trash(
    GFile* file,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      file,
      [file, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_trash_async(file, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_file_trash_finish(G_FILE(source_object), result, error),
            error,
            "cardio: g_file_trash_finish failed");
      });
}

/**
 * Copies a GFile asynchronously.
 *
 * @param source Source file.
 * @param destination Destination file.
 * @param flags Copy flags.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when copy completes.
 *
 * @remarks
 * Progress callbacks are intentionally not modeled by this single-shot helper.
 */
inline promise<void> copy(
    GFile* source,
    GFile* destination,
    GFileCopyFlags flags,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (source == nullptr || destination == nullptr) {
    return internal::null_argument_promise<void>("file");
  }
  return submit<void>(
      [source, destination, flags, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_copy_async(
            source,
            destination,
            flags,
            io_priority,
            cancellable,
            nullptr,
            nullptr,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_file_copy_finish(G_FILE(source_object), result, error),
            error,
            "cardio: g_file_copy_finish failed");
      });
}

/**
 * Creates a directory asynchronously.
 *
 * @param file Directory to create.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when mkdir completes.
 */
inline promise<void> make_directory(
    GFile* file,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      file,
      [file, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_make_directory_async(
            file, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_file_make_directory_finish(G_FILE(source_object), result, error),
            error,
            "cardio: g_file_make_directory_finish failed");
      });
}

/**
 * Loads full file contents asynchronously.
 *
 * @param file File to load.
 * @return Promise that resolves with copied file contents and etag.
 */
inline promise<file_contents> load_contents(GFile* file) {
  return internal::submit_checked<file_contents>(
      file,
      [file](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_load_contents_async(file, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto* contents = static_cast<char*>(nullptr);
        auto length = gsize{};
        auto* etag = static_cast<char*>(nullptr);
        auto loaded = file_contents{};
        const auto succeeded = g_file_load_contents_finish(
            G_FILE(source_object), result, &contents, &length, &etag, error);
        internal::ensure_boolean_result(
            succeeded, error, "cardio: g_file_load_contents_finish failed");
        if (!succeeded) {
          return loaded;
        }
        loaded.bytes.resize(static_cast<std::size_t>(length));
        if (length != 0) {
          std::memcpy(loaded.bytes.data(), contents, static_cast<std::size_t>(length));
        }
        if (etag != nullptr) {
          loaded.etag = etag;
        }
        g_free(contents);
        g_free(etag);
        return loaded;
      });
}

/**
 * Replaces full file contents asynchronously.
 *
 * @param file File to replace.
 * @param contents Source bytes. They must remain valid until completion.
 * @param etag Entity tag, or nullptr.
 * @param make_backup Whether GIO should make a backup.
 * @param flags File create flags.
 * @return Promise that resolves with the new etag, or an empty string.
 */
inline promise<std::string> replace_contents(
    GFile* file,
    std::span<const std::byte> contents,
    const char* etag,
    bool make_backup,
    GFileCreateFlags flags) {
  if (file == nullptr) {
    return internal::null_argument_promise<std::string>("file");
  }
  if (!internal::is_valid_buffer_size(contents.size())) {
    return internal::too_large_size_promise<std::string>();
  }
  const auto* data = reinterpret_cast<const char*>(contents.data());
  const auto size = contents.size();
  return submit<std::string>(
      [file, data, size, etag, make_backup, flags](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_replace_contents_async(
            file,
            data,
            static_cast<gsize>(size),
            etag,
            make_backup ? TRUE : FALSE,
            flags,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto* new_etag = static_cast<char*>(nullptr);
        const auto succeeded = g_file_replace_contents_finish(
            G_FILE(source_object), result, &new_etag, error);
        internal::ensure_boolean_result(
            succeeded, error, "cardio: g_file_replace_contents_finish failed");
        auto etag_result = std::string{};
        if (succeeded && new_etag != nullptr) {
          etag_result = new_etag;
        }
        g_free(new_etag);
        return etag_result;
      });
}

/**
 * Replaces full file contents from GBytes asynchronously.
 *
 * @param file File to replace.
 * @param contents Source bytes. GIO holds a reference until completion.
 * @param etag Entity tag, or nullptr.
 * @param make_backup Whether GIO should make a backup.
 * @param flags File create flags.
 * @return Promise that resolves with the new etag, or an empty string.
 */
inline promise<std::string> replace_contents_bytes(
    GFile* file,
    GBytes* contents,
    const char* etag,
    bool make_backup,
    GFileCreateFlags flags) {
  if (file == nullptr || contents == nullptr) {
    return internal::null_argument_promise<std::string>("object");
  }
  return submit<std::string>(
      [file, contents, etag, make_backup, flags](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_replace_contents_bytes_async(
            file,
            contents,
            etag,
            make_backup ? TRUE : FALSE,
            flags,
            cancellable,
            callback,
            user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        auto* new_etag = static_cast<char*>(nullptr);
        const auto succeeded = g_file_replace_contents_finish(
            G_FILE(source_object), result, &new_etag, error);
        internal::ensure_boolean_result(
            succeeded, error, "cardio: g_file_replace_contents_finish failed");
        auto etag_result = std::string{};
        if (succeeded && new_etag != nullptr) {
          etag_result = new_etag;
        }
        g_free(new_etag);
        return etag_result;
      });
}

/**
 * Reads the next files from a GFileEnumerator asynchronously.
 *
 * @param enumerator Enumerator to read.
 * @param count Maximum number of files.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GList of GFileInfo objects.
 *
 * @remarks
 * The caller must release the returned list with g_list_free_full(list,
 * g_object_unref).
 */
inline promise<GList*> next_files(
    GFileEnumerator* enumerator,
    int count,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (enumerator == nullptr) {
    return internal::null_argument_promise<GList*>("enumerator");
  }
  return submit<GList*>(
      [enumerator, count, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_enumerator_next_files_async(
            enumerator, count, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_enumerator_next_files_finish(
            G_FILE_ENUMERATOR(source_object), result, error);
      });
}

/**
 * Closes a GFileEnumerator asynchronously.
 *
 * @param enumerator Enumerator to close.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves when close completes.
 */
inline promise<void> close(
    GFileEnumerator* enumerator,
    int io_priority = G_PRIORITY_DEFAULT) {
  return internal::submit_checked<void>(
      enumerator,
      [enumerator, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_enumerator_close_async(
            enumerator, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        internal::ensure_boolean_result(
            g_file_enumerator_close_finish(
                G_FILE_ENUMERATOR(source_object), result, error),
            error,
            "cardio: g_file_enumerator_close_finish failed");
      });
}

/**
 * Queries GFileInputStream information asynchronously.
 *
 * @param stream Stream to query.
 * @param attributes Attribute query string.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInfo.
 */
inline promise<GFileInfo*> query_info(
    GFileInputStream* stream,
    const char* attributes,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileInfo*>("attributes");
  }
  return internal::submit_checked<GFileInfo*>(
      stream,
      [stream, attributes, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_input_stream_query_info_async(
            stream, attributes, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_input_stream_query_info_finish(
            G_FILE_INPUT_STREAM(source_object), result, error);
      });
}

/**
 * Queries GFileOutputStream information asynchronously.
 *
 * @param stream Stream to query.
 * @param attributes Attribute query string.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInfo.
 */
inline promise<GFileInfo*> query_info(
    GFileOutputStream* stream,
    const char* attributes,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileInfo*>("attributes");
  }
  return internal::submit_checked<GFileInfo*>(
      stream,
      [stream, attributes, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_output_stream_query_info_async(
            stream, attributes, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_output_stream_query_info_finish(
            G_FILE_OUTPUT_STREAM(source_object), result, error);
      });
}

/**
 * Queries GFileIOStream information asynchronously.
 *
 * @param stream Stream to query.
 * @param attributes Attribute query string.
 * @param io_priority GIO I/O priority.
 * @return Promise that resolves with a caller-owned GFileInfo.
 */
inline promise<GFileInfo*> query_info(
    GFileIOStream* stream,
    const char* attributes,
    int io_priority = G_PRIORITY_DEFAULT) {
  if (attributes == nullptr) {
    return internal::null_argument_promise<GFileInfo*>("attributes");
  }
  return internal::submit_checked<GFileInfo*>(
      stream,
      [stream, attributes, io_priority](
          GCancellable* cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data) {
        g_file_io_stream_query_info_async(
            stream, attributes, io_priority, cancellable, callback, user_data);
      },
      [](GObject* source_object, GAsyncResult* result, GError** error) {
        return g_file_io_stream_query_info_finish(
            G_FILE_IO_STREAM(source_object), result, error);
      });
}

}  // namespace gio
#endif

#if CARDIO_WITH_LINUX_IO_URING
namespace io_urings {

  /**
   * Submits a raw single-shot io_uring operation.
   *
   * @tparam Prepare SQE preparation callable type.
   * @param io Queue used to submit the operation.
   * @param prepare Callable that receives an io_uring_sqe pointer and prepares
   *   the submission.
   * @return Promise that resolves with the copied CQE completion fields.
   */
  template <typename Prepare>
  inline promise<io_uring_completion> submit(
      io_uring& io,
      Prepare prepare) {
    return io.submit<io_uring_completion>(
        std::move(prepare),
        [](io_uring_completion completion) noexcept {
          return completion;
        });
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Submits a cancellable raw single-shot io_uring operation.
   *
   * @tparam Prepare SQE preparation callable type.
   * @param io Queue used to submit the operation.
   * @param prepare Callable that receives an io_uring_sqe pointer and prepares
   *   the submission.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the copied CQE completion fields.
   */
  template <typename Prepare>
  inline promise<io_uring_completion> submit(
      io_uring& io,
      Prepare prepare,
      cancellation cancellation) {
    return io.submit<io_uring_completion>(
        std::move(prepare),
        [](io_uring_completion completion) noexcept {
          return completion;
        },
        std::move(cancellation));
  }
#endif

  namespace internal {
    inline constexpr auto current_file_offset =
        static_cast<std::uint64_t>(-1);

    template <typename T>
    inline promise<T> invalid_argument_promise(const char* message) {
#if CARDIO_HAS_EXCEPTIONS
      if constexpr (std::is_void_v<T>) {
        return rejected(std::invalid_argument(message));
      } else {
        return rejected<T>(std::invalid_argument(message));
      }
#else
      ::cardio::internal::fail_invalid_argument(message);
#endif
    }

    template <typename T, typename Prepare, typename Complete>
    inline promise<T> submit(
        io_uring& io,
        Prepare prepare,
        Complete complete,
        const cancellation* cancellation_signal) {
#if CARDIO_HAS_EXCEPTIONS
      if (cancellation_signal != nullptr) {
        return io.submit<T>(
            std::move(prepare), std::move(complete), *cancellation_signal);
      }
#else
      (void)cancellation_signal;
#endif
      return io.submit<T>(std::move(prepare), std::move(complete));
    }

    template <typename T>
    inline promise<T> reject_negative_fd(int fd) {
      if (fd >= 0) {
        ::cardio::internal::fail_logic_error(
            "cardio: validated fd was rejected");
      }
      return invalid_argument_promise<T>(
          "cardio: file descriptor must not be negative");
    }

    template <typename T>
    inline promise<T> reject_invalid_dfd(int dfd) {
      if (::cardio::internal::is_valid_io_uring_dfd(dfd)) {
        ::cardio::internal::fail_logic_error(
            "cardio: validated dfd was rejected");
      }
      return invalid_argument_promise<T>(
          "cardio: directory file descriptor must be AT_FDCWD or non-negative");
    }

    template <typename T>
    inline promise<T> reject_too_large_size() {
      return invalid_argument_promise<T>(
          "cardio: io_uring buffer is too large");
    }

    inline promise<std::size_t> read_at_impl(
        io_uring& io,
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (fd < 0) {
        return reject_negative_fd<std::size_t>(fd);
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }

      if (!::cardio::internal::is_valid_io_uring_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          io,
          [fd, data, size, offset](::io_uring_sqe* sqe) {
            ::io_uring_prep_read(
                sqe, fd, data, static_cast<unsigned>(size), offset);
          },
          [](io_uring_completion completion) {
            return ::cardio::internal::io_uring_size_result(completion);
          },
          cancellation_signal);
    }

    inline promise<std::size_t> write_at_impl(
        io_uring& io,
        int fd,
        std::span<const std::byte> buffer,
        std::uint64_t offset,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (fd < 0) {
        return reject_negative_fd<std::size_t>(fd);
      }

      const auto size = buffer.size();
      if (size == 0) {
        return resolved(std::size_t{0});
      }

      if (!::cardio::internal::is_valid_io_uring_buffer_size(size)) {
        return reject_too_large_size<std::size_t>();
      }

      auto* data = buffer.data();
      return submit<std::size_t>(
          io,
          [fd, data, size, offset](::io_uring_sqe* sqe) {
            ::io_uring_prep_write(
                sqe, fd, data, static_cast<unsigned>(size), offset);
          },
          [](io_uring_completion completion) {
            return ::cardio::internal::io_uring_size_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> fsync_impl(
        io_uring& io,
        int fd,
        unsigned flags,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (fd < 0) {
        return reject_negative_fd<void>(fd);
      }

      return submit<void>(
          io,
          [fd, flags](::io_uring_sqe* sqe) {
            ::io_uring_prep_fsync(sqe, fd, flags);
          },
          [](io_uring_completion completion) {
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

    inline promise<int> openat_impl(
        io_uring& io,
        int dfd,
        std::string path,
        int flags,
        mode_t mode,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (!::cardio::internal::is_valid_io_uring_dfd(dfd)) {
        return reject_invalid_dfd<int>(dfd);
      }

      auto path_holder = std::make_shared<std::string>(std::move(path));
      return submit<int>(
          io,
          [dfd, path_holder, flags, mode](::io_uring_sqe* sqe) {
            ::io_uring_prep_openat(
                sqe, dfd, path_holder->c_str(), flags, mode);
          },
          [path_holder](io_uring_completion completion) {
            (void)path_holder;
            return ::cardio::internal::io_uring_int_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> close_impl(
        io_uring& io,
        int fd,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (fd < 0) {
        return reject_negative_fd<void>(fd);
      }

      return submit<void>(
          io,
          [fd](::io_uring_sqe* sqe) {
            ::io_uring_prep_close(sqe, fd);
          },
          [](io_uring_completion completion) {
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> statx_impl(
        io_uring& io,
        int dfd,
        std::string path,
        int flags,
        unsigned mask,
        struct statx& statxbuf,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (!::cardio::internal::is_valid_io_uring_dfd(dfd)) {
        return reject_invalid_dfd<void>(dfd);
      }

      auto path_holder = std::make_shared<std::string>(std::move(path));
      auto* statxbuf_ptr = &statxbuf;
      return submit<void>(
          io,
          [dfd, path_holder, flags, mask, statxbuf_ptr](::io_uring_sqe* sqe) {
            ::io_uring_prep_statx(
                sqe, dfd, path_holder->c_str(), flags, mask, statxbuf_ptr);
          },
          [path_holder](io_uring_completion completion) {
            (void)path_holder;
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> renameat_impl(
        io_uring& io,
        int olddfd,
        std::string oldpath,
        int newdfd,
        std::string newpath,
        unsigned flags,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (!::cardio::internal::is_valid_io_uring_dfd(olddfd)) {
        return reject_invalid_dfd<void>(olddfd);
      }
      if (!::cardio::internal::is_valid_io_uring_dfd(newdfd)) {
        return reject_invalid_dfd<void>(newdfd);
      }

      auto oldpath_holder = std::make_shared<std::string>(std::move(oldpath));
      auto newpath_holder = std::make_shared<std::string>(std::move(newpath));
      return submit<void>(
          io,
          [olddfd, oldpath_holder, newdfd, newpath_holder, flags](
              ::io_uring_sqe* sqe) {
            ::io_uring_prep_renameat(
                sqe,
                olddfd,
                oldpath_holder->c_str(),
                newdfd,
                newpath_holder->c_str(),
                flags);
          },
          [oldpath_holder, newpath_holder](io_uring_completion completion) {
            (void)oldpath_holder;
            (void)newpath_holder;
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> unlinkat_impl(
        io_uring& io,
        int dfd,
        std::string path,
        int flags,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (!::cardio::internal::is_valid_io_uring_dfd(dfd)) {
        return reject_invalid_dfd<void>(dfd);
      }

      auto path_holder = std::make_shared<std::string>(std::move(path));
      return submit<void>(
          io,
          [dfd, path_holder, flags](::io_uring_sqe* sqe) {
            ::io_uring_prep_unlinkat(sqe, dfd, path_holder->c_str(), flags);
          },
          [path_holder](io_uring_completion completion) {
            (void)path_holder;
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

    inline promise<void> mkdirat_impl(
        io_uring& io,
        int dfd,
        std::string path,
        mode_t mode,
        const cancellation* cancellation_signal) {
      (void)::cardio::internal::require_current_dispatcher();

      if (!::cardio::internal::is_valid_io_uring_dfd(dfd)) {
        return reject_invalid_dfd<void>(dfd);
      }

      auto path_holder = std::make_shared<std::string>(std::move(path));
      return submit<void>(
          io,
          [dfd, path_holder, mode](::io_uring_sqe* sqe) {
            ::io_uring_prep_mkdirat(sqe, dfd, path_holder->c_str(), mode);
          },
          [path_holder](io_uring_completion completion) {
            (void)path_holder;
            ::cardio::internal::io_uring_void_result(completion);
          },
          cancellation_signal);
    }

  }  // namespace internal

  /**
   * Reads into a buffer asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to read from.
   * @param buffer Destination buffer.
   * @return Promise that resolves with the number of bytes read.
   *
   * @remarks
   * The file descriptor and buffer are not owned by the returned promise. They
   * must remain valid until the operation completes.
   */
  inline promise<std::size_t> read(
      io_uring& io,
      int fd,
      std::span<std::byte> buffer) {
    return internal::read_at_impl(
        io, fd, buffer, internal::current_file_offset, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Reads into a buffer asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to read from.
   * @param buffer Destination buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes read.
   *
   * @remarks
   * Cancellation is best-effort for the native operation. The file descriptor
   * and buffer are not owned by the returned promise.
   */
  inline promise<std::size_t> read(
      io_uring& io,
      int fd,
      std::span<std::byte> buffer,
      cancellation cancellation) {
    return internal::read_at_impl(
        io,
        fd,
        buffer,
        internal::current_file_offset,
        &cancellation);
  }
#endif

  /**
   * Writes a buffer asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to write to.
   * @param buffer Source buffer.
   * @return Promise that resolves with the number of bytes written.
   *
   * @remarks
   * The file descriptor and buffer are not owned by the returned promise. They
   * must remain valid until the operation completes.
   */
  inline promise<std::size_t> write(
      io_uring& io,
      int fd,
      std::span<const std::byte> buffer) {
    return internal::write_at_impl(
        io, fd, buffer, internal::current_file_offset, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Writes a buffer asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to write to.
   * @param buffer Source buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the number of bytes written.
   *
   * @remarks
   * Cancellation is best-effort for the native operation. The file descriptor
   * and buffer are not owned by the returned promise.
   */
  inline promise<std::size_t> write(
      io_uring& io,
      int fd,
      std::span<const std::byte> buffer,
      cancellation cancellation) {
    return internal::write_at_impl(
        io,
        fd,
        buffer,
        internal::current_file_offset,
        &cancellation);
  }
#endif

  /**
   * Synchronizes file data asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to synchronize.
   * @return Promise that resolves when fsync completes.
   */
  inline promise<void> fsync(io_uring& io, int fd) {
    return internal::fsync_impl(io, fd, 0, nullptr);
  }

  /**
   * Synchronizes file data asynchronously with fsync flags.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to synchronize.
   * @param flags io_uring fsync flags.
   * @return Promise that resolves when fsync completes.
   */
  inline promise<void> fsync(io_uring& io, int fd, unsigned flags) {
    return internal::fsync_impl(io, fd, flags, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Synchronizes file data asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to synchronize.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when fsync completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> fsync(
      io_uring& io,
      int fd,
      cancellation cancellation) {
    return internal::fsync_impl(io, fd, 0, &cancellation);
  }

  /**
   * Synchronizes file data asynchronously with fsync flags and cancellation
   * support.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to synchronize.
   * @param flags io_uring fsync flags.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when fsync completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> fsync(
      io_uring& io,
      int fd,
      unsigned flags,
      cancellation cancellation) {
    return internal::fsync_impl(io, fd, flags, &cancellation);
  }
#endif

  /**
   * Opens a path asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param path Path to open. The string is copied until completion.
   * @param flags open flags.
   * @param mode File mode used when creating a file.
   * @return Promise that resolves with the opened file descriptor.
   */
  inline promise<int> open(
      io_uring& io,
      std::string path,
      int flags,
      mode_t mode = 0) {
    return internal::openat_impl(
        io, AT_FDCWD, std::move(path), flags, mode, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Opens a path asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param path Path to open. The string is copied until completion.
   * @param flags open flags.
   * @param mode File mode used when creating a file.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves with the opened file descriptor.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<int> open(
      io_uring& io,
      std::string path,
      int flags,
      mode_t mode,
      cancellation cancellation) {
    return internal::openat_impl(
        io, AT_FDCWD, std::move(path), flags, mode, &cancellation);
  }
#endif

  /**
   * Closes a file descriptor asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to close.
   * @return Promise that resolves when close completes.
   */
  inline promise<void> close(io_uring& io, int fd) {
    return internal::close_impl(io, fd, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Closes a file descriptor asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param fd File descriptor to close.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when close completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> close(
      io_uring& io,
      int fd,
      cancellation cancellation) {
    return internal::close_impl(io, fd, &cancellation);
  }
#endif

  /**
   * Reads file metadata asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param dfd Directory file descriptor or AT_FDCWD.
   * @param path Path to query. The string is copied until completion.
   * @param flags statx flags.
   * @param mask statx field mask.
   * @param statxbuf Destination statx buffer.
   * @return Promise that resolves when statx completes.
   *
   * @remarks
   * statxbuf is not owned by the returned promise and must remain valid until
   * the operation completes.
   */
  inline promise<void> statx(
      io_uring& io,
      int dfd,
      std::string path,
      int flags,
      unsigned mask,
      struct statx& statxbuf) {
    return internal::statx_impl(
        io, dfd, std::move(path), flags, mask, statxbuf, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Reads file metadata asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param dfd Directory file descriptor or AT_FDCWD.
   * @param path Path to query. The string is copied until completion.
   * @param flags statx flags.
   * @param mask statx field mask.
   * @param statxbuf Destination statx buffer.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when statx completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation. statxbuf is not owned
   * by the returned promise.
   */
  inline promise<void> statx(
      io_uring& io,
      int dfd,
      std::string path,
      int flags,
      unsigned mask,
      struct statx& statxbuf,
      cancellation cancellation) {
    return internal::statx_impl(
        io, dfd, std::move(path), flags, mask, statxbuf, &cancellation);
  }
#endif

  /**
   * Renames a path asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param oldpath Existing path. The string is copied until completion.
   * @param newpath New path. The string is copied until completion.
   * @return Promise that resolves when rename completes.
   */
  inline promise<void> rename(
      io_uring& io,
      std::string oldpath,
      std::string newpath) {
    return internal::renameat_impl(
        io,
        AT_FDCWD,
        std::move(oldpath),
        AT_FDCWD,
        std::move(newpath),
        0,
        nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Renames a path asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param oldpath Existing path. The string is copied until completion.
   * @param newpath New path. The string is copied until completion.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when rename completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> rename(
      io_uring& io,
      std::string oldpath,
      std::string newpath,
      cancellation cancellation) {
    return internal::renameat_impl(
        io,
        AT_FDCWD,
        std::move(oldpath),
        AT_FDCWD,
        std::move(newpath),
        0,
        &cancellation);
  }
#endif

  /**
   * Removes a filesystem entry asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param path Path to remove. The string is copied until completion.
   * @param flags unlink flags.
   * @return Promise that resolves when unlink completes.
   */
  inline promise<void> unlink(
      io_uring& io,
      std::string path,
      int flags = 0) {
    return internal::unlinkat_impl(
        io, AT_FDCWD, std::move(path), flags, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Removes a filesystem entry asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param path Path to remove. The string is copied until completion.
   * @param flags unlink flags.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when unlink completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> unlink(
      io_uring& io,
      std::string path,
      int flags,
      cancellation cancellation) {
    return internal::unlinkat_impl(
        io, AT_FDCWD, std::move(path), flags, &cancellation);
  }
#endif

  /**
   * Creates a directory asynchronously.
   *
   * @param io Queue used to submit the operation.
   * @param path Directory path. The string is copied until completion.
   * @param mode Directory mode.
   * @return Promise that resolves when mkdir completes.
   */
  inline promise<void> mkdir(
      io_uring& io,
      std::string path,
      mode_t mode) {
    return internal::mkdirat_impl(
        io, AT_FDCWD, std::move(path), mode, nullptr);
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Creates a directory asynchronously with cancellation support.
   *
   * @param io Queue used to submit the operation.
   * @param path Directory path. The string is copied until completion.
   * @param mode Directory mode.
   * @param cancellation Cancellation signal.
   * @return Promise that resolves when mkdir completes.
   *
   * @remarks
   * Cancellation is best-effort for the native operation.
   */
  inline promise<void> mkdir(
      io_uring& io,
      std::string path,
      mode_t mode,
      cancellation cancellation) {
    return internal::mkdirat_impl(
        io, AT_FDCWD, std::move(path), mode, &cancellation);
  }
#endif

}  // namespace io_urings
#endif

namespace promises {

namespace internal {

template <typename T> struct start_new_return_traits {
  using result_type = T;
  static constexpr bool returns_promise = false;
};

template <typename T> struct start_new_return_traits<promise<T>> {
  using result_type = T;
  static constexpr bool returns_promise = true;
};

template <typename T> struct start_new_state {
  promise_source<T> source;
};

template <typename T>
inline void start_new_complete_rejected(
    const std::shared_ptr<start_new_state<T>>& state) {
#if CARDIO_HAS_EXCEPTIONS
  try {
    throw;
  } catch (const canceled_exception&) {
    (void)state->source.try_cancel();
  } catch (...) {
    (void)state->source.try_reject(std::current_exception());
  }
#else
  (void)state;
#endif
}

template <typename T>
inline promise<void> start_new_watch_async(
    std::shared_ptr<start_new_state<T>> state,
    promise<T> target) {
#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    if constexpr (std::is_void_v<T>) {
      co_await target;
      (void)state->source.try_resolve();
    } else {
      co_await target;
      (void)state->source.try_resolve(
          std::move(target).unsafe_result());
    }
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    start_new_complete_rejected(state);
  }
#endif
}

template <typename T, typename Function>
inline void start_new_run_body(
    const std::shared_ptr<start_new_state<T>>& state,
    dispatcher_host& dispatcher,
    Function& function) {
  using return_type = std::invoke_result_t<Function&>;
  using traits =
      start_new_return_traits<std::decay_t<return_type>>;

  if constexpr (traits::returns_promise) {
    auto target = std::invoke(function);
    auto watcher = start_new_watch_async(state, std::move(target));
    (void)watcher;
    dispatcher.park();
  } else if constexpr (std::is_void_v<return_type>) {
    std::invoke(function);
    (void)state->source.try_resolve();
  } else {
    auto value = std::invoke(function);
    (void)state->source.try_resolve(std::move(value));
  }
}

template <typename T, typename Function>
inline void start_new_worker(
    std::shared_ptr<start_new_state<T>> state,
    Function function) {
#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    dispatcher_host dispatcher;
    set_current_dispatcher(&dispatcher);
    start_new_run_body(state, dispatcher, function);
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    start_new_complete_rejected(state);
  }
#endif
}

}  // namespace internal

/**
 * Runs a callable on a newly created worker thread.
 *
 * @tparam Function Callable object type.
 * @param function Callable to run on the worker thread.
 * @return Promise resolved from the callable result.
 *
 * @remarks
 * This helper creates a worker thread for each call and installs an independent
 * dispatcher_host on that thread. If the callable returns cardio::promise<T>,
 * the worker dispatcher is parked until that promise completes, and the
 * returned promise resolves with T. The current thread must already have a
 * dispatcher because the returned promise belongs to the current dispatcher.
 *
 * Cancellation is cooperative: capture a cardio::cancellation in the callable
 * and pass it to cancellable operations or, when exception support is enabled,
 * call throw_if_cancellation_requested(). The caller dispatcher must outlive
 * the returned promise.
 */
template <typename Function>
inline auto start_new(Function&& function) {
  using function_type = std::decay_t<Function>;
  using return_type = std::invoke_result_t<function_type&>;
  using traits =
      internal::start_new_return_traits<std::decay_t<return_type>>;
  using result_type = typename traits::result_type;

  auto state =
      std::make_shared<internal::start_new_state<result_type>>();
  auto result = state->source.get_promise();

#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    auto worker = ::cardio::internal::thread(
        [state,
         function = function_type(std::forward<Function>(function))]() mutable {
          internal::start_new_worker(state, std::move(function));
        });
    worker.detach();
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    internal::start_new_complete_rejected(state);
  }
#endif

  return result;
}

/**
 * Creates a promise that resolves after a delay.
 *
 * @param msec Delay duration in milliseconds.
 * @return Promise that resolves after the delay expires.
 *
 * @remarks
 * Non-zero delays are observed by the current dispatcher's internal timer
 * queue. The current dispatcher must be parked for the delay to complete.
 */
inline promise<void> delay(std::uint64_t msec) {
  if (msec == 0) {
    return resolved();
  }

  return ::cardio::internal::delay_until_impl(
      ::cardio::internal::deadline_after_milliseconds(msec));
}

}  // namespace promises

namespace cancellations {

/**
 * Creates a cancellation source that requests cancellation after a timeout.
 *
 * @param msec Timeout duration in milliseconds.
 * @return Cancellation source that is canceled after the timeout expires.
 *
 * @remarks
 * This helper is similar to JavaScript AbortSignal.timeout(). A zero duration
 * returns an already canceled source. Non-zero timeouts are observed by the
 * current dispatcher's internal timer queue, so a current dispatcher is
 * required and must be parked for the timeout to fire.
 */
inline cancellation_source timeout(std::uint64_t msec) {
  auto source = cancellation_source{};
  if (msec == 0) {
    (void)source.cancel();
    return source;
  }

  auto& current = internal::require_current_dispatcher();
  const auto deadline = internal::deadline_after_milliseconds(msec);
  internal::configure_timeout_cancellation(source, deadline, &current);
  return source;
}

}  // namespace cancellations

namespace promises {

#if CARDIO_HAS_EXCEPTIONS
/**
 * Creates a cancellable promise that resolves after a delay.
 *
 * @param msec Delay duration in milliseconds.
 * @param cancellation Cancellation signal.
 * @return Promise that resolves after the delay expires.
 *
 * @remarks
 * When cancellation is requested before the delay expires, the promise fails
 * with canceled_exception. Non-zero delays are observed by the current
 * dispatcher's internal timer queue.
 */
inline promise<void> delay(
    std::uint64_t msec,
    cancellation cancellation_signal) {
  if (cancellation_signal.is_cancellation_requested()) {
    return rejected(canceled_exception());
  }

  if (msec == 0) {
    return resolved();
  }

  return ::cardio::internal::delay_until_impl(
      ::cardio::internal::deadline_after_milliseconds(msec),
      std::move(cancellation_signal));
}
#endif

namespace internal {

template <typename T> struct all_result_element {
  using type = std::tuple<T>;
};

template <> struct all_result_element<void> {
  using type = std::tuple<>;
};

template <typename... Ts>
using all_result_tuple = decltype(std::tuple_cat(
    std::declval<typename all_result_element<Ts>::type>()...));

template <typename Tuple> struct all_optional_tuple;

template <typename... Ts> struct all_optional_tuple<std::tuple<Ts...>> {
  using type = std::tuple<std::optional<Ts>...>;
};

template <std::size_t Target, typename... Ts>
inline constexpr std::size_t all_nonvoid_index_before = [] {
  auto count = std::size_t{};
  auto index = std::size_t{};
  ((index++ < Target && !std::is_void_v<Ts> ? ++count : count), ...);
  return count;
}();

template <typename ResultTuple> struct all_tuple_state {
  using optional_tuple_type =
      typename all_optional_tuple<ResultTuple>::type;

  ::cardio::internal::mutex mutex;
  std::size_t remaining;
  bool completed = false;
  optional_tuple_type values;
  promise_source<ResultTuple> source;

  explicit all_tuple_state(std::size_t remaining)
      : remaining(remaining) {
  }

  template <std::size_t Index, typename T>
  inline std::optional<ResultTuple> complete_value(T value) {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return std::nullopt;
    }

    std::get<Index>(values).emplace(std::move(value));
    --remaining;
    if (remaining != 0) {
      return std::nullopt;
    }

    completed = true;
    return make_result();
  }

  inline std::optional<ResultTuple> complete_void() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return std::nullopt;
    }

    --remaining;
    if (remaining != 0) {
      return std::nullopt;
    }

    completed = true;
    return make_result();
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool fail() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return false;
    }

    completed = true;
    return true;
  }
#endif

private:
  template <std::size_t... Indexes>
  inline ResultTuple make_result(
      std::index_sequence<Indexes...>) {
    return ResultTuple(std::move(*std::get<Indexes>(values))...);
  }

  inline ResultTuple make_result() {
    return make_result(
        std::make_index_sequence<std::tuple_size<ResultTuple>::value>{});
  }
};

struct all_void_state {
  ::cardio::internal::mutex mutex;
  std::size_t remaining;
  bool completed = false;
  promise_source<void> source;

  explicit all_void_state(std::size_t remaining)
      : remaining(remaining) {
  }

  inline bool complete_one() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return false;
    }

    --remaining;
    if (remaining != 0) {
      return false;
    }

    completed = true;
    return true;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool fail() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return false;
    }

    completed = true;
    return true;
  }
#endif
};

template <typename T> struct all_vector_state {
  ::cardio::internal::mutex mutex;
  std::size_t remaining;
  bool completed = false;
  std::vector<std::optional<T>> values;
  promise_source<std::vector<T>> source;

  explicit all_vector_state(std::size_t remaining)
      : remaining(remaining),
        values(remaining) {
  }

  inline std::optional<std::vector<T>> complete_value(
      std::size_t index,
      T value) {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return std::nullopt;
    }

    values[index].emplace(std::move(value));
    --remaining;
    if (remaining != 0) {
      return std::nullopt;
    }

    completed = true;

    auto result = std::vector<T>{};
    result.reserve(values.size());
    for (auto& item : values) {
      result.push_back(std::move(*item));
    }
    return result;
  }

#if CARDIO_HAS_EXCEPTIONS
  inline bool fail() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(mutex);
    if (completed) {
      return false;
    }

    completed = true;
    return true;
  }
#endif
};

template <typename State, std::size_t Index, typename T, typename... Ts>
inline promise<void> watch_tuple_promise(
    std::shared_ptr<State> state,
    promise<T> child) {
#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    if constexpr (std::is_void_v<T>) {
      co_await child;
      auto result = state->complete_void();
      if (result) {
        (void)state->source.try_resolve(std::move(*result));
      }
    } else {
      co_await child;
      constexpr auto result_index =
          all_nonvoid_index_before<Index, Ts...>;
      auto result =
          state->template complete_value<result_index>(
              std::move(child).unsafe_result());
      if (result) {
        (void)state->source.try_resolve(std::move(*result));
      }
    }
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    auto exception = std::current_exception();
    if (state->fail()) {
      (void)state->source.try_reject(std::move(exception));
    }
  }
#endif
}

inline promise<void> watch_void_promise(
    std::shared_ptr<all_void_state> state,
    promise<void> child) {
#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    co_await child;
    if (state->complete_one()) {
      (void)state->source.try_resolve();
    }
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    auto exception = std::current_exception();
    if (state->fail()) {
      (void)state->source.try_reject(std::move(exception));
    }
  }
#endif
}

template <typename T>
inline promise<void> watch_vector_promise(
    std::shared_ptr<all_vector_state<T>> state,
    std::size_t index,
    promise<T> child) {
#if CARDIO_HAS_EXCEPTIONS
  try {
#endif
    co_await child;
    auto result = state->complete_value(
        index, std::move(child).unsafe_result());
    if (result) {
      (void)state->source.try_resolve(std::move(*result));
    }
#if CARDIO_HAS_EXCEPTIONS
  } catch (...) {
    auto exception = std::current_exception();
    if (state->fail()) {
      (void)state->source.try_reject(std::move(exception));
    }
  }
#endif
}

template <std::size_t Index, typename... Ts>
inline void start_tuple_watcher(
    std::vector<promise<void>>& watchers,
    std::shared_ptr<all_tuple_state<all_result_tuple<Ts...>>> state,
    promise<std::tuple_element_t<Index, std::tuple<Ts...>>> child) {
  using value_type = std::tuple_element_t<Index, std::tuple<Ts...>>;
  watchers.push_back(watch_tuple_promise<
      all_tuple_state<all_result_tuple<Ts...>>,
      Index,
      value_type,
      Ts...>(std::move(state), std::move(child)));
}

template <typename... Ts, std::size_t... Indexes>
inline std::vector<promise<void>> start_tuple_watchers(
    std::shared_ptr<all_tuple_state<all_result_tuple<Ts...>>> state,
    std::index_sequence<Indexes...>,
    promise<Ts>... promises) {
  auto watchers = std::vector<promise<void>>{};
  watchers.reserve(sizeof...(Ts));
  (start_tuple_watcher<Indexes, Ts...>(
       watchers, state, std::move(promises)),
   ...);
  return watchers;
}

template <typename... Ts>
inline std::vector<promise<void>> start_void_watchers(
    std::shared_ptr<all_void_state> state,
    promise<Ts>... promises) {
  auto watchers = std::vector<promise<void>>{};
  watchers.reserve(sizeof...(Ts));
  (watchers.push_back(
       watch_void_promise(state, std::move(promises))),
   ...);
  return watchers;
}

template <typename ResultTuple, typename... Ts>
inline promise<ResultTuple> all_tuple_impl(promise<Ts>... promises) {
  auto state = std::make_shared<all_tuple_state<ResultTuple>>(sizeof...(Ts));
  auto result = state->source.get_promise();
  auto watchers = start_tuple_watchers<Ts...>(
      state, std::index_sequence_for<Ts...>{}, std::move(promises)...);

  auto& value = co_await result;
  (void)watchers;
  co_return std::move(value);
}

template <typename... Ts>
inline promise<void> all_void_impl(promise<Ts>... promises) {
  auto state = std::make_shared<all_void_state>(sizeof...(Ts));
  auto result = state->source.get_promise();
  auto watchers = start_void_watchers(state, std::move(promises)...);

  co_await result;
  (void)watchers;
}

template <typename T>
inline promise<std::vector<T>> all_vector_impl(
    std::vector<promise<T>> promises) {
  if (promises.empty()) {
    co_return std::vector<T>{};
  }

  auto state = std::make_shared<all_vector_state<T>>(promises.size());
  auto result = state->source.get_promise();
  auto watchers = std::vector<promise<void>>{};
  watchers.reserve(promises.size());
  for (auto index = std::size_t{}; index < promises.size(); ++index) {
    watchers.push_back(watch_vector_promise(
        state, index, std::move(promises[index])));
  }

  auto& value = co_await result;
  (void)watchers;
  co_return std::move(value);
}

inline promise<void> all_vector_void_impl(
    std::vector<promise<void>> promises) {
  if (promises.empty()) {
    co_return;
  }

  auto state = std::make_shared<all_void_state>(promises.size());
  auto result = state->source.get_promise();
  auto watchers = std::vector<promise<void>>{};
  watchers.reserve(promises.size());
  for (auto& promise : promises) {
    watchers.push_back(watch_void_promise(state, std::move(promise)));
  }

  co_await result;
  (void)watchers;
}

}  // namespace internal

/**
 * Creates an already resolved aggregate promise.
 *
 * @return Resolved void promise.
 */
inline promise<void> all() {
  return resolved();
}

/**
 * Creates a promise that resolves when all input promises resolve.
 *
 * @tparam Ts Input promise value types.
 * @param promises Input promises.
 * @return Promise that resolves with a tuple of non-void input values in input
 * order, or a void promise when all inputs are void.
 *
 * @remarks
 * This function owns the input promises. When exceptions are enabled, the
 * aggregate promise fails as soon as the first input promise fails. void input
 * values are omitted from the result tuple.
 */
template <
    typename... Ts,
    typename = std::enable_if_t<(sizeof...(Ts) > 0)>>
inline auto all(promise<Ts>... promises) {
  using result_tuple = internal::all_result_tuple<Ts...>;
  if constexpr (std::tuple_size<result_tuple>::value == 0) {
    return internal::all_void_impl(std::move(promises)...);
  } else {
    return internal::all_tuple_impl<result_tuple>(
        std::move(promises)...);
  }
}

/**
 * Creates a promise that resolves when all input promises resolve.
 *
 * @tparam T Input promise value type.
 * @param promises Input promises.
 * @return Promise that resolves with a vector of input values in input order.
 *
 * @remarks
 * This function owns the input promises. When exceptions are enabled, the
 * aggregate promise fails as soon as the first input promise fails.
 */
template <
    typename T,
    typename = std::enable_if_t<!std::is_void_v<T>>>
inline promise<std::vector<T>> all(std::vector<promise<T>> promises) {
  return internal::all_vector_impl(std::move(promises));
}

/**
 * Creates a promise that resolves when all input void promises resolve.
 *
 * @param promises Input promises.
 * @return Promise that resolves when all input promises resolve.
 *
 * @remarks
 * This function owns the input promises. When exceptions are enabled, the
 * aggregate promise fails as soon as the first input promise fails.
 */
inline promise<void> all(std::vector<promise<void>> promises) {
  return internal::all_vector_void_impl(std::move(promises));
}

}  // namespace promises
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_WITH_PRIMITIVES
namespace primitives {

/**
 * Move-only handle that releases an acquired asynchronous primitive slot.
 *
 * @remarks
 * Destroying an active handle calls release(). release() is idempotent, so it
 * is safe to call explicitly before the handle leaves scope.
 */
class lock_handle {
private:
  std::function<void()> release_;
  bool active_ = false;

public:
  /**
   * Creates an inactive handle.
   */
  inline lock_handle() noexcept = default;

  /**
   * Creates an active handle with a release callback.
   *
   * @param release Callback invoked when the handle is released.
   */
  inline explicit lock_handle(std::function<void()> release)
      : release_(std::move(release)),
        active_(static_cast<bool>(release_)) {
  }

  /**
   * Releases the handle if it is active.
   */
  inline ~lock_handle() {
    release();
  }

  lock_handle(const lock_handle&) = delete;
  lock_handle& operator=(const lock_handle&) = delete;

  /**
   * Moves a handle.
   *
   * @param other Source handle.
   */
  inline lock_handle(lock_handle&& other) noexcept
      : release_(std::move(other.release_)),
        active_(other.active_) {
    other.active_ = false;
  }

  /**
   * Moves a handle.
   *
   * @param other Source handle.
   * @return This handle.
   */
  inline lock_handle& operator=(lock_handle&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    release_ = std::move(other.release_);
    active_ = other.active_;
    other.active_ = false;
    return *this;
  }

  /**
   * Returns whether this handle still owns an acquired slot.
   *
   * @return True when the handle is active.
   */
  inline bool is_active() const noexcept {
    return active_;
  }

  /**
   * Releases the acquired slot.
   *
   * @remarks
   * Calling this function more than once has no effect.
   */
  inline void release() {
    if (!active_) {
      return;
    }

    active_ = false;
    auto release = std::move(release_);
    if (release) {
      release();
    }
  }
};

/**
 * Reader/writer lock queue policy.
 */
enum class reader_writer_lock_policy {
  /**
   * Readers are granted while no writer is active, even when writers are waiting.
   */
  read_preferring,

  /**
   * Writers waiting in the queue block new readers.
   */
  write_preferring
};

namespace internal {

template <typename Waiter>
inline bool erase_waiter(
    std::deque<std::shared_ptr<Waiter>>& waiters,
    const std::shared_ptr<Waiter>& target) noexcept {
  for (auto iterator = waiters.begin(); iterator != waiters.end();
       ++iterator) {
    if (iterator->get() == target.get()) {
      waiters.erase(iterator);
      return true;
    }
  }
  return false;
}

struct mutex_waiter {
  promise_source<lock_handle> source;
#if CARDIO_HAS_EXCEPTIONS
  cancellation_registration registration;
#endif
  bool queued = true;
};

struct mutex_state {
  ::cardio::internal::mutex mutex;
  bool locked = false;
  std::deque<std::shared_ptr<mutex_waiter>> waiters;
};

inline void release_mutex(const std::shared_ptr<mutex_state>& state);

inline lock_handle make_mutex_handle(
    const std::shared_ptr<mutex_state>& state) {
  return lock_handle([state] {
    internal::release_mutex(state);
  });
}

inline void complete_mutex_waiter(
    const std::shared_ptr<mutex_state>& state,
    const std::shared_ptr<mutex_waiter>& waiter) {
#if CARDIO_HAS_EXCEPTIONS
  waiter->registration.reset();
#endif
  (void)waiter->source.try_resolve(make_mutex_handle(state));
}

inline void process_mutex_queue(
    const std::shared_ptr<mutex_state>& state) {
  auto waiter = std::shared_ptr<mutex_waiter>{};
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (state->locked) {
      return;
    }

    while (!state->waiters.empty()) {
      waiter = state->waiters.front();
      state->waiters.pop_front();
      if (waiter && waiter->queued) {
        waiter->queued = false;
        state->locked = true;
        break;
      }
      waiter.reset();
    }
  }

  if (waiter) {
    complete_mutex_waiter(state, waiter);
  }
}

inline void release_mutex(const std::shared_ptr<mutex_state>& state) {
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (!state->locked) {
      return;
    }
    state->locked = false;
  }

  process_mutex_queue(state);
}

#if CARDIO_HAS_EXCEPTIONS
inline void cancel_mutex_waiter(
    std::weak_ptr<mutex_state> weak_state,
    std::weak_ptr<mutex_waiter> weak_waiter) {
  auto state = weak_state.lock();
  auto waiter = weak_waiter.lock();
  if (!state || !waiter) {
    return;
  }

  auto should_cancel = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (waiter->queued) {
      (void)erase_waiter(state->waiters, waiter);
      waiter->queued = false;
      should_cancel = true;
    }
  }

  if (should_cancel) {
    waiter->registration.reset();
    (void)waiter->source.try_cancel();
  }
}
#endif

struct semaphore_waiter {
  promise_source<lock_handle> source;
#if CARDIO_HAS_EXCEPTIONS
  cancellation_registration registration;
#endif
  bool queued = true;
};

struct semaphore_state {
  ::cardio::internal::mutex mutex;
  std::size_t available = 0;
  std::deque<std::shared_ptr<semaphore_waiter>> waiters;
};

inline void release_semaphore(const std::shared_ptr<semaphore_state>& state);

inline lock_handle make_semaphore_handle(
    const std::shared_ptr<semaphore_state>& state) {
  return lock_handle([state] {
    internal::release_semaphore(state);
  });
}

inline void complete_semaphore_waiter(
    const std::shared_ptr<semaphore_state>& state,
    const std::shared_ptr<semaphore_waiter>& waiter) {
#if CARDIO_HAS_EXCEPTIONS
  waiter->registration.reset();
#endif
  (void)waiter->source.try_resolve(make_semaphore_handle(state));
}

inline void process_semaphore_queue(
    const std::shared_ptr<semaphore_state>& state,
    std::vector<std::shared_ptr<semaphore_waiter>>& ready) {
  while (state->available > 0 && !state->waiters.empty()) {
    auto waiter = state->waiters.front();
    state->waiters.pop_front();
    if (!waiter || !waiter->queued) {
      continue;
    }

    waiter->queued = false;
    --state->available;
    ready.push_back(std::move(waiter));
  }
}

inline void release_semaphore(const std::shared_ptr<semaphore_state>& state) {
  auto ready = std::vector<std::shared_ptr<semaphore_waiter>>{};
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    ++state->available;
    process_semaphore_queue(state, ready);
  }

  for (auto& waiter : ready) {
    complete_semaphore_waiter(state, waiter);
  }
}

#if CARDIO_HAS_EXCEPTIONS
inline void cancel_semaphore_waiter(
    std::weak_ptr<semaphore_state> weak_state,
    std::weak_ptr<semaphore_waiter> weak_waiter) {
  auto state = weak_state.lock();
  auto waiter = weak_waiter.lock();
  if (!state || !waiter) {
    return;
  }

  auto should_cancel = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (waiter->queued) {
      (void)erase_waiter(state->waiters, waiter);
      waiter->queued = false;
      should_cancel = true;
    }
  }

  if (should_cancel) {
    waiter->registration.reset();
    (void)waiter->source.try_cancel();
  }
}
#endif

struct reader_writer_waiter {
  promise_source<lock_handle> source;
#if CARDIO_HAS_EXCEPTIONS
  cancellation_registration registration;
#endif
  bool queued = true;
};

struct reader_writer_state {
  explicit reader_writer_state(reader_writer_lock_policy policy)
      : policy(policy) {
  }

  ::cardio::internal::mutex mutex;
  reader_writer_lock_policy policy;
  std::size_t current_readers = 0;
  bool has_writer = false;
  std::deque<std::shared_ptr<reader_writer_waiter>> read_waiters;
  std::deque<std::shared_ptr<reader_writer_waiter>> write_waiters;
};

struct reader_writer_completion {
  std::shared_ptr<reader_writer_waiter> waiter;
  lock_handle handle;
};

inline void release_reader_lock(
    const std::shared_ptr<reader_writer_state>& state);
inline void release_writer_lock(
    const std::shared_ptr<reader_writer_state>& state);

inline lock_handle make_reader_handle(
    const std::shared_ptr<reader_writer_state>& state) {
  return lock_handle([state] {
    internal::release_reader_lock(state);
  });
}

inline lock_handle make_writer_handle(
    const std::shared_ptr<reader_writer_state>& state) {
  return lock_handle([state] {
    internal::release_writer_lock(state);
  });
}

inline void complete_reader_writer_waiters(
    std::vector<reader_writer_completion>& completions) {
  for (auto& completion : completions) {
#if CARDIO_HAS_EXCEPTIONS
    completion.waiter->registration.reset();
#endif
    (void)completion.waiter->source.try_resolve(
        std::move(completion.handle));
  }
}

inline void collect_readers(
    const std::shared_ptr<reader_writer_state>& state,
    std::vector<reader_writer_completion>& completions) {
  while (!state->read_waiters.empty()) {
    auto waiter = state->read_waiters.front();
    state->read_waiters.pop_front();
    if (!waiter || !waiter->queued) {
      continue;
    }

    waiter->queued = false;
    ++state->current_readers;
    completions.push_back(
        reader_writer_completion{std::move(waiter), make_reader_handle(state)});
  }
}

inline bool collect_writer(
    const std::shared_ptr<reader_writer_state>& state,
    std::vector<reader_writer_completion>& completions) {
  while (!state->write_waiters.empty()) {
    auto waiter = state->write_waiters.front();
    state->write_waiters.pop_front();
    if (!waiter || !waiter->queued) {
      continue;
    }

    waiter->queued = false;
    state->has_writer = true;
    completions.push_back(
        reader_writer_completion{std::move(waiter), make_writer_handle(state)});
    return true;
  }
  return false;
}

inline void process_reader_writer_queue(
    const std::shared_ptr<reader_writer_state>& state) {
  auto completions = std::vector<reader_writer_completion>{};
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (state->has_writer) {
      return;
    }

    if (state->policy == reader_writer_lock_policy::write_preferring) {
      if (state->current_readers == 0 &&
          collect_writer(state, completions)) {
      } else if (state->write_waiters.empty()) {
        collect_readers(state, completions);
      }
    } else {
      if (!state->read_waiters.empty()) {
        collect_readers(state, completions);
      } else if (state->current_readers == 0) {
        (void)collect_writer(state, completions);
      }
    }
  }

  complete_reader_writer_waiters(completions);
}

inline void release_reader_lock(
    const std::shared_ptr<reader_writer_state>& state) {
  auto should_process = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (state->current_readers == 0) {
      return;
    }

    --state->current_readers;
    should_process = state->current_readers == 0;
  }

  if (should_process) {
    process_reader_writer_queue(state);
  }
}

inline void release_writer_lock(
    const std::shared_ptr<reader_writer_state>& state) {
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (!state->has_writer) {
      return;
    }
    state->has_writer = false;
  }

  process_reader_writer_queue(state);
}

#if CARDIO_HAS_EXCEPTIONS
inline void cancel_reader_writer_waiter(
    std::weak_ptr<reader_writer_state> weak_state,
    std::weak_ptr<reader_writer_waiter> weak_waiter,
    bool read_waiter) {
  auto state = weak_state.lock();
  auto waiter = weak_waiter.lock();
  if (!state || !waiter) {
    return;
  }

  auto should_cancel = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (waiter->queued) {
      if (read_waiter) {
        (void)erase_waiter(state->read_waiters, waiter);
      } else {
        (void)erase_waiter(state->write_waiters, waiter);
      }
      waiter->queued = false;
      should_cancel = true;
    }
  }

  if (should_cancel) {
    waiter->registration.reset();
    (void)waiter->source.try_cancel();
  }
}
#endif

struct conditional_waiter {
  promise_source<void> source;
#if CARDIO_HAS_EXCEPTIONS
  cancellation_registration registration;
#endif
  bool queued = true;
};

struct conditional_state {
  ::cardio::internal::mutex mutex;
  bool raised = false;
  std::deque<std::shared_ptr<conditional_waiter>> waiters;
};

inline void complete_conditional_waiter(
    const std::shared_ptr<conditional_waiter>& waiter) {
#if CARDIO_HAS_EXCEPTIONS
  waiter->registration.reset();
#endif
  (void)waiter->source.try_resolve();
}

inline std::shared_ptr<conditional_waiter> pop_conditional_waiter(
    conditional_state& state) {
  while (!state.waiters.empty()) {
    auto waiter = state.waiters.front();
    state.waiters.pop_front();
    if (waiter && waiter->queued) {
      waiter->queued = false;
      return waiter;
    }
  }
  return {};
}

#if CARDIO_HAS_EXCEPTIONS
inline void cancel_conditional_waiter(
    std::weak_ptr<conditional_state> weak_state,
    std::weak_ptr<conditional_waiter> weak_waiter) {
  auto state = weak_state.lock();
  auto waiter = weak_waiter.lock();
  if (!state || !waiter) {
    return;
  }

  auto should_cancel = false;
  {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
    if (waiter->queued) {
      (void)erase_waiter(state->waiters, waiter);
      waiter->queued = false;
      should_cancel = true;
    }
  }

  if (should_cancel) {
    waiter->registration.reset();
    (void)waiter->source.try_cancel();
  }
}
#endif

}  // namespace internal

/**
 * Promise-based mutual exclusion primitive.
 */
class mutex {
private:
  std::shared_ptr<internal::mutex_state> state_ =
      std::make_shared<internal::mutex_state>();

public:
  /**
   * Acquires the mutex.
   *
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> lock() {
    auto state = state_;
    auto waiter = std::shared_ptr<internal::mutex_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (!state->locked) {
        state->locked = true;
        return resolved(internal::make_mutex_handle(state));
      }

      waiter = std::make_shared<internal::mutex_waiter>();
      state->waiters.push_back(waiter);
    }

    return waiter->source.get_promise();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Acquires the mutex with cancellation.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> lock(cancellation cancellation_signal) {
    if (cancellation_signal.is_cancellation_requested()) {
      return rejected<lock_handle>(canceled_exception());
    }

    auto state = state_;
    auto waiter = std::shared_ptr<internal::mutex_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (!state->locked) {
        state->locked = true;
        return resolved(internal::make_mutex_handle(state));
      }

      waiter = std::make_shared<internal::mutex_waiter>();
      state->waiters.push_back(waiter);
    }

    waiter->registration =
        cancellation_signal.on_cancellation_requested([
            weak_state = std::weak_ptr<internal::mutex_state>(state),
            weak_waiter = std::weak_ptr<internal::mutex_waiter>(waiter)] {
          internal::cancel_mutex_waiter(
              std::move(weak_state), std::move(weak_waiter));
        });
    return waiter->source.get_promise();
  }
#endif

  /**
   * Returns whether the mutex is currently locked.
   *
   * @return True when the mutex is locked.
   */
  inline bool is_locked() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->locked;
  }

  /**
   * Returns the number of pending lock requests.
   *
   * @return Pending waiter count.
   */
  inline std::size_t pending_count() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->waiters.size();
  }
};

/**
 * Promise-based semaphore primitive.
 */
class semaphore {
private:
  std::shared_ptr<internal::semaphore_state> state_ =
      std::make_shared<internal::semaphore_state>();

public:
  /**
   * Creates a semaphore.
   *
   * @param count Initial available slot count. Must be greater than zero.
   */
  inline explicit semaphore(std::size_t count) {
    if (count == 0) {
#if CARDIO_HAS_EXCEPTIONS
      throw std::invalid_argument(
          "cardio: semaphore count must be greater than 0");
#else
      std::terminate();
#endif
    }

    state_->available = count;
  }

  /**
   * Acquires one semaphore slot.
   *
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> acquire() {
    auto state = state_;
    auto waiter = std::shared_ptr<internal::semaphore_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (state->available > 0) {
        --state->available;
        return resolved(internal::make_semaphore_handle(state));
      }

      waiter = std::make_shared<internal::semaphore_waiter>();
      state->waiters.push_back(waiter);
    }

    return waiter->source.get_promise();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Acquires one semaphore slot with cancellation.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> acquire(cancellation cancellation_signal) {
    if (cancellation_signal.is_cancellation_requested()) {
      return rejected<lock_handle>(canceled_exception());
    }

    auto state = state_;
    auto waiter = std::shared_ptr<internal::semaphore_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (state->available > 0) {
        --state->available;
        return resolved(internal::make_semaphore_handle(state));
      }

      waiter = std::make_shared<internal::semaphore_waiter>();
      state->waiters.push_back(waiter);
    }

    waiter->registration =
        cancellation_signal.on_cancellation_requested([
            weak_state = std::weak_ptr<internal::semaphore_state>(state),
            weak_waiter =
                std::weak_ptr<internal::semaphore_waiter>(waiter)] {
          internal::cancel_semaphore_waiter(
              std::move(weak_state), std::move(weak_waiter));
        });
    return waiter->source.get_promise();
  }
#endif

  /**
   * Returns the currently available slot count.
   *
   * @return Available slot count.
   */
  inline std::size_t available_count() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->available;
  }

  /**
   * Returns the number of pending acquisition requests.
   *
   * @return Pending waiter count.
   */
  inline std::size_t pending_count() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->waiters.size();
  }
};

/**
 * Promise-based reader/writer lock primitive.
 */
class reader_writer_lock {
private:
  std::shared_ptr<internal::reader_writer_state> state_;

  inline bool can_read_immediately() const {
    if (state_->policy == reader_writer_lock_policy::read_preferring) {
      return !state_->has_writer;
    }

    return !state_->has_writer && state_->write_waiters.empty();
  }

public:
  /**
   * Creates a reader/writer lock.
   *
   * @param policy Queue policy. Defaults to write-preferring.
   */
  inline explicit reader_writer_lock(
      reader_writer_lock_policy policy =
          reader_writer_lock_policy::write_preferring)
      : state_(std::make_shared<internal::reader_writer_state>(policy)) {
  }

  /**
   * Acquires a read lock.
   *
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> read_lock() {
    auto state = state_;
    auto waiter = std::shared_ptr<internal::reader_writer_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (can_read_immediately()) {
        ++state->current_readers;
        return resolved(internal::make_reader_handle(state));
      }

      waiter = std::make_shared<internal::reader_writer_waiter>();
      state->read_waiters.push_back(waiter);
    }

    return waiter->source.get_promise();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Acquires a read lock with cancellation.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> read_lock(cancellation cancellation_signal) {
    if (cancellation_signal.is_cancellation_requested()) {
      return rejected<lock_handle>(canceled_exception());
    }

    auto state = state_;
    auto waiter = std::shared_ptr<internal::reader_writer_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (can_read_immediately()) {
        ++state->current_readers;
        return resolved(internal::make_reader_handle(state));
      }

      waiter = std::make_shared<internal::reader_writer_waiter>();
      state->read_waiters.push_back(waiter);
    }

    waiter->registration =
        cancellation_signal.on_cancellation_requested([
            weak_state = std::weak_ptr<internal::reader_writer_state>(state),
            weak_waiter =
                std::weak_ptr<internal::reader_writer_waiter>(waiter)] {
          internal::cancel_reader_writer_waiter(
              std::move(weak_state), std::move(weak_waiter), true);
        });
    return waiter->source.get_promise();
  }
#endif

  /**
   * Acquires a write lock.
   *
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> write_lock() {
    auto state = state_;
    auto waiter = std::shared_ptr<internal::reader_writer_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (!state->has_writer && state->current_readers == 0) {
        state->has_writer = true;
        return resolved(internal::make_writer_handle(state));
      }

      waiter = std::make_shared<internal::reader_writer_waiter>();
      state->write_waiters.push_back(waiter);
    }

    return waiter->source.get_promise();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Acquires a write lock with cancellation.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving to an active lock handle.
   */
  inline promise<lock_handle> write_lock(cancellation cancellation_signal) {
    if (cancellation_signal.is_cancellation_requested()) {
      return rejected<lock_handle>(canceled_exception());
    }

    auto state = state_;
    auto waiter = std::shared_ptr<internal::reader_writer_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state->mutex);
      if (!state->has_writer && state->current_readers == 0) {
        state->has_writer = true;
        return resolved(internal::make_writer_handle(state));
      }

      waiter = std::make_shared<internal::reader_writer_waiter>();
      state->write_waiters.push_back(waiter);
    }

    waiter->registration =
        cancellation_signal.on_cancellation_requested([
            weak_state = std::weak_ptr<internal::reader_writer_state>(state),
            weak_waiter =
                std::weak_ptr<internal::reader_writer_waiter>(waiter)] {
          internal::cancel_reader_writer_waiter(
              std::move(weak_state), std::move(weak_waiter), false);
        });
    return waiter->source.get_promise();
  }
#endif

  /**
   * Returns the active reader count.
   *
   * @return Active reader count.
   */
  inline std::size_t current_readers() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->current_readers;
  }

  /**
   * Returns whether a writer is active.
   *
   * @return True when a writer holds the lock.
   */
  inline bool has_writer() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->has_writer;
  }

  /**
   * Returns the number of pending reader requests.
   *
   * @return Pending reader count.
   */
  inline std::size_t pending_readers_count() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->read_waiters.size();
  }

  /**
   * Returns the number of pending writer requests.
   *
   * @return Pending writer count.
   */
  inline std::size_t pending_writers_count() const noexcept {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
    return state_->write_waiters.size();
  }
};

/**
 * Promise-based conditional primitive that releases one waiter per trigger.
 */
class conditional {
private:
  std::shared_ptr<internal::conditional_state> state_ =
      std::make_shared<internal::conditional_state>();

protected:
  inline explicit conditional(bool raised)
      : state_(std::make_shared<internal::conditional_state>()) {
    state_->raised = raised;
  }

  inline promise<void> wait_when_not_raised() {
    auto waiter = std::shared_ptr<internal::conditional_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      waiter = std::make_shared<internal::conditional_waiter>();
      state_->waiters.push_back(waiter);
    }

    return waiter->source.get_promise();
  }

#if CARDIO_HAS_EXCEPTIONS
  inline promise<void> wait_when_not_raised(
      cancellation cancellation_signal) {
    if (cancellation_signal.is_cancellation_requested()) {
      return rejected(canceled_exception());
    }

    auto waiter = std::shared_ptr<internal::conditional_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      waiter = std::make_shared<internal::conditional_waiter>();
      state_->waiters.push_back(waiter);
    }

    waiter->registration =
        cancellation_signal.on_cancellation_requested([
            weak_state = std::weak_ptr<internal::conditional_state>(state_),
            weak_waiter =
                std::weak_ptr<internal::conditional_waiter>(waiter)] {
          internal::cancel_conditional_waiter(
              std::move(weak_state), std::move(weak_waiter));
        });
    return waiter->source.get_promise();
  }
#endif

  inline std::shared_ptr<internal::conditional_state> state() const noexcept {
    return state_;
  }

public:
  /**
   * Creates a conditional.
   */
  inline conditional() = default;

  /**
   * Waits until trigger() releases this waiter.
   *
   * @return Promise resolving when this waiter is released.
   */
  inline promise<void> wait() {
    return wait_when_not_raised();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Waits until trigger() releases this waiter or cancellation is requested.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving when this waiter is released.
   */
  inline promise<void> wait(cancellation cancellation_signal) {
    return wait_when_not_raised(std::move(cancellation_signal));
  }
#endif

  /**
   * Releases one pending waiter.
   *
   * @remarks
   * If no waiter is pending, the trigger is dropped.
   */
  inline void trigger() {
    auto waiter = std::shared_ptr<internal::conditional_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state_->mutex);
      waiter = internal::pop_conditional_waiter(*state_);
    }

    if (waiter) {
      internal::complete_conditional_waiter(waiter);
    }
  }
};

/**
 * Promise-based conditional primitive with manual raised/dropped state.
 */
class manually_conditional : public conditional {
public:
  /**
   * Creates a manually controlled conditional.
   *
   * @param initial_state Initial raised state.
   */
  inline explicit manually_conditional(bool initial_state = false)
      : conditional(initial_state) {
  }

  /**
   * Waits until the condition is raised or this waiter is triggered.
   *
   * @return Promise resolving when the condition allows this waiter through.
   */
  inline promise<void> wait() {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state()->mutex);
      if (state()->raised) {
        return resolved();
      }
    }

    return wait_when_not_raised();
  }

#if CARDIO_HAS_EXCEPTIONS
  /**
   * Waits until the condition is raised, this waiter is triggered, or
   * cancellation is requested.
   *
   * @param cancellation_signal Cancellation signal for the pending wait.
   * @return Promise resolving when the condition allows this waiter through.
   */
  inline promise<void> wait(cancellation cancellation_signal) {
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state()->mutex);
      if (state()->raised) {
        return resolved();
      }
    }

    return wait_when_not_raised(std::move(cancellation_signal));
  }
#endif

  /**
   * Releases one pending waiter and drops the raised state.
   */
  inline void trigger() {
    auto waiter = std::shared_ptr<internal::conditional_waiter>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state()->mutex);
      state()->raised = false;
      waiter = internal::pop_conditional_waiter(*state());
    }

    if (waiter) {
      internal::complete_conditional_waiter(waiter);
    }
  }

  /**
   * Raises the condition and releases all pending waiters.
   */
  inline void raise() {
    auto waiters = std::vector<std::shared_ptr<internal::conditional_waiter>>{};
    {
      auto lock = std::lock_guard<::cardio::internal::mutex>(state()->mutex);
      state()->raised = true;
      while (auto waiter = internal::pop_conditional_waiter(*state())) {
        waiters.push_back(std::move(waiter));
      }
    }

    for (auto& waiter : waiters) {
      internal::complete_conditional_waiter(waiter);
    }
  }

  /**
   * Drops the raised state.
   */
  inline void drop() {
    auto lock = std::lock_guard<::cardio::internal::mutex>(state()->mutex);
    state()->raised = false;
  }
};

}  // namespace primitives
#endif

//-----------------------------------------------------------------------------------------------

}  // namespace cardio

#endif  // CARDIO_H
