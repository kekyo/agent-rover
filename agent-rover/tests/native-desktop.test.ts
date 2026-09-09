// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { it } from 'vitest';
import { nativeTestPaths } from './helpers/native-paths';

it('observes mixed DPI, negative origins, configuration changes, and unavailable DPI', async () => {
  const { agentsDirectory, windowsAgentDirectory } = nativeTestPaths(
    import.meta.url
  );
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-desktop-'));
  const executable = join(directory, 'desktop-test');
  const run = async (file: string, args: string[]): Promise<void> => {
    await new Promise<void>((resolve, reject) => {
      execFile(file, args, (error, stdout, stderr) => {
        if (error === null) resolve();
        else reject(new Error(`${file} failed: ${stdout}\n${stderr}`));
      });
    });
  };
  try {
    await run('g++', [
      '-std=c++20',
      '-I',
      join(agentsDirectory, 'tests/desktop/fake'),
      '-I',
      windowsAgentDirectory,
      join(agentsDirectory, 'tests/desktop/desktop.cpp'),
      join(windowsAgentDirectory, 'win32_desktop.cpp'),
      '-o',
      executable,
    ]);
    await run(executable, []);
  } finally {
    await rm(directory, { force: true, recursive: true });
  }
});
