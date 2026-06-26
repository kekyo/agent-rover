// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "tcp_server.h"

#include <winsock2.h>

#include <cstring>
#include <string>
#include <vector>

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

static bool AuthenticateClient(SOCKET client, const ServerOptions& options) {
  if (!options.auth_required) {
    return true;
  }

  std::vector<unsigned char> challenge;
  std::string error;
  if (!GenerateAuthChallenge(&challenge, &error) ||
      !SendAuthChallenge(client, challenge)) {
    return false;
  }

  FrameHeader header = {};
  if (!ReadFrameHeader(client, &header, &error)) {
    return false;
  }
  if (header.kind != FrameKind::AuthResponse ||
      header.payload_length != kAuthResponseBytes) {
    return false;
  }

  std::vector<unsigned char> payload;
  if (!ReadFramePayload(client, header.payload_length, &payload, &error)) {
    return false;
  }

  const std::vector<unsigned char> expected =
      CreateAuthChallengeResponse(options.auth_token, challenge);
  return AuthResponseEquals(payload, expected);
}

static void HandleFrame(
    SOCKET client,
    const Frame& frame,
    BinaryTransferStore* transfers,
    bool* should_close) {
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
          return;
        }
      }
      if (!SendJson(client, response)) {
        *should_close = true;
        return;
      }
      break;
    }
    case FrameKind::Ping:
      if (!SendPong(client)) {
        *should_close = true;
        return;
      }
      break;
    case FrameKind::Pong:
      break;
    case FrameKind::Close:
      *should_close = true;
      return;
    case FrameKind::Binary: {
      BinaryTransferChunk chunk = {};
      if (!DecodeBinaryTransferChunkPayload(frame.payload, &chunk, &error)) {
        *should_close = true;
        return;
      }
      if (!AcceptBinaryTransferChunk(transfers, chunk, &error)) {
        *should_close = true;
        return;
      }
      break;
    }
    case FrameKind::AuthChallenge:
    case FrameKind::AuthResponse:
      *should_close = true;
      break;
  }
}

static void HandleClient(SOCKET client, const ServerOptions& options) {
  if (!AuthenticateClient(client, options) || !SendReadyEvent(client)) {
    return;
  }
  BinaryTransferStore transfers = {};

  for (;;) {
    Frame frame = {};
    std::string error;
    if (!ReadFrame(client, kMaxJsonPayloadBytes, &frame, &error)) {
      return;
    }

    bool should_close = false;
    HandleFrame(client, frame, &transfers, &should_close);
    if (should_close) {
      return;
    }
  }
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

  for (;;) {
    SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      closesocket(listener);
      WSACleanup();
      *error = "accept failed.";
      return 1;
    }
    HandleClient(client, options);
    closesocket(client);
  }
}

}  // namespace agent_rover
