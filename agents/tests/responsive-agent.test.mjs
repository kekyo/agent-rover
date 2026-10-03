import { spawn, execFile } from 'node:child_process';
import { createConnection } from 'node:net';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { once } from 'node:events';
import { mkdir, mkdtemp, rm } from 'node:fs/promises';
import { expect, it } from 'vitest';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it('prepares another connection while the first ready connection remains idle', async () => {
  await mkdir(resolve(root, '.build'), { recursive: true });
  const directory = await mkdtemp(resolve(root, '.build/responsive-wine-'));
  const executable = resolve(directory, 'agent.exe');
  await exec(
    'make',
    [
      '-j4',
      'amd64',
      `AMD64_OUTPUT=${executable}`,
      `OBJ_ROOT=${resolve(directory, 'obj')}`,
      `GENERATED_DIR=${resolve(directory, 'generated')}`,
    ],
    { cwd: root, maxBuffer: 8 * 1024 * 1024 }
  );
  await exec(
    'make',
    ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
    { cwd: root }
  );
  const port = 39741;
  const child = spawn(
    'wine',
    [executable, '--host', '127.0.0.1', '--port', String(port), '--no-auth'],
    { env: { ...process.env, WINEDEBUG: '-all' }, stdio: 'ignore' }
  );
  const sockets = [];
  const connect = async () => {
    const socket = createConnection({ host: '127.0.0.1', port });
    sockets.push(socket);
    await once(socket, 'connect');
    return socket;
  };
  const ready = async (socket) => {
    const chunks = [];
    for await (const data of socket.iterator({ destroyOnReturn: false })) {
      chunks.push(data);
      const bytes = Buffer.concat(chunks);
      if (bytes.length >= 20 && bytes.length >= 20 + bytes.readUInt32LE(12)) {
        return JSON.parse(
          bytes.subarray(20, 20 + bytes.readUInt32LE(12)).toString()
        );
      }
    }
    throw new Error('Connection closed before ready.');
  };
  try {
    let first;
    // This only discovers startup; independence is asserted before closing first.
    for (let attempt = 0; attempt < 100; attempt++) {
      try {
        first = await connect();
        break;
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
    }
    expect(first).toBeDefined();
    expect((await ready(first)).name).toBe('agent.ready');
    const second = await connect();
    second.setTimeout(5000, () =>
      second.destroy(
        new Error('Second connection was blocked by the idle first connection.')
      )
    );
    expect((await ready(second)).name).toBe('agent.ready');
    expect(first.destroyed).toBe(false);
  } finally {
    for (const socket of sockets) socket.destroy();
    try {
      await exec(
        'wine',
        [resolve(root, '.build/gui/control-amd64.exe'), 'exit', String(port)],
        { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 30000 }
      );
    } catch {
      /* A startup failure may have exited before creating the GUI. */
    }
    if (child.exitCode === null && child.signalCode === null) {
      const exited = once(child, 'exit');
      child.kill('SIGKILL');
      await exited;
    }
    await rm(directory, { recursive: true, force: true });
  }
}, 180000);
