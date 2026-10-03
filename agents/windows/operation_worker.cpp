// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "operation_worker.h"
#include "agent_log.h"
#include "command_line.h"
#include "json_protocol.h"
#include "win32_util.h"
#include "win32_video.h"
#include "win32_cleanup.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace agent_rover {

struct OperationWorker {
  HANDLE input = INVALID_HANDLE_VALUE;
  HANDLE output = INVALID_HANDLE_VALUE;
  HANDLE process = nullptr;
  HelperRole role = HelperRole::Probe;
  std::vector<unsigned char> capture_root;
  Worker recovery;
  bool resources_recovered = false;
  ~OperationWorker() {
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (process) CloseHandle(process);
  }
};

static std::runtime_error NativeFailure(const char* operation) {
  const DWORD code = GetLastError();
  return std::runtime_error(std::string(operation) + " Win32=" + std::to_string(code));
}

static HANDLE CreateWorkerPipe(HANDLE* child, bool parent_reads) {
  static uint64_t next_pipe = 0;
  const auto name = L"\\\\.\\pipe\\agent-rover-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(++next_pipe);
  const auto parent = CreateNamedPipeW(name.c_str(),
      (parent_reads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
  if (parent == INVALID_HANDLE_VALUE) throw NativeFailure("CreateNamedPipeW");
  SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, TRUE};
  *child = CreateFileW(name.c_str(), parent_reads ? GENERIC_WRITE : GENERIC_READ,
      0, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (*child == INVALID_HANDLE_VALUE) { const auto error = NativeFailure("CreateFileW(pipe)"); CloseHandle(parent); throw error; }
  // Opening the client in this process has already connected this unique pipe.
  return parent;
}

Worker StartOperationWorker(HelperRole role, uint64_t max_transfer_bytes) {
  auto worker = std::make_shared<OperationWorker>();
  worker->role = role;
  HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
  try {
    worker->input = CreateWorkerPipe(&input, false);
    worker->output = CreateWorkerPipe(&output, true);
    wchar_t path[32768] = {};
    if (!GetModuleFileNameW(nullptr, path, 32768)) throw NativeFailure("GetModuleFileNameW");
    const auto mode = role == HelperRole::Probe ? L"--agent-probe" :
        role == HelperRole::FileLogger ? L"--agent-log-worker" :
        role == HelperRole::Cleanup ? L"--agent-cleanup-worker" : L"--agent-worker";
    std::vector<std::wstring> arguments = {mode};
    if (role == HelperRole::Operations) {
      arguments.insert(arguments.end(), {L"--max-transfer-size",
          std::to_wstring(max_transfer_bytes / kBytesPerMiB)});
    }
    auto command = BuildCommandLine(path, arguments);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(path, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) throw NativeFailure("CreateProcessW(worker)");
    worker->process = process.hProcess;
    CloseHandle(process.hThread);
  } catch (...) {
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    throw;
  }
  CloseHandle(input);
  CloseHandle(output);
  return worker;
}

static cardio::promise<void> ReadPipe(HANDLE pipe, std::span<unsigned char> bytes,
    cardio::cancellation cancellation) {
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::read(pipe, std::as_writable_bytes(bytes), cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
}

cardio::promise<void> WriteWorker(Worker worker, WorkerMessage message, cardio::cancellation cancellation) {
  if (message.payload.size() > kMaxJsonPayloadBytes) throw std::runtime_error("Worker message limit exceeded.");
  uint32_t header[2] = {static_cast<uint32_t>(message.kind), static_cast<uint32_t>(message.payload.size())};
  std::span<const std::byte> bytes = std::as_bytes(std::span(header));
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::write(worker->input, bytes, cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
  bytes = std::as_bytes(std::span(message.payload));
  while (!bytes.empty()) {
    const auto count = co_await cardio::win32::write(worker->input, bytes, cancellation);
    if (!count) throw std::runtime_error("Worker pipe closed.");
    bytes = bytes.subspan(count);
  }
}

cardio::promise<WorkerMessage> ReadWorker(Worker worker, cardio::cancellation cancellation) {
  uint32_t header[2] = {};
  co_await ReadPipe(worker->output, {reinterpret_cast<unsigned char*>(header), sizeof(header)}, cancellation);
  if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker message limit exceeded.");
  WorkerMessage message = {static_cast<WorkerMessageKind>(header[0]), std::vector<unsigned char>(header[1])};
  co_await ReadPipe(worker->output, message.payload, cancellation);
  if (worker->role == HelperRole::Operations && message.kind == WorkerMessageKind::CaptureRoot) {
    if (!worker->capture_root.empty() || message.payload.size() <= sizeof(CaptureIdentity) || message.payload.size() > 32768)
      throw std::runtime_error("Invalid capture ownership message.");
    worker->capture_root = message.payload;
  }
  co_return message;
}

cardio::promise<bool> StopOperationWorker(Worker worker) {
  if (!worker || !worker->process) co_return true;
  // Only call after pending pipe I/O has completed (including native cancellation).
  if (worker->input != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(worker->input, INVALID_HANDLE_VALUE));
  bool exited = false;
  {
    auto grace = cardio::cancellations::timeout(250);
    try { co_await cardio::from_win32_handle(worker->process, grace.get_cancellation()); exited = true; }
    catch (const cardio::canceled_exception&) {}
  }
  if (!exited) {
    if (!TerminateProcess(worker->process, ERROR_TIMEOUT) && WaitForSingleObject(worker->process, 0) != WAIT_OBJECT_0)
      co_return false;
    auto deadline = cardio::cancellations::timeout(2000);
    try { co_await cardio::from_win32_handle(worker->process, deadline.get_cancellation()); exited = true; }
    catch (const cardio::canceled_exception&) {}
  }
  if (!exited || worker->role != HelperRole::Operations || worker->resources_recovered) co_return exited;
  // An initialization failure before ownership was delivered may have created
  // a directory. Keep the slot quarantined instead of guessing a deletion path.
  if (worker->capture_root.empty()) co_return false;
  bool recovered = false;
  try {
    worker->recovery = StartOperationWorker(HelperRole::Cleanup);
    auto deadline = cardio::cancellations::timeout(3000);
    WorkerMessage command = {WorkerMessageKind::CaptureRoot, worker->capture_root};
    co_await WriteWorker(worker->recovery, std::move(command), deadline.get_cancellation());
    const auto response = co_await ReadWorker(worker->recovery, deadline.get_cancellation());
    recovered = response.kind == WorkerMessageKind::Complete;
    if (!recovered) PrintAgentLogEvent("phase=cleanup-failed " + std::string(response.payload.begin(), response.payload.end()));
  } catch (const std::exception& error) { PrintAgentLogEvent(std::string("phase=cleanup-failed reason=") + error.what()); }
  const auto cleanup_exited = co_await StopOperationWorker(worker->recovery);
  if (cleanup_exited) worker->recovery.reset();
  worker->resources_recovered = recovered && cleanup_exited;
  co_return worker->resources_recovered;
}

static void BlockingWrite(const void* data, size_t size) {
  auto bytes = static_cast<const unsigned char*>(data);
  while (size) {
    DWORD written = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes,
        static_cast<DWORD>(std::min<size_t>(size, 65536)), &written, nullptr) || !written)
      throw NativeFailure("WriteFile(worker IPC)");
    bytes += written;
    size -= written;
  }
}

static bool BlockingRead(void* data, size_t size) {
  auto bytes = static_cast<unsigned char*>(data);
  while (size) {
    DWORD read = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes,
        static_cast<DWORD>(std::min<size_t>(size, 65536)), &read, nullptr) || !read) return false;
    bytes += read;
    size -= read;
  }
  return true;
}

void SendWorkerReply(WorkerMessageKind kind, const std::vector<unsigned char>& payload) {
  uint32_t header[2] = {static_cast<uint32_t>(kind), static_cast<uint32_t>(payload.size())};
  BlockingWrite(header, sizeof(header));
  BlockingWrite(payload.data(), payload.size());
}

void SendWorkerReplyText(WorkerMessageKind kind, const std::string& text) {
  SendWorkerReply(kind, {text.begin(), text.end()});
}

bool ReadWorkerCommand(WorkerMessage* message) {
  uint32_t header[2] = {};
  if (!BlockingRead(header, sizeof(header))) return false;
  if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker input exceeds limit.");
  message->kind = static_cast<WorkerMessageKind>(header[0]);
  message->payload.resize(header[1]);
  return BlockingRead(message->payload.data(), message->payload.size());
}

int RunCleanupWorker() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  try {
    WorkerMessage command;
    if (!ReadWorkerCommand(&command) || command.kind != WorkerMessageKind::CaptureRoot ||
        command.payload.size() <= sizeof(CaptureIdentity) || command.payload.size() > 32768) return 1;
    CaptureIdentity identity = {};
    std::memcpy(&identity, command.payload.data(), sizeof(identity));
    const std::string path(command.payload.begin() + sizeof(identity), command.payload.end());
    OperationError error;
    CleanupPolicy policy; policy.recursive = true; policy.ignore_missing = true; policy.managed_cleanup = true;
    const auto owned = RestoreCaptureIdentity(path, identity, &error);
    const auto missing = !owned && (error.os_code == ERROR_FILE_NOT_FOUND || error.os_code == ERROR_PATH_NOT_FOUND);
    if (missing || (owned && RemoveWithPolicy(path, policy, &error))) {
      SendWorkerReply(WorkerMessageKind::Complete, {});
      return 0;
    }
    SendWorkerReplyText(WorkerMessageKind::LogError,
        error.native_operation + " Win32=" + std::to_string(error.os_code) + " path=" + path);
  } catch (...) {}
  return 1;
}

static void SendWorkerChunk(const BinaryTransferChunk& chunk) {
  std::vector<unsigned char> payload;
  EncodeBinaryTransferChunkPayload(chunk, &payload);
  SendWorkerReply(WorkerMessageKind::Binary, payload);
}

static void SendWorkerFile(const OutboundFileTransfer& transfer) {
  HANDLE file = CreateFileW(Utf8ToWide(transfer.path).c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw NativeFailure("CreateFileW(video transfer)");
  try {
    uint64_t offset = 0;
    uint32_t sequence = 0;
    do {
      BinaryTransferChunk chunk = {};
      chunk.transfer_id = transfer.transfer_id;
      chunk.content_type = transfer.content_type;
      chunk.sequence = sequence++;
      chunk.data.resize(static_cast<size_t>(std::min<uint64_t>(65536, transfer.total_bytes - offset)));
      DWORD read = 0;
      if (!chunk.data.empty() && (!ReadFile(file, chunk.data.data(), chunk.data.size(), &read, nullptr) || !read))
        throw NativeFailure("ReadFile(video transfer)");
      chunk.data.resize(read);
      offset += read;
      chunk.final = offset == transfer.total_bytes;
      if (chunk.final) {
        chunk.total_bytes = transfer.total_bytes;
        chunk.has_total_bytes = true;
        chunk.sha256 = transfer.sha256;
        chunk.has_sha256 = true;
      }
      SendWorkerChunk(chunk);
    } while (offset < transfer.total_bytes);
  } catch (...) { CloseHandle(file); throw; }
  CloseHandle(file);
}

int RunOperationWorker(bool probe, uint64_t max_transfer_bytes) {
  const auto input = GetStdHandle(STD_INPUT_HANDLE);
  const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
  // Child applications must never inherit our protocol pipe handles.
  SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
  BinaryTransferStore transfers = {};
  transfers.max_transfer_bytes = max_transfer_bytes;
  VideoRecordingStore recordings = {};
  try {
    if (probe) {
      uint32_t header[2] = {};
      if (!BlockingRead(header, sizeof(header)) || header[1] > 255) return 1;
      std::string host(header[1], '\0');
      if (!BlockingRead(host.data(), host.size())) return 1;
      WSADATA data = {};
      if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
      in_addr address = {};
      address.s_addr = host.empty() ? INADDR_ANY : inet_addr(host.c_str());
      if (address.s_addr == INADDR_NONE && host != "255.255.255.255") {
        const auto entry = gethostbyname(host.c_str());
        if (!entry || !entry->h_addr_list || !entry->h_addr_list[0]) return 1;
        std::memcpy(&address, entry->h_addr_list[0], sizeof(address));
      }
      std::vector<unsigned char> detected(5);
      detected[0] = static_cast<unsigned char>(IsVideoCaptureSupported());
      std::memcpy(detected.data() + 1, &address, sizeof(address));
      SendWorkerReply(WorkerMessageKind::Capability, detected);
      WSACleanup();
      return 0;
    }
    std::string directory;
    OperationError directory_error;
    CaptureIdentity identity = {};
    if (!CreateCaptureDirectory(&directory, &directory_error) || !GetCaptureIdentity(directory, &identity))
      throw std::runtime_error("Could not establish connection capture ownership.");
    std::vector<unsigned char> ownership(sizeof(identity));
    std::memcpy(ownership.data(), &identity, sizeof(identity));
    ownership.insert(ownership.end(), directory.begin(), directory.end());
    SendWorkerReply(WorkerMessageKind::CaptureRoot, ownership);
    SetCaptureRoot(directory);
    SetAgentLogSink([](const AgentLogRecord& record) { SendWorkerReplyText(WorkerMessageKind::Log, record.event); });
    for (;;) {
      uint32_t header[2] = {};
      if (!BlockingRead(header, sizeof(header))) break;
      if (header[1] > kMaxJsonPayloadBytes) throw std::runtime_error("Worker input exceeds limit.");
      std::vector<unsigned char> payload(header[1]);
      if (!BlockingRead(payload.data(), payload.size())) break;
      if (header[0] == static_cast<uint32_t>(WorkerMessageKind::Binary)) {
        BinaryTransferChunk chunk = {};
        std::string error;
        if (!DecodeBinaryTransferChunkPayload(payload, &chunk, &error) ||
            !AcceptBinaryTransferChunk(&transfers, chunk, &error)) throw std::runtime_error(error);
      } else if (header[0] == static_cast<uint32_t>(WorkerMessageKind::Json)) {
        std::vector<BinaryTransferChunk> chunks;
        OutboundFileTransfer file = {};
        const auto response = HandleJsonRequest({payload.begin(), payload.end()}, &transfers, &recordings, &chunks, &file);
        PrintAgentLogEvent(CreateAgentResultLog(response));
        try {
          for (const auto& chunk : chunks) SendWorkerChunk(chunk);
          SendWorkerReplyText(WorkerMessageKind::Json, response);
          if (file.present) SendWorkerFile(file);
        } catch (...) {
          if (file.present) { DeleteFileW(Utf8ToWide(file.path).c_str()); RemoveDirectoryW(Utf8ToWide(file.directory).c_str()); }
          throw;
        }
        if (file.present) { DeleteFileW(Utf8ToWide(file.path).c_str()); RemoveDirectoryW(Utf8ToWide(file.directory).c_str()); }
      } else throw std::runtime_error("Unknown worker command.");
      SendWorkerReply(WorkerMessageKind::Complete, {});
    }
    CancelVideoRecording(&recordings);
    return 0;
  } catch (...) {
    // The host records the failed operation and reaps this isolated process.
    return 1;
  }
}

}  // namespace agent_rover
