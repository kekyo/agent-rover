// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Windows configuration integration tests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
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
  'uses, retains and edits Unicode HOME configuration on Windows %s',
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
    let restarted: { managedProcessId: number } | undefined;
    let editorOpened = false;
    let editorClosed = false;
    const clients: RemoteAgent[] = [];
    const forwarders: WindowsForwarder[] = [];
    const connect = async (nativePort: number) => {
      const reverseHost = process.env.AGENT_ROVER_WIN11_REVERSE_HOST;
      const forwarder = reverseHost
        ? await createWindowsForwarder(bootstrap, reverseHost, nativePort)
        : undefined;
      if (forwarder) forwarders.push(forwarder);
      const client = await waitForResult(
        async () => {
          try {
            return await connectTestAgent({
              host: forwarder?.host ?? host!,
              port: forwarder?.port ?? nativePort,
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
    const notepad = async (
      windowId: string,
      action: 'inspect' | 'select-all' | 'close',
      tabId: string
    ) => {
      const stdout = `${directory}\\notepad.json`;
      const stderr = `${directory}\\notepad-error.txt`;
      const process = (await bootstrap.request('process.launchManaged', {
        path: 'powershell.exe',
        arguments: [
          '-NoProfile',
          '-NonInteractive',
          '-ExecutionPolicy',
          'Bypass',
          '-File',
          `${directory}\\notepad.ps1`,
          '-WindowId',
          windowId,
          '-Action',
          action,
          '-TabId',
          tabId,
        ],
        stdoutPath: stdout,
        stderrPath: stderr,
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      try {
        await waitForResult(
          async () => {
            expect(
              await bootstrap.request('process.managedRunning', process)
            ).toBe(false);
          },
          { timeoutMs: 30000 }
        );
        const snapshot = await bootstrap.request(
          'process.managedSnapshot',
          process
        );
        if (
          !snapshot ||
          typeof snapshot !== 'object' ||
          !('exitCode' in snapshot) ||
          snapshot.exitCode !== 0
        )
          throw new Error((await bootstrap.download(stderr)).toString('utf8'));
        return JSON.parse(
          (await bootstrap.download(stdout)).toString('utf8')
        ) as {
          tabs: { id: string; name: string; selected: boolean }[];
          selectedId: string;
          text: string | null;
          dialog: boolean;
        };
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
      await bootstrap.upload(
        `${directory}\\notepad.ps1`,
        await readFile(join(agents, 'tests/configuration/notepad.ps1'))
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
      const first = await connect(port);
      const second = await connect(port);
      expect((await second.capabilities()).platform).toBe('windows');
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
      const recovered = await connect(port);
      const concurrent = await connect(port);
      expect((await concurrent.capabilities()).platform).toBe('windows');
      await expect(
        recovered.files.readFile(`${directory}\\oversized.bin`)
      ).rejects.toMatchObject({ code: 'OPERATION_FAILED' });
      await run('private-token');
      const original = Buffer.from(
        `[agent]\r\nport=${port}\r\nunsafe-token=${token}\r\n`
      );
      await bootstrap.upload(ini, original);
      editorOpened = true;
      await run('open-config');
      const editor = await recovered.waitForWindow(
        { titleRegex: /\.agent-rover-agent\.ini/u, visible: true },
        { timeoutMs: 20000 }
      );
      expect(await bootstrap.download(ini)).toEqual(original);
      expect((await concurrent.capabilities()).platform).toBe('windows');
      let previousClipboard = '';
      try {
        previousClipboard = await recovered.clipboard.readText();
      } catch (error) {
        // The shared desktop may have a non-text or expired delayed clipboard.
        if (
          !(error instanceof Error) ||
          !error.message.includes('GetClipboardData')
        )
          throw error;
      }
      await writeFile(
        resolve(agents, `.build/config-editor-before-${arch}.png`),
        (await editor.screenshot()).image
      );
      // UI Automation identifies the exact tab and its document independently
      // from duplicate titles, delayed clipboard rendering and keyboard timing.
      const opened = await waitForResult(async () => {
        const state = await notepad(editor.id, 'inspect', '');
        expect(state.dialog).toBe(false);
        expect(state.text?.replace(/\r\n?/gu, '\n').trim()).toBe(
          original.toString('utf8').replace(/\r\n?/gu, '\n').trim()
        );
        return state;
      });
      await notepad(editor.id, 'select-all', opened.selectedId);
      expect((await editor.refresh()).active).toBe(true);
      const edited = `[agent]\r\nhost=127.0.0.1\r\nport=39421\r\nunsafe-token=${token}\r\n`;
      const videoPath = resolve(agents, `.build/config-editor-${arch}.mp4`);
      await rm(videoPath, { force: true });
      const [video] = await Promise.all([
        concurrent.recordVideo(2000, videoPath, {
          rect: editor.frameBounds,
          fps: 8,
        }),
        (async () => {
          // This worker request follows recording startup on the same session.
          expect(await concurrent.files.exists(ini)).toBe(true);
          await recovered.keyboard.pasteText(edited, {
            restoreClipboard: false,
          });
          await recovered.keyboard.press('s', { modifiers: ['Control'] });
          await waitForResult(
            async () => {
              const saved = await bootstrap.download(ini);
              expect(
                saved.toString(saved[0] === 255 ? 'utf16le' : 'utf8')
              ).toContain('port=39421');
            },
            { timeoutMs: 10000 }
          );
        })(),
      ]);
      expect(video.frameCount).toBeGreaterThan(1);
      const savedEditor = await notepad(editor.id, 'inspect', '');
      expect(savedEditor.dialog).toBe(false);
      expect(savedEditor.text?.replace(/\r\n?/gu, '\n').trim()).toBe(
        edited.replace(/\r\n?/gu, '\n').trim()
      );
      await writeFile(
        resolve(agents, `.build/config-editor-saved-${arch}.png`),
        (await editor.screenshot()).image
      );
      recovered.release();
      concurrent.release();
      await run('exit');
      await waitForResult(async () => {
        expect(
          await bootstrap.request('process.managedSnapshot', {
            managedProcessId: deployment!.managedProcessId,
          })
        ).toMatchObject({ running: false, exitCode: 0 });
      });
      expect(
        await bootstrap.request('process.snapshot', {
          processId: editor.process.id,
        })
      ).toMatchObject({ running: true });
      restarted = (await bootstrap.request('process.launchManaged', {
        path: `${directory}\\agent.exe`,
        environment: { HOME: directory },
        createNoWindow: true,
        killTreeOnRelease: true,
      })) as { managedProcessId: number };
      const configured = await connect(39421);
      expect((await configured.capabilities()).platform).toBe('windows');
      const retained = (await configured.windows()).find(
        (window) => window.id === editor.id
      );
      expect(retained).toBeDefined();
      expect((await notepad(editor.id, 'inspect', '')).selectedId).toBe(
        opened.selectedId
      );
      // Close just the test document when modern Notepad reuses a window with tabs.
      await notepad(editor.id, 'close', opened.selectedId);
      await waitForResult(async () => {
        const remaining = (await configured.windows()).find(
          (window) => window.id === editor.id
        );
        if (!remaining) return;
        const state = await notepad(editor.id, 'inspect', '');
        expect(state.dialog).toBe(false);
        expect(state.tabs.some((tab) => tab.id === opened.selectedId)).toBe(
          false
        );
      });
      editorClosed = true;
      await configured.clipboard.writeText(previousClipboard);
      console.info(
        `Windows ${arch}: INI startup, retained settings, tray editing, saved settings and independent editor lifetime passed.`
      );
    } finally {
      for (const client of clients) client.release();
      try {
        for (const forwarder of forwarders) await forwarder.close();
        if (restarted)
          await bootstrap.releaseManaged(restarted.managedProcessId);
        if (deployment)
          await bootstrap.releaseManaged(deployment.managedProcessId);
        if (editorOpened && !editorClosed) {
          // A failed UI test must not delete a document still open in Notepad.
          console.info(
            `Preserving configuration test files until its tab closes: ${directory}`
          );
        } else
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
