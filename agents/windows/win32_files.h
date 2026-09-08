// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_FILES_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_FILES_H

#include "operation_error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace agent_rover {

/** File or directory metadata. */
struct FileStat {
  /** Entry type: file, directory, or other. */
  std::string type;
  /** Size in bytes. */
  uint64_t size;
  /** Creation timestamp as ISO UTC text. */
  std::string created_at;
  /** Modification timestamp as ISO UTC text. */
  std::string modified_at;
};

/** Directory entry with metadata. */
struct DirectoryEntry {
  /** Entry name. */
  std::string name;
  /** Entry metadata. */
  FileStat stat;
};

/** Recursive directory manifest entry. */
struct DirectoryManifestEntry {
  /** Relative path using forward slashes. */
  std::string path;
  /** Entry type: file, directory, or other. */
  std::string type;
  /** Size in bytes. */
  uint64_t size;
  /** Modification timestamp as ISO UTC text. */
  std::string modified_at;
  /** SHA-256 hex digest for file entries. */
  std::string sha256;
  /** Whether sha256 is available. */
  bool has_sha256;
};

/**
 * Reads a complete file.
 *
 * @param path UTF-8 path on the agent machine.
 * @param data Receives file bytes.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool ReadFileBytes(
    const std::string& path,
    std::vector<unsigned char>* data,
    OperationError* error);

/**
 * Computes a file SHA-256 digest without retaining the file in memory.
 *
 * @param path UTF-8 path on the agent machine.
 * @param total_bytes Receives the file size in bytes.
 * @param sha256 Receives the lowercase hexadecimal digest.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool HashFileSha256(
    const std::string& path,
    uint64_t* total_bytes,
    std::string* sha256,
    OperationError* error);

/**
 * Writes a complete file, recursively creating parent directories.
 *
 * @param path UTF-8 path on the agent machine.
 * @param data Bytes to write.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool WriteFileBytes(
    const std::string& path,
    const std::vector<unsigned char>& data,
    OperationError* error);

/**
 * Returns whether a path exists.
 *
 * @param path UTF-8 path on the agent machine.
 * @return true when the path exists.
 */
bool PathExists(const std::string& path);
/**
 * Reads metadata for a path.
 *
 * @param path UTF-8 path on the agent machine.
 * @param stat Receives path metadata.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool StatPath(const std::string& path, FileStat* stat, OperationError* error);
/**
 * Creates a directory.
 *
 * @param path UTF-8 directory path on the agent machine.
 * @param recursive Whether missing parent directories should be created.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool MakeDirectory(
    const std::string& path,
    bool recursive,
    OperationError* error);
/**
 * Reads direct directory entries.
 *
 * @param path UTF-8 directory path on the agent machine.
 * @param entries Receives direct child entries.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool ReadDirectoryEntries(
    const std::string& path,
    std::vector<DirectoryEntry>* entries,
    OperationError* error);
/**
 * Reads a recursive directory manifest without following reparse points.
 *
 * @param path UTF-8 directory path on the agent machine.
 * @param entries Receives recursive entries relative to path.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool ReadDirectoryManifest(
    const std::string& path,
    std::vector<DirectoryManifestEntry>* entries,
    OperationError* error);
/**
 * Removes a file or directory.
 *
 * @param path UTF-8 path on the agent machine.
 * @param recursive Whether directory contents may be removed recursively.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool RemovePath(const std::string& path, bool recursive, OperationError* error);
/**
 * Renames or moves a file or directory.
 *
 * @param from Existing UTF-8 path on the agent machine.
 * @param to Destination UTF-8 path on the agent machine.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool RenamePath(
    const std::string& from,
    const std::string& to,
    OperationError* error);
/**
 * Creates a temporary directory from a prefix.
 *
 * @param prefix UTF-8 path prefix used to create a unique directory.
 * @param path Receives the created directory path.
 * @param error Receives a structured diagnostic on failure.
 * @return true on success.
 */
bool MakeTempDirectory(
    const std::string& prefix,
    std::string* path,
    OperationError* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_FILES_H
