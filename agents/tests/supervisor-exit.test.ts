import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent } from '../../agent-rover/src/driver/connection';
import { waitForResult } from '../../agent-rover/src/wait';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it('exits the host and its supervisor normally without watchdog termination', async () => {
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
  const port = 39746;
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
          throw new Error('Exit test agent is starting.', { cause: error });
        }
      },
      { timeoutMs: 30000 }
    );
    await agent.clipboard.readText();
    agent.release();
    agent = undefined;
    const exited = once(server, 'exit', { signal: AbortSignal.timeout(30000) });
    // The control verifies the native host exit code is zero, so a successful
    // monitor exit cannot hide a forced termination of the host.
    await exec('wine', [control, 'exit', String(port)], {
      env: environment,
      timeout: 30000,
    });
    expect((await exited)[0]).toBe(0);
  } finally {
    agent?.release();
    try {
      await exec('wine', [control, 'exit', String(port)], {
        env: environment,
        timeout: 30000,
      });
    } catch {
      /* The native host may already have exited. */
    }
    if (server.exitCode === null && server.signalCode === null) {
      const exited = once(server, 'exit');
      server.kill('SIGKILL');
      await exited;
    }
  }
}, 120000);
