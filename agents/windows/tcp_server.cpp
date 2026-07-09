// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "tcp_server.h"

#include <winsock2.h>

#include <cstring>
#include <string>
#include <vector>

#include "agent_log.h"
#include "auth.h"
#include "binary_transfer.h"
#include "frame.h"
#include "json_protocol.h"

namespace agent_rover {

static std::vector<unsigned char> StringToPayload(const std::string& value) {
  return std::vector<unsigned char>(value.begin(), value.end());
}

static bool ResolveHost(const std::string& host, in_addr* address) {
  if (host.empty() || host == "0.0.0.0") {
    address->s_addr = htonl(INADDR_ANY);
    return true;
  }

  const unsigned long parsed = inet_addr(host.c_str());
  if (parsed != INADDR_NONE || host == "255.255.255.255") {
    address->s_addr = parsed;
    return true;
  }

  hostent* entry = gethostbyname(host.c_str());
  if (entry == nullptr || entry->h_addr_list == nullptr ||
      entry->h_addr_list[0] == nullptr) {
    return false;
  }
  std::memcpy(address, entry->h_addr_list[0], sizeof(in_addr));
  return true;
}

static bool SendJson(SOCKET client, const std::string& json) {
  std::string error;
  const Frame frame = {FrameKind::Json, StringToPayload(json)};
  return WriteFrame(client, frame, &error);
}

static bool SendBinaryChunk(SOCKET client, const BinaryTransferChunk& chunk) {
  std::vector<unsigned char> payload;
  std::string error;
  EncodeBinaryTransferChunkPayload(chunk, &payload);
  const Frame frame = {FrameKind::Binary, payload};
  return WriteFrame(client, frame, &error);
}

static bool SendReadyEvent(SOCKET client) {
  return SendJson(client, CreateReadyEventJson());
}

static bool SendPong(SOCKET client) {
  std::string error;
  const Frame frame = {FrameKind::Pong, std::vector<unsigned char>()};
  return WriteFrame(client, frame, &error);
}

static bool SendAuthChallenge(
    SOCKET client,
    const std::vector<unsigned char>& challenge) {
  std::string error;
  const Frame frame = {FrameKind::AuthChallenge, challenge};
  return WriteFrame(client, frame, &error);
}

static bool AuthenticateClient(
    SOCKET client,
    const ServerOptions& options,
    std::string* failure_reason) {
  if (!options.auth_required) {
    return true;
  }

  std::vector<unsigned char> challenge;
  std::string error;
  if (!GenerateAuthChallenge(&challenge, &error)) {
    *failure_reason = error;
    return false;
  }
  if (!SendAuthChallenge(client, challenge)) {
    *failure_reason = "failed to send authentication challenge";
    return false;
  }

  FrameHeader header = {};
  if (!ReadFrameHeader(client, &header, &error)) {
    *failure_reason = "failed to read authentication response: " + error;
    return false;
  }
  if (header.kind != FrameKind::AuthResponse ||
      header.payload_length != kAuthResponseBytes) {
    *failure_reason = "invalid authentication response frame";
    return false;
  }

  std::vector<unsigned char> payload;
  if (!ReadFramePayload(client, header.payload_length, &payload, &error)) {
    *failure_reason = "failed to read authentication response payload: " +
                      error;
    return false;
  }

  const std::vector<unsigned char> expected =
      CreateAuthChallengeResponse(options.auth_token, challenge);
  if (!AuthResponseEquals(payload, expected)) {
    *failure_reason = "challenge response mismatch";
    return false;
  }
  return true;
}

static void HandleFrame(
    SOCKET client,
    const Frame& frame,
    BinaryTransferStore* transfers,
    bool* should_close,
    std::string* close_reason) {
  std::string error;

  switch (frame.kind) {
    case FrameKind::Json: {
      const std::string request(frame.payload.begin(), frame.payload.end());
      std::vector<BinaryTransferChunk> outbound_chunks;
      const std::string response =
          HandleJsonRequest(request, transfers, &outbound_chunks);
      for (const BinaryTransferChunk& chunk : outbound_chunks) {
        if (!SendBinaryChunk(client, chunk)) {
          *should_close = true;
          *close_reason = "failed to send binary transfer chunk";
          return;
        }
      }
      if (!SendJson(client, response)) {
        *should_close = true;
        *close_reason = "failed to send JSON response";
        return;
      }
      break;
    }
    case FrameKind::Ping:
      if (!SendPong(client)) {
        *should_close = true;
        *close_reason = "failed to send pong";
        return;
      }
      break;
    case FrameKind::Pong:
      break;
    case FrameKind::Close:
      *should_close = true;
      *close_reason = "peer requested close";
      return;
    case FrameKind::Binary: {
      BinaryTransferChunk chunk = {};
      if (!DecodeBinaryTransferChunkPayload(frame.payload, &chunk, &error)) {
        *should_close = true;
        *close_reason = "invalid binary transfer chunk";
        return;
      }
      if (!AcceptBinaryTransferChunk(transfers, chunk, &error)) {
        *should_close = true;
        *close_reason = "binary transfer rejected: " + error;
        return;
      }
      break;
    }
    case FrameKind::AuthChallenge:
    case FrameKind::AuthResponse:
      *should_close = true;
      *close_reason = "unexpected authentication frame";
      break;
  }
}

static void HandleClient(
    SOCKET client,
    const ServerOptions& options,
    uint32_t connection_id) {
  std::string auth_failure;
  if (!AuthenticateClient(client, options, &auth_failure)) {
    PrintAgentLogEvent(CreateAgentConnectionDisconnectedLogEvent(
        connection_id, "authentication failed: " + auth_failure));
    return;
  }
  PrintAgentLogEvent(CreateAgentConnectionStateLogEvent(
      connection_id,
      options.auth_required ? "authenticated" : "authentication skipped"));

  if (!SendReadyEvent(client)) {
    PrintAgentLogEvent(CreateAgentConnectionDisconnectedLogEvent(
        connection_id, "failed to send ready event"));
    return;
  }
  PrintAgentLogEvent(
      CreateAgentConnectionStateLogEvent(connection_id, "ready"));
  BinaryTransferStore transfers = {};

  for (;;) {
    Frame frame = {};
    std::string error;
    if (!ReadFrame(client, kMaxJsonPayloadBytes, &frame, &error)) {
      PrintAgentLogEvent(CreateAgentConnectionDisconnectedLogEvent(
          connection_id, error));
      return;
    }

    bool should_close = false;
    std::string close_reason;
    HandleFrame(client, frame, &transfers, &should_close, &close_reason);
    if (should_close) {
      PrintAgentLogEvent(CreateAgentConnectionDisconnectedLogEvent(
          connection_id,
          close_reason.empty() ? "connection closed" : close_reason));
      return;
    }
  }
}

static std::string ClientEndpoint(const sockaddr_in& address) {
  const char* host = inet_ntoa(address.sin_addr);
  std::string output = host == nullptr ? "unknown" : host;
  output += ":";
  output += std::to_string(static_cast<unsigned int>(ntohs(address.sin_port)));
  return output;
}

static bool BindListener(
    SOCKET listener,
    const ServerOptions& options,
    std::string* error) {
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(options.port);
  if (!ResolveHost(options.host, &address.sin_addr)) {
    *error = "Failed to resolve listen host.";
    return false;
  }

  const int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
      SOCKET_ERROR) {
    *error = "bind failed.";
    return false;
  }
  if (listen(listener, SOMAXCONN) == SOCKET_ERROR) {
    *error = "listen failed.";
    return false;
  }
  return true;
}

int RunTcpServer(const ServerOptions& options, std::string* error) {
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    *error = "WSAStartup failed.";
    return 1;
  }

  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) {
    WSACleanup();
    *error = "socket failed.";
    return 1;
  }

  if (!BindListener(listener, options, error)) {
    closesocket(listener);
    WSACleanup();
    return 1;
  }

  uint32_t next_connection_id = 1;
  for (;;) {
    sockaddr_in client_address = {};
    int client_address_length = sizeof(client_address);
    SOCKET client = accept(
        listener,
        reinterpret_cast<sockaddr*>(&client_address),
        &client_address_length);
    if (client == INVALID_SOCKET) {
      closesocket(listener);
      WSACleanup();
      *error = "accept failed.";
      return 1;
    }

    const uint32_t connection_id = next_connection_id;
    next_connection_id += 1;
    if (next_connection_id == 0) {
      next_connection_id = 1;
    }
    PrintAgentLogEvent(CreateAgentConnectionAcceptedLogEvent(
        connection_id, ClientEndpoint(client_address)));
    HandleClient(client, options, connection_id);
    closesocket(client);
  }
}

}  // namespace agent_rover
