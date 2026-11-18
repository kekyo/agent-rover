// agent-rover - Transactional replacement of an isolated Windows agent
// Copyright (c) Kouji Matsui. Under MIT.
import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { createHash, randomUUID } from 'node:crypto';
import { copyFile, mkdtemp, readFile, readdir, rm } from 'node:fs/promises';
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
    authToken: 'update-fixture',
    retry: false,
    timeoutMs: 45000,
  };
  const target = await resolveConnectionTarget(options);
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
    { env, stdio: 'ignore' }
  );
  let phase = 'first connect';
  const ended = once(child, 'exit');
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
      phase = 'first install';
      const result = await session.install(randomUUID(), sha256, {
        version,
        gitCommit: session.capabilities.gitCommit!,
        architecture: 'amd64',
        sha256,
        executable: data,
        updateProtocol: 1,
      });
      expect(result).toMatchObject({
        status: 'applied',
        actualVersion: version,
      });
    } finally {
      session.release();
    }
    await ended;
    phase = 'second connect';
    const replacement = await connectAgentForUpdate(target, options);
    try {
      phase = 'second inspect';
      expect(await replacement.imageHash()).toBe(sha256);
      phase = 'hold file';
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
        phase = 'second install';
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
  } catch (error) {
    const files = await readdir(dir);
    const records = await Promise.all(
      files
        .filter((f) => f.endsWith('.update-result'))
        .map(async (f) => await readFile(join(dir, f), 'utf8'))
    );
    throw new Error(
      phase +
        ' ' +
        String(error) +
        '; ' +
        files.join(', ') +
        '; ' +
        records.join(', '),
      { cause: error }
    );
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
