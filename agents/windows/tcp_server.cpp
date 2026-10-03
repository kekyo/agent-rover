// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "async_io.h"
#include "tcp_server.h"
#include "agent_gui.h"
#include "agent_log.h"
#include "auth.h"
#include "frame.h"
#include "json_protocol.h"
#include "operation_worker.h"
#include "version_banner.h"
#include "win32_util.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <list>
#include <stdexcept>

namespace agent_rover {

static constexpr size_t kMaxConnections = 16, kMaxWorkers = 8, kMaxRequests = 64;
static constexpr size_t kConnectionQueueBytes = 32 * 1024 * 1024;
static constexpr size_t kServerQueueBytes = 64 * 1024 * 1024;
static constexpr uint32_t kAuthenticationMs = 10000, kFrameMs = 30000, kOperationMs = 180000;
using MonotonicClock = std::chrono::steady_clock;

struct QueuedRequest {
  Frame frame;
  std::string id, method;
  MonotonicClock::time_point received;
};

struct ServerState;
struct ClientSession {
  std::shared_ptr<ServerState> server;
  SocketConnection socket;
  uint32_t id;
  bool closed = false;
  cardio::cancellation_source stop;
  std::deque<QueuedRequest> requests;
  std::deque<Frame> output;
  size_t request_bytes = 0, output_bytes = 0;
  AsyncSignal request_ready, output_ready, output_space;
  Worker worker;
  cardio::promise<void> lifetime;
};

struct ServerState {
  ServerOptions options;
  SocketConnection listener;
  AgentGuiHandle gui;
  cardio::cancellation_source stop;
  std::list<std::shared_ptr<ClientSession>> sessions;
  // Failed reaping occupies a slot permanently instead of spawning unbounded replacements.
  std::vector<Worker> quarantine;
  AsyncSignal sessions_changed;
  size_t workers = 0, request_bytes = 0, output_bytes = 0, reading_bytes = 0;
  bool desktop_busy = false;
  std::deque<std::shared_ptr<cardio::promise_source<void>>> desktop_waiters;
};

static Frame JsonFrame(const std::string& text) { return {FrameKind::Json, {text.begin(), text.end()}}; }

static void CloseSession(const std::shared_ptr<ClientSession>& session, const std::string& reason) {
  if (session->closed) return;
  session->closed = true;
  session->stop.cancel();
  CloseSocket(session->socket);
  session->request_ready.Notify();
  session->output_ready.Notify();
  session->output_space.Notify();
  PrintAgentLogEvent(CreateAgentConnectionDisconnectedLogEvent(session->id, reason));
}

static void StopServer(const std::shared_ptr<ServerState>& server) {
  if (server->stop.get_cancellation().is_cancellation_requested()) return;
  server->stop.cancel();
  CloseSocket(server->listener);
  for (const auto& session : server->sessions) CloseSession(session, "agent shutdown");
  server->sessions_changed.Notify();
  SetAgentGuiStatus(server->gui, "Stopping...");
}

static void EnqueueOutput(const std::shared_ptr<ClientSession>& session, Frame frame) {
  if (session->closed) throw std::runtime_error("Connection closed.");
  const auto size = frame.payload.size() + kFrameHeaderBytes;
  if (session->output.size() >= kMaxRequests || session->output_bytes + size > kConnectionQueueBytes ||
      session->server->output_bytes + size > kServerQueueBytes)
    throw std::runtime_error("Outbound queue limit exceeded.");
  session->output_bytes += size;
  session->server->output_bytes += size;
  session->output.push_back(std::move(frame));
  session->output_ready.Notify();
}

static cardio::promise<void> SendResponses(std::shared_ptr<ClientSession> session) {
  try {
    while (!session->closed) {
      if (session->output.empty()) {
        co_await session->output_ready.Wait(session->stop.get_cancellation());
        continue;
      }
      const auto& frame = session->output.front();
      const auto encoded = EncodeFrame(frame);
      auto timeout = cardio::cancellations::timeout(kFrameMs);
      auto cancelled = cardio::cancellations::any(timeout.get_cancellation(), session->stop.get_cancellation());
      co_await WriteSocket(session->socket, encoded, cancelled.get_cancellation());
      session->output_bytes -= encoded.size();
      session->server->output_bytes -= encoded.size();
      session->output.pop_front();
      session->output_space.Notify();
    }
  } catch (const std::exception& error) { CloseSession(session, std::string("send: ") + error.what()); }
}

static cardio::promise<Frame> ReceiveFrame(std::shared_ptr<ClientSession> session,
    cardio::cancellation cancellation, bool idle_allowed) {
  unsigned char bytes[kFrameHeaderBytes] = {};
  if (idle_allowed) co_await ReadSocket(session->socket, {bytes, 1}, cancellation);
  auto timeout = cardio::cancellations::timeout(kFrameMs);
  auto cancelled = cardio::cancellations::any(cancellation, timeout.get_cancellation());
  co_await ReadSocket(session->socket, {bytes + (idle_allowed ? 1 : 0),
      static_cast<size_t>(kFrameHeaderBytes - (idle_allowed ? 1 : 0))}, cancelled.get_cancellation());
  FrameHeader header = {};
  std::string error;
  if (!DecodeFrameHeader(bytes, &header, &error)) throw std::runtime_error(error);
  if (header.payload_length > kMaxJsonPayloadBytes ||
      session->server->reading_bytes + header.payload_length > kServerQueueBytes)
    throw std::runtime_error("Incoming frame limit exceeded.");
  session->server->reading_bytes += header.payload_length;
  Frame frame = {header.kind, {}};
  try {
    frame.payload.resize(header.payload_length);
    co_await ReadSocket(session->socket, frame.payload, cancelled.get_cancellation());
  } catch (...) { session->server->reading_bytes -= header.payload_length; throw; }
  session->server->reading_bytes -= header.payload_length;
  co_return frame;
}

static bool UsesDesktop(const std::string& method) {
  return method.starts_with("window.") || method.starts_with("clipboard.") ||
      method.starts_with("input.") || method == "applications.launch" || method == "process.launchManaged";
}

static void ReleaseDesktop(const std::shared_ptr<ServerState>& server) {
  while (!server->desktop_waiters.empty()) {
    auto next = server->desktop_waiters.front();
    server->desktop_waiters.pop_front();
    if (next->try_resolve()) return;
  }
  server->desktop_busy = false;
}

static cardio::promise<void> AcquireDesktop(std::shared_ptr<ServerState> server,
    cardio::cancellation cancellation) {
  cancellation.throw_if_cancellation_requested();
  if (!server->desktop_busy) { server->desktop_busy = true; co_return; }
  auto source = std::make_shared<cardio::promise_source<void>>();
  server->desktop_waiters.push_back(source);
  auto registration = cancellation.on_cancellation_requested([source] { source->try_cancel(); });
  try { co_await source->get_promise(); }
  catch (...) { std::erase(server->desktop_waiters, source); throw; }
  // A grant racing a cancellation still owns the permit; the caller releases it.
}

static cardio::promise<void> ExecuteRequests(std::shared_ptr<ClientSession> session) {
  try {
    while (!session->closed) {
      if (session->requests.empty()) {
        co_await session->request_ready.Wait(session->stop.get_cancellation());
        continue;
      }
      auto request = std::move(session->requests.front());
      session->requests.pop_front();
      const auto size = request.frame.payload.size() + kFrameHeaderBytes;
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(MonotonicClock::now() - request.received).count();
      if (elapsed >= kOperationMs) {
        if (!request.id.empty()) EnqueueOutput(session, JsonFrame(CreateAgentFailure(request.id, "TIMEOUT", "Operation expired before execution.")));
        session->request_bytes -= size;
        session->server->request_bytes -= size;
        continue;
      }
      auto timeout = cardio::cancellations::timeout(kOperationMs - elapsed);
      auto cancel = cardio::cancellations::any(timeout.get_cancellation(), session->stop.get_cancellation());
      bool desktop = false;
      std::string failure;
      try {
        if (!session->worker) {
          if (session->server->workers >= kMaxWorkers) throw std::runtime_error("Operation worker limit reached.");
          session->worker = StartOperationWorker(false);
          ++session->server->workers;
        }
        if (UsesDesktop(request.method)) {
          co_await AcquireDesktop(session->server, cancel.get_cancellation());
          desktop = true;
        }
        cancel.get_cancellation().throw_if_cancellation_requested();
        const auto context = "connection #" + std::to_string(session->id) + " request=" +
            SanitizeAgentLogField(request.id) + " method=" + SanitizeAgentLogField(request.method);
        if (!request.id.empty()) PrintAgentLogEvent(context + " phase=execute");
        WorkerMessage command = {request.frame.kind == FrameKind::Json ? WorkerMessageKind::Json : WorkerMessageKind::Binary,
            std::move(request.frame.payload)};
        co_await WriteWorker(session->worker, std::move(command), cancel.get_cancellation());
        for (;;) {
          auto response = co_await ReadWorker(session->worker, cancel.get_cancellation());
          if (response.kind == WorkerMessageKind::Complete) break;
          if (response.kind == WorkerMessageKind::Log) {
            PrintAgentLogEvent(context + " " + std::string(response.payload.begin(), response.payload.end()));
            continue;
          }
          if (response.kind != WorkerMessageKind::Json && response.kind != WorkerMessageKind::Binary)
            throw std::runtime_error("Unexpected operation worker response.");
          while (session->output_bytes + response.payload.size() + kFrameHeaderBytes > kConnectionQueueBytes ||
              session->output.size() >= kMaxRequests)
            co_await session->output_space.Wait(cancel.get_cancellation());
          EnqueueOutput(session, {response.kind == WorkerMessageKind::Json ? FrameKind::Json : FrameKind::Binary,
              std::move(response.payload)});
        }
        if (!request.id.empty()) PrintAgentLogEvent(context + " phase=complete elapsedMs=" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(MonotonicClock::now() - request.received).count()));
      } catch (const std::exception& error) { failure = error.what(); }
      session->request_bytes -= size;
      session->server->request_bytes -= size;
      if (!failure.empty()) {
        if (desktop) {
          // Cancellation of IPC is not cancellation of the remote Win32 call.
          // Keep desktop ownership until the old worker really has stopped.
          const auto reaped = co_await StopOperationWorker(session->worker);
          if (reaped) { --session->server->workers; ReleaseDesktop(session->server); }
          else {
            session->server->quarantine.push_back(session->worker);
            PrintAgentLogEvent("desktop worker cleanup failed; desktop execution remains unavailable");
          }
          session->worker.reset();
        }
        // Partial transfers and worker-owned state cannot safely continue in this session.
        CloseSession(session, "operation failed or cancelled: " + failure);
        break;
      }
      if (desktop) ReleaseDesktop(session->server);
    }
  } catch (const std::exception& error) { CloseSession(session, std::string("operation: ") + error.what()); }
}

static cardio::promise<void> ServeClient(std::shared_ptr<ClientSession> session) {
  auto sender = SendResponses(session);
  auto executor = ExecuteRequests(session);
  try {
    const auto& options = session->server->options;
    if (options.auth_required) {
      std::vector<unsigned char> challenge;
      std::string error;
      if (!GenerateAuthChallenge(&challenge, &error)) throw std::runtime_error(error);
      EnqueueOutput(session, {FrameKind::AuthChallenge, challenge});
      auto timeout = cardio::cancellations::timeout(kAuthenticationMs);
      auto cancel = cardio::cancellations::any(timeout.get_cancellation(), session->stop.get_cancellation());
      auto frame = co_await ReceiveFrame(session, cancel.get_cancellation(), false);
      if (frame.kind == FrameKind::Close) throw std::runtime_error("Peer closed during authentication.");
      if (frame.kind != FrameKind::AuthResponse || frame.payload.size() != kAuthResponseBytes)
        throw std::runtime_error("Invalid authentication response frame.");
      if (!AuthResponseEquals(frame.payload, CreateAuthChallengeResponse(options.auth_token, challenge)))
        throw std::runtime_error("Authentication challenge response mismatch.");
    }
    PrintAgentLogEvent(CreateAgentConnectionStateLogEvent(session->id, options.auth_required ? "authenticated" : "authentication skipped"));
    EnqueueOutput(session, JsonFrame(CreateReadyEventJson()));
    PrintAgentLogEvent(CreateAgentConnectionStateLogEvent(session->id, "ready"));
    while (!session->closed) {
      auto frame = co_await ReceiveFrame(session, session->stop.get_cancellation(), true);
      if (frame.kind == FrameKind::Close) { CloseSession(session, "peer requested close"); break; }
      if (frame.kind == FrameKind::Ping) { EnqueueOutput(session, {FrameKind::Pong, {}}); continue; }
      if (frame.kind == FrameKind::Pong) continue;
      QueuedRequest request = {std::move(frame), {}, {}, MonotonicClock::now()};
      if (request.frame.kind == FrameKind::Json) {
        const std::string text(request.frame.payload.begin(), request.frame.payload.end());
        if (!ReadAgentRequest(text, &request.id, &request.method)) throw std::runtime_error("Invalid JSON request envelope.");
        if (request.method == "agent.capabilities") {
          EnqueueOutput(session, JsonFrame(CreateCapabilitiesResponse(request.id)));
          continue;
        }
      } else if (request.frame.kind != FrameKind::Binary) throw std::runtime_error("Unexpected frame kind.");
      const auto size = request.frame.payload.size() + kFrameHeaderBytes;
      if (session->requests.size() >= kMaxRequests || session->request_bytes + size > kConnectionQueueBytes ||
          session->server->request_bytes + size > kServerQueueBytes) throw std::runtime_error("Incoming operation queue limit exceeded.");
      session->request_bytes += size;
      session->server->request_bytes += size;
      session->requests.push_back(std::move(request));
      session->request_ready.Notify();
    }
  } catch (const std::exception& error) { CloseSession(session, error.what()); }
  CloseSession(session, "session completed");
  co_await sender;
  co_await executor;
  co_await DrainSocket(session->socket);
  session->server->request_bytes -= session->request_bytes;
  session->server->output_bytes -= session->output_bytes;
  session->requests.clear();
  session->output.clear();
  if (session->worker) {
    const auto reaped = co_await StopOperationWorker(session->worker);
    if (reaped) --session->server->workers;
    else {
      session->server->quarantine.push_back(session->worker);
      PrintAgentLogEvent("worker cleanup deadline exceeded; slot quarantined");
    }
    session->worker.reset();
  }
  session->server->sessions_changed.Notify();
}

static cardio::promise<void> ReapSessions(std::shared_ptr<ServerState> server) {
  for (;;) {
    for (auto it = server->sessions.begin(); it != server->sessions.end();) {
      if ((*it)->lifetime.is_ready()) {
        // Destroy a completed coroutine outside that coroutine's own stack.
        (*it)->lifetime = cardio::promise<void>();
        it = server->sessions.erase(it);
      } else ++it;
    }
    if (server->stop.get_cancellation().is_cancellation_requested() && server->sessions.empty()) break;
    co_await server->sessions_changed.Wait({});
  }
}

static cardio::promise<void> Serve(std::shared_ptr<ServerState> server, int* result, std::string* error) {
  Worker probe;
  try {
    probe = StartOperationWorker(true);
    auto timeout = cardio::cancellations::timeout(kAuthenticationMs);
    auto cancel = cardio::cancellations::any(timeout.get_cancellation(), server->stop.get_cancellation());
    WorkerMessage probe_request;
    // GCC 12 can double-destroy a nontrivial aggregate passed directly in a
    // co_await expression. Give the owning message an explicit local lifetime.
    probe_request.kind = WorkerMessageKind::Json;
    probe_request.payload.assign(server->options.host.begin(), server->options.host.end());
    co_await WriteWorker(probe, std::move(probe_request), cancel.get_cancellation());
    const auto detected = co_await ReadWorker(probe, cancel.get_cancellation());
    if (detected.kind != WorkerMessageKind::Capability || detected.payload.size() != 5)
      throw std::runtime_error("Capability/host detection failed.");
    SetAgentVideoCapability(detected.payload[0] != 0);
    const auto reaped = co_await StopOperationWorker(probe);
    if (!reaped) throw std::runtime_error("Capability probe did not exit.");
    probe.reset();
    server->stop.get_cancellation().throw_if_cancellation_requested();
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(server->options.port);
    std::memcpy(&address.sin_addr, detected.payload.data() + 1, sizeof(address.sin_addr));
    const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) throw std::runtime_error("socket failed WSA=" + std::to_string(WSAGetLastError()));
    server->listener = AdoptSocket(listener, true);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, SOMAXCONN) != 0)
      throw std::runtime_error("bind/listen failed WSA=" + std::to_string(WSAGetLastError()));
    const auto status = "Listening on " + server->options.host + ":" + std::to_string(server->options.port);
    PrintAgentLogEvent("version=" + BuildAgentVersionText() + " " + status);
    SetAgentGuiStatus(server->gui, status);
    uint32_t next_id = 0;
    for (;;) {
      std::string endpoint;
      auto socket = co_await AcceptSocket(server->listener, &endpoint);
      if (server->sessions.size() >= kMaxConnections) {
        co_await DrainSocket(socket);
        PrintAgentLogEvent("connection limit reached");
        continue;
      }
      auto session = std::make_shared<ClientSession>();
      session->server = server;
      session->socket = std::move(socket);
      session->id = ++next_id;
      PrintAgentLogEvent(CreateAgentConnectionAcceptedLogEvent(session->id, endpoint));
      server->sessions.push_back(session);
      session->lifetime = ServeClient(session);
    }
  } catch (const std::exception& failure) {
    if (!server->stop.get_cancellation().is_cancellation_requested()) {
      *result = 1;
      *error = failure.what();
      PrintAgentLogEvent(*error);
      SetAgentGuiStatus(server->gui, "Startup failed: " + *error);
    }
  }
  StopServer(server);
  co_await DrainSocket(server->listener);
  if (probe) {
    const auto reaped = co_await StopOperationWorker(probe);
    if (!reaped) server->quarantine.push_back(probe);
  }
}

int RunTcpServer(const ServerOptions& options, std::string* error) {
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { *error = "WSAStartup failed."; return 1; }
  int result = 0;
  try {
    cardio::dispatcher_host_win32_auto dispatcher;
    auto server = std::make_shared<ServerState>();
    server->options = options;
    const auto weak = std::weak_ptr<ServerState>(server);
    server->gui = CreateAgentGui(options, [weak] { if (auto state = weak.lock()) StopServer(state); });
    SetAgentLogSink([weak](const AgentLogRecord&) { if (auto state = weak.lock()) NotifyAgentGuiLog(state->gui); });
    auto reaping = ReapSessions(server);
    auto serving = Serve(server, &result, error);
    dispatcher.park();
    SetAgentLogSink({});
  } catch (const std::exception& failure) { *error = failure.what(); result = 1; }
  WSACleanup();
  return result;
}

}  // namespace agent_rover
