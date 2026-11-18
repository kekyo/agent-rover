// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Transactional replacement of an isolated Windows agent
// Copyright (c) Kouji Matsui. Under MIT.
import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { createHash, randomUUID } from 'node:crypto';
import { copyFile, mkdtemp, readdir, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import {
  connectAgentForUpdate,
  resolveConnectionTarget,
} from '../src/driver/connection.ts';
import { waitForResult } from '../src/wait.ts';
const exec = promisify(execFile);
const agents = resolve(import.meta.dirname, '../../agents');
const env = { ...process.env, WINEDEBUG: '-all' };
it('hands the maintenance connection to a helper and confirms the replacement listener', async () => {
  const port = 39864;
  await exec('make', ['-j4', 'amd64'], { cwd: agents });
  await exec(
    'make',
    ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
    { cwd: agents }
  );
  const dir = await mkdtemp(join(tmpdir(), 'agent update 日本語 '));
  const file = join(dir, 'test rover.exe'),
    control = join(agents, '.build/gui/control-amd64.exe');
  await copyFile(join(agents, 'dist/agent-amd64.exe'), file);
  const data = await readFile(file),
    sha256 = createHash('sha256').update(data).digest('hex');
  const options = {
    host: '127.0.0.1',
    port,
    authToken: randomUUID(),
    retry: { maxRetries: 30, intervalMs: 100 },
    timeoutMs: 45000,
  };
  const target = await resolveConnectionTarget(options);
  const home = (await exec('winepath', ['-w', dir], { env })).stdout.trim();
  const child = spawn(
    'wine',
    [
      file,
      '--host',
      options.host,
      '--port',
      String(port),
      '--unsafe-token',
      options.authToken,
    ],
    { cwd: dir, env: { ...env, APPDATA: home }, stdio: 'ignore' }
  );

  const ended = once(child, 'exit');
  const updateId = randomUUID();
  try {
    const session = await waitForResult(
      async () => {
        try {
          return await connectAgentForUpdate(target, options);
        } catch (error) {
          throw new Error('Starting test copy.', { cause: error });
        }
      },
      { timeoutMs: 30000 }
    );
    try {
      expect(session.capabilities.features).toContain('agent.update.v1');
      expect(await session.imageHash()).toBe(sha256);
      const version = session.capabilities.agentVersion!;
      const release = {
        version,
        gitCommit: session.capabilities.gitCommit!,
        architecture: 'amd64' as const,
        sha256,
        executable: data,
        updateProtocol: 1,
      };
      await expect(
        session.install(randomUUID(), sha256, {
          ...release,
          sha256: '0'.repeat(64),
        })
      ).rejects.toThrow(/checksum|sha256|digest|metadata/i);
      // Acceptance checks the server instance again. A hash error must leave
      // this same process available for the next, correctly verified transfer.
      const result = await session.install(updateId, sha256, release);
      expect(result).toMatchObject({
        status: 'applied',
        actualVersion: version,
      });
    } finally {
      session.release();
    }
    await ended;
    await waitForResult(async () => {
      expect(await readdir(dir)).not.toContain('.agent-rover-update-' + updateId);
    }, { timeoutMs: 10000 });

    const replacement = await connectAgentForUpdate(target, options);
    try {
      expect(await replacement.imageHash()).toBe(sha256);

      const path = (
        await exec('winepath', ['-w', file], { env })
      ).stdout.trim();
      const holder = spawn('wine', [control, 'hold-update-file', path], {
        env,
        stdio: ['ignore', 'pipe', 'ignore'],
      });
      const holdExit = once(holder, 'exit');
      const [line] = await once(holder.stdout, 'data');
      const pid = /held (\d+)/u.exec(String(line))![1]!;
      try {
        const version = replacement.capabilities.agentVersion!;

        const result = await replacement.install(randomUUID(), sha256, {
          version,
          gitCommit: replacement.capabilities.gitCommit!,
          architecture: 'amd64',
          sha256,
          executable: data,
          updateProtocol: 1,
        });
        expect(result, result.message).toMatchObject({
          status: 'restored',
          actualVersion: version,
        });
      } finally {
        await exec('wine', [control, 'release-file', pid], { env });
        await holdExit;
      }
    } finally {
      replacement.release();
    }
  } finally {
    try {
      await exec('wine', [control, 'exit', String(port)], {
        env,
        timeout: 15000,
      });
    } catch (error) {
      if (child.exitCode === null && child.signalCode === null) throw error;
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      await ended;
      await rm(dir, { recursive: true, force: true });
    }
  }
}, 120000);
