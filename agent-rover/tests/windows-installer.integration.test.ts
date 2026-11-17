// agent-rover - Current-user installation and startup on Windows 11
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
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

const enabled = Boolean(
  process.env.AGENT_ROVER_WIN11_HOST && process.env.AGENT_ROVER_WIN11_TOKEN
);

it.skipIf(!enabled)(
  'installs as a standard user, launches the menu shortcut, preserves startup on update and retains caller data on uninstall',
  async () => {
    const exec = promisify(execFile);
    const root = resolve(import.meta.dirname, '../..');
    await exec('npm', ['run', 'pack', '--workspace', 'agents'], {
      cwd: root,
      maxBuffer: 16 * 1024 * 1024,
    });
    await exec('make', ['-C', 'tests/installer'], {
      cwd: resolve(root, 'agents'),
    });
    await exec(
      'make',
      ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
      { cwd: resolve(root, 'agents') }
    );
    const version = JSON.parse(
      (
        await exec(process.execPath, [
          resolve(root, 'node_modules/screw-up/dist/main.mjs'),
          'dump',
          resolve(root, 'agents'),
        ])
      ).stdout
    ).version as string;
    const bootstrap = await connectWindowsBootstrap();
    const directory = `C:\\Windows\\Temp\\rover-installer-${process.pid}-${Date.now()} 日本語`;
    const token = randomBytes(24).toString('base64url');
    const command = `${directory}\\command.exe`;
    const setup = `${directory}\\setup.exe`;
    const jobs: number[] = [];
    const forwarders: WindowsForwarder[] = [];
    let client: RemoteAgent | undefined;
    let installed = '',
      menu = '';
    let namespaceOwned = false;
    const exists = async (path: string) =>
      (
        (await bootstrap.request('file.exists', { path })) as {
          exists: boolean;
        }
      ).exists;
    const run = async (path: string, args: string[]) => {
      const process = (await bootstrap.request('process.launchManaged', {
        path,
        arguments: args,
        createNoWindow: true,
        killTreeOnRelease: true,
        stderrPath: `${directory}\\command-stderr.log`,
      })) as { managedProcessId: number };
      try {
        await waitForResult(
          async () => {
            expect(
              await bootstrap.request('process.managedRunning', process)
            ).toBe(false);
          },
          { timeoutMs: 45000 }
        );
        const status = (await bootstrap.request(
          'process.managedSnapshot',
          process
        )) as { exitCode: number };
        if (status.exitCode !== 0) {
          const message = (
            await bootstrap.download(`${directory}\\command-stderr.log`)
          ).toString();
          if (message) console.error(message);
        }
        return status.exitCode;
      } finally {
        await bootstrap.releaseManaged(process.managedProcessId);
      }
    };
    const tool = async (mode: string, first: string, second: string) =>
      expect(await run(command, [mode, first, second]), mode).toBe(0);
    const connect = async (port: number) => {
      const reverse = process.env.AGENT_ROVER_WIN11_REVERSE_HOST;
      const forwarder = reverse
        ? await createWindowsForwarder(bootstrap, reverse, port)
        : undefined;
      if (forwarder) forwarders.push(forwarder);
      return await waitForResult(
        async () => {
          try {
            return await connectTestAgent({
              host: forwarder?.host ?? process.env.AGENT_ROVER_WIN11_HOST!,
              port: forwarder?.port ?? port,
              authToken: token,
              timeoutMs: 10000,
              retry: false,
            });
          } catch (error) {
            throw new Error('Installed Windows agent is starting', {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 }
      );
    };
    const saveIni = async (port: number) =>
      await bootstrap.upload(
        `${directory}\\.agent-rover-agent.ini`,
        Buffer.from(
          `[agent]\r\nhost=127.0.0.1\r\nport=${port}\r\nunsafe-token=${token}\r\nsafe-base-dir=${directory}\r\n`
        )
      );
    const stop = async (port: number, id: number) => {
      client?.release();
      client = undefined;
      expect(
        await run(`${directory}\\control.exe`, ['exit', String(port)])
      ).toBe(0);
      await waitForResult(
        async () => {
          expect(
            await bootstrap.request('process.managedRunning', {
              managedProcessId: id,
            })
          ).toBe(false);
        },
        { timeoutMs: 20000 }
      );
      await bootstrap.releaseManaged(id);
      jobs.splice(jobs.indexOf(id), 1);
    };
    try {
      await bootstrap.request('file.mkdir', {
        path: directory,
        recursive: true,
      });
      console.info(`Installer test directory: ${directory}`);
      for (const [name, source] of [
        ['setup.exe', `artifacts/agent-rover-agent-win32-${version}-setup.exe`],
        ['command.exe', 'agents/.build/installer/command.exe'],
        ['control.exe', 'agents/.build/gui/control-amd64.exe'],
      ])
        await bootstrap.upload(
          `${directory}\\${name}`,
          await readFile(resolve(root, source!))
        );
      const stateFile = `${directory}\\state.json`;
      const script = `$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64); $run=$base.OpenSubKey('Software\\Microsoft\\Windows\\CurrentVersion\\Run'); $uninstall=$base.OpenSubKey('Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AgentRover'); $programs=[Environment]::GetFolderPath([Environment+SpecialFolder]::Programs); $local=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData); $target=$local+'\\Programs\\agent-rover'; $state=[pscustomobject]@{admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator); runExists=($null -ne $run -and $null -ne $run.GetValue('AgentRover')); uninstallExists=($null -ne $uninstall); menuExists=(Test-Path -LiteralPath ($programs+'\\agent-rover')); installExists=(Test-Path -LiteralPath $target); installed=$target; menu=($programs+'\\agent-rover')}; $state | ConvertTo-Json -Compress | Out-File -LiteralPath '${stateFile.replaceAll("'", "''")}' -Encoding UTF8; if($run){$run.Dispose()}; if($uninstall){$uninstall.Dispose()}; $base.Dispose()`;
      expect(
        await run('powershell.exe', [
          '-NoProfile',
          '-NonInteractive',
          '-EncodedCommand',
          Buffer.from(script, 'utf16le').toString('base64'),
        ])
      ).toBe(0);
      const state = JSON.parse(
        (await bootstrap.download(stateFile))
          .toString('utf8')
          .replace(/^\uFEFF/u, '')
      ) as {
        admin: boolean;
        runExists: boolean;
        uninstallExists: boolean;
        menuExists: boolean;
        installExists: boolean;
        installed: string;
        menu: string;
      };
      expect(state).toMatchObject({
        admin: false,
        runExists: false,
        uninstallExists: false,
        menuExists: false,
        installExists: false,
      });
      installed = state.installed;
      menu = state.menu;
      namespaceOwned = true;
      await tool('run', setup, '/S');
      await tool('installed', installed, version);
      await tool('startup-is', '<absent>', 'unused');
      await tool(
        'link-target',
        `${menu}\\agent-rover.lnk`,
        `${installed}\\agent-rover-agent.exe`
      );
      await saveIni(39423);
      const launched = (await bootstrap.request('process.launchManaged', {
        path: command,
        arguments: ['shortcut', `${menu}\\agent-rover.lnk`, directory],
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      jobs.push(launched.managedProcessId);
      client = await connect(39423);
      expect(await client.files.safeBaseDir()).toBe(directory);
      await tool('tray', '39423', 'unused');
      await waitForResult(
        async () => {
          await tool('dialog', '39423', 'close');
        },
        { timeoutMs: 10000 }
      );
      const registration = `"${installed}\\agent-rover-agent.exe"`;
      await tool('startup-is', registration, 'unused');
      await waitForResult(
        async () => {
          expect(await run(command, ['dialog', '39423', 'inspect'])).toBe(24);
        },
        { timeoutMs: 10000 }
      );
      expect(await run(command, ['run', setup, '/S'])).not.toBe(0);
      expect(
        (
          (await bootstrap.request('file.readdir', { path: installed })) as {
            name: string;
            type: string;
          }[]
        )
          .filter((entry) => entry.type === 'directory')
          .map((entry) => entry.name)
      ).toEqual(['images']);
      await tool('installed', installed, version);
      expect(await client.files.safeBaseDir()).toBe(directory);
      await stop(39423, launched.managedProcessId);
      await tool('run', setup, '/S');
      await tool('startup-is', registration, 'unused');
      await saveIni(39424);
      // Starting the registered executable with no arguments exercises INI startup;
      // this is distinct from the separate, user-controlled real sign-in test.
      const restarted = (await bootstrap.request('process.launchManaged', {
        path: `${installed}\\agent-rover-agent.exe`,
        environment: { HOME: directory },
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      jobs.push(restarted.managedProcessId);
      client = await connect(39424);
      expect(await client.files.safeBaseDir()).toBe(directory);
      await tool('tray', '39424', 'unused');
      await waitForResult(
        async () => {
          await tool('dialog', '39424', 'close');
        },
        { timeoutMs: 10000 }
      );
      await tool('startup-is', '<absent>', 'unused');
      await stop(39424, restarted.managedProcessId);
      await bootstrap.upload(
        `${installed}\\keep.txt`,
        Buffer.from('caller data')
      );
      const portable = `${directory}\\portable agent.exe`;
      await bootstrap.upload(
        portable,
        await bootstrap.download(`${installed}\\agent-rover-agent.exe`)
      );
      await tool('set-startup', `"${portable}"`, 'unused');
      await tool('link', `${menu}\\agent-rover.lnk`, portable);
      await bootstrap.upload(
        `${directory}\\remove.exe`,
        await bootstrap.download(`${installed}\\uninstall.exe`)
      );
      await tool('run', `${directory}\\remove.exe`, `/S _?=${installed}`);
      expect(await exists(`${installed}\\agent-rover-agent.exe`)).toBe(false);
      expect(
        (await bootstrap.download(`${installed}\\keep.txt`)).toString()
      ).toBe('caller data');
      expect(
        (
          await bootstrap.download(`${directory}\\.agent-rover-agent.ini`)
        ).toString()
      ).toContain('port=39424');
      await tool('startup-is', `"${portable}"`, 'unused');
      await tool('link-target', `${menu}\\agent-rover.lnk`, portable);
      console.info(
        'Windows 11: standard-user install, menu launch, tray startup, INI restart, update, foreign registration and caller data retention passed; real sign-in is separate.'
      );
    } catch (error) {
      console.error('Installer test failed before cleanup:', error);
      throw error;
    } finally {
      client?.release();
      try {
        for (const forwarder of forwarders) await forwarder.close();
        for (const id of jobs) await bootstrap.releaseManaged(id);
        if (namespaceOwned) {
          if (await exists(`${installed}\\uninstall.exe`)) {
            await bootstrap.upload(
              `${directory}\\remove.exe`,
              await bootstrap.download(`${installed}\\uninstall.exe`)
            );
            await tool('run', `${directory}\\remove.exe`, `/S _?=${installed}`);
          }
          const portable = `${directory}\\portable agent.exe`;
          if (
            (await run(command, ['startup-is', `"${portable}"`, 'unused'])) ===
            0
          )
            await tool('clear-startup', `"${portable}"`, 'unused');
          if (
            (await run(command, [
              'link-target',
              `${menu}\\agent-rover.lnk`,
              portable,
            ])) === 0
          )
            await bootstrap.request('file.remove', {
              path: `${menu}\\agent-rover.lnk`,
              recursive: false,
            });
          if (await exists(`${installed}\\keep.txt`))
            await bootstrap.request('file.remove', {
              path: `${installed}\\keep.txt`,
              recursive: false,
            });
          if (await exists(installed))
            await bootstrap.request('file.remove', {
              path: installed,
              recursive: false,
            });
          if (await exists(menu))
            await bootstrap.request('file.remove', {
              path: menu,
              recursive: false,
            });
        }
        await bootstrap.request('file.remove', {
          path: directory,
          recursive: true,
        });
      } finally {
        await bootstrap.close();
      }
    }
  },
  240000
);
