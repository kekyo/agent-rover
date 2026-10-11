// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { writeFileSync } from 'node:fs';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { connectRemoteAgent } from '../src/index';
import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

const home = vi.hoisted(() => ({ directory: '', working: '' }));
vi.mock('node:os', async (original) => ({
  ...(await original<typeof import('node:os')>()),
  homedir: () => home.directory,
}));
beforeEach(async () => {
  home.directory = await mkdtemp(join(tmpdir(), 'agent-rover-sdk-config-'));
  home.working = join(home.directory, 'working');
  await mkdir(home.working);
  vi.spyOn(process, 'cwd').mockReturnValue(home.working);
});
afterEach(async () => {
  vi.restoreAllMocks();
  await rm(home.directory, { recursive: true, force: true });
});

it('uses the home default and prefers a literal named section in the working directory', async () => {
  const first = await startFakeTcpAgent({ authToken: 'one=;#,' });
  const second = await startFakeTcpAgent({
    authToken: 'two',
    windows: [{ ...defaultFakeWindow, title: 'named' }],
  });
  try {
    await writeFile(
      join(home.directory, '.agent-rover'),
      `[default]\n${first.host}:${first.port}=one=;#,\n[ci.windows]\n${first.host}:${first.port}=one=;#,\n`
    );
    await writeFile(
      join(home.working, '.agent-rover'),
      `[ci.windows]\n${second.host}:${second.port}=two\n`
    );
    for (const options of [undefined, {}, { section: 'ci.windows' }]) {
      const agent = await connectRemoteAgent(options);
      try {
        expect((await agent.windows())[0]!.title).toBe(
          options?.section ? 'named' : defaultFakeWindow.title
        );
      } finally {
        agent.release();
      }
    }
  } finally {
    await first.close();
    await second.close();
  }
});

it('advances past failed authentication and uses the next endpoint credential', async () => {
  vi.spyOn(Math, 'random').mockReturnValue(0.99);
  const first = await startFakeTcpAgent({ authToken: 'correct-first' });
  const second = await startFakeTcpAgent({ authToken: 'correct-second' });
  try {
    await writeFile(
      join(home.directory, '.agent-rover'),
      `[default]\n${first.host}:${first.port}=wrong-first\n${second.host}:${second.port}=correct-second\n`
    );
    const onRetry = vi.fn();
    const agent = await connectRemoteAgent({ retry: false, onRetry });
    try {
      expect((await agent.capabilities()).platform).toBe('windows');
    } finally {
      agent.release();
    }
    expect(onRetry).not.toHaveBeenCalled();
  } finally {
    await first.close();
    await second.close();
  }
});

it('fails before connecting when the file or the selected section is unavailable', async () => {
  await expect(connectRemoteAgent()).rejects.toMatchObject({
    code: 'INVALID_ARGUMENT',
  });
  await writeFile(
    join(home.directory, '.agent-rover'),
    '[other]\nlocalhost=SECRET\n'
  );
  await expect(connectRemoteAgent()).rejects.toMatchObject({
    code: 'INVALID_ARGUMENT',
  });
  await expect(connectRemoteAgent({ section: '' })).rejects.toMatchObject({
    code: 'INVALID_ARGUMENT',
  });
});

it.each(['directory', 'working'] as const)(
  'keeps the %s snapshot through retries and reloads for a new call',
  async (source) => {
    const first = await startFakeTcpAgent({ authToken: 'first-token' });
    const second = await startFakeTcpAgent({ authToken: 'second-token' });
    const path = join(home[source], '.agent-rover');
    const onRetry = vi.fn(() => {
      writeFileSync(
        path,
        `[default]\n${second.host}:${second.port}=second-token\n`
      );
    });
    try {
      await writeFile(
        join(home.directory, '.agent-rover'),
        `[default]\n${second.host}:${second.port}=second-token\n`
      );
      await writeFile(
        path,
        `[default]\n${first.host}:${first.port}=wrong-token\n`
      );
      await expect(
        connectRemoteAgent({ retry: { maxRetries: 1, intervalMs: 0 }, onRetry })
      ).rejects.toMatchObject({ code: 'AUTHENTICATION_FAILED' });
      expect(onRetry).toHaveBeenCalledTimes(1);
      const agent = await connectRemoteAgent({ retry: false });
      try {
        expect((await agent.capabilities()).platform).toBe('windows');
      } finally {
        agent.release();
      }
    } finally {
      await first.close();
      await second.close();
    }
  }
);
