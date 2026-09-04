// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "binary_transfer.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "binary_codec.h"

namespace agent_rover {

static void WriteUInt32Le(std::vector<unsigned char>* output, uint32_t value) {
  output->push_back(static_cast<unsigned char>(value & 0xff));
  output->push_back(static_cast<unsigned char>((value >> 8) & 0xff));
  output->push_back(static_cast<unsigned char>((value >> 16) & 0xff));
  output->push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

static uint32_t ReadUInt32Le(const unsigned char* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

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
        output.push_back(ch);
        break;
    }
  }
  return output;
}

static void AppendJsonString(std::string* output, const std::string& value) {
  output->push_back('"');
  *output += JsonEscape(value);
  output->push_back('"');
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

static bool FindJsonUInt64Field(
    const std::string& json,
    const std::string& key,
    uint64_t* value) {
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
  if (value_position >= json.size() || json[value_position] < '0' ||
      json[value_position] > '9') {
    return false;
  }
  errno = 0;
  const unsigned long long parsed =
      std::strtoull(json.c_str() + value_position, &end, 10);
  if (end == json.c_str() + value_position || errno == ERANGE) {
    return false;
  }
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
    end += 1;
  }
  if (*end != ',' && *end != '}') {
    return false;
  }
  *value = static_cast<uint64_t>(parsed);
  return true;
}

static bool FindJsonNumberField(
    const std::string& json,
    const std::string& key,
    uint32_t* value) {
  uint64_t parsed = 0;
  if (!FindJsonUInt64Field(json, key, &parsed) || parsed > 0xffffffffu) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

static std::string ChunkMetadataJson(const BinaryTransferChunk& chunk) {
  std::string output = "{\"transferId\":";
  AppendJsonString(&output, chunk.transfer_id);
  output += ",\"sequence\":";
  output += std::to_string(chunk.sequence);
  output += ",\"final\":";
  output += chunk.final ? "true" : "false";
  output += ",\"contentType\":";
  AppendJsonString(&output, chunk.content_type);
  if (chunk.has_total_bytes) {
    output += ",\"totalBytes\":";
    output += std::to_string(chunk.total_bytes);
  }
  if (chunk.has_sha256) {
    output += ",\"sha256\":";
    AppendJsonString(&output, chunk.sha256);
  }
  output += "}";
  return output;
}

void EncodeBinaryTransferChunkPayload(
    const BinaryTransferChunk& chunk,
    std::vector<unsigned char>* payload) {
  const std::string metadata = ChunkMetadataJson(chunk);
  payload->clear();
  payload->reserve(4 + metadata.size() + chunk.data.size());
  WriteUInt32Le(payload, static_cast<uint32_t>(metadata.size()));
  payload->insert(payload->end(), metadata.begin(), metadata.end());
  payload->insert(payload->end(), chunk.data.begin(), chunk.data.end());
}

bool DecodeBinaryTransferChunkPayload(
    const std::vector<unsigned char>& payload,
    BinaryTransferChunk* chunk,
    std::string* error) {
  if (payload.size() < 4) {
    *error = "Binary transfer frame payload is too short.";
    return false;
  }
  const uint32_t metadata_length = ReadUInt32Le(payload.data());
  const size_t metadata_start = 4;
  const size_t data_start = metadata_start + metadata_length;
  if (metadata_length == 0 || data_start > payload.size()) {
    *error = "Binary transfer metadata length is invalid.";
    return false;
  }
  const std::string metadata(
      payload.begin() + metadata_start, payload.begin() + data_start);

  *chunk = {};
  if (!FindJsonStringField(metadata, "transferId", &chunk->transfer_id) ||
      !FindJsonNumberField(metadata, "sequence", &chunk->sequence) ||
      !FindJsonBoolField(metadata, "final", &chunk->final) ||
      !FindJsonStringField(metadata, "contentType", &chunk->content_type)) {
    *error = "Binary transfer metadata is invalid.";
    return false;
  }
  chunk->has_total_bytes =
      FindJsonUInt64Field(metadata, "totalBytes", &chunk->total_bytes);
  chunk->has_sha256 = FindJsonStringField(metadata, "sha256", &chunk->sha256);
  chunk->data.assign(payload.begin() + data_start, payload.end());
  return true;
}

void CreateBinaryTransferChunks(
    const std::string& transfer_id,
    const std::string& content_type,
    const std::vector<unsigned char>& data,
    uint32_t chunk_size,
    std::vector<BinaryTransferChunk>* chunks) {
  chunks->clear();
  const uint64_t total_bytes = static_cast<uint64_t>(data.size());
  const std::string checksum = Sha256Hex(data);
  const uint32_t safe_chunk_size = std::max<uint32_t>(1, chunk_size);
  const uint32_t chunk_count =
      std::max<uint32_t>(
          1,
          static_cast<uint32_t>(
              (total_bytes + safe_chunk_size - 1) / safe_chunk_size));

  for (uint32_t sequence = 0; sequence < chunk_count; sequence += 1) {
    const uint32_t start = sequence * safe_chunk_size;
    const uint32_t end = std::min<uint32_t>(
        start + safe_chunk_size, static_cast<uint32_t>(total_bytes));
    const bool final = sequence == chunk_count - 1;
    BinaryTransferChunk chunk = {};
    chunk.transfer_id = transfer_id;
    chunk.sequence = sequence;
    chunk.final = final;
    chunk.content_type = content_type;
    chunk.data.assign(data.begin() + start, data.begin() + end);
    if (final) {
      chunk.total_bytes = total_bytes;
      chunk.has_total_bytes = true;
      chunk.sha256 = checksum;
      chunk.has_sha256 = true;
    }
    chunks->push_back(chunk);
  }
}

bool AcceptBinaryTransferChunk(
    BinaryTransferStore* store,
    const BinaryTransferChunk& chunk,
    std::string* error) {
  BinaryTransfer& transfer = store->pending[chunk.transfer_id];
  uint32_t& next_sequence = store->next_sequence[chunk.transfer_id];
  if (transfer.transfer_id.empty()) {
    if (chunk.sequence != 0) {
      *error = "First binary transfer chunk must use sequence 0.";
      return false;
    }
    transfer.transfer_id = chunk.transfer_id;
    transfer.content_type = chunk.content_type;
  } else if (transfer.content_type != chunk.content_type) {
    *error = "Binary transfer content type changed between chunks.";
    return false;
  }
  if (chunk.sequence != next_sequence) {
    *error = "Binary transfer chunk sequence is not contiguous.";
    return false;
  }

  transfer.data.insert(
      transfer.data.end(), chunk.data.begin(), chunk.data.end());
  next_sequence += 1;
  if (!chunk.final) {
    return true;
  }
  if (!chunk.has_total_bytes || !chunk.has_sha256) {
    *error = "Final binary transfer chunk is missing totalBytes or sha256.";
    return false;
  }
  if (transfer.data.size() != chunk.total_bytes) {
    *error = "Binary transfer size mismatch.";
    return false;
  }
  if (Sha256Hex(transfer.data) != chunk.sha256) {
    *error = "Binary transfer checksum mismatch.";
    return false;
  }
  transfer.sha256 = chunk.sha256;
  store->completed[chunk.transfer_id] = transfer;
  store->pending.erase(chunk.transfer_id);
  store->next_sequence.erase(chunk.transfer_id);
  return true;
}

bool ConsumeBinaryTransfer(
    BinaryTransferStore* store,
    const std::string& transfer_id,
    const std::string& expected_content_type,
    uint32_t expected_total_bytes,
    const std::string& expected_sha256,
    std::vector<unsigned char>* data,
    std::string* error) {
  const auto found = store->completed.find(transfer_id);
  if (found == store->completed.end()) {
    *error = "Binary transfer is not complete.";
    return false;
  }
  const BinaryTransfer transfer = found->second;
  if (transfer.content_type != expected_content_type) {
    *error = "Binary transfer content type mismatch.";
    return false;
  }
  if (transfer.data.size() != expected_total_bytes) {
    *error = "Binary transfer size mismatch.";
    return false;
  }
  if (transfer.sha256 != expected_sha256 ||
      Sha256Hex(transfer.data) != expected_sha256) {
    *error = "Binary transfer checksum mismatch.";
    return false;
  }
  *data = transfer.data;
  store->completed.erase(found);
  return true;
}

}  // namespace agent_rover
