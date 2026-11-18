// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import type { RemoteAgent } from '../src/index';
import { waitForResult } from '../src/wait';
import { connectTestAgent } from './helpers/test-connection';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';
import {
  createWindowsForwarder,
  type WindowsForwarder,
} from './helpers/windows-forwarder';

const exec = promisify(execFile);
const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);

it.skipIf(!enabled).each(['amd64', 'i686'])(
  'tracks native Job children after root exit, output and release on Windows %s',
  async (arch) => {
    const agents = resolve(import.meta.dirname, '../../agents');
    const local = await mkdtemp(join(agents, '.build/windows-managed-'));
    // Native regression suites also build agents/dist; isolate every test binary.
    await exec(
      'make',
      [
        '-j4',
        arch,
        `OBJ_ROOT=${local}/obj`,
        `GENERATED_DIR=${local}/generated`,
        `${arch.toUpperCase()}_OUTPUT=${local}/agent.exe`,
      ],
      { cwd: agents, maxBuffer: 8 * 1024 * 1024 }
    );
    await exec(
      'make',
      ['-C', 'tests/gui', `OUTPUT=${local}`, `${local}/control-${arch}.exe`],
      { cwd: agents }
    );
    const bootstrap = await connectWindowsBootstrap();
    const directory = `C:\\Windows\\Temp\\rover-job-${process.pid}-${Date.now()}`;
    const port = 39419;
    const token = randomBytes(24).toString('base64url');
    let deployment: { managedProcessId: number } | undefined;
    let agent: RemoteAgent | undefined;
    let forwarder: WindowsForwarder | undefined;
    try {
      await bootstrap.request('file.mkdir', {
        path: directory,
        recursive: true,
      });
      await bootstrap.upload(
        `${directory}\\agent.exe`,
        await readFile(join(local, 'agent.exe'))
      );
      const fixture = `${directory}\\control.exe`;
      await bootstrap.upload(
        fixture,
        await readFile(join(local, `control-${arch}.exe`))
      );
      deployment = (await bootstrap.request('process.launchManaged', {
        path: `${directory}\\agent.exe`,
        arguments: ['--port', String(port), '--unsafe-token', token],
        killTreeOnRelease: true,
        createNoWindow: true,
      })) as { managedProcessId: number };
      const reverseHost = process.env.AGENT_ROVER_WIN11_REVERSE_HOST;
      if (reverseHost)
        forwarder = await createWindowsForwarder(bootstrap, reverseHost, port);
      agent = await waitForResult(
        async () => {
          try {
            return await connectTestAgent({
              host: forwarder?.host ?? host!,
              port: forwarder?.port ?? port,
              authToken: token,
              timeoutMs: 5000,
            });
          } catch (error) {
            throw new Error('Managed test agent is starting.', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
      const title = `Job-${arch}-${Date.now()}`;
      const unrelated = await agent.processes.launchManaged({
        path: fixture,
        arguments: ['block', `${title}-unrelated`],
        createNoWindow: true,
      });
      const unrelatedWindow = await unrelated.waitForWindow({
        title: `${title}-unrelated`,
      });
      const tree = await agent.processes.launchManaged({
        path: fixture,
        arguments: ['block-tree', title],
        createNoWindow: true,
        captureStdout: true,
        captureStderr: true,
      });
      try {
        const parent = await tree.waitForWindow({ title });
        const child = await tree.waitForWindow({ title: `${title}-child` });
        // The shared fault-injection fixture blocks window-position messages,
        // including destruction, until its explicit release event is signaled.
        for (const name of [title, `${title}-child`]) {
          const signal = await agent.processes.launchManaged({
            path: fixture,
            arguments: ['release', name],
            createNoWindow: true,
          });
          try {
            expect((await signal.waitForExit()).root.exitCode).toBe(0);
          } finally {
            await signal.releaseAsync();
          }
        }
        await parent.close();
        await waitForResult(async () => {
          if ((await tree.rootSnapshot()).running)
            throw new Error('Launcher has not exited.');
        });
        const snapshot = await tree.snapshot();
        expect(snapshot.root).toMatchObject({ running: false, exitCode: 0 });
        expect(snapshot.running).toBe(true);
        // Windows may put a console host in the Job along with the GUI child.
        expect(snapshot.processes.map(({ id }) => id)).toContain(
          child.process.id
        );
        expect(snapshot.processes.map(({ id }) => id)).not.toContain(
          unrelated.id
        );
        const windows = (await tree.windows()).map(({ id }) => id);
        expect(windows).toContain(child.id);
        expect(windows).not.toContain(parent.id);
        expect(windows).not.toContain(unrelatedWindow.id);
        expect(await tree.windows({ title: `${title}-unrelated` })).toEqual([]);
        await child.close();
        expect((await tree.waitForExit()).running).toBe(false);
        expect(await tree.processes()).toEqual([]);
        expect(await tree.windows()).toEqual([]);
        expect(await tree.stdoutText()).toContain(`child=${child.process.id}`);
        expect(await tree.stderrText()).toBe('');
        expect((await unrelated.snapshot()).running).toBe(true);
      } finally {
        await tree.releaseAsync();
        await unrelated.releaseAsync();
      }
      const kept = await agent.processes.launchManaged({
        path: fixture,
        arguments: ['block', `${title}-kept`],
        createNoWindow: true,
        killTreeOnRelease: false,
      });
      await kept.waitForWindow({ title: `${title}-kept` });
      await kept.releaseAsync();
      expect((await agent.processes.snapshot(kept.id)).running).toBe(true);
      await agent.processes.kill(kept.id);
      await agent.processes.waitForExit(kept.id);
      console.info(
        `Windows ${arch}: Job descendants, windows, output, normal exit and opted-out release passed.`
      );
    } catch (error) {
      console.error('Managed process test failed before cleanup:', error);
      throw error;
    } finally {
      agent?.release();
      try {
        await forwarder?.close();
        if (deployment !== undefined)
          await bootstrap.releaseManaged(deployment.managedProcessId);
        // The 0.10 bootstrap may still have closing image sections after Job exit.
        await waitForResult(
          async () => {
            try {
              await bootstrap.request('file.remove', {
                path: directory,
                recursive: true,
              });
            } catch (error) {
              throw new Error('Deployment directory is still in use.', {
                cause: error,
              });
            }
          },
          { timeoutMs: 10000 }
        );
      } finally {
        await bootstrap.close();
        await rm(local, { recursive: true, force: true });
      }
    }
  },
  180000
);
