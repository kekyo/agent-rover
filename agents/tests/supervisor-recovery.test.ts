import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it('restarts the host when worker termination cannot complete after disconnect', async () => {
  const directory = resolve(root, '.build/supervisor-fixture');
  await mkdir(directory, { recursive: true });
  const object = resolve(directory, 'termination-denied.o');
  await exec(
    resolve(root, 'scripts/windows-toolchain.sh'),
    [
      'x86_64-w64-mingw32-g++-win32',
      '-std=c++20',
      '-c',
      'tests/supervisor/termination-denied.cpp',
      '-o',
      object,
    ],
    { cwd: root }
  );
  const executable = resolve(directory, 'agent.exe');
  await exec(
    'make',
    [
      '-j4',
      'amd64',
      `AMD64_OUTPUT=${executable}`,
      `LDFLAGS=-static -static-libgcc -static-libstdc++ -s -municode -mwindows ${object} -Wl,--wrap=__imp_TerminateProcess`,
    ],
    { cwd: root, maxBuffer: 8 * 1024 * 1024 }
  );
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
  const port = 39747;
  const server = spawn(
    'wine',
    [executable, '--host', '127.0.0.1', '--port', String(port), '--no-auth'],
    { env: environment, stdio: 'ignore' }
  );
  const connect = async () =>
    await waitForResult(
      async () => {
        let candidate:
          Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
        try {
          candidate = await connectRemoteAgent({
            host: '127.0.0.1',
            port,
            timeoutMs: 3000,
          });
          await candidate.clipboard.readText();
          return candidate;
        } catch (error) {
          candidate?.release();
          throw new Error('Waiting for recovery after denied termination.', {
            cause: error,
          });
        }
      },
      { timeoutMs: 60000 }
    );
  let agent: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
  let applicationPid: number | undefined;
  let interrupted: Promise<string> | undefined;
  try {
    agent = await connect();
    const initial = (await run('metrics', String(port))).stdout.match(
      /pid=(\d+)/u
    )![1];
    const remoteControl = (
      await exec('winepath', ['-w', control], { env: environment })
    ).stdout.trim();
    const title = `agent-rover-denied-stop-${process.pid}-${Date.now()}`;
    const application = await agent.processes.launchManaged({
      path: remoteControl,
      arguments: ['block', title],
    });
    applicationPid = application.id;
    const window = await agent.waitForWindow({ title, visible: true });
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
    agent.release();
    agent = undefined;
    expect(await interrupted).toBe('DISCONNECTED');
    agent = await connect();
    expect(
      (await run('metrics', String(port))).stdout.match(/pid=(\d+)/u)![1]
    ).not.toBe(initial);
    expect(await agent.processes.exists(applicationPid)).toBe(false);
    agent.release();
    agent = undefined;
    const exited = once(server, 'exit', { signal: AbortSignal.timeout(30000) });
    await run('exit', String(port));
    expect((await exited)[0]).toBe(0);
  } finally {
    agent?.release();
    if (interrupted) await interrupted;
    if (applicationPid !== undefined) {
      try {
        await run('kill-process', String(applicationPid));
      } catch {
        /* Already exited. */
      }
    }
    try {
      await run('exit', String(port));
    } catch {
      /* Startup may have failed. */
    }
    if (server.exitCode === null && server.signalCode === null) {
      const exited = once(server, 'exit');
      server.kill('SIGKILL');
      await exited;
    }
  }
}, 180000);
