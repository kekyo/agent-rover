import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it('recovers desktop execution after TCP disconnect while capture cleanup is locked', async () => {
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
  const port = 39742;
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
  const title = `agent-rover-recovery-${process.pid}-${Date.now()}`;
  const fixture = spawn('wine', [control, 'block', title], {
    env: environment,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const ready = once(fixture.stdout, 'data', {
    signal: AbortSignal.timeout(30000),
  });
  let holder: ReturnType<typeof spawn> | undefined;
  let holderPid: string | undefined;
  let first: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
  let second: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
  let interrupted: Promise<void> | undefined;
  const connect = async () =>
    await connectRemoteAgent({ host: '127.0.0.1', port, timeoutMs: 10000 });
  try {
    await ready;
    first = await waitForResult(
      async () => {
        try {
          return await connect();
        } catch (error) {
          throw new Error('Recovery test agent is starting.', { cause: error });
        }
      },
      { timeoutMs: 30000 }
    );
    const remoteControl = (
      await exec('winepath', ['-w', control], { env: environment })
    ).stdout.trim();
    const captured = await first.processes.launchManaged({
      path: remoteControl,
      arguments: ['capture-file', 'unused'],
      captureStdout: true,
      createNoWindow: true,
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
    holderPid = String(held).match(/held (\d+)/u)?.[1];
    expect(holderPid).toBeDefined();
    const window = await first.waitForWindow({ title, visible: true });
    const operation = window.setBounds({
      ...window.bounds,
      x: window.bounds.x + 25,
    });
    interrupted = (async () => {
      await expect(operation).rejects.toMatchObject({ code: 'DISCONNECTED' });
    })();
    await run('entered', title);
    // More disconnects than the eight available execution slots must not
    // consume them permanently or replay the queued application launches.
    for (let index = 0; index < 12; ++index) {
      const queued = await connect();
      try {
        expect(await queued.files.exists(capturePath)).toBe(true);
        const launch = queued.applications.launch({
          path: remoteControl,
          arguments: ['block', `${title}-queued`],
        });
        const cancelled = (async () => {
          await expect(launch).rejects.toMatchObject({ code: 'DISCONNECTED' });
        })();
        // Capabilities bypass the desktop queue and acknowledge that the
        // preceding launch request reached the receiver before disconnect.
        await queued.capabilities();
        queued.release();
        await cancelled;
      } finally {
        queued.release();
      }
    }
    first.release();
    first = undefined;
    await interrupted;
    second = await connect();
    // This must finish while the foreign handle is still held, without
    // unblocking the target window or deleting its capture file first.
    expect(typeof (await second.clipboard.readText())).toBe('string');
    expect(
      (await second.windows()).some(
        (entry) => entry.title === `${title}-queued`
      )
    ).toBe(false);
    expect(await second.files.exists(capturePath)).toBe(true);
    await run('release-file', holderPid!);
    holderPid = undefined;
    await waitForResult(
      async () => {
        expect(await second!.files.exists(capturePath)).toBe(false);
      },
      { timeoutMs: 15000 }
    );
    await run('release', title);
  } finally {
    first?.release();
    second?.release();
    if (interrupted) await interrupted;
    if (holderPid) await run('release-file', holderPid);
    try {
      await run('release', title);
    } catch {
      /* The fixture may have failed during startup. */
    }
    try {
      await run('exit', String(port));
    } catch {
      /* Startup failures have no tray window. */
    }
    for (const child of [holder, fixture, server]) {
      if (child && child.exitCode === null && child.signalCode === null) {
        const exited = once(child, 'exit');
        child.kill('SIGKILL');
        await exited;
      }
    }
  }
}, 180000);
