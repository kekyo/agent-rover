// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { loadConnectionSection } from '../src/driver/configuration';

const directories = vi.hoisted(() => ({ home: '', working: '' }));
vi.mock('node:os', async (original) => ({
  ...(await original<typeof import('node:os')>()),
  homedir: () => directories.home,
}));
vi.mock('node:fs/promises', async (original) => {
  const actual = await original<typeof import('node:fs/promises')>();
  return { ...actual, readFile: vi.fn(actual.readFile) };
});

let directory: string;
beforeEach(async () => {
  directory = await mkdtemp(join(tmpdir(), 'agent-rover-fallback-'));
  directories.home = join(directory, 'home');
  directories.working = join(directory, 'project');
  await mkdir(directories.home);
  await mkdir(directories.working);
  vi.spyOn(process, 'cwd').mockImplementation(() => directories.working);
});
afterEach(async () => {
  vi.restoreAllMocks();
  vi.mocked(readFile).mockReset();
  await rm(directory, { recursive: true, force: true });
});

it.each(['default', 'ci.windows'])(
  'prefers the entire local %s section and retains its credential filter',
  async (name) => {
    await writeFile(
      join(directories.working, '.agent-rover'),
      `[${name}]\nlocal.example:12345=local-token\n[other]\nother.example=other-token\n`
    );
    await writeFile(
      join(directories.home, '.agent-rover'),
      `[${name}]\nhome.example=home-token\n`
    );
    const section = await loadConnectionSection(name);
    expect(section.endpoints).toEqual([
      { host: 'local.example', port: 12345, authToken: 'local-token' },
    ]);
    expect(section.redact('local-token other-token')).toBe(
      '[redacted] [redacted]'
    );
  }
);

it('accepts a local section even when the home file is invalid', async () => {
  await writeFile(
    join(directories.working, '.agent-rover'),
    '[default]\nlocal.example=\n'
  );
  await writeFile(join(directories.home, '.agent-rover'), 'invalid');
  expect((await loadConnectionSection('default')).endpoints).toEqual([
    { host: 'local.example', port: 39397, authToken: undefined },
  ]);
});

it.each([undefined, '', '[other]\nother.example=other-token\n'])(
  'falls back to the same home section when the local file or section is absent: %j',
  async (local) => {
    if (local !== undefined)
      await writeFile(join(directories.working, '.agent-rover'), local);
    await writeFile(
      join(directories.home, '.agent-rover'),
      '[default]\ndefault.example=default-token\n[ci.windows]\nnamed.example=home-token\n'
    );
    for (const [name, host] of [
      ['default', 'default.example'],
      ['ci.windows', 'named.example'],
    ] as const) {
      expect((await loadConnectionSection(name)).endpoints[0]!.host).toBe(host);
    }
  }
);

it.each([
  '[default]\n',
  '[default]\nlocal.example=SECRET\ninvalid',
  '[default]\nlocal.example=SECRET\n[default]\n',
  '[other]\ninvalid=SECRET\ninvalid',
])(
  'rejects invalid local configuration instead of falling back: %j',
  async (local) => {
    const path = join(directories.working, '.agent-rover');
    await writeFile(path, local);
    await writeFile(
      join(directories.home, '.agent-rover'),
      '[default]\nhome.example=home-token\n'
    );
    await expect(loadConnectionSection('default')).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
      message: expect.stringContaining(path),
    });
  }
);

it.each(['EACCES', 'EIO'])(
  'reports local read errors without falling back or exposing their contents: %s',
  async (code) => {
    await writeFile(
      join(directories.home, '.agent-rover'),
      '[default]\nhome.example=home-token\n'
    );
    vi.mocked(readFile).mockRejectedValueOnce(
      Object.assign(new Error('SECRET'), { code })
    );
    await expect(loadConnectionSection('default')).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
      message: `Cannot read agent configuration: ${join(directories.working, '.agent-rover')}.`,
    });
  }
);

it.each([undefined, '[default]\nfallback.example=SECRET\n'])(
  'reports the requested name and both paths when neither file provides the section: %j',
  async (contents) => {
    const paths = [
      join(directories.working, '.agent-rover'),
      join(directories.home, '.agent-rover'),
    ];
    if (contents !== undefined) {
      for (const path of paths) await writeFile(path, contents);
    }
    await expect(loadConnectionSection('missing')).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
      message: `Selected section "missing" was not found. Searched: ${paths.join(', ')}.`,
    });
  }
);

it('does not search parent directories', async () => {
  await writeFile(
    join(directory, '.agent-rover'),
    '[default]\nparent.example=\n'
  );
  await writeFile(
    join(directories.home, '.agent-rover'),
    '[default]\nhome.example=\n'
  );
  expect((await loadConnectionSection('default')).endpoints[0]!.host).toBe(
    'home.example'
  );
});

it('reports a shared working/home path once when the section is missing', async () => {
  directories.working = directories.home;
  await expect(loadConnectionSection('default')).rejects.toMatchObject({
    code: 'INVALID_ARGUMENT',
    message: `Selected section "default" was not found. Searched: ${join(directories.home, '.agent-rover')}.`,
  });
});
