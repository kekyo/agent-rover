// agent-rover - Socket lifetime regression
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.

#include "async_io.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace agent_rover {
void PrintAgentLogEvent(const std::string&) {}
}

static cardio::promise<void> Exercise(int* result) {
  using namespace agent_rover;
  const auto listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listening, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listening, 4))
    throw std::runtime_error("listen failed");
  int size = sizeof(address);
  if (getsockname(listening, reinterpret_cast<sockaddr*>(&address), &size)) throw std::runtime_error("getsockname failed");
  auto listener = AdoptSocket(listening, true);
  for (unsigned iteration = 0; iteration < 4000; ++iteration) {
    std::string endpoint;
    auto accepting = AcceptSocket(listener, &endpoint);
    const auto connected = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(connected, reinterpret_cast<sockaddr*>(&address), sizeof(address))) throw std::runtime_error("connect failed");
    auto client = AdoptSocket(connected, false);
    auto accepted = co_await accepting;
    unsigned char sent = static_cast<unsigned char>(iteration), received = 0;
    co_await WriteSocket(client, std::span(&sent, 1), {});
    co_await ReadSocket(accepted, std::span(&received, 1), {});
    if (received != sent) throw std::runtime_error("payload mismatch");
    co_await DrainSocket(client);
    client.reset();
    co_await DrainSocket(accepted);
    accepted.reset();
    if (iteration % 1000 == 0) { std::printf("completed=%u\n", iteration); std::fflush(stdout); }
  }
  co_await DrainSocket(listener);
  *result = 0;
  std::puts("PASS");
}

int main() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) return 1;
  cardio::dispatcher_host_win32_auto dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr failure) {
    try { std::rethrow_exception(failure); }
    catch (const std::exception& error) { std::fprintf(stderr, "dispatcher failure: %s\n", error.what()); }
    std::fflush(stderr);
    std::_Exit(2);
  });
  int result = 3;
  auto exercise = Exercise(&result);
  dispatcher.park();
  WSACleanup();
  return result;
}
