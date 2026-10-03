import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it('keeps control operations available at capture recovery capacity and recovers every held directory', async () => {
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
  const port = 39745;
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
  const holder = spawn('wine', [control, 'hold-files', '64'], {
    env: environment,
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  const lines = createInterface({ input: holder.stdout! })[
    Symbol.asyncIterator
  ]();
  const connect = async () =>
    await connectRemoteAgent({ host: '127.0.0.1', port, timeoutMs: 10000 });
  let agent: Awaited<ReturnType<typeof connectRemoteAgent>> | undefined;
  let holderPid: string | undefined;
  try {
    holderPid = (await lines.next()).value!.match(/holder=(\d+)/u)![1];
    agent = await waitForResult(
      async () => {
        try {
          return await connect();
        } catch (error) {
          throw new Error('Capacity test agent is starting.', { cause: error });
        }
      },
      { timeoutMs: 30000 }
    );
    const remoteControl = (
      await exec('winepath', ['-w', control], { env: environment })
    ).stdout.trim();
    const paths: string[] = [];
    for (let index = 0; index < 64; ++index) {
      const process = await agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['capture-file', 'unused'],
        captureStdout: true,
      });
      await process.waitForExit();
      const path = (await process.stdoutText())
        .trim()
        .replace(/^\\\\\?\\/u, '');
      paths.push(path);
      await new Promise<void>((resolve, reject) => {
        holder.stdin!.write(`${path}\n`, (error) => {
          if (error) reject(error);
          else resolve();
        });
      });
      expect((await lines.next()).value).toBe(`held=${index + 1}`);
      agent.release();
      agent = await connect();
    }
    holder.stdin!.end();
    await expect(
      agent.processes.launchManaged({
        path: remoteControl,
        arguments: ['capture-file', 'unused'],
        captureStdout: true,
      })
    ).rejects.toMatchObject({ code: 'DISCONNECTED' });
    agent.release();
    agent = await connect();
    expect(typeof (await agent.clipboard.readText())).toBe('string');
    expect(await agent.processes.list()).not.toHaveLength(0);
    await run('release-file', holderPid);
    holderPid = undefined;
    await waitForResult(
      async () => {
        for (const path of paths)
          expect(await agent!.files.exists(path)).toBe(false);
      },
      { timeoutMs: 120000 }
    );
    const replacement = await agent.processes.launchManaged({
      path: remoteControl,
      arguments: ['capture-file', 'unused'],
      captureStdout: true,
    });
    await replacement.waitForExit();
    await replacement.releaseAsync();
    agent.release();
    agent = undefined;
    const exited = once(server, 'exit', { signal: AbortSignal.timeout(30000) });
    await run('exit', String(port));
    expect((await exited)[0]).toBe(0);
  } finally {
    agent?.release();
    holder.stdin!.end();
    if (holderPid) {
      try {
        await run('release-file', holderPid);
      } catch {
        /* Already exited. */
      }
    }
    try {
      await run('exit', String(port));
    } catch {
      /* Startup may have failed. */
    }
    for (const child of [holder, server]) {
      if (child.exitCode === null && child.signalCode === null) {
        const exited = once(child, 'exit');
        child.kill('SIGKILL');
        await exited;
      }
    }
  }
}, 360000);
