// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Exercise actual release ZIPs and an installed Windows agent in Wine
// Copyright (c) Kouji Matsui. Under MIT.
import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { randomUUID } from 'node:crypto';
import {
  copyFile,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  watch,
  writeFile,
} from 'node:fs/promises';
import { createServer } from 'node:net';
import { join, resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { waitForResult } from '../src/wait.ts';

const versions = process.env.AGENT_ROVER_UPDATE_TEST_VERSIONS?.split(',');
const root = resolve(import.meta.dirname, '../..'),
  exec = promisify(execFile);
// Two real local build archives stand in for future GitHub releases. The legacy
// downgrade at the end still uses the public release and its real API digest.
it.skipIf(versions?.length !== 2)(
  'upgrades, restores, downgrades and manually reinstalls a registered agent',
  async () => {
    const [oldVersion, newVersion] = versions!;
    if (!oldVersion || !newVersion || oldVersion === newVersion)
      throw new Error('Supply two distinct packaged versions.');
    const testRoot = join(root, '.build');
    await mkdir(testRoot, { recursive: true });
    const dir = await mkdtemp(join(testRoot, 'agent-rover 更新 ')),
      env = {
        ...process.env,
        WINEPREFIX: join(dir, 'wine'),
        WINEDEBUG: '-all',
      };
    const run = async (args: string[]) =>
      await exec('wine', args, { env, timeout: 60000 });
    const toWindows = async (path: string) =>
      (await exec('winepath', ['-w', path], { env })).stdout.trim();
    const command = join(root, 'agents/.build/installer/command.exe'),
      control = join(root, 'agents/.build/gui/control-amd64.exe');
    const port = '39865',
      token = randomUUID();
    const archive = (version: string) =>
      join(root, 'artifacts', 'agent-rover-agent-win32-' + version);
    const fixture = join(
      import.meta.dirname,
      'helpers/update-release-fixture.mjs'
    );
    const cli = async (
      args: string[],
      newArchive = archive(newVersion) + '.zip'
    ) =>
      JSON.parse(
        (
          await exec(
            process.execPath,
            [
              '--import',
              fixture,
              join(dir, 'arctl.mjs'),
              '--no-retry',
              '--timeout',
              '60',
              '--json',
              'update',
              ...args,
            ],
            {
              cwd: dir,
              env: {
                ...process.env,
                AGENT_ROVER_HOST: '127.0.0.1',
                AGENT_ROVER_PORT: port,
                AGENT_ROVER_AUTH_TOKEN: token,
                AGENT_ROVER_TEST_ARCHIVES: JSON.stringify({
                  [oldVersion]: archive(oldVersion) + '.zip',
                  [newVersion]: newArchive,
                }),
              },
              timeout: 95000,
            }
          )
        ).stdout
      ).result;
    try {
      await copyFile(
        join(root, 'agent-rover/dist/arctl.mjs'),
        join(dir, 'arctl.mjs')
      );
      await exec('make', ['-C', join(root, 'agents/tests/installer')]);
      await exec('make', [
        '-C',
        join(root, 'agents/tests/configuration'),
        '../../.build/configuration/loader-amd64.exe',
      ]);
      await exec('make', [
        '-C',
        join(root, 'agents/tests/gui'),
        '../../.build/gui/control-amd64.exe',
      ]);
      await run(['wineboot', '-u']);
      const home = await toWindows(dir),
        installed = home + '\\installed',
        image = installed + '\\agent-rover-agent.exe';
      const setup = await toWindows(archive(oldVersion) + '-setup.exe');
      await run([command, 'run', setup, '/S /D=' + installed]);
      const ini =
        '[agent]\nhost=127.0.0.1\nport=' +
        port +
        '\nunsafe-token=' +
        token +
        '\n';
      await writeFile(join(dir, '.agent-rover-agent.ini'), ini);
      await run([command, 'set-startup', '"' + image + '"', 'unused']);
      const launch = async () => {
        await run([
          join(root, 'agents/.build/configuration/loader-amd64.exe'),
          'launch',
          home,
          image,
        ]);
        await waitForResult(
          async () => {
            expect((await cli([])).updateSupported).toBe(true);
          },
          { timeoutMs: 30000 }
        );
      };
      const snapshot = async (version: string) => {
        await run([command, 'installed', installed, version]);
        await run([command, 'startup-is', '"' + image + '"', 'unused']);
        expect(
          await readFile(join(dir, '.agent-rover-agent.ini'), 'utf8')
        ).toBe(ini);
        expect(await readFile(join(dir, 'installed/keep.txt'), 'utf8')).toBe(
          'user data'
        );
      };
      await writeFile(join(dir, 'installed/keep.txt'), 'user data');
      await launch();
      const before = (await run([control, 'parent', port])).stdout.trim();
      expect((await cli([oldVersion])).status).toBe('unchanged');
      expect((await run([control, 'parent', port])).stdout.trim()).toBe(before);
      expect(await cli([newVersion])).toMatchObject({
        status: 'applied',
        actualVersion: newVersion,
      });
      await snapshot(newVersion);
      // A release can be rebuilt without changing its three-part version number.
      const rebuild = process.env.AGENT_ROVER_UPDATE_TEST_REBUILD;
      if (rebuild) {
        const first = (await run([control, 'parent', port])).stdout.trim();
        expect(await cli([newVersion], resolve(rebuild))).toMatchObject({
          status: 'applied',
          actualVersion: newVersion,
        });
        expect((await run([control, 'parent', port])).stdout.trim()).not.toBe(
          first
        );
        expect((await cli([newVersion])).status).toBe('applied');
        await snapshot(newVersion);
      }
      // Invalid install metadata is detected before shutting down the working agent.
      const key =
        'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AgentRover';
      const parentBeforeFailure = (
        await run([control, 'parent', port])
      ).stdout.trim();
      try {
        await run([
          'reg',
          'add',
          key,
          '/v',
          'DisplayVersion',
          '/t',
          'REG_DWORD',
          '/d',
          '1',
          '/f',
        ]);
        await expect(cli([oldVersion])).rejects.toThrow(/registration/i);
        expect((await run([control, 'parent', port])).stdout.trim()).toBe(
          parentBeforeFailure
        );
      } finally {
        await run([
          'reg',
          'add',
          key,
          '/v',
          'DisplayVersion',
          '/t',
          'REG_SZ',
          '/d',
          newVersion,
          '/f',
        ]);
      }
      await snapshot(newVersion);
      const expectRestored = async () => {
        try {
          await cli([oldVersion]);
          throw new Error('Update unexpectedly succeeded.');
        } catch (e) {
          if (!(e instanceof Error) || !('stdout' in e) || !e.stdout) throw e;
          expect(JSON.parse(String(e.stdout)).result).toMatchObject({
            status: 'restored',
            actualVersion: newVersion,
          });
        }
      };
      // Deny delete sharing while the helper tries to replace the installed image.
      const holder = spawn('wine', [control, 'hold-update-file', image], {
        env,
        stdio: ['ignore', 'pipe', 'ignore'],
      });
      const ended = once(holder, 'exit');
      const [line] = await once(holder.stdout, 'data');
      const held = /held (\d+)/u.exec(String(line))![1]!;
      try {
        await expectRestored();
      } finally {
        await run([control, 'release-file', held]);
        await ended;
      }
      await snapshot(newVersion);
      // Occupy the port only while the target image is in place. Filesystem
      // notifications tie the conflict and its release to the actual swap and rollback.
      const installedDir = join(dir, 'installed'),
        exeName = 'agent-rover-agent.exe';
      const currentImage = await readFile(join(installedDir, exeName));
      const occupied = createServer((socket) => socket.end()),
        stop = new AbortController();
      let sawReplacement = false;
      const holdPort = async () => {
        try {
          for await (const event of watch(installedDir, {
            signal: stop.signal,
          })) {
            if (event.filename !== exeName || event.eventType !== 'rename')
              continue;
            let bytes: Buffer;
            try {
              bytes = await readFile(join(installedDir, exeName));
            } catch (error) {
              if ((error as NodeJS.ErrnoException).code === 'ENOENT') continue;
              throw error;
            }
            if (bytes.equals(currentImage)) {
              if (sawReplacement) return;
            } else if (!sawReplacement) {
              const listening = once(occupied, 'listening');
              occupied.listen(Number(port), '127.0.0.1');
              await listening;
              sawReplacement = true;
            }
          }
        } catch (error) {
          if (!stop.signal.aborted) throw error;
        } finally {
          if (occupied.listening)
            await new Promise<void>((done) => occupied.close(() => done()));
        }
      };
      const updateWithConflict = async () => {
        try {
          await expectRestored();
        } finally {
          stop.abort();
        }
      };
      const results = await Promise.allSettled([
        holdPort(),
        updateWithConflict(),
      ]);
      for (const result of results)
        if (result.status === 'rejected') throw result.reason;
      expect(sawReplacement).toBe(true);
      await snapshot(newVersion);
      expect((await cli([oldVersion])).actualVersion).toBe(oldVersion);
      await snapshot(oldVersion);
      await expect(cli(['0.10.0'])).rejects.toThrow('--allow-legacy-downgrade');
      await snapshot(oldVersion);
      expect(await cli(['0.10.0', '--allow-legacy-downgrade'])).toMatchObject({
        status: 'applied',
        actualVersion: '0.10.0',
        manualUpdateRequired: true,
      });
      await snapshot('0.10.0');
      expect((await cli([])).updateSupported).toBe(false);
      const parent = (await run([control, 'parent', port])).stdout.trim();
      await run([control, 'exit', port]);
      await run([control, 'wait-exit', parent]);
      await run([command, 'run', setup, '/S /D=' + installed]);
      await launch();
      await snapshot(oldVersion);
    } finally {
      // This prefix and every process using it belong exclusively to this test.
      try {
        await exec('wineserver', ['-k'], { env });
        await exec('wineserver', ['-w'], { env, timeout: 30000 });
      } finally {
        await rm(dir, { recursive: true, force: true });
      }
    }
  },
  600000
);
