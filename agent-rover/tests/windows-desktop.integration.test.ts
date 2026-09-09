// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import {
  connectRemoteAgent,
  type RemoteAgent,
  type AppWindowSnapshot,
} from '../src/index';
import { waitForResult } from '../src/wait';
import { nativeTestPaths } from './helpers/native-paths';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';

const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);

it.skipIf(!enabled)(
  'observes physical placement and DPI for unaware, system and per-monitor windows on Windows',
  async () => {
    const { agentsDirectory, repositoryDirectory } = nativeTestPaths(
      import.meta.url
    );
    const local = await mkdtemp(join(tmpdir(), 'agent-rover-desktop-win-'));
    const fixture = join(local, 'window.exe');
    const testAgentExecutable = join(local, 'agent.exe');
    const run = async (file: string, args: string[]): Promise<void> => {
      await new Promise<void>((resolve, reject) => {
        execFile(
          file,
          args,
          { cwd: agentsDirectory },
          (error, stdout, stderr) => {
            if (error === null) resolve();
            else reject(new Error(`${file}: ${stdout}\n${stderr}`));
          }
        );
      });
    };
    const unique = `desktop-${process.pid}-${Date.now()}`;
    const directory = `C:\\Windows\\Temp\\agent-rover-${unique}`;
    const token = randomBytes(24).toString('base64url');
    const port = 39408;
    const bootstrap = await connectWindowsBootstrap();
    let agent: RemoteAgent | undefined;
    let deployment: { managedProcessId: number } | undefined;
    try {
      // Concurrent integration suites must not overwrite each other's objects.
      await run('make', [
        'amd64',
        '-j4',
        `OBJ_ROOT=${join(local, 'obj')}`,
        `GENERATED_DIR=${join(local, 'generated')}`,
        `AMD64_OUTPUT=${testAgentExecutable}`,
      ]);
      await run('x86_64-w64-mingw32-g++', [
        '-std=c++20',
        '-D_WIN32_WINNT=0x0A00',
        '-municode',
        '-static',
        'tests/desktop/window.cpp',
        '-luser32',
        '-o',
        fixture,
      ]);
      await bootstrap.request('file.mkdir', {
        path: directory,
        recursive: true,
      });
      const agentPath = `${directory}\\agent.exe`;
      await bootstrap.upload(agentPath, await readFile(testAgentExecutable));
      await bootstrap.upload(
        `${directory}\\window.exe`,
        await readFile(fixture)
      );
      deployment = (await bootstrap.request('process.launchManaged', {
        path: agentPath,
        arguments: [
          '--host',
          '0.0.0.0',
          '--port',
          String(port),
          '--unsafe-token',
          token,
        ],
        killTreeOnRelease: true,
        createNoWindow: true,
      })) as { managedProcessId: number };
      agent = await waitForResult(
        async () => {
          try {
            return await connectRemoteAgent({
              host: host!,
              port,
              authToken: token,
              timeoutMs: 5000,
            });
          } catch (error) {
            throw new Error('Desktop test agent is not ready.', {
              cause: error,
            });
          }
        },
        { timeoutMs: 15000 }
      );
      const desktop = await agent.desktop();
      expect(desktop.monitors.length).toBeGreaterThan(0);
      expect(
        desktop.monitors.filter((monitor) => monitor.primary)
      ).toHaveLength(1);
      expect(await agent.bounds()).toEqual(desktop.bounds);
      expect(await agent.monitors()).toEqual(desktop.monitors);
      console.log(
        'Windows desktop coverage:',
        JSON.stringify(
          desktop.monitors.map(({ bounds, dpi, primary }) => ({
            bounds,
            dpi,
            primary,
          }))
        )
      );
      for (const monitor of desktop.monitors) {
        expect(monitor.dpi).toBeGreaterThan(0);
        expect(monitor.scaleFactor).toBe(monitor.dpi! / 96);
      }
      const placements: AppWindowSnapshot[] = [];
      for (const mode of ['unaware', 'system', 'per-monitor-v2'] as const) {
        const process = await agent.processes.launchManaged({
          path: `${directory}\\window.exe`,
          arguments: [mode, `${unique}-${mode}`],
          captureStdout: true,
          killTreeOnRelease: true,
        });
        try {
          let window = await agent.waitForWindow({
            title: `${unique}-${mode}`,
            visible: true,
          });
          const systemDpi = await waitForResult(async () => {
            const output = await process.stdoutText();
            return (JSON.parse(output) as { systemDpi: number }).systemDpi;
          });
          for (const monitor of desktop.monitors) {
            // Multiples of three are exactly representable at both 96 and
            // 144 DPI, including Windows bitmap scaling of unaware windows.
            const bounds = {
              x: monitor.workArea.x + 30,
              y: monitor.workArea.y + 30,
              width: 402,
              height: 300,
            };
            window = await window.setBounds(bounds);
            const expectedDpi =
              mode === 'unaware'
                ? 96
                : mode === 'system'
                  ? systemDpi
                  : monitor.dpi!;
            window = await window.waitForPlacement({
              bounds,
              monitorId: monitor.id,
              dpi: expectedDpi,
              desktopRevision: desktop.revision,
            });
            expect(window.bounds).toEqual(bounds);
            expect(window.monitorId).toBe(monitor.id);
            expect(window.dpiAwareness).toBe(mode);
            expect(window.dpi).toBe(
              mode === 'unaware'
                ? 96
                : mode === 'system'
                  ? systemDpi
                  : monitor.dpi
            );
            expect(window.clientBounds.x).toBeGreaterThanOrEqual(bounds.x);
            expect(window.clientBounds.y).toBeGreaterThanOrEqual(bounds.y);
            const child = (await window.children())[0]!;
            const childBounds = {
              x: window.clientBounds.x + 12,
              y: window.clientBounds.y + 18,
              width: 102,
              height: 42,
            };
            const movedChild = await child.setBounds(childBounds);
            expect(movedChild.bounds).toEqual(childBounds);
            expect(movedChild.dpi).toBe(window.dpi);
            const capture = await window.screenshot();
            expect(capture.bounds).toEqual(window.frameBounds);
            expect(capture.clipped).toBe(false);
            placements.push({ ...window });
          }
          expect((await agent.desktop()).revision).toBe(desktop.revision);
        } finally {
          await process.releaseAsync();
        }
      }
      const reports = join(repositoryDirectory, 'test-results');
      await mkdir(reports, { recursive: true });
      await writeFile(
        join(reports, 'windows-desktop.json'),
        JSON.stringify({ desktop, placements }, null, 2)
      );
    } finally {
      agent?.release();
      try {
        if (deployment !== undefined)
          await bootstrap.request('process.releaseManaged', {
            managedProcessId: deployment.managedProcessId,
          });
        await waitForResult(
          async () => {
            await bootstrap.request('file.remove', {
              path: directory,
              recursive: true,
              ignoreMissing: true,
            });
          },
          { timeoutMs: 15000 }
        );
      } finally {
        await bootstrap.close();
        await rm(local, { force: true, recursive: true });
      }
    }
  },
  120000
);
