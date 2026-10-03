// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "async_io.h"
#include "agent_log.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace agent_rover {

cardio::promise<void> AsyncSignal::Wait(cardio::cancellation cancellation) {
  cancellation.throw_if_cancellation_requested();
  if (pending) throw std::logic_error("Concurrent wait on a single-consumer signal.");
  auto source = std::make_shared<cardio::promise_source<void>>();
  pending = source;
  auto registration = cancellation.on_cancellation_requested([source] { source->try_cancel(); });
  try { co_await source->get_promise(); }
  catch (...) { if (pending == source) pending.reset(); throw; }
  if (pending == source) pending.reset();
  cancellation.throw_if_cancellation_requested();
}

void AsyncSignal::Notify() {
  auto source = std::exchange(pending, nullptr);
  if (source) source->try_resolve();
}

struct AsyncSocket {
  SOCKET socket = INVALID_SOCKET;
  WSAEVENT event = WSA_INVALID_EVENT;
  bool closed = false;
  bool eof = false;
  int error = 0;
  AsyncSignal readable, writable;
  cardio::cancellation_source stop;
  cardio::promise<void> notifications;
  ~AsyncSocket() {
    if (socket != INVALID_SOCKET) closesocket(socket);
    if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
  }
};

static std::runtime_error SocketError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + " WSA=" + std::to_string(error));
}

static void CloseSocketState(AsyncSocket* connection);

static cardio::promise<void> DispatchSocketEvents(AsyncSocket* connection) {
  try {
    while (!connection->closed) {
      co_await cardio::from_win32_handle(connection->event, connection->stop.get_cancellation());
      if (connection->closed) break;
      WSANETWORKEVENTS events = {};
      if (WSAEnumNetworkEvents(connection->socket, connection->event, &events) != 0)
        throw SocketError("WSAEnumNetworkEvents", WSAGetLastError());
      if (events.lNetworkEvents & FD_CLOSE) {
        connection->eof = true;
        connection->error = events.iErrorCode[FD_CLOSE_BIT];
        connection->readable.Notify();
        connection->writable.Notify();
      }
      if (events.lNetworkEvents & (FD_READ | FD_ACCEPT)) connection->readable.Notify();
      if (events.lNetworkEvents & FD_WRITE) connection->writable.Notify();
    }
  } catch (const cardio::canceled_exception&) {
  } catch (...) {
    connection->error = WSAECONNABORTED;
    CloseSocketState(connection);
  }
}

SocketConnection AdoptSocket(SOCKET socket, bool listener) {
  auto connection = std::make_shared<AsyncSocket>();
  connection->socket = socket;
  connection->event = WSACreateEvent();
  if (connection->event == WSA_INVALID_EVENT)
    throw SocketError("WSACreateEvent", WSAGetLastError());
  if (WSAEventSelect(socket, connection->event,
      listener ? FD_ACCEPT | FD_CLOSE : FD_READ | FD_WRITE | FD_CLOSE) != 0)
    throw SocketError("WSAEventSelect", WSAGetLastError());
  connection->notifications = DispatchSocketEvents(connection.get());
  return connection;
}

static void CloseSocketState(AsyncSocket* connection) {
  if (!connection || connection->closed) return;
  connection->closed = true;
  connection->stop.cancel();
  const auto socket = std::exchange(connection->socket, INVALID_SOCKET);
  if (socket != INVALID_SOCKET) closesocket(socket);
  connection->readable.Notify();
  connection->writable.Notify();
}

void CloseSocket(const SocketConnection& connection) { CloseSocketState(connection.get()); }

cardio::promise<void> DrainSocket(SocketConnection connection) {
  if (!connection) co_return;
  CloseSocket(connection);
  co_await connection->notifications;
}

cardio::promise<SocketConnection> AcceptSocket(SocketConnection connection, std::string* endpoint) {
  while (!connection->closed) {
    sockaddr_in address = {};
    int length = sizeof(address);
    const auto accepted = accept(connection->socket, reinterpret_cast<sockaddr*>(&address), &length);
    if (accepted != INVALID_SOCKET) {
      const auto host = inet_ntoa(address.sin_addr);
      *endpoint = std::string(host ? host : "unknown") + ":" + std::to_string(ntohs(address.sin_port));
      co_return AdoptSocket(accepted, false);
    }
    const auto error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) throw SocketError("accept", error);
    co_await connection->readable.Wait(connection->stop.get_cancellation());
  }
  throw std::runtime_error("Listener closed.");
}

cardio::promise<void> ReadSocket(SocketConnection connection, std::span<unsigned char> bytes,
    cardio::cancellation cancellation) {
  while (!bytes.empty()) {
    cancellation.throw_if_cancellation_requested();
    if (connection->closed) throw std::runtime_error("Socket closed.");
    const auto count = recv(connection->socket, reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(std::min<size_t>(bytes.size(), 65536)), 0);
    if (count > 0) { bytes = bytes.subspan(count); continue; }
    if (count == 0) throw std::runtime_error("Peer closed the connection.");
    const auto error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) throw SocketError("recv", error);
    if (connection->eof) throw SocketError("Peer closed the connection", connection->error);
    co_await connection->readable.Wait(cancellation);
  }
}

cardio::promise<void> WriteSocket(SocketConnection connection, std::span<const unsigned char> bytes,
    cardio::cancellation cancellation) {
  bool reported_wait = false;
  while (!bytes.empty()) {
    cancellation.throw_if_cancellation_requested();
    if (connection->closed || connection->eof) throw std::runtime_error("Socket closed while sending.");
    const auto count = send(connection->socket, reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(std::min<size_t>(bytes.size(), 65536)), 0);
    if (count > 0) { bytes = bytes.subspan(count); continue; }
    const auto error = WSAGetLastError();
    if (count == 0 || error != WSAEWOULDBLOCK) throw SocketError("send", error);
    if (!reported_wait) {
      PrintAgentLogEvent("socket=" + std::to_string(connection->socket) + " phase=send-backpressure remainingBytes=" +
          std::to_string(bytes.size()) + " wsaCode=" + std::to_string(error));
      reported_wait = true;
    }
    co_await connection->writable.Wait(cancellation);
  }
}

}  // namespace agent_rover
