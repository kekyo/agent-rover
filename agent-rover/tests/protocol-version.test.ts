// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHmac } from 'node:crypto';
import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { nativeTestPaths } from './helpers/native-paths';
import { createAuthChallengeResponse } from '../src/auth';
import {
  authChallengePrefix,
  protocolVersion,
  tcpFrameCapabilityId,
  tcpFrameVersion,
} from '../src/protocol_version';

const { agentsDirectory, repositoryDirectory } = nativeTestPaths(
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

describe('protocol version definitions', () => {
  it('defines TypeScript protocol wire identifiers in one module', () => {
    expect(protocolVersion).toBe('2026-07-07');
    expect(tcpFrameVersion).toBe(2);
    expect(authChallengePrefix).toEqual(
      Buffer.from('agent-rover-auth-v1\0', 'ascii')
    );
    expect(tcpFrameCapabilityId).toBe('transport.tcp-frame-v1');
  });

  it('uses the exported auth challenge prefix including the terminal NUL byte', () => {
    const challenge = Buffer.from(
      Array.from({ length: 32 }, (_, index) => index)
    );
    const hmac = createHmac('sha256', Buffer.from('secret-token', 'utf8'));
    hmac.update(Buffer.concat([authChallengePrefix, challenge]));

    expect(createAuthChallengeResponse('secret-token', challenge)).toEqual(
      hmac.digest()
    );
    expect(authChallengePrefix.at(-1)).toBe(0);
  });

  it('defines matching native protocol wire identifiers in one header', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-protocol-'));
    const harness = join(directory, 'protocol_version_test.cpp');
    const executable = join(directory, 'protocol_version_test');
    await writeFile(
      harness,
      String.raw`
#include "protocol_version.h"

#include <cstdio>
#include <cstring>

int main() {
  if (std::strcmp(agent_rover::kProtocolVersion, "2026-07-07") != 0) {
    std::fprintf(stderr, "unexpected JSON protocol version\n");
    return 1;
  }
  if (agent_rover::kTcpFrameVersion != 2) {
    std::fprintf(stderr, "unexpected TCP frame protocol version\n");
    return 1;
  }
  if (std::strcmp(agent_rover::kTcpFrameCapabilityId, "transport.tcp-frame-v1") != 0) {
    std::fprintf(stderr, "unexpected TCP frame capability id\n");
    return 1;
  }
  if (sizeof(agent_rover::kAuthChallengePrefix) != sizeof("agent-rover-auth-v1")) {
    std::fprintf(stderr, "unexpected auth challenge prefix size\n");
    return 1;
  }
  if (std::memcmp(
          agent_rover::kAuthChallengePrefix,
          "agent-rover-auth-v1",
          sizeof(agent_rover::kAuthChallengePrefix)) != 0) {
    std::fprintf(stderr, "unexpected auth challenge prefix bytes\n");
    return 1;
  }
  if (agent_rover::kAuthChallengePrefix[sizeof(agent_rover::kAuthChallengePrefix) - 1] != '\0') {
    std::fprintf(stderr, "auth challenge prefix must include terminal NUL\n");
    return 1;
  }
  return 0;
}
`,
      'utf8'
    );

    try {
      await execFileChecked(
        'g++',
        ['-std=c++20', '-I', agentsDirectory, harness, '-o', executable],
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
