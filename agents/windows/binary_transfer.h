// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_BINARY_TRANSFER_H
#define AGENT_ROVER_WINDOWS_AGENT_BINARY_TRANSFER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agent_rover {

/** Raw binary chunk carried by a TCP kind=2 frame. */
struct BinaryTransferChunk {
  /** Transfer id shared by all chunks for one payload. */
  std::string transfer_id;
  /** Zero-based contiguous chunk sequence. */
  uint32_t sequence;
  /** Raw chunk bytes. */
  std::vector<unsigned char> data;
  /** Whether this is the final chunk. */
  bool final;
  /** MIME type for the transfer. */
  std::string content_type;
  /** Total payload byte count, valid when has_total_bytes is true. */
  uint64_t total_bytes;
  /** Whether total_bytes is present. */
  bool has_total_bytes;
  /** SHA-256 hex digest, valid when has_sha256 is true. */
  std::string sha256;
  /** Whether sha256 is present. */
  bool has_sha256;
};

/** Completed file sent without loading the complete payload into memory. */
struct OutboundFileTransfer {
  /** Whether this structure contains a transfer to send. */
  bool present;
  /** Transfer id reported in binary chunk metadata. */
  std::string transfer_id;
  /** MIME type reported in binary chunk metadata. */
  std::string content_type;
  /** Source file path on the agent machine. */
  std::string path;
  /** Temporary directory removed after the transfer. */
  std::string directory;
  /** Total file size in bytes. */
  uint64_t total_bytes;
  /** SHA-256 hex digest for the complete file. */
  std::string sha256;
};

/** Completed binary transfer stored until its JSON request/response consumes it. */
struct BinaryTransfer {
  /** Transfer id. */
  std::string transfer_id;
  /** MIME type for the transfer. */
  std::string content_type;
  /** SHA-256 hex digest of data. */
  std::string sha256;
  /** Raw completed payload bytes. */
  std::vector<unsigned char> data;
};

/** In-memory store for incoming binary transfer chunks. */
struct BinaryTransferStore {
  /** Partially received transfer chunks by transfer id. */
  std::map<std::string, BinaryTransfer> pending;
  /** Next expected sequence by transfer id. */
  std::map<std::string, uint32_t> next_sequence;
  /** Completed transfer payloads by transfer id. */
  std::map<std::string, BinaryTransfer> completed;
};

/**
 * Encodes one binary transfer chunk as a TCP kind=2 frame payload.
 *
 * @param chunk Chunk to encode.
 * @param payload Receives encoded frame payload.
 */
void EncodeBinaryTransferChunkPayload(
    const BinaryTransferChunk& chunk,
    std::vector<unsigned char>* payload);

/**
 * Decodes one TCP kind=2 frame payload.
 *
 * @param payload Encoded frame payload.
 * @param chunk Receives decoded chunk.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool DecodeBinaryTransferChunkPayload(
    const std::vector<unsigned char>& payload,
    BinaryTransferChunk* chunk,
    std::string* error);

/**
 * Splits a completed payload into binary transfer chunks.
 *
 * @param transfer_id Transfer id used for every chunk.
 * @param content_type MIME type to publish in each chunk.
 * @param data Raw payload bytes.
 * @param chunk_size Maximum bytes per chunk.
 * @param chunks Receives generated chunks.
 */
void CreateBinaryTransferChunks(
    const std::string& transfer_id,
    const std::string& content_type,
    const std::vector<unsigned char>& data,
    uint32_t chunk_size,
    std::vector<BinaryTransferChunk>* chunks);

/**
 * Accepts one incoming chunk into the transfer store.
 *
 * @param store Mutable transfer store.
 * @param chunk Decoded incoming chunk.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool AcceptBinaryTransferChunk(
    BinaryTransferStore* store,
    const BinaryTransferChunk& chunk,
    std::string* error);

/**
 * Consumes one completed transfer and verifies its metadata.
 *
 * @param store Mutable transfer store.
 * @param transfer_id Transfer id to consume.
 * @param expected_content_type Required MIME type.
 * @param expected_total_bytes Required payload size.
 * @param expected_sha256 Required SHA-256 digest.
 * @param data Receives raw payload bytes.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool ConsumeBinaryTransfer(
    BinaryTransferStore* store,
    const std::string& transfer_id,
    const std::string& expected_content_type,
    uint32_t expected_total_bytes,
    const std::string& expected_sha256,
    std::vector<unsigned char>* data,
    std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_BINARY_TRANSFER_H
