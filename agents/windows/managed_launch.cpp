// agent-rover - Parent-owned application launch protocol
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "operation_worker.h"
#include "managed_launch.h"
#include <cstring>
#include <stdexcept>

namespace agent_rover {

static void AppendNumber(std::vector<unsigned char>* bytes, uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) bytes->push_back(static_cast<unsigned char>(value >> shift));
}
static uint32_t ReadNumber(std::span<const unsigned char>* bytes) {
  if (bytes->size() < 4) throw std::runtime_error("Truncated launch payload.");
  uint32_t value = 0;
  for (unsigned int i = 0; i < 4; ++i) value |= static_cast<uint32_t>((*bytes)[i]) << (8 * i);
  *bytes = bytes->subspan(4);
  return value;
}
static void AppendString(std::vector<unsigned char>* bytes, const std::string& text) {
  if (text.size() > kMaxJsonPayloadBytes) throw std::runtime_error("Launch field exceeds limit.");
  AppendNumber(bytes, text.size());
  bytes->insert(bytes->end(), text.begin(), text.end());
}
static std::string ReadString(std::span<const unsigned char>* bytes) {
  const auto size = ReadNumber(bytes);
  if (size > bytes->size()) throw std::runtime_error("Truncated launch string.");
  const std::string result(bytes->begin(), bytes->begin() + size);
  *bytes = bytes->subspan(size);
  return result;
}

std::vector<unsigned char> EncodeManagedLaunch(const ManagedProcessLaunchOptions& options) {
  std::vector<unsigned char> bytes;
  AppendNumber(&bytes, options.kill_tree_on_release);
  AppendNumber(&bytes, options.launch.create_no_window);
  for (const auto& value : {options.launch.path, options.launch.working_directory,
      options.launch.stdout_path, options.launch.stderr_path}) AppendString(&bytes, value);
  AppendNumber(&bytes, options.launch.arguments.size());
  for (const auto& value : options.launch.arguments) AppendString(&bytes, value);
  AppendNumber(&bytes, options.launch.environment.size());
  for (const auto& [key, value] : options.launch.environment) { AppendString(&bytes, key); AppendString(&bytes, value); }
  if (bytes.size() > kMaxJsonPayloadBytes) throw std::runtime_error("Launch payload exceeds limit.");
  return bytes;
}

ManagedProcessLaunchOptions DecodeManagedLaunch(const std::vector<unsigned char>& payload) {
  std::span<const unsigned char> bytes(payload);
  ManagedProcessLaunchOptions options = {};
  const auto kill = ReadNumber(&bytes), hidden = ReadNumber(&bytes);
  if (kill > 1 || hidden > 1) throw std::runtime_error("Invalid launch flags.");
  options.kill_tree_on_release = kill != 0;
  options.launch.create_no_window = hidden != 0;
  options.launch.path = ReadString(&bytes);
  options.launch.working_directory = ReadString(&bytes);
  options.launch.stdout_path = ReadString(&bytes);
  options.launch.stderr_path = ReadString(&bytes);
  const auto arguments = ReadNumber(&bytes);
  if (arguments > bytes.size() / 4) throw std::runtime_error("Invalid launch argument count.");
  for (uint32_t i = 0; i < arguments; ++i) options.launch.arguments.push_back(ReadString(&bytes));
  const auto variables = ReadNumber(&bytes);
  if (variables > bytes.size() / 8) throw std::runtime_error("Invalid launch environment count.");
  for (uint32_t i = 0; i < variables; ++i) {
    const auto key = ReadString(&bytes);
    options.launch.environment[key] = ReadString(&bytes);
  }
  if (!bytes.empty()) throw std::runtime_error("Trailing launch payload.");
  return options;
}

static bool LaunchThroughParent(const ManagedProcessLaunchOptions& options,
    ManagedProcessHandles* handles, OperationError* error) {
  SendWorkerReply(WorkerMessageKind::Launch, EncodeManagedLaunch(options));
  WorkerMessage response;
  if (!ReadWorkerCommand(&response)) throw std::runtime_error("Launch owner disconnected.");
  if (response.kind == WorkerMessageKind::LaunchError) {
    std::span<const unsigned char> bytes(response.payload);
    const auto code = ReadNumber(&bytes);
    const auto operation = ReadString(&bytes);
    *error = MakeOperationError(operation, options.launch.path, code);
    return false;
  }
  if (response.kind != WorkerMessageKind::Launch || response.payload.size() != sizeof(*handles))
    throw std::runtime_error("Invalid parent launch response.");
  std::memcpy(handles, response.payload.data(), sizeof(*handles));
  return true;
}

static bool ReleaseThroughParent(uint32_t id, OperationError*) {
  std::vector<unsigned char> bytes;
  AppendNumber(&bytes, id);
  SendWorkerReply(WorkerMessageKind::ReleaseLaunch, bytes);
  WorkerMessage response;
  if (!ReadWorkerCommand(&response) || response.kind != WorkerMessageKind::Complete)
    throw std::runtime_error("Launch owner disconnected during release.");
  return true;
}

void InstallManagedLaunchBroker() { SetManagedProcessBroker(LaunchThroughParent, ReleaseThroughParent); }

int RunManagedLaunchWorker() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  ManagedProcessHandles handles;
  try {
    WorkerMessage command;
    if (!ReadWorkerCommand(&command) || command.kind != WorkerMessageKind::Launch) return 1;
    const auto options = DecodeManagedLaunch(command.payload);
    OperationError error;
    if (!LaunchInOwnedJob(options.launch, &handles, &error)) {
      std::vector<unsigned char> bytes;
      AppendNumber(&bytes, error.os_code);
      AppendString(&bytes, error.native_operation);
      SendWorkerReply(WorkerMessageKind::LaunchError, bytes);
      return 1;
    }
    std::vector<unsigned char> bytes(sizeof(handles));
    std::memcpy(bytes.data(), &handles, sizeof(handles));
    SendWorkerReply(WorkerMessageKind::Launch, bytes);
    // Retain the source handle until the parent duplicates it and closes input.
    ReadWorkerCommand(&command);
    CloseHandle(handles.process);
    return 0;
  } catch (...) {
    if (handles.process) CloseHandle(handles.process);
    return 1;
  }
}
}
