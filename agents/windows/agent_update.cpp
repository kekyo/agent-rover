// agent-rover - Verify and stage a release for one running Windows agent
// Copyright (c) Kouji Matsui. Under MIT.
#include "update_private.h"
#include "binary_codec.h"
#include "command_line.h"
#include "json_protocol.h"
#include "resource.h"
#include "version.h"
#include "win32_files.h"
#include "win32_util.h"
#include <algorithm>
#include <cstring>

namespace agent_rover {

static std::runtime_error Failure(const char* operation) {
  return std::runtime_error(std::string(operation) + " Win32=" + std::to_string(GetLastError()));
}
std::wstring AgentImagePath() {
  wchar_t path[32768];
  const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
  if (!length || length >= 32768) throw Failure("GetModuleFileNameW");
  return std::wstring(path, length);
}
std::string AgentImageHash(const std::wstring& path) {
  std::string hash;
  uint64_t bytes;
  OperationError error;
  if (!HashFileSha256(WideToUtf8(path), &bytes, &hash, &error)) throw std::runtime_error(error.message);
  return hash;
}
std::string RunningAgentHash() { return AgentImageHash(AgentImagePath()); }

static bool IsHex(const std::string& text, size_t length) {
  return text.size() == length && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool IsAgentUpdateId(const std::string& id) {
  if (id.size() != 36) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (id[i] != '-') return false;
    } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
      return false;
  }
  return true;
}
static bool IsVersion(const std::string& text) {
  if (text.empty() || text.size() > 60) return false;
  size_t start = 0;
  for (int part = 0; part < 3; ++part) {
    const auto end = text.find('.', start);
    if ((part == 2) != (end == std::string::npos)) return false;
    const auto count = (end == std::string::npos ? text.size() : end) - start;
    if (!count || (count > 1 && text[start] == '0') || text.find_first_not_of("0123456789", start) != end)
      return false;
    start = end + 1;
  }
  return true;
}
static std::string Resource(HMODULE module, WORD id) {
  const auto found = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  if (!found) return {};
  const auto size = SizeofResource(module, found);
  const auto data = LockResource(LoadResource(module, found));
  return data && size ? std::string(static_cast<const char*>(data), size) : std::string();
}
void ValidateAgentUpdate(const AgentUpdateSpec& spec, const ServerOptions& o) {
  if (!IsAgentUpdateId(spec.id) || !IsVersion(spec.version) || !IsHex(spec.hash, 64) ||
      !IsHex(spec.source_hash, 64) || !spec.bytes ||
      spec.bytes > std::min<uint64_t>(64 * kBytesPerMiB, o.max_transfer_bytes) || spec.protocol > 1 ||
      spec.architecture != (sizeof(void*) == 8 ? "amd64" : "i686"))
    throw std::runtime_error("Invalid agent release metadata or transfer size.");
  if (!o.auth_required && spec.protocol && spec.source_hash != spec.hash &&
      !MatchesLegacyAgent(Resource(nullptr, IDR_AGENT_LEGACY_RELEASES), spec))
    throw std::runtime_error("Configure agent authentication to authorize a new remote release.");
  if (spec.source_hash != RunningAgentHash()) throw std::runtime_error("The running agent build changed.");
  if (!spec.protocol) {
    if (!MatchesLegacyAgent(Resource(nullptr, IDR_AGENT_LEGACY_RELEASES), spec))
      throw std::runtime_error("Unknown legacy release SHA-256.");
    const int minor = std::stoi(spec.version.substr(2));
    if ((minor < 8 && o.multiple_connections) ||
        (minor < 6 && o.max_transfer_bytes != kDefaultMaxTransferBytes))
      throw std::runtime_error("This release cannot preserve the connection or transfer settings.");
    wchar_t temp[32768];
    const auto count = GetTempPathW(32768, temp);
    auto workspace = Utf8ToWide(o.safe_base_dir);
    while (workspace.size() > 3 && workspace.back() == L'\\') workspace.pop_back();
    std::wstring default_dir = count && count < 32768 ? std::wstring(temp, count) : L"";
    while (default_dir.size() > 3 && default_dir.back() == L'\\') default_dir.pop_back();
    if (_wcsicmp(workspace.c_str(), default_dir.c_str()) != 0)
      throw std::runtime_error("This release cannot preserve the configured workspace.");
  }
}
std::wstring AgentUpdateDirectory(const std::wstring& original, const std::string& id) {
  return original.substr(0, original.find_last_of(L"\\/") + 1) + L".agent-rover-update-" + Utf8ToWide(id);
}
KernelHandle CreateUpdateMapping() {
  SECURITY_ATTRIBUTES attrs = {sizeof(attrs), nullptr, TRUE};
  KernelHandle h(
      CreateFileMappingW(INVALID_HANDLE_VALUE, &attrs, PAGE_READWRITE, 0, sizeof(UpdateBridge), nullptr));
  if (!h) throw Failure("CreateFileMappingW(update)");
  return h;
}
UpdateView MapUpdate(HANDLE h) {
  UpdateView view(
      static_cast<UpdateBridge*>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(UpdateBridge))));
  if (!view) throw Failure("MapViewOfFile(update)");
  return view;
}
template <class Char, size_t N>
static void CopyText(Char (&dest)[N], const std::basic_string<Char>& value) {
  if (value.size() >= N) throw std::runtime_error("Update settings exceed IPC limits.");
  std::copy(value.begin(), value.end(), dest);
  dest[value.size()] = 0;
}
static KernelHandle NewEvent() {
  SECURITY_ATTRIBUTES attrs = {sizeof(attrs), nullptr, TRUE};
  KernelHandle event(CreateEventW(&attrs, FALSE, FALSE, nullptr));
  if (!event) throw Failure("CreateEventW(update)");
  return event;
}
static void WriteNew(const std::wstring& path, const void* bytes, DWORD size) {
  KernelHandle file(
      CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) throw Failure("CreateFileW(update)");
  DWORD wrote = 0;
  if (!WriteFile(file.get(), bytes, size, &wrote, nullptr) || wrote != size ||
      !FlushFileBuffers(file.get()))
    throw Failure("WriteFile(update)");
}
void SaveAgentUpdateResult(const std::wstring& image, const std::string& record) {
  const auto path = image + L".update-result";
  const auto temp = path + L"." + std::to_wstring(GetCurrentProcessId());
  WriteNew(temp, record.data(), static_cast<DWORD>(record.size()));
  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto error = Failure("MoveFileExW(update result)");
    DeleteFileW(temp.c_str());
    throw error;
  }
}
cardio::promise<std::string> AgentUpdateStatus(std::string id) {
  if (!IsAgentUpdateId(id)) throw std::runtime_error("Invalid update ID.");
  KernelHandle file(CreateFileW((AgentImagePath() + L".update-result").c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) co_return "null";
    throw Failure("Open agent update result");
  }
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 1 || size.QuadPart > 8192)
    throw Failure("Read update result size");
  std::string result(static_cast<size_t>(size.QuadPart), '\0');
  auto buffer = std::as_writable_bytes(std::span(result));
  size_t offset = 0;
  while (offset < buffer.size()) {
    const auto count =
        co_await cardio::win32::read(file.get(), buffer.subspan(offset), static_cast<uint64_t>(offset));
    if (!count) throw std::runtime_error("Truncated agent update result.");
    offset += count;
  }
  std::string saved;
  co_return FindJsonStringField(result, "updateId", &saved) && saved == id ? result : "null";
}
static void CheckImage(const std::wstring& file, const AgentUpdateSpec& spec,
                       const std::vector<unsigned char>& image) {
  if (image.size() < 64 || image[0] != 'M' || image[1] != 'Z')
    throw std::runtime_error("Invalid PE image.");
  uint32_t pe = 0;
  std::memcpy(&pe, image.data() + 60, 4);
  if (pe < 64 || pe > image.size() - 26 || std::memcmp(image.data() + pe, "PE\0\0", 4) != 0)
    throw std::runtime_error("Invalid PE header.");
  uint16_t machine = 0, flags = 0;
  std::memcpy(&machine, image.data() + pe + 4, 2);
  std::memcpy(&flags, image.data() + pe + 22, 2);
  if (machine != (sizeof(void*) == 8 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386) ||
      (flags & (IMAGE_FILE_DLL | IMAGE_FILE_EXECUTABLE_IMAGE)) != IMAGE_FILE_EXECUTABLE_IMAGE)
    throw std::runtime_error("Agent executable architecture mismatch.");
  if (spec.protocol) {
    const auto module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) throw Failure("LoadLibraryExW(release resources)");
    const auto project = Resource(module, IDR_AGENT_UPDATE_PROJECT),
               version = Resource(module, IDR_AGENT_RELEASE_VERSION);
    FreeLibrary(module);
    if (project != "kekyo/agent-rover update 1" || version != spec.version)
      throw std::runtime_error("Agent product or version mismatch.");
  }
}
cardio::promise<void> HandOffAgentUpdate(AgentUpdateSpec spec, std::vector<unsigned char> image,
                                         ServerOptions options, SocketConnection socket,
                                         std::string request_id, cardio::cancellation stop) {
  ValidateAgentUpdate(spec, options);
  if (image.size() != spec.bytes || Sha256Hex(image) != spec.hash)
    throw std::runtime_error("Transferred executable SHA-256 mismatch.");
  const auto original = AgentImagePath(), dir = AgentUpdateDirectory(original, spec.id);
  const auto target = dir + L"\\release.exe", helper = dir + L"\\helper.exe";
  auto mapping = CreateUpdateMapping();
  auto bridge = MapUpdate(mapping.get());
  auto joined = NewEvent(), offered = NewEvent();
  HANDLE inherited = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(options.supervisor_handle),
                       GetCurrentProcess(), &inherited, SYNCHRONIZE, TRUE, 0))
    throw Failure("DuplicateHandle(update supervisor)");
  KernelHandle supervisor(inherited);
  bridge->magic = kUpdateMagic;
  bridge->supervisor = inherited;
  bridge->joined = joined.get();
  bridge->offered = offered.get();
  CopyText(bridge->original, original);
  CopyText(bridge->id, spec.id);
  CopyText(bridge->version, spec.version);
  CopyText(bridge->source_version, std::string(AGENT_ROVER_AGENT_VERSION));
  CopyText(bridge->source_hash, spec.source_hash);
  CopyText(bridge->hash, spec.hash);
  CopyText(bridge->request_id, request_id);
  bridge->protocol = spec.protocol;
  auto& settings = bridge->settings;
  settings.port = options.port;
  settings.auth = options.auth_required;
  settings.generated = options.show_auth_token;
  settings.multiple = options.multiple_connections;
  settings.max_transfer_bytes = options.max_transfer_bytes;
  CopyText(settings.host, options.host);
  CopyText(settings.token, options.auth_token);
  CopyText(settings.config, options.config_path);
  CopyText(settings.workspace, options.safe_base_dir);
  const auto length = GetCurrentDirectoryW(32768, bridge->working_directory);
  if (!length || length >= 32768) throw Failure("GetCurrentDirectoryW");
  if (!CreateDirectoryW(dir.c_str(), nullptr)) throw Failure("CreateDirectoryW(update)");
  try {
    WriteNew(target, image.data(), static_cast<DWORD>(image.size()));
    CheckImage(target, spec, image);
    if (!CopyFileW(original.c_str(), helper.c_str(), TRUE) || AgentImageHash(helper) != spec.source_hash)
      throw std::runtime_error("Unable to stage the running updater.");
    auto command = BuildCommandLine(
        helper, {L"--agent-update-helper", std::to_wstring(reinterpret_cast<uintptr_t>(mapping.get()))});
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                        dir.c_str(), &startup, &process))
      throw Failure("CreateProcessW(update)");
    KernelHandle child(process.hProcess), thread(process.hThread);
    try {
      ShareUpdateSocket(socket, process.dwProcessId, &bridge->connection);
      if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) throw Failure("ResumeThread(update)");
    } catch (...) {
      TerminateProcess(child.get(), 1);
      throw;
    }
    auto timeout = cardio::cancellations::timeout(15000);
    auto cancel = cardio::cancellations::any(stop, timeout.get_cancellation());
    co_await cardio::from_win32_handle(joined.get(), cancel.get_cancellation());
    if (bridge->error[0]) throw std::runtime_error(bridge->error);
    SetEvent(offered.get());
  } catch (...) {
    DeleteFileW(target.c_str());
    DeleteFileW(helper.c_str());
    RemoveDirectoryW(dir.c_str());
    throw;
  }
}
}  // namespace agent_rover
