import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it.each(['worker', 'host'])(
  'reclaims the application tree when the %s dies, preserving opted-out applications',
  async (failure) => {
    await exec('make', ['-j4', 'amd64'], {
      cwd: root,
      maxBuffer: 8 * 1024 * 1024,
    });
    await exec(
      'make',
      ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
      { cwd: root }
    );
    const environment = { ...process.env, WINEDEBUG: '-all' };
    const control = resolve(root, '.build/gui/control-amd64.exe');
    const run = async (mode: string, value: string) =>
      await exec('wine', [control, mode, value], {
        env: environment,
        timeout: 30000,
      });
    const port = 39743;
    const server = spawn(
      'wine',
      [
        resolve(root, 'dist/agent-amd64.exe'),
        '--host',
        '127.0.0.1',
        '--port',
        String(port),
        '--no-auth',
      ],
      {
        env: environment,
        stdio: 'ignore',
      }
    );
    let agent: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
    let ownedPid: number | undefined;
    let preservedPid: number | undefined;
    let childPid: number | undefined;
    let observer: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
    let interrupted: Promise<string> | undefined;
    try {
      agent = await waitForResult(
        async () => {
          try {
            return await connectRemoteAgent({
              host: '127.0.0.1',
              port,
              timeoutMs: 10000,
            });
          } catch (error) {
            throw new Error('Owned process test agent is starting.', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
      const remoteControl = (
        await exec('winepath', ['-w', control], { env: environment })
      ).stdout.trim();
      const title = `agent-rover-owned-${process.pid}-${Date.now()}`;
      const originalChildren = (await run('children', String(port))).stdout
        .trim()
        .split(/\s+/u)
        .map(Number);
      if (failure === 'worker') {
        const handles: number[] = [];
        for (let index = 0; index < 16; ++index) {
          const captured = await agent.processes.launchManaged({
            path: remoteControl,
            arguments: ['capture-file', 'unused'],
            captureStdout: true,
          });
          await captured.waitForExit();
          const path = (await captured.stdoutText())
            .trim()
            .replace(/^\\\\\?\\/u, '');
          await captured.releaseAsync();
          expect(await agent.files.exists(path)).toBe(false);
          await expect(
            agent.processes.launchManaged({ path: `${remoteControl}.missing` })
          ).rejects.toThrow();
          const metrics = (await run('metrics', String(port))).stdout;
          handles.push(Number(metrics.match(/handles=(\d+)/u)![1]));
        }
        expect(
          Math.max(...handles.slice(4)) - Math.min(...handles.slice(4))
        ).toBeLessThanOrEqual(8);
      }
      const preserved = await agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['block', `${title}-preserved`],
        killTreeOnRelease: false,
      });
      preservedPid = preserved.id;
      const owned = await agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['block-tree', title],
        captureStdout: true,
      });
      ownedPid = owned.id;
      const window = await agent.waitForWindow({ title, visible: true });
      await agent.waitForWindow({ title: `${title}-child`, visible: true });
      childPid = Number((await owned.stdoutText()).match(/child=(\d+)/u)![1]);
      const children = (await run('children', String(port))).stdout
        .trim()
        .split(/\s+/u)
        .map(Number);
      const workers = children.filter((pid) => !originalChildren.includes(pid));
      expect(workers).toHaveLength(1);
      const operation = window.setBounds({
        ...window.bounds,
        x: window.bounds.x + 25,
      });
      interrupted = (async () => {
        try {
          await operation;
          return 'completed';
        } catch (error) {
          return (error as { code: string }).code;
        }
      })();
      await run('entered', title);
      await run(
        failure === 'host' ? 'kill-server' : 'kill-process',
        String(failure === 'host' ? port : workers[0])
      );
      await run('wait-exit', String(ownedPid));
      await run('wait-exit', String(childPid));
      expect(await interrupted).toBe('DISCONNECTED');
      await run('running', String(preservedPid));
      if (failure === 'worker') {
        observer = await connectRemoteAgent({
          host: '127.0.0.1',
          port,
          timeoutMs: 10000,
        });
        expect(typeof (await observer.clipboard.readText())).toBe('string');
      }
    } finally {
      agent?.release();
      observer?.release();
      if (interrupted) await interrupted;
      for (const pid of [ownedPid, preservedPid, childPid]) {
        if (pid !== undefined) {
          try {
            await run('kill-process', String(pid));
          } catch {
            /* Already exited. */
          }
        }
      }
      try {
        await run('exit', String(port));
      } catch {
        /* The host may already have exited. */
      }
      if (server.exitCode === null && server.signalCode === null) {
        const exited = once(server, 'exit');
        server.kill('SIGKILL');
        await exited;
      }
    }
  },
  240000
);
