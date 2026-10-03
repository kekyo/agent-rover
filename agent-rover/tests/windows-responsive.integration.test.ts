// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdir, mkdtemp, readFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent, type RemoteAgent } from '../src/index';
import { waitForResult } from '../src/wait';
import { nativeTestPaths } from './helpers/native-paths';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';

const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);
const exec = promisify(execFile);

it.skipIf(!enabled)(
  'serves authentication, capabilities and the GUI while a target window is blocked',
  async () => {
    const { agentsDirectory } = nativeTestPaths(import.meta.url);
    await mkdir(join(agentsDirectory, '.build'), { recursive: true });
    const local = await mkdtemp(join(agentsDirectory, '.build/responsive-'));
    const unique = `responsive-${process.pid}-${Date.now()}`;
    const remote = `C:\\Windows\\Temp\\agent-rover-${unique}`;
    const token = randomBytes(24).toString('base64url');
    const port = 39409;
    const bootstrap = await connectWindowsBootstrap();
    let first: RemoteAgent | undefined, second: RemoteAgent | undefined;
    let deployment: { managedProcessId: number } | undefined;
    let fixture: { managedProcessId: number } | undefined;
    let operation: Promise<unknown> | undefined;
    let capturedPath: string | undefined;
    const launch = async (
      path: string,
      args: string[],
      appData: string = remote
    ) =>
      (await bootstrap.request('process.launchManaged', {
        path,
        arguments: args,
        createNoWindow: true,
        killTreeOnRelease: true,
        environment: { APPDATA: appData, AGENT_ROVER_TEST_SECRET: token },
      })) as { managedProcessId: number };
    const control = async (mode: string, name: string) => {
      const child = await launch(`${remote}\\control.exe`, [mode, name]);
      try {
        const snapshot = await waitForResult(
          async () => {
            const value = (await bootstrap.request('process.managedSnapshot', {
              managedProcessId: child.managedProcessId,
            })) as { running: boolean; exitCode: number | null };
            if (value.running)
              throw new Error('Control probe is still running.');
            return value;
          },
          { timeoutMs: 30000 }
        );
        expect(snapshot.exitCode, `${mode} probe exit code`).toBe(0);
      } finally {
        await bootstrap.request('process.releaseManaged', {
          managedProcessId: child.managedProcessId,
        });
      }
    };
    try {
      await exec(
        'make',
        [
          'amd64',
          '-j4',
          `OBJ_ROOT=${join(local, 'obj')}`,
          `GENERATED_DIR=${join(local, 'generated')}`,
          `AMD64_OUTPUT=${join(local, 'agent.exe')}`,
        ],
        { cwd: agentsDirectory, maxBuffer: 8 * 1024 * 1024 }
      );
      await exec('make', ['-C', 'tests/gui', '-j4', 'all'], {
        cwd: agentsDirectory,
        maxBuffer: 4 * 1024 * 1024,
      });
      await bootstrap.request('file.mkdir', { path: remote, recursive: true });
      await bootstrap.upload(
        `${remote}\\agent.exe`,
        await readFile(join(local, 'agent.exe'))
      );
      await bootstrap.upload(
        `${remote}\\control.exe`,
        await readFile(join(agentsDirectory, '.build/gui/control-amd64.exe'))
      );
      deployment = await launch(`${remote}\\agent.exe`, [
        '--host',
        '0.0.0.0',
        '--port',
        String(port),
        '--unsafe-token',
        token,
      ]);
      first = await waitForResult(
        async () => {
          try {
            return await connectRemoteAgent({
              host: host!,
              port,
              authToken: token,
              timeoutMs: 5000,
            });
          } catch (error) {
            throw new Error('Responsive test agent is starting.', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
      await control('hold-logger', String(port));
      expect((await first.capabilities()).platform).toBe('windows');
      second = await connectRemoteAgent({
        host: host!,
        port,
        authToken: token,
        timeoutMs: 10000,
      });
      expect((await second.capabilities()).platform).toBe('windows');
      await control('inspect', String(port));
      await control('logger-held', String(port));
      await control('resume-logger', String(port));
      second.release();
      second = undefined;
      for (let i = 0; i < 1005; ++i) await first.capabilities();
      const captured = await first.processes.launchManaged({
        path: `${remote}\\control.exe`, arguments: ['capture-file', 'unused'],
        captureStdout: true, createNoWindow: true, killTreeOnRelease: true,
      });
      await captured.waitForExit();
      capturedPath = (await captured.stdoutText()).trim().replace(/^\\\\\?\\/u, '');
      expect(capturedPath).toMatch(/stdout/u);
      fixture = await launch(`${remote}\\control.exe`, ['block', unique]);
      const window = await first.waitForWindow({
        title: unique,
        visible: true,
      });
      const bounds = { ...window.bounds, x: window.bounds.x + 30 };
      operation = (async () => await window.setBounds(bounds))();
      // Attach an awaiting observer immediately, including failure paths.
      const observed = (async () => {
        try {
          return { value: await operation };
        } catch (error) {
          return { error };
        }
      })();
      await control('entered', unique);
      expect((await first.capabilities()).platform).toBe('windows');
      second = await connectRemoteAgent({
        host: host!,
        port,
        authToken: token,
        timeoutMs: 10000,
      });
      expect((await second.capabilities()).platform).toBe('windows');
      await control('inspect', String(port));
      // Only now release the target; the checks above cannot pass by unblocking it.
      await control('release', unique);
      expect(await observed).not.toHaveProperty('error');
      first.release(); first = undefined;
      await waitForResult(async () => {
        const result = await bootstrap.request('file.exists', { path: capturedPath! }) as { exists: boolean };
        if (result.exists)
          throw new Error('Disconnected worker still owns its capture file.');
      }, { timeoutMs: 10000 });
      await control('exit', String(port));
      await control('logs', remote);
      first?.release();
      first = undefined;
      second.release();
      second = undefined;
      await bootstrap.request('process.releaseManaged', {
        managedProcessId: deployment.managedProcessId,
      });
      deployment = undefined;
      await bootstrap.upload(
        `${remote}\\not-a-directory`,
        Buffer.from('occupied')
      );
      deployment = await launch(
        `${remote}\\agent.exe`,
        ['--host', '0.0.0.0', '--port', String(port), '--unsafe-token', token],
        `${remote}\\not-a-directory`
      );
      first = await waitForResult(
        async () => {
          try {
            return await connectRemoteAgent({
              host: host!,
              port,
              authToken: token,
              timeoutMs: 5000,
            });
          } catch {
            throw new Error('Agent with failing log destination is starting.');
          }
        },
        { timeoutMs: 30000 }
      );
      expect((await first.capabilities()).platform).toBe('windows');
      await control('log-failure', String(port));
      await control('inspect', String(port));
      await control('exit', String(port));
    } finally {
      if (fixture) {
        await control('release', unique);
        await bootstrap.request('process.releaseManaged', {
          managedProcessId: fixture.managedProcessId,
        });
      }
      first?.release();
      second?.release();
      if (operation) {
        try {
          await operation;
        } catch {}
      }
      try {
        if (deployment)
          await bootstrap.request('process.releaseManaged', {
            managedProcessId: deployment.managedProcessId,
          });
        await waitForResult(
          async () =>
            await bootstrap.request('file.remove', {
              path: remote,
              recursive: true,
            }),
          { timeoutMs: 30000 }
        );
        if (capturedPath) await bootstrap.request('file.remove', {
          path: capturedPath.replace(/\\[^\\]+$/u, ''), recursive: true, ignoreMissing: true,
        });
      } finally {
        await bootstrap.close();
        await rm(local, { recursive: true, force: true });
      }
    }
  },
  240000
);
