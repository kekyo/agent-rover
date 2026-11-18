// agent-rover - Temporary Windows release replacement and rollback
// Copyright (c) Kouji Matsui. Under MIT.
#include "update_private.h"
#include "command_line.h"
#include "frame.h"
#include "json_protocol.h"
#include "supervisor.h"
#include "version.h"
#include "win32_util.h"
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <cstring>

namespace agent_rover {
static std::runtime_error Failure(const char* op, DWORD code = GetLastError()) {
  return std::runtime_error(std::string(op) + " Win32=" + std::to_string(code));
}
static KernelHandle ArgumentHandle(const wchar_t* text) {
  wchar_t* end = nullptr;
  const auto value = std::wcstoull(text, &end, 10);
  if (!value || !end || *end || value > UINTPTR_MAX) throw std::runtime_error("Invalid update mapping.");
  const auto h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
  if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)) throw Failure("Invalid inherited handle");
  return KernelHandle(h);
}
static std::wstring RegText(HKEY key, const wchar_t* name) {
  DWORD type = 0, bytes = 0;
  auto code = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
  if (code != ERROR_SUCCESS || type != REG_SZ || bytes < 2 || bytes > 65536)
    throw Failure("Read uninstall registration", code);
  std::vector<wchar_t> buffer(bytes / 2 + 1);
  code = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes);
  if (code != ERROR_SUCCESS) throw Failure("Read uninstall registration", code);
  return buffer.data();
}
struct RegCloser {
  void operator()(HKEY key) const { RegCloseKey(key); }
};
using Registration = std::unique_ptr<HKEY__, RegCloser>;
static Registration OpenRegistration(const std::wstring& image) {
  const auto sep = image.find_last_of(L"\\/");
  if (sep == std::wstring::npos || _wcsicmp(image.substr(sep + 1).c_str(), L"agent-rover-agent.exe"))
    return {};
  const wchar_t* path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AgentRover";
  HKEY raw = nullptr;
  auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &raw);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return {};
  if (status != ERROR_SUCCESS) throw Failure("Open uninstall registration", status);
  Registration read(raw);
  auto installed = RegText(raw, L"InstallLocation");
  while (installed.size() > 3 && installed.back() == L'\\') installed.pop_back();
  if (_wcsicmp(installed.c_str(), image.substr(0, sep).c_str())) return {};
  status =
      RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &raw);
  if (status != ERROR_SUCCESS) throw Failure("Open DisplayVersion for update", status);
  return Registration(raw);
}
static void SetVersion(const Registration& key, const std::wstring& version) {
  if (!key) return;
  const auto code = RegSetValueExW(key.get(), L"DisplayVersion", 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(version.c_str()),
                                   static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
  if (code != ERROR_SUCCESS || RegText(key.get(), L"DisplayVersion") != version)
    throw Failure("Set DisplayVersion", code);
}
static bool OwnsListener(DWORD root, uint16_t port) {
  DWORD bytes = 0;
  auto code = GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
  if (code != ERROR_INSUFFICIENT_BUFFER || bytes > 16 * 1024 * 1024)
    throw Failure("GetExtendedTcpTable(size)", code);
  std::vector<unsigned char> buffer(bytes);
  code = GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
  if (code == ERROR_INSUFFICIENT_BUFFER) return false;
  if (code != NO_ERROR) throw Failure("GetExtendedTcpTable", code);
  const auto table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const auto& row = table->table[i];
    if (ntohs(static_cast<u_short>(row.dwLocalPort)) != port) continue;
    if (row.dwOwningPid == root) return true;
    KernelHandle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (processes.get() == INVALID_HANDLE_VALUE) throw Failure("CreateToolhelp32Snapshot");
    PROCESSENTRY32W p = {};
    p.dwSize = sizeof(p);
    if (Process32FirstW(processes.get(), &p)) do {
        if (p.th32ProcessID == row.dwOwningPid && p.th32ParentProcessID == root) return true;
      } while (Process32NextW(processes.get(), &p));
  }
  return false;
}

static cardio::promise<void> StartAndConfirm(UpdateBridge* b, bool legacy, const std::string& version) {
  const std::wstring image = b->original;
  std::vector<std::wstring> args;
  auto mapping = CreateUpdateMapping();
  auto restart = MapUpdate(mapping.get());
  SECURITY_ATTRIBUTES attrs = {sizeof(attrs), nullptr, TRUE};
  KernelHandle ready(CreateEventW(&attrs, TRUE, FALSE, nullptr));
  if (!ready) throw Failure("CreateEventW(restart)");
  restart->magic = kUpdateMagic;
  restart->settings = b->settings;
  restart->joined = ready.get();
  std::copy(version.begin(), version.end(), restart->version);
  if (!legacy)
    args = {L"--agent-update-restart", std::to_wstring(reinterpret_cast<uintptr_t>(mapping.get()))};
  else {
    const auto& settings = b->settings;
    args = {L"--host", Utf8ToWide(settings.host), L"--port", std::to_wstring(settings.port)};
    if (!settings.auth)
      args.emplace_back(L"--no-auth");
    else if (!settings.generated)
      args.insert(args.end(), {L"--unsafe-token", Utf8ToWide(settings.token)});
    const int minor = std::stoi(version.substr(2));
    if (minor >= 6)
      args.insert(args.end(),
                  {L"--max-transfer-size", std::to_wstring(settings.max_transfer_bytes / kBytesPerMiB)});
    if (minor >= 8)
      args.insert(args.end(), {L"--connection-mode", settings.multiple ? L"multiple" : L"single"});
  }
  auto command = BuildCommandLine(image, args);
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, !legacy, 0, nullptr,
                      b->working_directory, &startup, &pi))
    throw Failure("Start agent");
  CloseHandle(pi.hThread);
  KernelHandle child(pi.hProcess);
  std::string failure;
  try {
    auto deadline = cardio::cancellations::timeout(20000);
    if (!legacy) co_await cardio::from_win32_handle(ready.get(), deadline.get_cancellation());
    // Historical binaries cannot signal a private readiness event. Check ownership of the
    // actual listening socket while waiting for exit, with a bounded observation interval.
    while (!OwnsListener(pi.dwProcessId, b->settings.port)) {
      if (!legacy) throw std::runtime_error("New agent signaled readiness without owning its listener.");
      deadline.get_cancellation().throw_if_cancellation_requested();
      auto interval = cardio::cancellations::timeout(100);
      bool exited = false;
      try {
        co_await cardio::from_win32_handle(child.get(), interval.get_cancellation());
        exited = true;
      } catch (const cardio::canceled_exception&) {
      }
      if (exited) throw std::runtime_error("Replacement exited before listening.");
    }
    if (WaitForSingleObject(child.get(), 0) != WAIT_TIMEOUT)
      throw std::runtime_error("Replacement stopped.");
  } catch (const std::exception& e) {
    failure = e.what();
  }
  if (!failure.empty()) {
    if (WaitForSingleObject(child.get(), 0) == WAIT_TIMEOUT) {
      if (!TerminateProcess(child.get(), 1)) throw Failure("Stop failed replacement");
      auto timeout = cardio::cancellations::timeout(10000);
      co_await cardio::from_win32_handle(child.get(), timeout.get_cancellation());
    }
    throw std::runtime_error(failure);
  }
}
int RunUpdatedAgent(const wchar_t* text) {
  try {
    auto mapping = ArgumentHandle(text);
    auto b = MapUpdate(mapping.get());
    if (b->magic != kUpdateMagic || std::string(b->version) != AGENT_ROVER_AGENT_VERSION) return 1;
    const auto& s = b->settings;
    ServerOptions options = {s.host,     s.port,      s.auth,   s.token,    s.max_transfer_bytes,
                             s.multiple, s.generated, s.config, s.workspace};
    KernelHandle ready(b->joined);
    SetHandleInformation(ready.get(), HANDLE_FLAG_INHERIT, 0);
    options.update_ready = reinterpret_cast<uintptr_t>(ready.get());
    b.reset();
    mapping.reset();
    std::string error;
    return RunSupervisor(options, &error);
  } catch (...) {
    return 1;
  }
}

static std::string Result(const UpdateBridge& b, const std::string& state, const std::string& actual,
                          const std::string& detail) {
  return "{\"updateId\":\"" + JsonEscape(b.id) + "\",\"sourceSha256\":\"" + b.source_hash +
         "\",\"sha256\":\"" + b.hash + "\",\"status\":\"" + state +
         "\",\"actualVersion\":" + (actual.empty() ? "null" : "\"" + JsonEscape(actual) + "\"") +
         ",\"message\":\"" + JsonEscape(detail) + "\"}";
}
// A second portable instance can use a different port while mapping this same file.
static void CheckOtherInstances(const std::wstring& image) {
  const auto name = image.substr(image.find_last_of(L"\\/") + 1);
  KernelHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snapshot.get() == INVALID_HANDLE_VALUE) throw Failure("Enumerate agent copies");
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot.get(), &entry)) do {
      if (_wcsicmp(entry.szExeFile, name.c_str())) continue;
      KernelHandle process(
          OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID));
      if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
        throw Failure("Check active agent copy");
      }
      wchar_t path[32768];
      const auto size = GetModuleFileNameExW(process.get(), nullptr, path, 32768);
      if (size && _wcsicmp(image.c_str(), std::wstring(path, size).c_str()) == 0)
        throw std::runtime_error("Another instance is using this agent executable.");
    } while (Process32NextW(snapshot.get(), &entry));
}
cardio::promise<void> RecoverAgentUpdate() {
  const auto record = co_await ReadAgentUpdateRecord();
  std::string status, id, source, target, version, previous, display;
  if (!FindJsonStringField(record, "status", &status) || status != "applying") co_return;
  if (!FindJsonStringField(record, "updateId", &id) || !IsAgentUpdateId(id) ||
      !FindJsonStringField(record, "sourceSha256", &source) ||
      !FindJsonStringField(record, "sha256", &target) ||
      !FindJsonStringField(record, "requestedVersion", &version) ||
      !FindJsonStringField(record, "previousVersion", &previous) ||
      !FindJsonStringField(record, "previousDisplayVersion", &display))
    throw std::runtime_error("Invalid interrupted transaction.");
  const auto path = AgentImagePath();
  KernelHandle lock(CreateFileW((path + L".update-lock").c_str(), DELETE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
  if (lock.get() == INVALID_HANDLE_VALUE) co_return;
  const auto again = co_await ReadAgentUpdateRecord();
  if (record != again) co_return;
  const auto actual = RunningAgentHash();
  const bool applied = actual == target && version == AGENT_ROVER_AGENT_VERSION;
  const bool restored = actual == source && previous == AGENT_ROVER_AGENT_VERSION;
  if (!applied && !restored)
    throw std::runtime_error("Interrupted update does not match the running image; keep its backup.");
  if (!display.empty()) SetVersion(OpenRegistration(path), Utf8ToWide(applied ? version : display));
  SaveAgentUpdateResult(path, "{\"updateId\":\"" + id + "\",\"sourceSha256\":\"" + JsonEscape(source) +
                                  "\",\"sha256\":\"" + JsonEscape(target) + "\",\"status\":\"" +
                                  (applied ? "applied" : "restored") +
                                  "\",\"actualVersion\":\"" AGENT_ROVER_AGENT_VERSION
                                  "\",\"message\":\"Recovered after restart.\"}");
  const auto dir = AgentUpdateDirectory(path, id);
  const auto attrs = GetFileAttributesW(dir.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
      !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
    for (const auto& [name, hash] : std::initializer_list<std::pair<std::wstring, std::string>>{
             {L"previous.exe", source}, {L"helper.exe", source}, {L"release.exe", target}}) {
      const auto file = dir + L"\\" + name;
      if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES && AgentImageHash(file) == hash)
        DeleteFileW(file.c_str());
    }
    RemoveDirectoryW(dir.c_str());
  }
}
static cardio::promise<void> ReplaceAgent(UpdateBridge* b, int* exit_code) {
  const std::wstring original = b->original, dir = AgentUpdateDirectory(original, b->id);
  const auto candidate = dir + L"\\release.exe", backup = dir + L"\\previous.exe";
  const auto supervisor = b->supervisor;
  SocketConnection socket;
  bool committed = false, replaced = false, registered = false, stopped = false;
  Registration registration;
  KernelHandle update_lock;
  std::wstring old_version;
  std::string failure, state = "recovery-required", actual;
  try {
    auto handoff = cardio::cancellations::timeout(15000);
    const auto descriptor = WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                                       &b->connection, 0, WSA_FLAG_OVERLAPPED);
    if (descriptor == INVALID_SOCKET) throw Failure("Adopt update connection", WSAGetLastError());
    socket = AdoptSocket(descriptor, false);
    update_lock.reset(CreateFileW((original + L".update-lock").c_str(), GENERIC_READ | DELETE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                  nullptr));
    if (update_lock.get() == INVALID_HANDLE_VALUE) throw Failure("Lock update destination");
    registration = OpenRegistration(original);
    if (registration) old_version = RegText(registration.get(), L"DisplayVersion");
    if (AgentImageHash(original) != b->source_hash || AgentImageHash(candidate) != b->hash)
      throw std::runtime_error("Staged update digest mismatch.");
    if (!CopyFileW(original.c_str(), backup.c_str(), TRUE) || AgentImageHash(backup) != b->source_hash)
      throw std::runtime_error("Could not preserve the original agent.");
    auto pending = Result(*b, "applying", "", "");
    pending.pop_back();
    pending += ",\"requestedVersion\":\"" + JsonEscape(b->version) + "\",\"previousVersion\":\"" +
               JsonEscape(b->source_version) + "\",\"previousDisplayVersion\":\"" +
               JsonEscape(WideToUtf8(old_version)) + "\"}";
    SaveAgentUpdateResult(original, pending);
    SetEvent(b->joined);
    co_await cardio::from_win32_handle(b->offered, handoff.get_cancellation());
    committed = true;
    auto shutdown = cardio::cancellations::timeout(30000);
    co_await cardio::from_win32_handle(supervisor, shutdown.get_cancellation());
    stopped = true;
    CheckOtherInstances(original);
    if (AgentImageHash(original) != b->source_hash)
      throw std::runtime_error("Agent changed before replacement.");
    if (!MoveFileExW(candidate.c_str(), original.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw Failure("Replace running agent file");
    replaced = true;
    if (AgentImageHash(original) != b->hash)
      throw std::runtime_error("Installed executable SHA-256 mismatch.");
    registered = registration != nullptr;
    SetVersion(registration, Utf8ToWide(b->version));
    co_await StartAndConfirm(b, b->protocol == 0, b->version);
    actual = b->version;
    state = "applied";
  } catch (const std::exception& e) {
    failure = e.what();
  }
  if (!committed && !failure.empty()) {
    std::strncpy(b->error, failure.c_str(), sizeof(b->error) - 1);
    SetEvent(b->joined);
  }
  if (committed && stopped && state != "applied") {
    try {
      if (replaced) {
        const auto restore = dir + L"\\restore.exe";
        if (!CopyFileW(backup.c_str(), restore.c_str(), TRUE) ||
            !MoveFileExW(restore.c_str(), original.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
          throw Failure("Restore original agent");
      }
      if (registered) SetVersion(registration, old_version);
      if (AgentImageHash(original) != b->source_hash)
        throw std::runtime_error("Original executable SHA-256 mismatch.");
      co_await StartAndConfirm(b, false, b->source_version);
      actual = b->source_version;
      state = "restored";
    } catch (const std::exception& e) {
      failure += "; recovery: " + std::string(e.what()) + "; backup: " + WideToUtf8(backup);
    }
  }
  if (committed) {
    auto record = Result(*b, state, actual, failure);
    try {
      SaveAgentUpdateResult(original, record);
    } catch (const std::exception& e) {
      record = Result(*b, state, actual, failure + " Result journal: " + e.what());
    }
    if (socket) try {
        const auto response = SuccessResponseJson(b->request_id, record);
        Frame frame = {FrameKind::Json, {response.begin(), response.end()}};
        auto bytes = EncodeFrame(frame);
        auto timeout = cardio::cancellations::timeout(5000);
        co_await WriteSocket(socket, bytes, timeout.get_cancellation());
      } catch (const std::exception&) {
      }
  }
  if (socket) co_await DrainSocket(socket);
  DeleteFileW(candidate.c_str());
  if (!committed || state != "recovery-required") DeleteFileW(backup.c_str());
  *exit_code = state == "applied" ? 0 : 1;
}
// The read end reaches EOF only when Windows closes this helper's noninheritable
// writer at process exit. The shell can then remove the unloaded image on XP.
static void ScheduleHelperRemoval(const UpdateBridge& b) {
  SECURITY_ATTRIBUTES attrs = {sizeof(attrs), nullptr, TRUE};
  HANDLE input = nullptr, output = nullptr;
  if (!CreatePipe(&input, &output, &attrs, 0)) throw Failure("CreatePipe(helper cleanup)");
  KernelHandle read(input), write(output);
  if (!SetHandleInformation(write.get(), HANDLE_FLAG_INHERIT, 0)) throw Failure("Protect cleanup writer");
  wchar_t system[MAX_PATH];
  if (!GetSystemDirectoryW(system, MAX_PATH)) throw Failure("GetSystemDirectoryW");
  const auto shell = std::wstring(system) + L"\\cmd.exe";
  auto command = QuoteCommandLineArgument(shell) +
                 L" /d /q /v:off /c set /p done= & del /q helper.exe & cd .. & rd .agent-rover-update-" +
                 Utf8ToWide(b.id);
  KernelHandle sink(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, &attrs,
                                OPEN_EXISTING, 0, nullptr));
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = read.get();
  startup.hStdOutput = sink.get();
  startup.hStdError = sink.get();
  PROCESS_INFORMATION pi = {};
  const auto dir = AgentUpdateDirectory(b.original, b.id);
  if (!CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                      dir.c_str(), &startup, &pi))
    throw Failure("CreateProcessW(helper cleanup)");
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  write.release();  // Lifetime is exactly this process, including abnormal exit.
}
int RunAgentUpdateHelper(const wchar_t* text) {
  WSADATA wsa = {};
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
  int result = 1;
  try {
    auto mapping = ArgumentHandle(text);
    auto b = MapUpdate(mapping.get());
    if (b->magic != kUpdateMagic) throw std::runtime_error("Invalid update handoff.");
    if (b->id[sizeof(b->id) - 1] || b->original[32767] || !IsAgentUpdateId(b->id) ||
        _wcsicmp(AgentImagePath().c_str(),
                 (AgentUpdateDirectory(b->original, b->id) + L"\\helper.exe").c_str()))
      throw std::runtime_error("Invalid private updater location.");
    for (const auto handle : {b->supervisor, b->offered, b->joined})
      SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
    ScheduleHelperRemoval(*b);
    cardio::dispatcher_host_win32_auto dispatcher;
    auto replacing = ReplaceAgent(b.get(), &result);
    dispatcher.park();
  } catch (...) {
  }
  WSACleanup();
  return result;
}
}  // namespace agent_rover
