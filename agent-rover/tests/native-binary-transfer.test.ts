// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { nativeTestPaths } from './helpers/native-paths';

const { repositoryDirectory, windowsAgentDirectory } = nativeTestPaths(
  import.meta.url
);

const execFileChecked = async (
  file: string,
  args: readonly string[],
  cwd: string
): Promise<void> => {
  await new Promise<void>((resolve, reject) => {
    execFile(file, args, { cwd }, (error, stdout, stderr) => {
      if (error === null) {
        resolve();
      } else {
        reject(
          new Error(`${file} failed\nstdout:\n${stdout}\nstderr:\n${stderr}`)
        );
      }
    });
  });
};

describe('native binary transfer codec', () => {
  it('round-trips raw TCP binary chunks and verifies the completed payload', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-transfer-'));
    const harness = join(directory, 'binary_transfer_test.cpp');
    const executable = join(directory, 'binary_transfer_test');
    await writeFile(
      harness,
      String.raw`
#include "binary_transfer.h"

#include <cstdio>
#include <vector>

int main() {
  const std::vector<unsigned char> data = {0, 1, 2, 3, 250, 251, 252};
  std::vector<agent_rover::BinaryTransferChunk> chunks;
  agent_rover::CreateBinaryTransferChunks(
      "transfer-1", "application/octet-stream", data, 3, &chunks);
  if (chunks.size() != 3) {
    std::fprintf(stderr, "unexpected chunk count\n");
    return 1;
  }

  agent_rover::BinaryTransferStore store = {};
  for (const agent_rover::BinaryTransferChunk& chunk : chunks) {
    std::vector<unsigned char> payload;
    agent_rover::EncodeBinaryTransferChunkPayload(chunk, &payload);
    if (payload.back() != chunk.data.back()) {
      std::fprintf(stderr, "payload does not end with raw chunk data\n");
      return 1;
    }

    agent_rover::BinaryTransferChunk decoded = {};
    std::string error;
    if (!agent_rover::DecodeBinaryTransferChunkPayload(payload, &decoded, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    if (!agent_rover::AcceptBinaryTransferChunk(&store, decoded, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
  }

  std::vector<unsigned char> completed;
  std::string error;
  if (agent_rover::ConsumeBinaryTransfer(&store, "transfer-1", "application/octet-stream",
      (1ull << 32) + data.size(), chunks.back().sha256, &completed, &error)) {
    std::fputs("Transfer size metadata was truncated to 32 bits.\n", stderr);
    return 1;
  }
  if (!agent_rover::ConsumeBinaryTransfer(
          &store,
          "transfer-1",
          "application/octet-stream",
          static_cast<uint32_t>(data.size()),
          chunks.back().sha256,
          &completed,
          &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (completed != data) return 1;
  agent_rover::BinaryTransferStore bounded = {};
  agent_rover::BinaryTransferChunk part = {};
  part.content_type = "application/octet-stream";
  part.data = {1};
  for (unsigned int i = 0; i < 16; ++i) {
    part.transfer_id = "pending-" + std::to_string(i);
    if (!agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 2;
  }
  part.transfer_id = "one-too-many";
  if (agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error) || bounded.pending.size() != 16) {
    std::fputs("Pending transfers grew beyond the session limit.\n", stderr);
    return 3;
  }
  bounded = {};
  part.transfer_id = "byte-limit";
  part.data.assign(4 * 1024 * 1024, 0);
  for (unsigned int i = 0; i < 16; ++i) {
    part.sequence = i;
    if (!agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 4;
  }
  part.sequence = 16;
  if (agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error) ||
      bounded.pending.begin()->second.data.size() != 64 * 1024 * 1024) {
    std::fputs("Binary bytes grew beyond the session limit.\n", stderr);
    return 5;
  }
  bounded.max_transfer_bytes = 65ull * 1024 * 1024;
  part.data.assign(1024 * 1024, 0);
  if (!agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 6;
  part.sequence += 1;
  part.data = {0};
  if (agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 7;

  // Pending and completed payloads share the configured budget. Consuming a
  // completed payload frees that budget for subsequent chunks.
  bounded = {};
  bounded.max_transfer_bytes = 4;
  const std::vector<unsigned char> small = {1, 2, 3};
  agent_rover::CreateBinaryTransferChunks("complete", "application/octet-stream", small, 3, &chunks);
  if (!agent_rover::AcceptBinaryTransferChunk(&bounded, chunks.front(), &error)) return 8;
  part = {};
  part.transfer_id = "pending";
  part.content_type = "application/octet-stream";
  part.data = {4};
  if (!agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 9;
  part.sequence = 1;
  if (agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error) ||
      bounded.pending.at("pending").data.size() != 1) return 10;
  if (!agent_rover::ConsumeBinaryTransfer(&bounded, "complete", "application/octet-stream",
      small.size(), chunks.front().sha256, &completed, &error) || completed != small) return 11;
  part.data = {5, 6, 7};
  if (!agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error)) return 12;
  part.sequence = 2;
  part.data = {8};
  if (agent_rover::AcceptBinaryTransferChunk(&bounded, part, &error) ||
      bounded.pending.at("pending").data.size() != 4) return 13;
  return 0;
}
`,
      'utf8'
    );

    try {
      await execFileChecked(
        'g++',
        [
          '-std=c++20',
          '-I',
          windowsAgentDirectory,
          join(windowsAgentDirectory, 'binary_codec.cpp'),
          join(windowsAgentDirectory, 'binary_transfer.cpp'),
          harness,
          '-o',
          executable,
        ],
        repositoryDirectory
      );
      await execFileChecked(executable, [], repositoryDirectory);
      expect(true).toBe(true);
    } finally {
      await rm(directory, {
        force: true,
        recursive: true,
      });
    }
  });
});
