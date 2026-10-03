// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createDeferred } from 'async-primitives';
import { expect, it } from 'vitest';
import {
  defaultFakeCapabilities,
  defaultFakeWindow,
  startFakeTcpAgent,
} from './helpers/fake-tcp-agent';

const executable = fileURLToPath(new URL('../dist/arctl.mjs', import.meta.url));
const run = async (args: readonly string[], env: NodeJS.ProcessEnv) =>
  await new Promise<{ code: number; stdout: string; stderr: string }>(
    (resolve, reject) => {
      execFile(
        process.execPath,
        [executable, ...args],
        {
          env: { ...process.env, AGENT_ROVER_AUTH_TOKEN: undefined, ...env },
          timeout: 15000,
        },
        (error, stdout, stderr) => {
          if (error !== null && typeof error.code !== 'number') reject(error);
          else resolve({ code: error?.code ?? 0, stdout, stderr });
        }
      );
    }
  );

it.each([undefined, defaultFakeWindow.id])(
  'records, transfers, and saves for longer than one request timeout (%s)',
  async (windowId) => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-record-'));
    const path = join(directory, 'nested', 'capture.mp4');
    const videoData = Buffer.from('MP4 transfer fixture');
    const videoRequests: Record<string, unknown>[] = [];
    const fake = await startFakeTcpAgent({ videoData, videoRequests });
    try {
      const result = await run(
        [
          'record',
          path,
          '--seconds',
          '1.201',
          '--fps',
          '24',
          '--timeout',
          '1',
          '--json',
          ...(windowId === undefined ? [] : ['--window', windowId]),
        ],
        { AGENT_ROVER_HOST: fake.host, AGENT_ROVER_PORT: String(fake.port) }
      );
      expect(result.code, result.stderr).toBe(0);
      expect(result.stderr).toBe('');
      expect(await readFile(path)).toEqual(videoData);
      expect(JSON.parse(result.stdout)).toMatchObject({
        command: 'record',
        result: {
          path,
          codec: 'h264',
          contentType: 'video/mp4',
          durationMs: 1201,
          fps: 24,
        },
      });
      expect(videoRequests).toEqual([
        {
          durationMs: 1201,
          fps: 24,
          quality: 90,
          ...(windowId === undefined
            ? {}
            : { windowId, tracking: 'followWindow' }),
        },
      ]);
    } finally {
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  }
);

it('validates recording duration, FPS, and required output before connecting', async () => {
  const env = { AGENT_ROVER_HOST: 'unused.invalid', AGENT_ROVER_PORT: '39397' };
  for (const args of [
    ['record'],
    ['record', 'out.mp4'],
    ['record', '--seconds', '1'],
    ...['0', '-1', '600.001', 'NaN', 'Infinity', '0.0001'].map((value) => [
      'record',
      'out.mp4',
      '--seconds',
      value,
    ]),
    ...['0', '241', '1.5'].map((value) => [
      'record',
      'out.mp4',
      '--seconds',
      '1',
      '--fps',
      value,
    ]),
  ]) {
    const result = await run(args, env);
    expect(result.code, JSON.stringify(args)).toBe(2);
    expect(result.stdout).toBe('');
  }
  const help = await run(['record', '--help'], env);
  expect(help.code).toBe(0);
  expect(help.stdout).toContain('--seconds');
});

it('reports unsupported video without sending a recording request', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'arctl-record-unsupported-'));
  const videoRequests: Record<string, unknown>[] = [];
  const fake = await startFakeTcpAgent({
    videoRequests,
    capabilities: {
      ...defaultFakeCapabilities,
      features: defaultFakeCapabilities.features.filter(
        (feature) => !feature.endsWith('recordVideo')
      ),
    },
  });
  try {
    const result = await run(
      ['record', join(directory, 'output.mp4'), '--seconds', '1'],
      { AGENT_ROVER_HOST: fake.host, AGENT_ROVER_PORT: String(fake.port) }
    );
    expect(result.code).toBe(1);
    expect(result.stdout).toBe('');
    expect(result.stderr).toMatch(/not support/iu);
    expect(videoRequests).toHaveLength(0);
  } finally {
    await fake.close();
    await rm(directory, { recursive: true, force: true });
  }
});

it('protects an existing video and rejects an unknown window before recording', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'arctl-record-errors-'));
  const path = join(directory, 'output.mp4');
  const videoRequests: Record<string, unknown>[] = [];
  const fake = await startFakeTcpAgent({ videoRequests });
  try {
    await writeFile(path, 'keep video');
    for (const args of [
      ['record', path, '--seconds', '1'],
      [
        'record',
        join(directory, 'invalid.mp4'),
        '--seconds',
        '1',
        '--window',
        '0xmissing',
      ],
    ]) {
      const result = await run(args, {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
      });
      expect(result.code).toBe(1);
      expect(result.stdout).toBe('');
    }
    expect(await readFile(path, 'utf8')).toBe('keep video');
    expect(videoRequests).toHaveLength(0);
  } finally {
    await fake.close();
    await rm(directory, { recursive: true, force: true });
  }
});

it('interrupts after receiving the recording request, without waiting for the duration', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'arctl-record-interrupt-'));
  const started = createDeferred<void>();
  const requests: Record<string, unknown>[] = [];
  const fake = await startFakeTcpAgent({
    videoRequests: requests,
    beforeRequest: (method) => {
      if (method === 'agent.recordVideo') started.resolve();
      return undefined;
    },
  });
  const child = spawn(
    process.execPath,
    [executable, 'record', join(directory, 'output.mp4'), '--seconds', '600'],
    {
      env: {
        ...process.env,
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
        AGENT_ROVER_AUTH_TOKEN: undefined,
        TMPDIR: directory,
      },
      stdio: 'ignore',
    }
  );
  const exited = once(child, 'exit');
  try {
    await started.promise;
    child.kill('SIGINT');
    expect(await exited).toEqual([130, null]);
    expect(requests).toHaveLength(1);
    expect(await readdir(directory)).toEqual([]);
  } finally {
    if (child.exitCode === null && child.signalCode === null) {
      child.kill('SIGKILL');
      await exited;
    }
    await fake.close();
    await rm(directory, { recursive: true, force: true });
  }
}, 10000);

it.each(['start', 'transfer'] as const)(
  'reports a lost recording outcome during %s and releases temporary files',
  async (phase) => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-record-disconnect-'));
    const videoRequests: Record<string, unknown>[] = [];
    const fake = await startFakeTcpAgent({
      videoRequests,
      videoData: Buffer.alloc(131072, 1),
      ...(phase === 'start'
        ? { disconnectAfterResponse: ['agent.recordVideo'] }
        : { interruptVideoTransfer: true }),
    });
    try {
      const result = await run(
        [
          'record',
          join(directory, 'output.mp4'),
          '--seconds',
          phase === 'start' ? '600' : '0.001',
        ],
        {
          AGENT_ROVER_HOST: fake.host,
          AGENT_ROVER_PORT: String(fake.port),
          TMPDIR: directory,
        }
      );
      expect(result.code).toBe(1);
      expect(result.stdout).toBe('');
      expect(result.stderr).toContain('outcome is unknown');
      expect(videoRequests).toHaveLength(1);
      expect(await readdir(directory)).toEqual([]);
    } finally {
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  }
);
