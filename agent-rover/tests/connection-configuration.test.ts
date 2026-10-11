// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import {
  parseConnectionConfiguration,
  readConnectionSection,
} from '../src/driver/configuration';

it('preserves endpoint/token pairs, literal section names, and token characters', () => {
  const sections = parseConnectionConfiguration(
    '\uFEFF # comment\r\n[default]\r\n foo.bar.com = true \r\n; comment\r\nbaz.hoge.com:12345= a=b;#c,\\d \r\n\r\n[ci.windows]\r\n[2001:db8::1]:23456=\r\n[constructor]\r\nlocalhost=null\r\n[__proto__]\r\nlocalhost=false',
    'test.ini'
  );
  expect([...sections]).toEqual([
    [
      'default',
      [
        { host: 'foo.bar.com', port: 39397, authToken: 'true' },
        { host: 'baz.hoge.com', port: 12345, authToken: 'a=b;#c,\\d' },
      ],
    ],
    [
      'ci.windows',
      [{ host: '2001:db8::1', port: 23456, authToken: undefined }],
    ],
    ['constructor', [{ host: 'localhost', port: 39397, authToken: 'null' }]],
    ['__proto__', [{ host: 'localhost', port: 39397, authToken: 'false' }]],
  ]);
});

it.each([
  'host=SECRET',
  '[]\nhost=SECRET',
  '[default]\nhost=SECRET\n[default]',
  '[default]\nhost=SECRET\nHOST:39397=SECRET',
  '[default]\n=SECRET',
  '[default]\nhost:0=SECRET',
  '[default]\nhost:65536=SECRET',
  '[default]\nhost:1.5=SECRET',
  '[default]\nhost:=SECRET',
  '[default]\nhost:abc=SECRET',
  '[default]\n::1=SECRET',
  '[default]\n[invalid]:123=SECRET',
  '[default]\nfoo;bar=SECRET',
  '[default]\nfoo,bar=SECRET',
  '[default]\nhttp://host=SECRET',
  '[default]\nhost name=SECRET',
  '[default]\nSECRET',
  '[default] SECRET',
])(
  'rejects invalid configuration without revealing its contents: %j',
  (source) => {
    let error: unknown;
    try {
      parseConnectionConfiguration(source, 'test.ini');
    } catch (caught) {
      error = caught;
    }
    expect(error).toMatchObject({ code: 'INVALID_ARGUMENT' });
    expect((error as Error).message).toMatch(/^test.ini:\d+:/u);
    expect((error as Error).message).not.toContain('SECRET');
  }
);

it('reads only the selected candidates and reports missing/empty sections and files', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-config-'));
  const path = join(directory, '.agent-rover');
  try {
    await expect(
      readConnectionSection([path], 'default')
    ).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
    });
    await writeFile(path, '[default]\na=one\n[other]\nb:12=two\n[empty]\n');
    expect(await readConnectionSection([path], 'other')).toMatchObject({
      name: 'other',
      endpoints: [{ host: 'b', port: 12, authToken: 'two' }],
    });
    const section = await readConnectionSection([path], 'default');
    expect(section.redact('one and two')).toBe('[redacted] and [redacted]');
    for (const name of ['missing', 'empty', 'DEFAULT', '']) {
      await expect(readConnectionSection([path], name)).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
    }
    await writeFile(path, '[other]\nb=changed\n');
    expect(
      (await readConnectionSection([path], 'other')).endpoints[0]!.authToken
    ).toBe('changed');
    await expect(
      readConnectionSection([path], 'default')
    ).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
    });
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
