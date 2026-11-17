// agent-rover - Current-user startup registration on Windows
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { waitForResult } from '../src/wait.ts';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap.ts';

const enabled = Boolean(
  process.env.AGENT_ROVER_WIN11_HOST && process.env.AGENT_ROVER_WIN11_TOKEN
);

it.skipIf(!enabled)(
  'handles current-user registration and tray state on Windows using an isolated registry key',
  async () => {
    const exec = promisify(execFile);
    const agents = resolve(import.meta.dirname, '../../agents');
    await exec('make', ['version-header'], { cwd: agents });
    await exec(
      'make',
      [
        '-C',
        'tests/gui',
        '-j4',
        '../../.build/gui/agent-amd64.res.o',
        '../../.build/gui/agent-i686.res.o',
      ],
      { cwd: agents }
    );
    await exec('make', ['-C', 'tests/autostart', '-j4'], {
      cwd: agents,
      maxBuffer: 8 * 1024 * 1024,
    });
    const bootstrap = await connectWindowsBootstrap();
    const directory = `C:\\Windows\\Temp\\rover-autostart-${process.pid}-${Date.now()} 日本語`;
    try {
      await bootstrap.request('file.mkdir', {
        path: directory,
        recursive: true,
      });
      for (const arch of ['amd64', 'i686']) {
        const path = `${directory}\\startup ${arch}.exe`;
        await bootstrap.upload(
          path,
          await readFile(
            resolve(agents, `.build/autostart/runtime-${arch}.exe`)
          )
        );
        const child = (await bootstrap.request('process.launchManaged', {
          path,
          createNoWindow: true,
          killTreeOnRelease: true,
        })) as { managedProcessId: number };
        try {
          await waitForResult(
            async () => {
              expect(
                await bootstrap.request('process.managedRunning', child)
              ).toBe(false);
            },
            { timeoutMs: 30000 }
          );
          expect(
            await bootstrap.request('process.managedSnapshot', child)
          ).toMatchObject({ running: false, exitCode: 0 });
        } finally {
          await bootstrap.releaseManaged(child.managedProcessId);
        }
      }
    } finally {
      try {
        await bootstrap.request('file.remove', {
          path: directory,
          recursive: true,
        });
      } finally {
        await bootstrap.close();
      }
    }
  },
  120000
);
