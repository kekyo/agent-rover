// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { delay } from 'async-primitives';
import { describe, expect, it } from 'vitest';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';
import { waitForResult } from '../src/wait';

import { connectRemoteAgent, type RemoteAgent } from '../src/index';

interface CommandResult {
  readonly stderr: string;
  readonly stdout: string;
}

const testDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryDirectory = join(testDirectory, '..', '..');
const agentDirectory = join(repositoryDirectory, 'agents');
const builtAgentPath = join(agentDirectory, 'dist', 'agent-amd64.exe');
const testAgentPort = 39407;
const hostEnvironmentName = 'AGENT_ROVER_WIN11_HOST2';
const tokenEnvironmentName = 'AGENT_ROVER_WIN11_TOKEN2';

const execFileResult = async (
  file: string,
  args: readonly string[],
  cwd: string
): Promise<CommandResult> =>
  await new Promise<CommandResult>((resolve, reject) => {
    execFile(file, args, { cwd }, (error, stdout, stderr) => {
      if (error === null) {
        resolve({ stderr, stdout });
      } else {
        reject(
          new Error(`${file} failed\nstdout:\n${stdout}\nstderr:\n${stderr}`)
        );
      }
    });
  });

const hasWin11Environment = (): boolean =>
  [hostEnvironmentName, tokenEnvironmentName].every((name) => {
    const value = process.env[name];
    return value !== undefined && value.length > 0;
  });

const requireEnvironment = (name: string): string => {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
};

const connectToTestAgent = async (
  host: string,
  token: string
): Promise<RemoteAgent> => {
  let lastError: unknown = undefined;
  for (let attempt = 0; attempt < 60; attempt += 1) {
    try {
      return await connectRemoteAgent({
        authToken: token,
        host,
        port: testAgentPort,
        timeoutMs: 5000,
      });
    } catch (error) {
      lastError = error;
      await delay(250);
    }
  }
  throw new Error(
    `Timed out connecting to the uploaded video test agent: ${String(
      lastError
    )}`
  );
};

const win11It = hasWin11Environment() ? it : it.skip;

describe('Windows video capture integration', () => {
  win11It(
    'records a moving Notepad window as a decodable H.264 MP4',
    async () => {
      const host = requireEnvironment(hostEnvironmentName);
      const testToken = randomBytes(24).toString('base64url');
      const uniqueName = `${String(process.pid)}-${String(Date.now())}`;
      const remoteDirectory = String.raw`C:\Windows\Temp\agent-rover-video-${uniqueName}`;
      const remoteAgentPath = `${remoteDirectory}\\agent-amd64.exe`;
      const remoteTextPath = `${remoteDirectory}\\video-${uniqueName}.txt`;
      const localDirectory = await mkdtemp(
        join(tmpdir(), 'agent-rover-win11-video-')
      );
      const outputPath = join(localDirectory, 'notepad.mp4');

      await execFileResult('make', ['amd64'], agentDirectory);
      const bootstrap = await connectWindowsBootstrap();
      let testAgent: RemoteAgent | undefined = undefined;
      let testAgentProcess: { managedProcessId: number } | undefined =
        undefined;
      let capturedWindow:
        Awaited<ReturnType<RemoteAgent['waitForWindow']>> | undefined =
        undefined;
      try {
        await bootstrap.request('file.mkdir', {
          path: remoteDirectory,
          recursive: true,
        });
        await bootstrap.upload(remoteAgentPath, await readFile(builtAgentPath));
        testAgentProcess = (await bootstrap.request('process.launchManaged', {
          killTreeOnRelease: true,
          arguments: [
            '--host',
            '0.0.0.0',
            '--port',
            String(testAgentPort),
            '--unsafe-token',
            testToken,
          ],
          createNoWindow: true,
          path: remoteAgentPath,
          workingDirectory: remoteDirectory,
        })) as { managedProcessId: number };
        testAgent = await connectToTestAgent(host, testToken);

        await testAgent.files.writeFile(
          remoteTextPath,
          Buffer.from(`agent-rover video integration ${uniqueName}`, 'utf8')
        );
        await testAgent.applications.launch({
          arguments: [remoteTextPath],
          path: 'notepad.exe',
        });
        capturedWindow = await testAgent.waitForWindow(
          {
            processName: 'notepad.exe',
            titleRegex: new RegExp(uniqueName, 'u'),
            visible: true,
          },
          { intervalMs: 250, timeoutMs: 10000 }
        );
        const initialBounds = capturedWindow.bounds;
        const [result, movedWindow] = await Promise.all([
          capturedWindow.recordVideo(800, outputPath),
          (async () => {
            await delay(250);
            return await capturedWindow.setBounds({
              ...initialBounds,
              x: initialBounds.x + 80,
              y: initialBounds.y + 60,
            });
          })(),
        ]);

        expect(result.path).toBe(outputPath);
        expect(result).toMatchObject({
          codec: 'h264',
          contentType: 'video/mp4',
          fps: 60,
        });
        expect(result.frameCount).toBeGreaterThan(0);
        expect(result.durationMs).toBeGreaterThanOrEqual(800);
        expect(result.durationMs).toBeLessThan(817);
        expect(result.initialBounds).toEqual(initialBounds);
        expect(result.finalBounds).toEqual(movedWindow.bounds);

        const probe = await execFileResult(
          'ffprobe',
          [
            '-v',
            'error',
            '-select_streams',
            'v:0',
            '-show_entries',
            'stream=codec_name,width,height,avg_frame_rate',
            '-of',
            'json',
            outputPath,
          ],
          repositoryDirectory
        );
        const probeResult = JSON.parse(probe.stdout) as {
          readonly streams?: readonly {
            readonly avg_frame_rate?: string;
            readonly codec_name?: string;
            readonly height?: number;
            readonly width?: number;
          }[];
        };
        expect(probeResult.streams?.[0]).toMatchObject({
          avg_frame_rate: '60/1',
          codec_name: 'h264',
          height: initialBounds.height + (initialBounds.height % 2),
          width: initialBounds.width + (initialBounds.width % 2),
        });
      } finally {
        if (capturedWindow !== undefined) {
          try {
            await capturedWindow.close();
          } catch {
            // Continue teardown when the test window has already closed.
          }
        }
        testAgent?.release();
        if (testAgentProcess !== undefined) {
          try {
            await bootstrap.request('process.releaseManaged', {
              managedProcessId: testAgentProcess.managedProcessId,
            });
          } catch {
            // Continue teardown when the uploaded agent has already exited.
          }
        }
        await delay(250);
        try {
          await bootstrap.request('file.remove', {
            path: remoteDirectory,
            recursive: true,
          });
        } catch {
          // A failed assertion must not be hidden by best-effort remote cleanup.
        }
        await bootstrap.close();
        await rm(localDirectory, { force: true, recursive: true });
      }
    },
    120000
  );
});

describe('Windows cleanup integration', () => {
  for (const abi of ['amd64', 'i686'] as const) {
    win11It(
      `reports real file failures on ${abi}`,
      async () => {
        await execFileResult('make', ['-j4'], agentDirectory);
        await execFileResult(
          'make',
          ['-j4'],
          join(agentDirectory, 'tests', 'cleanup')
        );
        const bootstrap = await connectWindowsBootstrap();
        const root = `C:/Windows/Temp/agent-rover-cleanup-${randomBytes(8).toString('hex')}`;
        const token = randomBytes(24).toString('base64url');
        const port = 39417;
        let agent: RemoteAgent | undefined;
        let agentId: number | undefined;
        try {
          await bootstrap.request('file.mkdir', {
            path: root,
            recursive: true,
          });
          await bootstrap.upload(
            `${root}/agent.exe`,
            await readFile(join(agentDirectory, 'dist', `agent-${abi}.exe`))
          );
          await bootstrap.upload(
            `${root}/helper.exe`,
            await readFile(
              join(agentDirectory, '.build', 'cleanup', abi, 'helper.exe')
            )
          );
          const launched = (await bootstrap.request('applications.launch', {
            path: `${root}/agent.exe`,
            arguments: [
              '--host',
              '0.0.0.0',
              '--port',
              String(port),
              '--unsafe-token',
              token,
            ],
            createNoWindow: true,
          })) as { id: number };
          agentId = launched.id;
          agent = await waitForResult(
            async () => {
              try {
                return await connectRemoteAgent({
                  host: requireEnvironment(hostEnvironmentName),
                  authToken: token,
                  port,
                  timeoutMs: 2000,
                });
              } catch (cause) {
                throw new Error('Uploaded agent is not ready.', { cause });
              }
            },
            { timeoutMs: 15000, intervalMs: 50 }
          );
          const activeAgent = agent;
          await agent.files.writeFile(
            `${root}/lifecycle.exe`,
            await readFile(
              join(agentDirectory, '.build', 'cleanup', abi, 'lifecycle.exe')
            )
          );
          const lifecycle = await agent.processes.launchManaged({
            path: `${root}/lifecycle.exe`,
            createNoWindow: true,
          });
          try {
            const exited = await lifecycle.waitForExit();
            expect(exited.root.exitCode).toBe(0);
          } finally {
            await lifecycle.releaseAsync();
          }
          const command = async (args: readonly string[]): Promise<void> => {
            const child = await activeAgent.processes.launchManaged({
              path: `${root}/helper.exe`,
              arguments: args,
              createNoWindow: true,
              killTreeOnRelease: false,
            });
            try {
              const result = await child.waitForExit();
              expect(result.root.exitCode).toBe(0);
            } finally {
              await child.releaseAsync();
            }
          };
          const file = `${root}/readonly.txt`;
          await agent.files.writeFile(file, Buffer.from('retained data'));
          await command(['readonly', file]);
          try {
            await expect(agent.files.remove(file)).rejects.toMatchObject({
              code: 'OPERATION_FAILED',
              details: {
                operation: 'file.remove',
                nativeOperation: 'DeleteFileW',
                path: file,
                osCode: 5,
                reason: 'readOnly',
              },
            });
            expect((await agent.files.readFile(file)).toString()).toBe(
              'retained data'
            );
          } finally {
            await command(['writable', file]);
          }
          for (const mode of ['capture', 'capture-tree']) {
            const ready = `${root}/${mode}.ready`;
            const event = `Local\\agent-rover-${randomBytes(12).toString('hex')}`;
            const captured = await agent.processes.launchManaged({
              path: `${root}/helper.exe`,
              arguments: [mode, ready, event],
              captureStdout: true,
              captureStderr: true,
              createNoWindow: true,
            });
            let signaled = false;
            try {
              await waitForResult(async () => {
                expect(await activeAgent.files.exists(ready)).toBe(true);
              });
              if (mode === 'capture') {
                expect(await captured.stdoutText()).toBe('start\n');
                expect(await captured.stderrText()).toBe('error-start\n');
                await command(['signal', event]);
                signaled = true;
                expect((await captured.waitForExit()).root.exitCode).toBe(0);
                expect(await captured.stdoutText()).toBe(
                  `start\n${'x'.repeat(131077)}\n終端\n`
                );
              } else {
                await waitForResult(async () => {
                  expect((await captured.rootSnapshot()).running).toBe(false);
                });
                expect((await captured.snapshot()).running).toBe(true);
                let settled = false;
                const reading = (async () => {
                  try {
                    return { value: await captured.stdoutText() };
                  } catch (error) {
                    return { error };
                  } finally {
                    settled = true;
                  }
                })();
                // This later RPC is a barrier: the first capture request has reached the agent.
                await activeAgent.files.stat(ready);
                expect(settled).toBe(false);
                await command(['signal', event]);
                signaled = true;
                expect(await reading).toEqual({
                  value: `start\n${'x'.repeat(131077)}\n終端\n`,
                });
              }
              expect(await captured.stderrText()).toBe(
                'error-start\nerror-end\n'
              );
            } finally {
              if (!signaled) await command(['signal', event]);
              await captured.releaseAsync();
            }
          }
          const empty = await agent.processes.launchManaged({
            path: `${root}/helper.exe`,
            arguments: ['empty', 'unused'],
            captureStdout: true,
            captureStderr: true,
            createNoWindow: true,
          });
          await empty.waitForExit();
          expect(await empty.stdoutText()).toBe('');
          expect(await empty.stderrText()).toBe('');
          await empty.releaseAsync();
          const ready = `${root}/no-kill.ready`;
          const event = `Local\\agent-rover-${randomBytes(12).toString('hex')}`;
          const noKill = await agent.processes.launchManaged({
            path: `${root}/helper.exe`,
            arguments: ['capture', ready, event],
            captureStdout: true,
            captureStderr: true,
            killTreeOnRelease: false,
            createNoWindow: true,
          });
          try {
            await waitForResult(async () => {
              expect(await activeAgent.files.exists(ready)).toBe(true);
            });
            await expect(
              noKill.releaseAsync({ timeoutMs: 0 })
            ).rejects.toMatchObject({
              details: { reason: 'busy', stage: 'processExit', timedOut: true },
            });
            expect((await agent.processes.snapshot(noKill.id)).running).toBe(
              true
            );
          } finally {
            await command(['signal', event]);
            await noKill.releaseAsync();
          }
        } finally {
          agent?.release();
          if (agentId !== undefined) {
            await bootstrap.request('process.kill', { processId: agentId });
            await waitForResult(
              async () => {
                const state = (await bootstrap.request('process.snapshot', {
                  processId: agentId!,
                })) as { running: boolean };
                expect(state.running).toBe(false);
              },
              { timeoutMs: 10000, intervalMs: 50 }
            );
          }
          // The preinstalled deployment agent is outside the implementation under test.
          try {
            await waitForResult(
              async () => {
                await bootstrap.request('file.remove', {
                  path: root,
                  recursive: true,
                });
              },
              { timeoutMs: 10000, intervalMs: 50 }
            );
          } finally {
            await bootstrap.close();
          }
        }
      },
      120000
    );
  }
});
