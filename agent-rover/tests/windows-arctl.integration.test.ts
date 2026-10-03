// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { mkdir, mkdtemp, readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  connectRemoteAgent,
  type RemoteAgent,
  type RemoteManagedProcess,
  type CapturedVideoResult,
} from '../src/index';
import { waitForResult } from '../src/wait';
import { nativeTestPaths } from './helpers/native-paths';
import {
  connectWindowsBootstrap,
  type WindowsBootstrap,
} from './helpers/windows-bootstrap';

const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);
if (process.env.AGENT_ROVER_REQUIRE_WINDOWS_TESTS === '1' && !enabled)
  throw new Error(
    'Windows CLI tests require AGENT_ROVER_WIN11_HOST and AGENT_ROVER_WIN11_TOKEN.'
  );
const exec = promisify(execFile);
const { repositoryDirectory: root, agentsDirectory } = nativeTestPaths(
  import.meta.url
);
const port = 39412;
const token = randomBytes(24).toString('base64url');
const unique = `arctl-${process.pid}-${Date.now()}`;
const remote = `C:\\Windows\\Temp\\agent-rover-${unique}`;
let local: string;
let bootstrap: WindowsBootstrap | undefined;
let deployment: { managedProcessId: number; id: number } | undefined;
let agent: RemoteAgent | undefined;
let display: RemoteManagedProcess | undefined;
const palette = [
  [36, 180, 90],
  [220, 40, 70],
  [40, 90, 220],
];

const arctl = async (args: readonly string[]) =>
  await exec(
    process.execPath,
    [join(root, 'agent-rover/dist/arctl.mjs'), ...args],
    {
      env: {
        ...process.env,
        AGENT_ROVER_HOST: host,
        AGENT_ROVER_PORT: String(port),
        AGENT_ROVER_AUTH_TOKEN: token,
      },
      timeout: 60000,
    }
  );

describe.skipIf(!enabled)('Windows arctl capture', () => {
  beforeAll(async () => {
    await exec('make', ['amd64', '-j4'], { cwd: agentsDirectory });
    await exec('make', ['-C', 'tests/arctl'], { cwd: agentsDirectory });
    await mkdir(join(root, 'artifacts'), { recursive: true });
    local = await mkdtemp(join(root, 'artifacts', 'arctl-windows-'));
    bootstrap = await connectWindowsBootstrap();
    await bootstrap.request('file.mkdir', { path: remote, recursive: true });
    await bootstrap.upload(
      `${remote}\\agent.exe`,
      await readFile(join(agentsDirectory, 'dist/agent-amd64.exe'))
    );
    await bootstrap.upload(
      `${remote}\\display.exe`,
      await readFile(join(agentsDirectory, '.build/arctl/display-amd64.exe'))
    );
    deployment = (await bootstrap.request('process.launchManaged', {
      path: `${remote}\\agent.exe`,
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
    })) as { managedProcessId: number; id: number };
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
          throw new Error('Windows CLI test agent is starting.', {
            cause: error,
          });
        }
      },
      { timeoutMs: 30000 }
    );
    display = await agent.processes.launchManaged({
      path: `${remote}\\display.exe`,
      arguments: [unique, 'animate'],
      killTreeOnRelease: true,
    });
    await display.waitForWindow({ title: unique, visible: true });
  }, 180000);

  afterAll(async () => {
    const errors: unknown[] = [];
    try {
      await display?.releaseAsync();
    } catch (error) {
      errors.push(error);
    }
    agent?.release();
    if (bootstrap) {
      if (deployment) {
        try {
          await bootstrap.releaseManaged(deployment.managedProcessId);
          await waitForResult(
            async () => {
              const snapshot = (await bootstrap!.request('process.snapshot', {
                processId: deployment!.id,
              })) as { running: boolean };
              expect(snapshot.running).toBe(false);
            },
            { timeoutMs: 10000 }
          );
        } catch (error) {
          errors.push(error);
        }
      }
      try {
        // The bootstrap agent may acknowledge release before Windows releases
        // executable image mappings. Observe removability as in the video suite.
        await waitForResult(
          async () => {
            try {
              await bootstrap!.request('file.remove', {
                path: remote,
                recursive: true,
                ignoreMissing: true,
              });
            } catch (error) {
              throw new Error('CLI deployment files are still in use.', {
                cause: error,
              });
            }
          },
          { timeoutMs: 10000 }
        );
      } catch (error) {
        errors.push(error);
      }
      await bootstrap.close();
    }
    if (errors.length)
      throw new AggregateError(errors, 'Windows CLI test cleanup failed.');
  }, 60000);

  it.each(['desktop', 'window'] as const)(
    'decodes changing frames and movement from a %s MP4',
    async (target) => {
      const window = await display!.waitForWindow({
        title: unique,
        visible: true,
      });
      const path = join(local, `${target}.mp4`);
      const recorded = await arctl([
        'record',
        path,
        '--seconds',
        '1.5',
        '--fps',
        '12',
        '--json',
        ...(target === 'desktop' ? [] : ['--window', window.id]),
      ]);
      const metadata = JSON.parse(recorded.stdout)
        .result as CapturedVideoResult;
      expect(metadata).toMatchObject({
        path,
        codec: 'h264',
        contentType: 'video/mp4',
        fps: 12,
        durationMs: 1500,
      });
      const probe = await exec('ffprobe', [
        '-v',
        'error',
        '-select_streams',
        'v:0',
        '-show_entries',
        'stream=codec_name,width,height,avg_frame_rate,nb_frames,duration',
        '-of',
        'json',
        path,
      ]);
      const stream = JSON.parse(probe.stdout).streams[0];
      expect(stream).toMatchObject({
        codec_name: 'h264',
        avg_frame_rate: '12/1',
        width:
          metadata.initialBounds.width + (metadata.initialBounds.width % 2),
        height:
          metadata.initialBounds.height + (metadata.initialBounds.height % 2),
      });
      expect(Number(stream.nb_frames)).toBe(metadata.frameCount);
      expect(Number(stream.duration)).toBeCloseTo(1.5, 1);
      const width = target === 'desktop' ? 480 : 240;
      const height = target === 'desktop' ? 270 : 180;
      const decoded = await exec(
        'ffmpeg',
        [
          '-v',
          'error',
          '-i',
          path,
          '-vf',
          `scale=${width}:${height}`,
          '-f',
          'rawvideo',
          '-pix_fmt',
          'rgb24',
          'pipe:1',
        ],
        { encoding: 'buffer', maxBuffer: 32 * 1024 * 1024 }
      );
      const frameBytes = width * height * 3;
      const frames = decoded.stdout.length / frameBytes;
      expect(frames).toBe(metadata.frameCount);
      const colors = new Set<number>();
      const centers: number[] = [];
      let coloredFrames = 0;
      for (let frame = 0; frame < frames; ++frame) {
        const counts = [0, 0, 0];
        const positions = [0, 0, 0];
        for (let pixel = 0; pixel < width * height; ++pixel) {
          const offset = frame * frameBytes + pixel * 3;
          const color = palette.findIndex((rgb) =>
            rgb.every(
              (value, channel) =>
                Math.abs(decoded.stdout[offset + channel]! - value) < 24
            )
          );
          if (color >= 0) {
            counts[color]! += 1;
            positions[color]! += pixel % width;
          }
        }
        const dominant = counts.indexOf(Math.max(...counts));
        if (
          counts[dominant]! >
          (target === 'desktop' ? 100 : width * height * 0.7)
        ) {
          coloredFrames += 1;
          colors.add(dominant);
          centers.push(positions[dominant]! / counts[dominant]!);
        }
      }
      expect(colors.size).toBeGreaterThanOrEqual(2);
      expect(coloredFrames).toBeGreaterThanOrEqual(frames * 0.8);
      if (target === 'desktop')
        expect(Math.max(...centers) - Math.min(...centers)).toBeGreaterThan(
          (150 * width) / metadata.initialBounds.width
        );
      console.log(
        `arctl ${target}: ${frames} frames, ${colors.size} colors, ${path}`
      );
    },
    90000
  );
});
