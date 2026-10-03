// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_ASYNC_IO_H
#define AGENT_ROVER_WINDOWS_ASYNC_IO_H

#include <winsock2.h>
#include <cardio.h>
#include <memory>
#include <span>
#include <string>

namespace agent_rover {

/** Single-consumer notification owned by the GUI dispatcher. */
struct AsyncSignal {
  /** Pending notification, or null when nobody is waiting. */
  std::shared_ptr<cardio::promise_source<void>> pending;
  /** Waits for the next notification; callers must first check their condition.
   * @param cancellation Signal ending this wait. @return Completion promise. */
  cardio::promise<void> Wait(cardio::cancellation cancellation);
  /** Wakes the current waiter; notifications without a waiter are coalesced. */
  void Notify();
};

/** Socket state confined to one dispatcher; destruction follows native wait completion. */
struct AsyncSocket;
/** Shared socket ownership, retained by every outstanding operation. */
using SocketConnection = std::shared_ptr<AsyncSocket>;

/** Adopts a socket and enables nonblocking event notifications.
 * @param socket Socket whose ownership transfers on entry.
 * @param listener Whether accept notifications are required. @return Owned socket. */
SocketConnection AdoptSocket(SOCKET socket, bool listener);
/** Accepts a connection without blocking the dispatcher.
 * @param listener Listening socket. @param endpoint Receives the peer endpoint.
 * @return New owned connection. */
cardio::promise<SocketConnection> AcceptSocket(SocketConnection listener, std::string* endpoint);
/** Reads exactly the supplied buffer or throws on EOF/failure/cancellation.
 * @param socket Connection. @param bytes Destination retained by the caller until completion.
 * @param cancellation Deadline or shutdown signal. @return Completion promise. */
cardio::promise<void> ReadSocket(SocketConnection socket, std::span<unsigned char> bytes,
    cardio::cancellation cancellation);
/** Writes the complete supplied buffer with partial-write handling.
 * @param socket Connection. @param bytes Data retained until completion.
 * @param cancellation Deadline or shutdown signal. @return Completion promise. */
cardio::promise<void> WriteSocket(SocketConnection socket, std::span<const unsigned char> bytes,
    cardio::cancellation cancellation);
/** Closes the socket and wakes all waiters. Safe to call repeatedly.
 * @param socket Connection to close. */
void CloseSocket(const SocketConnection& socket);
/** Finishes event-wait cancellation before releasing the native event handle.
 * @param socket Closed connection retained until completion. @return Completion promise. */
cardio::promise<void> DrainSocket(SocketConnection socket);

}  // namespace agent_rover
#endif
