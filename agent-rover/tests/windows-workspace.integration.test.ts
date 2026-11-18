// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Windows public workspace integration tests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { join, resolve, win32 } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import type { RemoteAgent } from '../src/index.ts';
import { waitForResult } from '../src/wait.ts';
import { connectTestAgent } from './helpers/test-connection.ts';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap.ts';
import {
  createWindowsForwarder,
  type WindowsForwarder,
} from './helpers/windows-forwarder.ts';

const exec = promisify(execFile);
const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);

it.skipIf(!enabled).each(['amd64', 'i686'])(
  'uses cached CLI, INI and OS workspace paths and creates caller-owned directories on Windows %s',
  async (arch) => {
    const agents = resolve(import.meta.dirname, '../../agents');
    const local = await mkdtemp(join(agents, '.build/windows-workspace-'));
    await exec(
      'make',
      [
        '-j4',
        arch,
        `OBJ_ROOT=${local}/obj`,
        `GENERATED_DIR=${local}/generated`,
        `${arch.toUpperCase()}_OUTPUT=${local}/agent.exe`,
      ],
      {
        cwd: agents,
        maxBuffer: 8 * 1024 * 1024,
      }
    );
    await exec(
      'make',
      ['-C', 'tests/gui', `OUTPUT=${local}`, `${local}/control-${arch}.exe`],
      { cwd: agents }
    );
    const bootstrap = await connectWindowsBootstrap();
    const directory = `C:\\Windows\\Temp\\rover-workspace-${process.pid}-${Date.now()} 日本語`;
    const token = randomBytes(24).toString('base64url');
    const port = 39422;
    const clients: RemoteAgent[] = [];
    const forwarders: WindowsForwarder[] = [];
    const managed: number[] = [];
    const control = async (mode: string, name: string) => {
      const child = (await bootstrap.request('process.launchManaged', {
        path: `${directory}\\control.exe`,
        arguments: [mode, name],
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      try {
        await waitForResult(
          async () => {
            expect(
              await bootstrap.request('process.managedRunning', child)
            ).toBe(false);
          },
          { timeoutMs: 30000 }
        );
        expect(
          await bootstrap.request('process.managedSnapshot', child)
        ).toMatchObject({ running: false, exitCode: 0 });
      } finally {
        await bootstrap.releaseManaged(child.managedProcessId);
      }
    };
    const connect = async () => {
      const reverseHost = process.env.AGENT_ROVER_WIN11_REVERSE_HOST;
      const forwarder = reverseHost
        ? await createWindowsForwarder(bootstrap, reverseHost, port)
        : undefined;
      if (forwarder) forwarders.push(forwarder);
      const client = await waitForResult(
        async () => {
          try {
            return await connectTestAgent({
              host: forwarder?.host ?? host!,
              port: forwarder?.port ?? port,
              authToken: token,
              timeoutMs: 10000,
            });
          } catch (error) {
            throw new Error('Workspace test agent is starting', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
      clients.push(client);
      return client;
    };
    try {
      for (const name of ['', '\\ini', '\\cli', '\\os-temp', '\\other'])
        await bootstrap.request('file.mkdir', {
          path: directory + name,
          recursive: true,
        });
      await bootstrap.upload(
        `${directory}\\agent.exe`,
        await readFile(join(local, 'agent.exe'))
      );
      const target = await readFile(join(local, `control-${arch}.exe`));
      await bootstrap.upload(`${directory}\\control.exe`, target);
      for (const mode of ['ini', 'cli', 'os-temp']) {
        const expected = `${directory}\\${mode}`;
        await bootstrap.upload(
          `${directory}\\.agent-rover-agent.ini`,
          Buffer.from(
            `[agent]\r\nhost=127.0.0.1\r\nport=${port}\r\nunsafe-token=${token}\r\nconnection-mode=multiple\r\n` +
              (mode === 'os-temp' ? '' : `safe-base-dir=${directory}\\ini\r\n`)
          )
        );
        const deployment = (await bootstrap.request('process.launchManaged', {
          path: `${directory}\\agent.exe`,
          arguments: mode === 'cli' ? ['--safe-base-dir', expected] : [],
          environment: {
            HOME: directory,
            APPDATA: directory,
            TEMP: `${directory}\\os-temp`,
            TMP: `${directory}\\os-temp`,
          },
          createNoWindow: true,
          killTreeOnRelease: true,
        })) as { managedProcessId: number };
        managed.push(deployment.managedProcessId);
        const first = await connect();
        const second = await connect();
        expect((await first.capabilities()).features).toContain(
          'file.safeBaseDir'
        );
        expect(
          win32.normalize(await first.files.safeBaseDir()).replace(/\\$/u, '')
        ).toBe(expected);
        // Editing the INI does not change the path used by fresh workers.
        await bootstrap.upload(
          `${directory}\\.agent-rover-agent.ini`,
          Buffer.from('invalid edited INI\n')
        );
        expect(
          win32.normalize(await second.files.safeBaseDir()).replace(/\\$/u, '')
        ).toBe(expected);
        const omitted = await first.files.mkdtemp();
        expect(win32.dirname(omitted)).toBe(expected);
        expect(win32.basename(omitted)).toMatch(/^agent-rover-.+/u);
        const paths = await Promise.all(
          Array.from({ length: 20 }, (_, index) =>
            (index % 2 ? first : second).files.mkdtemp('試験-')
          )
        );
        expect(new Set(paths).size).toBe(20);
        for (const path of paths) expect(win32.dirname(path)).toBe(expected);
        const other = await first.files.mkdtemp(`${directory}\\other\\case-`);
        expect(win32.dirname(other)).toBe(`${directory}\\other`);
        for (const prefix of [
          '',
          '..',
          '.',
          'nested/name',
          'nested\\name',
          'C:relative',
          '\\rooted',
        ])
          await expect(first.files.mkdtemp(prefix)).rejects.toMatchObject({
            code: 'INVALID_ARGUMENT',
          });
        for (const prefix of [null, 12])
          await expect(
            first.files.mkdtemp(prefix as unknown as string)
          ).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
        await expect(
          first.files.mkdtemp(`${directory}\\absent\\case-`)
        ).rejects.toMatchObject({ code: 'OPERATION_FAILED' });
        expect(await first.files.exists(`${directory}\\absent`)).toBe(false);
        await first.files.writeFile(`${omitted}\\target.exe`, target);
        await first.files.writeFile(
          `${omitted}\\keep.txt`,
          Buffer.from('public workspace')
        );
        const process = await first.processes.launchManaged({
          path: `${omitted}\\target.exe`,
          arguments: ['capture-file', 'unused'],
          captureStdout: true,
          createNoWindow: true,
        });
        let capture: string;
        try {
          expect((await process.waitForExit()).root.exitCode).toBe(0);
          capture = (await process.stdoutText())
            .trim()
            .replace(/^\\\\\?\\/u, '');
          expect(capture).toContain('stdout');
          expect(await second.files.exists(capture)).toBe(true);
          if (mode !== 'os-temp')
            expect(capture.startsWith(expected + '\\')).toBe(false);
        } finally {
          await process.releaseAsync();
        }
        expect(await second.files.exists(capture!)).toBe(false);
        expect(await second.files.exists(omitted)).toBe(true);

        if (mode === 'ini') {
          const unique = `workspace-block-${arch}-${Date.now()}`;
          const fixture = (await bootstrap.request('process.launchManaged', {
            path: `${directory}\\control.exe`,
            arguments: ['block', unique],
            createNoWindow: true,
            killTreeOnRelease: true,
          })) as { managedProcessId: number };
          managed.push(fixture.managedProcessId);
          const window = await first.waitForWindow({
            title: unique,
            visible: true,
          });
          const observed = (async () => {
            try {
              await window.setBounds({
                ...window.bounds,
                x: window.bounds.x + 30,
              });
              return undefined;
            } catch (error) {
              return error;
            }
          })();
          try {
            await control('entered', unique);
            // This must finish before the worker's blocked native operation is released.
            expect(await first.files.safeBaseDir()).toBe(expected);
            expect((await first.capabilities()).platform).toBe('windows');
          } finally {
            await control('release', unique);
          }
          expect(await observed).toBeUndefined();
          await bootstrap.releaseManaged(managed.pop()!);
        }
        first.release();
        expect(await second.files.readFile(`${omitted}\\keep.txt`)).toEqual(
          Buffer.from('public workspace')
        );
        for (const path of [...paths, other])
          await second.files.remove(path, { recursive: true });
        second.release();
        await control('exit', String(port));
        await waitForResult(async () =>
          expect(
            await bootstrap.request('process.managedRunning', deployment)
          ).toBe(false)
        );
        expect(await bootstrap.download(`${omitted}\\keep.txt`)).toEqual(
          Buffer.from('public workspace')
        );
        await bootstrap.releaseManaged(managed.pop()!);
        for (const forwarder of forwarders.splice(0)) await forwarder.close();
      }
    } finally {
      for (const client of clients) client.release();
      try {
        for (const forwarder of forwarders) await forwarder.close();
        for (const id of managed.reverse()) await bootstrap.releaseManaged(id);
        await waitForResult(
          async () => {
            await bootstrap.request('file.remove', {
              path: directory,
              recursive: true,
            });
          },
          { timeoutMs: 20000 }
        );
      } finally {
        await bootstrap.close();
        await rm(local, { recursive: true, force: true });
      }
    }
  },
  240000
);
