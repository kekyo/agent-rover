// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "json_protocol.h"

#include "../protocol_version.h"

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "binary_codec.h"
#include "win32_capture.h"
#include "win32_clipboard.h"
#include "win32_eventlog.h"
#include "win32_files.h"
#include "win32_input.h"
#include "win32_process.h"
#include "win32_windows.h"

namespace agent_rover {

static constexpr uint32_t kBinaryTransferChunkBytes = 64 * 1024;

static std::string JsonEscape(const std::string& value) {
  std::string output;
  for (const char ch : value) {
    switch (ch) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          output += "\\u00";
          const char* digits = "0123456789abcdef";
          const unsigned char byte = static_cast<unsigned char>(ch);
          output.push_back(digits[(byte >> 4) & 0x0f]);
          output.push_back(digits[byte & 0x0f]);
        } else {
          output.push_back(ch);
        }
        break;
    }
  }
  return output;
}

static bool ReadEscapedJsonString(
    const std::string& json,
    size_t start,
    std::string* value,
    size_t* end) {
  std::string output;
  size_t index = start;
  while (index < json.size()) {
    const char ch = json[index];
    if (ch == '"') {
      *value = output;
      *end = index + 1;
      return true;
    }
    if (ch == '\\') {
      index += 1;
      if (index >= json.size()) {
        return false;
      }
      const char escaped = json[index];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        default:
          return false;
      }
    } else {
      output.push_back(ch);
    }
    index += 1;
  }
  return false;
}

static bool FindJsonStringField(
    const std::string& json,
    const std::string& key,
    std::string* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.find(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }
  size_t value_position = colon_position + 1;
  while (value_position < json.size() &&
         (json[value_position] == ' ' || json[value_position] == '\t' ||
          json[value_position] == '\r' || json[value_position] == '\n')) {
    value_position += 1;
  }
  if (value_position >= json.size() || json[value_position] != '"') {
    return false;
  }

  size_t end = 0;
  return ReadEscapedJsonString(json, value_position + 1, value, &end);
}

static bool FindLastJsonStringField(
    const std::string& json,
    const std::string& key,
    std::string* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.rfind(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }
  size_t value_position = colon_position + 1;
  while (value_position < json.size() &&
         (json[value_position] == ' ' || json[value_position] == '\t' ||
          json[value_position] == '\r' || json[value_position] == '\n')) {
    value_position += 1;
  }
  if (value_position >= json.size() || json[value_position] != '"') {
    return false;
  }

  size_t end = 0;
  return ReadEscapedJsonString(json, value_position + 1, value, &end);
}

static bool FindJsonStringArrayField(
    const std::string& json,
    const std::string& key,
    std::vector<std::string>* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.find(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }

  size_t index = colon_position + 1;
  while (index < json.size() &&
         (json[index] == ' ' || json[index] == '\t' || json[index] == '\r' ||
          json[index] == '\n')) {
    index += 1;
  }
  if (index >= json.size() || json[index] != '[') {
    return false;
  }
  index += 1;
  value->clear();

  for (;;) {
    while (index < json.size() &&
           (json[index] == ' ' || json[index] == '\t' ||
            json[index] == '\r' || json[index] == '\n')) {
      index += 1;
    }
    if (index >= json.size()) {
      return false;
    }
    if (json[index] == ']') {
      return true;
    }
    if (json[index] != '"') {
      return false;
    }

    std::string item;
    size_t end = 0;
    if (!ReadEscapedJsonString(json, index + 1, &item, &end)) {
      return false;
    }
    value->push_back(item);
    index = end;
    while (index < json.size() &&
           (json[index] == ' ' || json[index] == '\t' ||
            json[index] == '\r' || json[index] == '\n')) {
      index += 1;
    }
    if (index >= json.size()) {
      return false;
    }
    if (json[index] == ',') {
      index += 1;
      continue;
    }
    if (json[index] == ']') {
      return true;
    }
    return false;
  }
}

static bool FindJsonBoolField(
    const std::string& json,
    const std::string& key,
    bool* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.find(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }
  size_t value_position = colon_position + 1;
  while (value_position < json.size() &&
         (json[value_position] == ' ' || json[value_position] == '\t' ||
          json[value_position] == '\r' || json[value_position] == '\n')) {
    value_position += 1;
  }
  if (json.compare(value_position, 4, "true") == 0) {
    *value = true;
    return true;
  }
  if (json.compare(value_position, 5, "false") == 0) {
    *value = false;
    return true;
  }
  return false;
}

static bool FindJsonNumberField(
    const std::string& json,
    const std::string& key,
    int* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.find(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }
  size_t value_position = colon_position + 1;
  while (value_position < json.size() &&
         (json[value_position] == ' ' || json[value_position] == '\t' ||
          json[value_position] == '\r' || json[value_position] == '\n')) {
    value_position += 1;
  }
  char* end = nullptr;
  const long parsed = std::strtol(json.c_str() + value_position, &end, 10);
  if (end == json.c_str() + value_position) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

static bool FindJsonObjectField(
    const std::string& json,
    const std::string& key,
    std::string* value);

static bool ReadJsonStringObject(
    const std::string& object,
    std::map<std::string, std::string>* values) {
  values->clear();
  size_t index = 1;
  for (;;) {
    while (index < object.size() &&
           (object[index] == ' ' || object[index] == '\t' ||
            object[index] == '\r' || object[index] == '\n')) {
      index += 1;
    }
    if (index >= object.size()) {
      return false;
    }
    if (object[index] == '}') {
      return true;
    }
    if (object[index] != '"') {
      return false;
    }
    std::string key;
    size_t end = 0;
    if (!ReadEscapedJsonString(object, index + 1, &key, &end)) {
      return false;
    }
    index = end;
    while (index < object.size() &&
           (object[index] == ' ' || object[index] == '\t' ||
            object[index] == '\r' || object[index] == '\n')) {
      index += 1;
    }
    if (index >= object.size() || object[index] != ':') {
      return false;
    }
    index += 1;
    while (index < object.size() &&
           (object[index] == ' ' || object[index] == '\t' ||
            object[index] == '\r' || object[index] == '\n')) {
      index += 1;
    }
    if (index >= object.size() || object[index] != '"') {
      return false;
    }
    std::string value;
    if (!ReadEscapedJsonString(object, index + 1, &value, &end)) {
      return false;
    }
    (*values)[key] = value;
    index = end;
    while (index < object.size() &&
           (object[index] == ' ' || object[index] == '\t' ||
            object[index] == '\r' || object[index] == '\n')) {
      index += 1;
    }
    if (index >= object.size()) {
      return false;
    }
    if (object[index] == ',') {
      index += 1;
      continue;
    }
    if (object[index] == '}') {
      return true;
    }
    return false;
  }
}

static bool FindJsonStringObjectField(
    const std::string& json,
    const std::string& key,
    std::map<std::string, std::string>* values) {
  std::string object;
  if (!FindJsonObjectField(json, key, &object)) {
    return false;
  }
  return ReadJsonStringObject(object, values);
}

static bool FindJsonObjectField(
    const std::string& json,
    const std::string& key,
    std::string* value) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = json.find(marker);
  if (key_position == std::string::npos) {
    return false;
  }
  const size_t colon_position = json.find(':', key_position + marker.size());
  if (colon_position == std::string::npos) {
    return false;
  }
  size_t index = colon_position + 1;
  while (index < json.size() &&
         (json[index] == ' ' || json[index] == '\t' || json[index] == '\r' ||
          json[index] == '\n')) {
    index += 1;
  }
  if (index >= json.size() || json[index] != '{') {
    return false;
  }
  const size_t start = index;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (; index < json.size(); index += 1) {
    const char ch = json[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      depth += 1;
    } else if (ch == '}') {
      depth -= 1;
      if (depth == 0) {
        *value = json.substr(start, index - start + 1);
        return true;
      }
    }
  }
  return false;
}

static std::string CapabilitiesJson() {
  std::string output =
      "{\"features\":["
      "\"capabilities\","
      "\"clipboard.clear\","
      "\"clipboard.readText\","
      "\"clipboard.writeText\","
      "\"agent.bounds\","
      "\"agent.cursor\","
      "\"agent.monitors\","
      "\"agent.screenshot\","
      "\"windows\","
      "\"window.children\","
      "\"applications.launch\","
      "\"window.activate\","
      "\"window.close\","
      "\"window.focus\","
      "\"window.setBounds\","
      "\"window.show\","
      "\"window.screenshot\","
      "\"window.snapshot\","
      "\"input.perform\","
      "\"file.read\","
      "\"file.write\","
      "\"file.exists\","
      "\"file.stat\","
      "\"file.mkdir\","
      "\"file.readdir\","
      "\"file.manifest\","
      "\"file.remove\","
      "\"file.rename\","
      "\"file.mkdtemp\","
      "\"process.kill\","
      "\"process.killManaged\","
      "\"process.list\","
      "\"process.releaseManaged\","
      "\"process.launchManaged\","
      "\"process.managedSnapshot\","
      "\"process.snapshot\","
      "\"eventLogs.read\","
      "\"";
  output += kTcpFrameCapabilityId;
  output += "\",";
  output +=
      "\"agent.native-windows\""
      "],\"platform\":\"windows\","
      "\"protocolVersion\":\"";
  output += kProtocolVersion;
  output += "\"}";
  return output;
}

static std::string SuccessResponseJson(
    const std::string& id,
    const std::string& result) {
  std::string output = "{\"id\":\"";
  output += JsonEscape(id);
  output += "\",\"kind\":\"response\",\"ok\":true,\"result\":";
  output += result;
  output += "}";
  return output;
}

static std::string FailureResponseJson(
    const std::string& id,
    const std::string& message) {
  std::string output = "{\"id\":\"";
  output += JsonEscape(id);
  output += "\",\"kind\":\"response\",\"ok\":false,\"error\":{";
  output += "\"code\":\"PROTOCOL_ERROR\",";
  output += "\"message\":\"";
  output += JsonEscape(message);
  output += "\"}}";
  return output;
}

static void AppendJsonString(std::string* output, const std::string& value) {
  output->push_back('"');
  *output += JsonEscape(value);
  output->push_back('"');
}

static std::string WindowJson(const WindowInfo& window) {
  std::string output = "{\"id\":";
  AppendJsonString(&output, window.id);
  output += ",\"parentId\":";
  if (window.parent_id.empty()) {
    output += "null";
  } else {
    AppendJsonString(&output, window.parent_id);
  }
  output += ",\"title\":";
  AppendJsonString(&output, window.title);
  output += ",\"className\":";
  AppendJsonString(&output, window.class_name);
  output += ",\"controlId\":";
  output += std::to_string(window.control_id);
  output += ",\"visible\":";
  output += window.visible ? "true" : "false";
  output += ",\"active\":";
  output += window.active ? "true" : "false";
  output += ",\"focused\":";
  output += window.focused ? "true" : "false";
  output += ",\"enabled\":";
  output += window.enabled ? "true" : "false";
  output += ",\"minimized\":";
  output += window.minimized ? "true" : "false";
  output += ",\"maximized\":";
  output += window.maximized ? "true" : "false";
  output += ",\"bounds\":{\"x\":";
  output += std::to_string(window.bounds.x);
  output += ",\"y\":";
  output += std::to_string(window.bounds.y);
  output += ",\"width\":";
  output += std::to_string(window.bounds.width);
  output += ",\"height\":";
  output += std::to_string(window.bounds.height);
  output += "},\"process\":{\"id\":";
  output += std::to_string(window.process.id);
  output += ",\"name\":";
  AppendJsonString(&output, window.process.name);
  output += ",\"path\":";
  AppendJsonString(&output, window.process.path);
  output += "}}";
  return output;
}

static std::string WindowArrayJson(const std::vector<WindowInfo>& windows) {
  std::string output = "[";
  for (size_t index = 0; index < windows.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += WindowJson(windows[index]);
  }
  output += "]";
  return output;
}

static std::string BinaryTransferMetadataJson(
    const std::string& transfer_id,
    const std::string& content_type,
    const std::vector<unsigned char>& data) {
  std::string output = "{\"transferId\":";
  AppendJsonString(&output, transfer_id);
  output += ",\"contentType\":";
  AppendJsonString(&output, content_type);
  output += ",\"totalBytes\":";
  output += std::to_string(data.size());
  output += ",\"sha256\":";
  AppendJsonString(&output, Sha256Hex(data));
  output += "}";
  return output;
}

static std::string FileStatJson(const FileStat& stat) {
  std::string output = "{\"type\":";
  AppendJsonString(&output, stat.type);
  output += ",\"size\":";
  output += std::to_string(stat.size);
  output += ",\"createdAt\":";
  AppendJsonString(&output, stat.created_at);
  output += ",\"modifiedAt\":";
  AppendJsonString(&output, stat.modified_at);
  output += "}";
  return output;
}

static std::string DirectoryEntryJson(const DirectoryEntry& entry) {
  std::string output = "{\"name\":";
  AppendJsonString(&output, entry.name);
  output += ",\"type\":";
  AppendJsonString(&output, entry.stat.type);
  output += ",\"size\":";
  output += std::to_string(entry.stat.size);
  output += ",\"createdAt\":";
  AppendJsonString(&output, entry.stat.created_at);
  output += ",\"modifiedAt\":";
  AppendJsonString(&output, entry.stat.modified_at);
  output += "}";
  return output;
}

static std::string DirectoryEntryArrayJson(
    const std::vector<DirectoryEntry>& entries) {
  std::string output = "[";
  for (size_t index = 0; index < entries.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += DirectoryEntryJson(entries[index]);
  }
  output += "]";
  return output;
}

static std::string DirectoryManifestEntryJson(
    const DirectoryManifestEntry& entry) {
  std::string output = "{\"path\":";
  AppendJsonString(&output, entry.path);
  output += ",\"type\":";
  AppendJsonString(&output, entry.type);
  output += ",\"size\":";
  output += std::to_string(entry.size);
  output += ",\"modifiedAt\":";
  AppendJsonString(&output, entry.modified_at);
  if (entry.has_sha256) {
    output += ",\"sha256\":";
    AppendJsonString(&output, entry.sha256);
  }
  output += "}";
  return output;
}

static std::string DirectoryManifestJson(
    const std::vector<DirectoryManifestEntry>& entries) {
  std::string output = "{\"entries\":[";
  for (size_t index = 0; index < entries.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += DirectoryManifestEntryJson(entries[index]);
  }
  output += "]}";
  return output;
}

static std::string ApplicationProcessJson(const ApplicationProcess& process) {
  std::string output = "{\"id\":";
  output += std::to_string(process.id);
  output += ",\"name\":";
  AppendJsonString(&output, process.name);
  output += "}";
  return output;
}

static std::string ManagedProcessJson(const ManagedProcess& process) {
  std::string output = "{\"managedProcessId\":";
  output += std::to_string(process.managed_id);
  output += ",\"id\":";
  output += std::to_string(process.process.id);
  output += ",\"name\":";
  AppendJsonString(&output, process.process.name);
  output += ",\"stdoutPath\":";
  if (process.stdout_path.empty()) {
    output += "null";
  } else {
    AppendJsonString(&output, process.stdout_path);
  }
  output += ",\"stderrPath\":";
  if (process.stderr_path.empty()) {
    output += "null";
  } else {
    AppendJsonString(&output, process.stderr_path);
  }
  output += "}";
  return output;
}

static std::string ProcessSnapshotJson(const ProcessSnapshot& process) {
  std::string output = "{\"id\":";
  output += std::to_string(process.id);
  output += ",\"name\":";
  AppendJsonString(&output, process.name);
  output += ",\"path\":";
  AppendJsonString(&output, process.path);
  output += ",\"parentProcessId\":";
  if (process.has_parent_process_id) {
    output += std::to_string(process.parent_process_id);
  } else {
    output += "null";
  }
  output += ",\"createdAt\":";
  if (process.created_at.empty()) {
    output += "null";
  } else {
    AppendJsonString(&output, process.created_at);
  }
  output += ",\"running\":";
  output += process.running ? "true" : "false";
  output += ",\"exitCode\":";
  if (process.has_exit_code) {
    output += std::to_string(process.exit_code);
  } else {
    output += "null";
  }
  output += "}";
  return output;
}

static std::string ProcessSnapshotArrayJson(
    const std::vector<ProcessSnapshot>& processes) {
  std::string output = "[";
  for (size_t index = 0; index < processes.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += ProcessSnapshotJson(processes[index]);
  }
  output += "]";
  return output;
}

static std::string ClipboardTextJson(const std::string& text) {
  std::string output = "{\"text\":";
  AppendJsonString(&output, text);
  output += "}";
  return output;
}

static void AppendRectJson(std::string* output, const WindowRect& rect) {
  *output += "{\"x\":";
  *output += std::to_string(rect.x);
  *output += ",\"y\":";
  *output += std::to_string(rect.y);
  *output += ",\"width\":";
  *output += std::to_string(rect.width);
  *output += ",\"height\":";
  *output += std::to_string(rect.height);
  *output += "}";
}

static std::string MonitorJson(const ScreenMonitor& monitor) {
  std::string output = "{\"id\":";
  AppendJsonString(&output, monitor.id);
  output += ",\"name\":";
  AppendJsonString(&output, monitor.name);
  output += ",\"bounds\":";
  AppendRectJson(&output, monitor.bounds);
  output += ",\"workArea\":";
  AppendRectJson(&output, monitor.work_area);
  output += ",\"primary\":";
  output += monitor.primary ? "true" : "false";
  output += ",\"scaleFactor\":";
  output += std::to_string(monitor.scale_factor);
  output += "}";
  return output;
}

static std::string MonitorArrayJson(
    const std::vector<ScreenMonitor>& monitors) {
  std::string output = "[";
  for (size_t index = 0; index < monitors.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += MonitorJson(monitors[index]);
  }
  output += "]";
  return output;
}

static std::string CursorJson(const ScreenCursor& cursor) {
  std::string output = "{\"point\":{\"x\":";
  output += std::to_string(cursor.x);
  output += ",\"y\":";
  output += std::to_string(cursor.y);
  output += "},\"visible\":";
  output += cursor.visible ? "true" : "false";
  output += "}";
  return output;
}

static std::string ScreenshotJson(
    const WindowScreenshot& screenshot,
    const std::string& transfer_id) {
  std::string output = "{\"transferId\":";
  AppendJsonString(&output, transfer_id);
  output += ",\"contentType\":\"image/png\",";
  output += "\"totalBytes\":";
  output += std::to_string(screenshot.image.size());
  output += ",\"sha256\":";
  AppendJsonString(&output, Sha256Hex(screenshot.image));
  output += ",\"clipped\":";
  output += screenshot.clipped ? "true" : "false";
  output += ",\"bounds\":";
  AppendRectJson(&output, screenshot.bounds);
  output += ",\"visibleBounds\":";
  AppendRectJson(&output, screenshot.visible_bounds);
  output += "}";
  return output;
}

static void AddBinaryTransferChunks(
    const std::string& transfer_id,
    const std::string& content_type,
    const std::vector<unsigned char>& data,
    std::vector<BinaryTransferChunk>* outbound_chunks) {
  std::vector<BinaryTransferChunk> chunks;
  CreateBinaryTransferChunks(
      transfer_id, content_type, data, kBinaryTransferChunkBytes, &chunks);
  outbound_chunks->insert(
      outbound_chunks->end(), chunks.begin(), chunks.end());
}

static std::string EventLogEntryJson(const EventLogEntry& entry) {
  std::string output = "{\"id\":";
  output += std::to_string(entry.id);
  output += ",\"provider\":";
  AppendJsonString(&output, entry.provider);
  output += ",\"level\":";
  AppendJsonString(&output, entry.level);
  output += ",\"timestamp\":";
  AppendJsonString(&output, entry.timestamp);
  output += ",\"message\":";
  AppendJsonString(&output, entry.message);
  output += "}";
  return output;
}

static std::string EventLogArrayJson(
    const std::vector<EventLogEntry>& entries) {
  std::string output = "[";
  for (size_t index = 0; index < entries.size(); index += 1) {
    if (index != 0) {
      output += ",";
    }
    output += EventLogEntryJson(entries[index]);
  }
  output += "]";
  return output;
}

static bool ReadPointField(
    const std::string& payload,
    const std::string& key,
    InputPoint* point) {
  std::string object;
  if (!FindJsonObjectField(payload, key, &object)) {
    return false;
  }
  return FindJsonNumberField(object, "x", &point->x) &&
         FindJsonNumberField(object, "y", &point->y);
}

static bool ReadRectField(
    const std::string& payload,
    const std::string& key,
    WindowRect* rect) {
  std::string object;
  if (!FindJsonObjectField(payload, key, &object)) {
    return false;
  }
  return FindJsonNumberField(object, "x", &rect->x) &&
         FindJsonNumberField(object, "y", &rect->y) &&
         FindJsonNumberField(object, "width", &rect->width) &&
         FindJsonNumberField(object, "height", &rect->height);
}

static bool ReadApplicationLaunchOptions(
    const std::string& payload,
    const std::string& method,
    ApplicationLaunchOptions* options,
    std::string* error) {
  *options = {};
  if (!FindJsonStringField(payload, "path", &options->path)) {
    *error = method + " requires path.";
    return false;
  }
  std::vector<std::string> arguments;
  if (FindJsonStringArrayField(payload, "arguments", &arguments)) {
    options->arguments = arguments;
  }
  std::string working_directory;
  if (FindJsonStringField(payload, "workingDirectory", &working_directory)) {
    options->working_directory = working_directory;
  }
  FindJsonStringObjectField(payload, "environment", &options->environment);
  FindJsonStringField(payload, "stdoutPath", &options->stdout_path);
  FindJsonStringField(payload, "stderrPath", &options->stderr_path);
  FindJsonBoolField(payload, "createNoWindow", &options->create_no_window);
  return true;
}

static bool ParseInputOperation(
    const std::string& payload,
    InputOperation* operation,
    std::string* error) {
  *operation = {};
  operation->button = "left";
  if (!FindLastJsonStringField(payload, "kind", &operation->kind)) {
    *error = "input.perform requires an operation kind.";
    return false;
  }
  FindJsonStringField(payload, "key", &operation->key);
  FindJsonStringField(payload, "text", &operation->text);
  FindJsonStringField(payload, "button", &operation->button);
  FindJsonStringArrayField(payload, "modifiers", &operation->modifiers);
  operation->has_point = ReadPointField(payload, "point", &operation->point);
  ReadPointField(payload, "from", &operation->from);
  ReadPointField(payload, "to", &operation->to);
  FindJsonNumberField(payload, "deltaX", &operation->delta_x);
  FindJsonNumberField(payload, "deltaY", &operation->delta_y);

  if ((operation->kind == "mouse.move" || operation->kind == "mouse.click") &&
      !operation->has_point) {
    *error = operation->kind + " requires point.";
    return false;
  }
  if (operation->kind == "mouse.drag" &&
      (!ReadPointField(payload, "from", &operation->from) ||
       !ReadPointField(payload, "to", &operation->to))) {
    *error = "mouse.drag requires from and to.";
    return false;
  }
  return true;
}

std::string CreateReadyEventJson() {
  std::string output = "{\"data\":{\"capabilities\":";
  output += CapabilitiesJson();
  output += ",\"protocolVersion\":\"";
  output += kProtocolVersion;
  output += "\"},\"kind\":\"event\",\"name\":\"agent.ready\"}";
  return output;
}

std::string HandleJsonRequest(
    const std::string& payload,
    BinaryTransferStore* transfers,
    std::vector<BinaryTransferChunk>* outbound_chunks) {
  outbound_chunks->clear();
  std::string kind;
  std::string id;
  std::string method;
  if (!FindJsonStringField(payload, "kind", &kind) ||
      !FindJsonStringField(payload, "id", &id)) {
    return FailureResponseJson("invalid-request", "Invalid request message.");
  }
  if (kind != "request" || !FindJsonStringField(payload, "method", &method)) {
    return FailureResponseJson(id, "Only request messages are supported.");
  }
  if (method == "agent.capabilities") {
    return SuccessResponseJson(id, CapabilitiesJson());
  }
  if (method == "clipboard.readText") {
    std::string text;
    std::string error;
    if (!ReadClipboardText(&text, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ClipboardTextJson(text));
  }
  if (method == "clipboard.writeText") {
    std::string text;
    if (!FindJsonStringField(payload, "text", &text)) {
      return FailureResponseJson(id, "clipboard.writeText requires text.");
    }
    std::string error;
    if (!WriteClipboardText(text, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "clipboard.clear") {
    std::string error;
    if (!ClearClipboard(&error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "agent.bounds") {
    WindowRect bounds = {};
    std::string error;
    if (!GetScreenBounds(&bounds, &error)) {
      return FailureResponseJson(id, error);
    }
    std::string output;
    AppendRectJson(&output, bounds);
    return SuccessResponseJson(id, output);
  }
  if (method == "agent.monitors") {
    std::vector<ScreenMonitor> monitors;
    std::string error;
    if (!ListScreenMonitors(&monitors, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, MonitorArrayJson(monitors));
  }
  if (method == "agent.cursor") {
    ScreenCursor cursor = {};
    std::string error;
    if (!ReadScreenCursor(&cursor, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, CursorJson(cursor));
  }
  if (method == "agent.screenshot") {
    WindowRect bounds = {};
    WindowRect* requested_bounds = nullptr;
    std::string rect_object;
    if (FindJsonObjectField(payload, "rect", &rect_object)) {
      if (!ReadRectField(payload, "rect", &bounds)) {
        return FailureResponseJson(id, "agent.screenshot rect is invalid.");
      }
      requested_bounds = &bounds;
    }
    WindowScreenshot screenshot = {};
    std::string error;
    if (!CaptureScreenScreenshot(requested_bounds, &screenshot, &error)) {
      return FailureResponseJson(id, error);
    }
    const std::string transfer_id = id + "-screen-screenshot";
    AddBinaryTransferChunks(
        transfer_id, "image/png", screenshot.image, outbound_chunks);
    return SuccessResponseJson(id, ScreenshotJson(screenshot, transfer_id));
  }
  if (method == "agent.windows") {
    std::vector<WindowInfo> windows;
    std::string error;
    if (!ListTopLevelWindows(&windows, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowArrayJson(windows));
  }
  if (method == "window.children") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.children requires windowId.");
    }
    std::vector<WindowInfo> windows;
    std::string error;
    if (!ListChildWindows(window_id, &windows, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowArrayJson(windows));
  }
  if (method == "applications.launch") {
    ApplicationLaunchOptions options;
    std::string parse_error;
    if (!ReadApplicationLaunchOptions(
            payload, "applications.launch", &options, &parse_error)) {
      return FailureResponseJson(id, parse_error);
    }
    ApplicationProcess process = {};
    std::string error;
    if (!LaunchApplication(options, &process, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ApplicationProcessJson(process));
  }
  if (method == "process.launchManaged") {
    ManagedProcessLaunchOptions options = {};
    std::string parse_error;
    if (!ReadApplicationLaunchOptions(
            payload, "process.launchManaged", &options.launch, &parse_error)) {
      return FailureResponseJson(id, parse_error);
    }
    FindJsonBoolField(
        payload, "killTreeOnRelease", &options.kill_tree_on_release);
    ManagedProcess process = {};
    std::string error;
    if (!LaunchManagedProcess(options, &process, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ManagedProcessJson(process));
  }
  if (method == "process.managedSnapshot") {
    int managed_process_id = 0;
    if (!FindJsonNumberField(payload, "managedProcessId", &managed_process_id) ||
        managed_process_id < 0) {
      return FailureResponseJson(
          id, "process.managedSnapshot requires managedProcessId.");
    }
    ProcessSnapshot process = {};
    std::string error;
    if (!SnapshotManagedProcess(
            static_cast<uint32_t>(managed_process_id), &process, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ProcessSnapshotJson(process));
  }
  if (method == "process.killManaged") {
    int managed_process_id = 0;
    if (!FindJsonNumberField(payload, "managedProcessId", &managed_process_id) ||
        managed_process_id < 0) {
      return FailureResponseJson(
          id, "process.killManaged requires managedProcessId.");
    }
    std::string error;
    if (!KillManagedProcess(static_cast<uint32_t>(managed_process_id), &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "process.releaseManaged") {
    int managed_process_id = 0;
    if (!FindJsonNumberField(payload, "managedProcessId", &managed_process_id) ||
        managed_process_id < 0) {
      return FailureResponseJson(
          id, "process.releaseManaged requires managedProcessId.");
    }
    std::string error;
    if (!ReleaseManagedProcess(
            static_cast<uint32_t>(managed_process_id), &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "process.snapshot") {
    int process_id = 0;
    if (!FindJsonNumberField(payload, "processId", &process_id) ||
        process_id < 0) {
      return FailureResponseJson(id, "process.snapshot requires processId.");
    }
    ProcessSnapshot process = {};
    std::string error;
    if (!SnapshotProcess(
            static_cast<uint32_t>(process_id), &process, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ProcessSnapshotJson(process));
  }
  if (method == "process.list") {
    ProcessListOptions options;
    FindJsonStringField(payload, "name", &options.name);
    std::vector<ProcessSnapshot> processes;
    std::string error;
    if (!ListProcesses(options, &processes, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, ProcessSnapshotArrayJson(processes));
  }
  if (method == "process.kill") {
    int process_id = 0;
    if (!FindJsonNumberField(payload, "processId", &process_id) ||
        process_id < 0) {
      return FailureResponseJson(id, "process.kill requires processId.");
    }
    std::string error;
    if (!KillProcess(static_cast<uint32_t>(process_id), &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "window.snapshot") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.snapshot requires windowId.");
    }
    WindowInfo window = {};
    std::string error;
    if (!SnapshotWindowById(window_id, &window, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowJson(window));
  }
  if (method == "window.activate") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.activate requires windowId.");
    }
    WindowInfo window = {};
    std::string error;
    if (!ActivateWindowById(window_id, &window, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowJson(window));
  }
  if (method == "window.focus") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.focus requires windowId.");
    }
    WindowInfo window = {};
    std::string error;
    if (!FocusWindowById(window_id, &window, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowJson(window));
  }
  if (method == "window.show") {
    std::string window_id;
    std::string state;
    if (!FindJsonStringField(payload, "windowId", &window_id) ||
        !FindJsonStringField(payload, "state", &state)) {
      return FailureResponseJson(id, "window.show requires windowId and state.");
    }
    WindowInfo window = {};
    std::string error;
    if (!ShowWindowById(window_id, state, &window, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowJson(window));
  }
  if (method == "window.setBounds") {
    std::string window_id;
    WindowRect bounds = {};
    if (!FindJsonStringField(payload, "windowId", &window_id) ||
        !ReadRectField(payload, "bounds", &bounds)) {
      return FailureResponseJson(
          id, "window.setBounds requires windowId and bounds.");
    }
    WindowInfo window = {};
    std::string error;
    if (!SetWindowBoundsById(window_id, bounds, &window, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, WindowJson(window));
  }
  if (method == "window.close") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.close requires windowId.");
    }
    std::string error;
    if (!CloseWindowById(window_id, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "window.screenshot") {
    std::string window_id;
    if (!FindJsonStringField(payload, "windowId", &window_id)) {
      return FailureResponseJson(id, "window.screenshot requires windowId.");
    }
    WindowScreenshot screenshot = {};
    std::string error;
    if (!CaptureWindowScreenshot(window_id, &screenshot, &error)) {
      return FailureResponseJson(id, error);
    }
    const std::string transfer_id = id + "-screenshot";
    AddBinaryTransferChunks(
        transfer_id, "image/png", screenshot.image, outbound_chunks);
    return SuccessResponseJson(id, ScreenshotJson(screenshot, transfer_id));
  }
  if (method == "input.perform") {
    InputOperation operation = {};
    std::string error;
    if (!ParseInputOperation(payload, &operation, &error)) {
      return FailureResponseJson(id, error);
    }
    if (!PerformInput(operation, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "file.exists") {
    std::string path;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.exists requires path.");
    }
    const std::string output =
        std::string("{\"exists\":") + (PathExists(path) ? "true}" : "false}");
    return SuccessResponseJson(id, output);
  }
  if (method == "file.stat") {
    std::string path;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.stat requires path.");
    }
    FileStat stat = {};
    std::string error;
    if (!StatPath(path, &stat, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, FileStatJson(stat));
  }
  if (method == "file.mkdir") {
    std::string path;
    bool recursive = false;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.mkdir requires path.");
    }
    FindJsonBoolField(payload, "recursive", &recursive);
    std::string error;
    if (!MakeDirectory(path, recursive, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "file.readdir") {
    std::string path;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.readdir requires path.");
    }
    std::vector<DirectoryEntry> entries;
    std::string error;
    if (!ReadDirectoryEntries(path, &entries, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, DirectoryEntryArrayJson(entries));
  }
  if (method == "file.manifest") {
    std::string path;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.manifest requires path.");
    }
    std::vector<DirectoryManifestEntry> entries;
    std::string error;
    if (!ReadDirectoryManifest(path, &entries, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, DirectoryManifestJson(entries));
  }
  if (method == "file.remove") {
    std::string path;
    bool recursive = false;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.remove requires path.");
    }
    FindJsonBoolField(payload, "recursive", &recursive);
    std::string error;
    if (!RemovePath(path, recursive, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "file.rename") {
    std::string from;
    std::string to;
    if (!FindJsonStringField(payload, "from", &from) ||
        !FindJsonStringField(payload, "to", &to)) {
      return FailureResponseJson(id, "file.rename requires from and to.");
    }
    std::string error;
    if (!RenamePath(from, to, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "file.mkdtemp") {
    std::string prefix;
    if (!FindJsonStringField(payload, "prefix", &prefix)) {
      return FailureResponseJson(id, "file.mkdtemp requires prefix.");
    }
    std::string path;
    std::string error;
    if (!MakeTempDirectory(prefix, &path, &error)) {
      return FailureResponseJson(id, error);
    }
    std::string output = "{\"path\":";
    AppendJsonString(&output, path);
    output += "}";
    return SuccessResponseJson(id, output);
  }
  if (method == "file.read") {
    std::string path;
    if (!FindJsonStringField(payload, "path", &path)) {
      return FailureResponseJson(id, "file.read requires path.");
    }
    std::vector<unsigned char> data;
    std::string error;
    if (!ReadFileBytes(path, &data, &error)) {
      return FailureResponseJson(id, error);
    }
    const std::string transfer_id = id + "-file-read";
    AddBinaryTransferChunks(
        transfer_id, "application/octet-stream", data, outbound_chunks);
    return SuccessResponseJson(
        id,
        BinaryTransferMetadataJson(
            transfer_id, "application/octet-stream", data));
  }
  if (method == "file.write") {
    std::string path;
    std::string transfer_id;
    std::string content_type;
    std::string expected_sha256;
    int expected_total_bytes = 0;
    if (FindJsonStringField(payload, "path", &path) &&
        FindJsonStringField(payload, "transferId", &transfer_id) &&
        FindJsonStringField(payload, "contentType", &content_type) &&
        FindJsonStringField(payload, "sha256", &expected_sha256) &&
        FindJsonNumberField(payload, "totalBytes", &expected_total_bytes) &&
        expected_total_bytes >= 0) {
      if (content_type != "application/octet-stream") {
        return FailureResponseJson(id, "file.write contentType is invalid.");
      }
      std::vector<unsigned char> data;
      std::string error;
      if (!ConsumeBinaryTransfer(
              transfers, transfer_id, content_type,
              static_cast<uint32_t>(expected_total_bytes), expected_sha256,
              &data, &error)) {
        return FailureResponseJson(id, error);
      }
      if (!WriteFileBytes(path, data, &error)) {
        return FailureResponseJson(id, error);
      }
      return SuccessResponseJson(id, "null");
    }

    std::string data_base64;
    if (!FindJsonStringField(payload, "path", &path) ||
        !FindJsonStringField(payload, "dataBase64", &data_base64) ||
        !FindJsonStringField(payload, "sha256", &expected_sha256)) {
      return FailureResponseJson(
          id, "file.write requires path, dataBase64, and sha256.");
    }
    std::vector<unsigned char> data;
    std::string error;
    if (!Base64Decode(data_base64, &data, &error)) {
      return FailureResponseJson(id, error);
    }
    if (Sha256Hex(data) != expected_sha256) {
      return FailureResponseJson(id, "file.write checksum mismatch.");
    }
    if (!WriteFileBytes(path, data, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, "null");
  }
  if (method == "eventLogs.read") {
    EventLogQuery query = {};
    int max_entries = 0;
    FindJsonStringField(payload, "source", &query.source);
    FindJsonStringField(payload, "since", &query.since);
    if (FindJsonNumberField(payload, "maxEntries", &max_entries) &&
        max_entries > 0) {
      query.max_entries = static_cast<uint32_t>(max_entries);
    }
    std::vector<EventLogEntry> entries;
    std::string error;
    if (!ReadEventLogs(query, &entries, &error)) {
      return FailureResponseJson(id, error);
    }
    return SuccessResponseJson(id, EventLogArrayJson(entries));
  }
  return FailureResponseJson(id, "Unsupported method: " + method + ".");
}

}  // namespace agent_rover
