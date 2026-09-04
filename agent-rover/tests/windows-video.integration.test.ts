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

import {
  connectRemoteAgent,
  type RemoteAgent,
  type RemoteManagedProcess,
} from '../src/index';

interface CommandResult {
  readonly stderr: string;
  readonly stdout: string;
}

const testDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryDirectory = join(testDirectory, '..', '..');
const agentDirectory = join(repositoryDirectory, 'agents');
const builtAgentPath = join(agentDirectory, 'dist', 'agent-amd64.exe');
const bootstrapPort = 39397;
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
      const bootstrapToken = requireEnvironment(tokenEnvironmentName);
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
      const bootstrap = await connectRemoteAgent({
        authToken: bootstrapToken,
        host,
        port: bootstrapPort,
        timeoutMs: 30000,
      });
      let testAgent: RemoteAgent | undefined = undefined;
      let testAgentProcess: RemoteManagedProcess | undefined = undefined;
      let capturedWindow:
        Awaited<ReturnType<RemoteAgent['waitForWindow']>> | undefined =
        undefined;
      try {
        await bootstrap.files.mkdir(remoteDirectory, { recursive: true });
        await bootstrap.files.writeFile(
          remoteAgentPath,
          await readFile(builtAgentPath)
        );
        testAgentProcess = await bootstrap.processes.launchManaged({
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
        });
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
            await testAgentProcess.releaseAsync();
          } catch {
            // Continue teardown when the uploaded agent has already exited.
          }
        }
        await delay(250);
        try {
          await bootstrap.files.remove(remoteDirectory, { recursive: true });
        } catch {
          // A failed assertion must not be hidden by best-effort remote cleanup.
        }
        bootstrap.release();
        await rm(localDirectory, { force: true, recursive: true });
      }
    },
    120000
  );
});
