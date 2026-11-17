// agent-rover - Windows configuration integration tests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { join, resolve } from 'node:path';
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
  'uses Unicode HOME configuration and retains it through Windows %s host recovery',
  async (arch) => {
    const agents = resolve(import.meta.dirname, '../../agents');
    const local = await mkdtemp(join(agents, '.build/windows-configuration-'));
    await exec(
      'make',
      [
        '-j4',
        arch,
        `OBJ_ROOT=${local}/obj`,
        `GENERATED_DIR=${local}/generated`,
        `${arch.toUpperCase()}_OUTPUT=${local}/agent.exe`,
      ],
      { cwd: agents, maxBuffer: 8 * 1024 * 1024 }
    );
    await exec(
      'make',
      ['-C', 'tests/gui', `OUTPUT=${local}`, `${local}/control-${arch}.exe`],
      { cwd: agents }
    );
    const bootstrap = await connectWindowsBootstrap();
    const directory = `C:\\Windows\\Temp\\rover-config-${process.pid}-${Date.now()} 日本語`;
    const port = 39420;
    const token = randomBytes(24).toString('base64url');
    let deployment: { managedProcessId: number } | undefined;
    const clients: RemoteAgent[] = [];
    const forwarders: WindowsForwarder[] = [];
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
              timeoutMs: 3000,
            });
          } catch (error) {
            throw new Error('Configured Windows agent is starting', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
      clients.push(client);
      return client;
    };
    const run = async (mode: string) => {
      const process = (await bootstrap.request('process.launchManaged', {
        path: `${directory}\\control.exe`,
        arguments: [mode, String(port)],
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      try {
        await waitForResult(
          async () => {
            const state = await bootstrap.request('process.managedRunning', {
              managedProcessId: process.managedProcessId,
            });
            if (state !== false)
              throw new Error('Configuration control is running');
          },
          { timeoutMs: 30000 }
        );
        expect(
          await bootstrap.request('process.managedSnapshot', {
            managedProcessId: process.managedProcessId,
          })
        ).toMatchObject({ running: false, exitCode: 0 });
      } finally {
        await bootstrap.releaseManaged(process.managedProcessId);
      }
    };
    try {
      await bootstrap.request('file.mkdir', {
        path: directory,
        recursive: true,
      });
      await bootstrap.upload(
        `${directory}\\agent.exe`,
        await readFile(join(local, 'agent.exe'))
      );
      await bootstrap.upload(
        `${directory}\\control.exe`,
        await readFile(join(local, `control-${arch}.exe`))
      );
      const ini = `${directory}\\.agent-rover-agent.ini`;
      await bootstrap.upload(
        ini,
        Buffer.concat([
          Buffer.from([255, 254]),
          Buffer.from(
            `[agent]\r\nhost=127.0.0.1\r\nport=${port}\r\nunsafe-token=${token}\r\nconnection-mode=multiple\r\nmax-transfer-size=1\r\nsafe-base-dir=${directory}\r\n`,
            'utf16le'
          ),
        ])
      );
      await bootstrap.upload(
        `${directory}\\oversized.bin`,
        Buffer.alloc(1024 * 1024 + 1)
      );
      deployment = (await bootstrap.request('process.launchManaged', {
        path: `${directory}\\agent.exe`,
        environment: { HOME: directory },
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      const first = await connect();
      const second = await connect();
      expect(typeof (await second.clipboard.readText())).toBe('string');
      await expect(
        first.files.readFile(`${directory}\\oversized.bin`)
      ).rejects.toMatchObject({ code: 'OPERATION_FAILED' });
      await run('private-token');
      await bootstrap.upload(
        ini,
        Buffer.from('invalid changed configuration\n')
      );
      first.release();
      second.release();
      await run('kill-server');
      const recovered = await connect();
      const concurrent = await connect();
      expect(typeof (await concurrent.clipboard.readText())).toBe('string');
      await expect(
        recovered.files.readFile(`${directory}\\oversized.bin`)
      ).rejects.toMatchObject({ code: 'OPERATION_FAILED' });
      await run('private-token');
      console.info(
        `Windows ${arch}: INI startup, authentication, concurrent sessions, transfer limit and retained settings passed.`
      );
    } finally {
      for (const client of clients) client.release();
      try {
        for (const forwarder of forwarders) await forwarder.close();
        if (deployment)
          await bootstrap.releaseManaged(deployment.managedProcessId);
        await waitForResult(
          async () => {
            try {
              await bootstrap.request('file.remove', {
                path: directory,
                recursive: true,
              });
            } catch (error) {
              throw new Error('Configuration deployment is still closing', {
                cause: error,
              });
            }
          },
          { timeoutMs: 20000 }
        );
      } finally {
        await bootstrap.close();
        await rm(local, { recursive: true, force: true });
      }
    }
  },
  180000
);
