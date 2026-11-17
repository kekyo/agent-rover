// agent-rover - Validation of the real Windows distribution command
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import {
  access,
  copyFile,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectTestAgent } from '../../agent-rover/tests/helpers/test-connection.ts';
import { waitForResult } from '../../agent-rover/src/wait.ts';

it('packs the same native build into a ZIP and a per-user installer', async () => {
  const exec = promisify(execFile);
  const root = resolve(import.meta.dirname, '../..');
  const version = JSON.parse(
    (
      await exec('node', [
        resolve(root, 'node_modules/screw-up/dist/main.mjs'),
        'dump',
        resolve(root, 'agents'),
      ])
    ).stdout
  ).version as string;
  await exec('npm', ['run', 'pack', '--workspace', 'agents'], {
    cwd: root,
    maxBuffer: 16 * 1024 * 1024,
  });
  const base = resolve(root, `artifacts/agent-rover-agent-win32-${version}`);
  const installer = await readFile(`${base}-setup.exe`);
  expect(installer.subarray(0, 2).toString()).toBe('MZ');
  const archive = `${base}.zip`;
  expect(
    (await exec('unzip', ['-Z1', archive])).stdout.trim().split('\n').sort()
  ).toEqual([
    'LICENSE',
    'README_pack.md',
    'agent-amd64.exe',
    'agent-i686.exe',
    'images/README.md',
    'images/agent-rover-120.png',
  ]);
  for (const arch of ['amd64', 'i686']) {
    const content = await exec('unzip', ['-p', archive, `agent-${arch}.exe`], {
      encoding: 'buffer',
      maxBuffer: 8 * 1024 * 1024,
    });
    expect(content.stdout).toEqual(
      await readFile(resolve(root, `agents/dist/agent-${arch}.exe`))
    );
  }
  const directory = await mkdtemp(
    resolve(root, 'agents/.build/distribution 日本語 ')
  );
  const env = {
    ...process.env,
    WINEDEBUG: '-all',
    WINEPREFIX: resolve(directory, 'wine'),
  };
  const run = async (args: string[]) =>
    await exec('wine', args, {
      env,
      timeout: 60000,
      maxBuffer: 4 * 1024 * 1024,
    });
  let connected: Awaited<ReturnType<typeof connectTestAgent>> | undefined;
  try {
    const boot = spawn('wine', ['wineboot', '-u'], { env, stdio: 'ignore' });
    expect((await once(boot, 'exit'))[0]).toBe(0);
    const home = (
      await exec('winepath', ['-w', directory], { env })
    ).stdout.trim();
    const installed = `${home}\\installed`;
    await exec('make', ['-C', 'tests/installer'], {
      cwd: resolve(root, 'agents'),
    });
    const command = resolve(root, 'agents/.build/installer/command.exe');
    const setup = (
      await exec('winepath', ['-w', `${base}-setup.exe`], { env })
    ).stdout.trim();
    await run([command, 'run', setup, `/S /D=${installed}`]);
    expect(
      await readFile(resolve(directory, 'installed/agent-rover-agent.exe'))
    ).toEqual(await readFile(resolve(root, 'agents/dist/agent-amd64.exe')));
    await run([command, 'installed', installed, version]);
    await expect(
      run([
        'reg',
        'query',
        'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run',
        '/v',
        'AgentRover',
        '/reg:64',
      ])
    ).rejects.toThrow();
    const port = 39752;
    const token = 'distribution-test-private-token';
    await writeFile(
      resolve(directory, '.agent-rover-agent.ini'),
      `[agent]\nhost=127.0.0.1\nport=${port}\nunsafe-token=${token}\nsafe-base-dir=${home}\n`
    );
    await exec('make', ['-C', 'tests/configuration', '-j4'], {
      cwd: resolve(root, 'agents'),
      maxBuffer: 8 * 1024 * 1024,
    });
    await exec(
      'make',
      ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
      { cwd: resolve(root, 'agents') }
    );
    // The launcher supplies a test HOME without changing the user's profile.
    const launcher = spawn(
      'wine',
      [
        resolve(root, 'agents/.build/configuration/loader-amd64.exe'),
        'launch',
        home,
        `${installed}\\agent-rover-agent.exe`,
      ],
      { env, stdio: 'ignore' }
    );
    expect((await once(launcher, 'exit'))[0]).toBe(0);
    connected = await waitForResult(
      async () => {
        try {
          return await connectTestAgent({
            host: '127.0.0.1',
            port,
            authToken: token,
            timeoutMs: 1000,
            retry: false,
          });
        } catch (error) {
          throw new Error('Installed agent is starting', { cause: error });
        }
      },
      { timeoutMs: 30000 }
    );
    expect(await connected.files.safeBaseDir()).toBe(home);
    const control = resolve(root, 'agents/.build/gui/control-amd64.exe');
    const supervisor = (
      await run([control, 'parent', String(port)])
    ).stdout.trim();
    connected.release();
    connected = undefined;
    await run([
      resolve(root, 'agents/.build/gui/control-amd64.exe'),
      'exit',
      String(port),
    ]);
    await run([control, 'wait-exit', supervisor]);
    await copyFile(
      resolve(directory, 'installed/uninstall.exe'),
      resolve(directory, 'remove.exe')
    );
    await run([command, 'run', `${home}\\remove.exe`, `/S _?=${installed}`]);
    await expect(
      access(resolve(directory, 'installed/agent-rover-agent.exe'))
    ).rejects.toMatchObject({ code: 'ENOENT' });
    expect(
      await readFile(resolve(directory, '.agent-rover-agent.ini'), 'utf8')
    ).toContain(`port=${port}`);
  } finally {
    connected?.release();
    try {
      try {
        await exec('wineserver', ['-k'], { env });
      } catch (error) {
        if ((error as { code?: number }).code !== 1) throw error;
      }
      await exec('wineserver', ['-w'], { env });
    } finally {
      await rm(directory, { recursive: true, force: true });
    }
  }
}, 180000);
