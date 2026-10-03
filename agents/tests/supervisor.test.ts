import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { setTimeout as delay } from 'node:timers/promises';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it.each(['amd64', 'i686'])(
  'restarts a frozen or crashed %s host with its authentication, options and pending capture ownership',
  async (architecture) => {
    await exec('make', ['-j4', architecture], {
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
    const port = 39744;
    const authToken = 'local-supervisor-regression-token';
    const directory = await mkdtemp(
      resolve(root, '.build/supervisor-transfer-')
    );
    const oversized = resolve(directory, 'oversized.bin');
    await writeFile(oversized, Buffer.alloc(1024 * 1024 + 1));
    const remoteOversized = (
      await exec('winepath', ['-w', oversized], { env: environment })
    ).stdout.trim();
    const server = spawn(
      'wine',
      [
        resolve(root, `dist/agent-${architecture}.exe`),
        '--host',
        '127.0.0.1',
        '--port',
        String(port),
        '--unsafe-token',
        authToken,
        '--max-transfer-size',
        '1',
      ],
      {
        env: environment,
        stdio: 'ignore',
      }
    );
    const connect = async () =>
      await waitForResult(
        async () => {
          let agent: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
          try {
            agent = await connectRemoteAgent({
              host: '127.0.0.1',
              port,
              authToken,
              timeoutMs: 3000,
            });
            await agent.clipboard.readText();
            return agent;
          } catch (error) {
            agent?.release();
            throw new Error('Waiting for supervised agent readiness.', {
              cause: error,
            });
          }
        },
        { timeoutMs: 60000 }
      );
    let agent: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
    let holder: ReturnType<typeof spawn> | undefined;
    let holderPid: string | undefined;
    try {
      agent = await connect();
      const initial = (await run('metrics', String(port))).stdout.match(
        /pid=(\d+)/u
      )![1];
      const remoteControl = (
        await exec('winepath', ['-w', control], { env: environment })
      ).stdout.trim();
      const captured = await agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['capture-file', 'unused'],
        captureStdout: true,
      });
      await captured.waitForExit();
      const capturePath = (await captured.stdoutText())
        .trim()
        .replace(/^\\\\\?\\/u, '');
      holder = spawn('wine', [control, 'hold-file', capturePath], {
        env: environment,
        stdio: ['ignore', 'pipe', 'pipe'],
      });
      const [held] = await once(holder.stdout!, 'data', {
        signal: AbortSignal.timeout(30000),
      });
      holderPid = String(held).match(/held (\d+)/u)![1];
      agent.release();
      agent = undefined;
      await run('suspend-server', String(port));
      agent = await connect();
      const restarted = (await run('metrics', String(port))).stdout.match(
        /pid=(\d+)/u
      )![1];
      expect(restarted).not.toBe(initial);
      await expect(agent.files.readFile(remoteOversized)).rejects.toMatchObject(
        { code: 'OPERATION_FAILED' }
      );
      expect(await agent.files.exists(capturePath)).toBe(true);
      await run('release-file', holderPid);
      holderPid = undefined;
      await waitForResult(
        async () => {
          expect(await agent!.files.exists(capturePath)).toBe(false);
        },
        { timeoutMs: 20000 }
      );
      // A deliberate interval longer than the watchdog deadline verifies idle
      // health; success also requires the same process and a real operation.
      await delay(18000);
      expect(
        (await run('metrics', String(port))).stdout.match(/pid=(\d+)/u)![1]
      ).toBe(restarted);
      expect(typeof (await agent.clipboard.readText())).toBe('string');
      agent.release();
      agent = undefined;
      agent = await connectRemoteAgent({
        host: '127.0.0.1',
        port,
        authToken,
        timeoutMs: 30000,
      });
      const title = `agent-rover-long-operation-${process.pid}-${Date.now()}`;
      const application = await agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['block', title],
      });
      const window = await agent.waitForWindow({ title, visible: true });
      const operation = window.setBounds({
        ...window.bounds,
        x: window.bounds.x + 25,
      });
      const finished = (async () => {
        try {
          await operation;
          return 'completed';
        } catch (error) {
          return String(error);
        }
      })();
      await run('entered', title);
      await delay(18000);
      expect(
        (await run('metrics', String(port))).stdout.match(/pid=(\d+)/u)![1]
      ).toBe(restarted);
      await run('release', title);
      expect(await finished).toBe('completed');
      await application.releaseAsync();
      agent.release();
      agent = undefined;
      await run('kill-server', String(port));
      agent = await connect();
      expect(
        (await run('metrics', String(port))).stdout.match(/pid=(\d+)/u)![1]
      ).not.toBe(restarted);
      agent.release();
      agent = undefined;
      const exited = once(server, 'exit', {
        signal: AbortSignal.timeout(30000),
      });
      await run('exit', String(port));
      expect((await exited)[0]).toBe(0);
    } finally {
      agent?.release();
      if (holderPid) {
        try {
          await run('release-file', holderPid);
        } catch {
          /* Already exited. */
        }
      }
      try {
        await run('kill-server', String(port));
      } catch {
        /* Already exited. */
      }
      for (const child of [holder, server]) {
        if (child && child.exitCode === null && child.signalCode === null) {
          const exited = once(child, 'exit');
          child.kill('SIGKILL');
          await exited;
        }
      }
      await rm(directory, { recursive: true, force: true });
    }
  },
  240000
);
