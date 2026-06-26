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

describe('native command line quoting', () => {
  it('matches Windows CreateProcess command line quoting rules', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-quote-'));
    const harness = join(directory, 'command_line_test.cpp');
    const executable = join(directory, 'command_line_test');
    await writeFile(
      harness,
      String.raw`
#include "command_line.h"

#include <iostream>
#include <string>
#include <vector>

static bool ExpectEqual(const std::wstring& actual, const std::wstring& expected, const char* name) {
  if (actual == expected) {
    return true;
  }
  std::wcerr << L"mismatch: " << name << L"\nactual: " << actual << L"\nexpected: " << expected << L"\n";
  return false;
}

int main() {
  const wchar_t* start_with_token[] = {
      L"agent-rover-agent.exe",
      L"--host",
      L"127.0.0.1",
      L"--port",
      L"39391",
      L"--unsafe-token",
      L"fixed-token"};
  const agent_rover::AgentCommandLineParseResult parsed =
      agent_rover::ParseAgentCommandLine(7, start_with_token);
  if (!parsed.ok) {
    std::wcerr << L"unexpected parse failure: " << parsed.error << L"\n";
    return 1;
  }
  if (parsed.options.host != "127.0.0.1" || parsed.options.port != 39391 ||
      !parsed.options.auth_required || parsed.options.auth_token != "fixed-token") {
    std::wcerr << L"unsafe token options were not parsed correctly\n";
    return 1;
  }

  const wchar_t* no_auth_with_token[] = {
      L"agent-rover-agent.exe",
      L"--no-auth",
      L"--unsafe-token",
      L"fixed-token"};
  const agent_rover::AgentCommandLineParseResult conflict =
      agent_rover::ParseAgentCommandLine(4, no_auth_with_token);
  if (conflict.ok) {
    std::wcerr << L"--no-auth and --unsafe-token should conflict\n";
    return 1;
  }

  using agent_rover::BuildCommandLine;
  using agent_rover::QuoteCommandLineArgument;
  bool ok = true;
  ok = ExpectEqual(QuoteCommandLineArgument(L"simple"), L"simple", "simple") && ok;
  ok = ExpectEqual(QuoteCommandLineArgument(L"two words"), L"\"two words\"", "space") && ok;
  ok = ExpectEqual(QuoteCommandLineArgument(L"C:\\path\\"), L"C:\\path\\", "bare trailing slash") && ok;
  ok = ExpectEqual(QuoteCommandLineArgument(L"C:\\Program Files\\App\\"), L"\"C:\\Program Files\\App\\\\\"", "quoted trailing slash") && ok;
  ok = ExpectEqual(QuoteCommandLineArgument(L"a\"b"), L"\"a\\\"b\"", "embedded quote") && ok;
  ok = ExpectEqual(
    BuildCommandLine(L"C:\\Program Files\\App\\app.exe", {L"plain", L"two words", L"a\"b"}),
    L"\"C:\\Program Files\\App\\app.exe\" plain \"two words\" \"a\\\"b\"",
    "full command line") && ok;
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
          join(windowsAgentDirectory, 'command_line.cpp'),
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
