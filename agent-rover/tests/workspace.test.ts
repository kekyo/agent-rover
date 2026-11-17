// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Public workspace path contract
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { win32 } from 'node:path';
import { expect, it } from 'vitest';
import { connectTestAgent } from './helpers/test-connection.ts';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent.ts';
import type { JsonValue } from '../src/protocol.ts';

it('uses the remote suggested base for omitted and name prefixes and keeps public workspaces after disconnect', async () => {
  const base = 'C:\\Rover Work 日本語';
  const requests: { method: string; params: JsonValue | undefined }[] = [];
  const fake = await startFakeTcpAgent({
    safeBaseDir: base,
    beforeRequest: (method, params) => {
      requests.push({ method, params });
      return undefined;
    },
  });
  const first = await connectTestAgent({ host: fake.host, port: fake.port });
  const second = await connectTestAgent({ host: fake.host, port: fake.port });
  try {
    expect((await first.capabilities()).features).toContain('file.safeBaseDir');
    expect(await first.files.safeBaseDir()).toBe(base);
    expect(await first.files.readdir(base)).toEqual([]);
    const omitted = await first.files.mkdtemp();
    expect(win32.dirname(omitted)).toBe(base);
    expect(win32.basename(omitted)).toMatch(/^agent-rover-.+/u);
    expect(
      requests.find(({ method }) => method === 'file.mkdtemp')?.params
    ).toEqual({});
    const paths = await Promise.all(
      Array.from({ length: 20 }, (_, index) =>
        (index % 2 ? first : second).files.mkdtemp('case-')
      )
    );
    expect(new Set(paths).size).toBe(paths.length);
    for (const path of paths) {
      expect(win32.dirname(path)).toBe(base);
      expect(win32.basename(path)).toMatch(/^case-.+/u);
    }
    await first.files.mkdir('D:\\OtherWork');
    const other = await first.files.mkdtemp('D:\\OtherWork\\test-');
    expect(win32.dirname(other)).toBe('D:\\OtherWork');
    await first.files.writeFile(
      `${omitted}\\owned.txt`,
      Buffer.from('caller-owned')
    );
    first.release();
    expect(await second.files.readFile(`${omitted}\\owned.txt`)).toEqual(
      Buffer.from('caller-owned')
    );
    await second.files.remove(omitted, { recursive: true });
    expect(await second.files.exists(omitted)).toBe(false);
    expect(await second.files.exists(base)).toBe(true);
  } finally {
    first.release();
    second.release();
    await fake.close();
  }
});

it.each([
  '',
  '..',
  '.',
  'nested/name-',
  'nested\\name-',
  'C:relative-',
  '\\rooted-',
])('rejects ambiguous prefix %j', async (prefix) => {
  const fake = await startFakeTcpAgent({});
  const agent = await connectTestAgent({ host: fake.host, port: fake.port });
  try {
    await expect(agent.files.mkdtemp(prefix)).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
    });
  } finally {
    agent.release();
    await fake.close();
  }
});

it.each([null, {}, { path: '' }, { path: 12 }])(
  'rejects malformed suggested base response %j',
  async (value) => {
    const fake = await startFakeTcpAgent({ safeBaseDirResult: value });
    const agent = await connectTestAgent({ host: fake.host, port: fake.port });
    try {
      await expect(agent.files.safeBaseDir()).rejects.toMatchObject({
        code: 'PROTOCOL_ERROR',
      });
    } finally {
      agent.release();
      await fake.close();
    }
  }
);
