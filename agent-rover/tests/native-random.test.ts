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

it('generates authentication randomness with modern and legacy APIs and reports failures', async () => {
  const { agentsDirectory, windowsAgentDirectory } = nativeTestPaths(
    import.meta.url
  );
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-random-'));
  const executable = join(directory, 'random-test');
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
      '-D_WIN32',
      '-I',
      join(agentsDirectory, 'tests/random/fake'),
      '-I',
      windowsAgentDirectory,
      join(agentsDirectory, 'tests/random/random.cpp'),
      join(windowsAgentDirectory, 'auth.cpp'),
      join(windowsAgentDirectory, 'binary_codec.cpp'),
      join(windowsAgentDirectory, 'win32_random.cpp'),
      '-o',
      executable,
    ]);
    for (const mode of [
      'modern',
      'legacy',
      'missing-export',
      'legacy-failure',
      'acquire-failure',
      'cng-failure',
      'open-failure',
    ])
      await run(executable, [mode]);
  } finally {
    await rm(directory, { force: true, recursive: true });
  }
});
