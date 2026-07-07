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

describe('native agent lifecycle logging helpers', () => {
  it('formats lifecycle log lines and sanitizes field text', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-log-'));
    const harness = join(directory, 'agent_log_test.cpp');
    const executable = join(directory, 'agent_log_test');
    await writeFile(
      harness,
      String.raw`
#include "agent_log.h"

#include <cstdio>
#include <string>

static bool ExpectEqual(const std::string& actual, const std::string& expected, const char* name) {
  if (actual == expected) {
    return true;
  }
  std::fprintf(stderr, "mismatch: %s\nactual: %s\nexpected: %s\n",
      name, actual.c_str(), expected.c_str());
  return false;
}

int main() {
  bool ok = true;
  ok = ExpectEqual(
      agent_rover::SanitizeAgentLogField("line\r\nnext\tname\x7f"),
      "line??next?name?",
      "sanitize control characters") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentLogLine(
          "2026-07-07T12:34:56Z",
          agent_rover::CreateAgentConnectionAcceptedLogEvent(
              1, "192.0.2.10:50123")),
      "agent-rover agent event: 2026-07-07T12:34:56Z connection #1 accepted from 192.0.2.10:50123",
      "connection accepted line") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentConnectionDisconnectedLogEvent(
          1, "peer requested close"),
      "connection #1 disconnected: peer requested close",
      "connection disconnected event") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentApplicationLaunchedLogEvent(
          4321, "notepad.exe", "notepad.exe"),
      "application launched pid=4321 name=notepad.exe path=notepad.exe",
      "application launched event") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentManagedProcessLaunchedLogEvent(
          7, 4321, "tool.exe", "C:\\Tools\\tool.exe"),
      "managed process launched managedId=7 pid=4321 name=tool.exe path=C:\\Tools\\tool.exe",
      "managed process launched event") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentManagedProcessOperationFailedLogEvent(
          "release", 7, "TerminateJobObject failed.\nretry denied"),
      "managed process release failed managedId=7 reason=TerminateJobObject failed.?retry denied",
      "managed process failed event") && ok;
  ok = ExpectEqual(
      agent_rover::CreateAgentProcessKilledLogEvent(4321),
      "process killed pid=4321",
      "process killed event") && ok;
  return ok ? 0 : 1;
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
          join(windowsAgentDirectory, 'agent_log.cpp'),
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
