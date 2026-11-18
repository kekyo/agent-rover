// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Verified Windows releases
// Copyright (c) Kouji Matsui. Under MIT.
import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { zipSync } from 'fflate';
import {
  checkRelease,
  compareVersions,
  extractExecutable,
} from '../src/update/release.ts';

const hash = (data: Buffer) => createHash('sha256').update(data).digest('hex');
const executable = Buffer.alloc(512);
executable.write('MZ');
executable.writeUInt32LE(64, 60);
executable.write('PE\0\0', 64, 'latin1');
executable.writeUInt16LE(0x14c, 68);
executable.writeUInt16LE(224, 84);
executable.writeUInt16LE(2, 86);
executable.writeUInt16LE(0x10b, 88);
const archive = Buffer.from(
  zipSync({
    'dist/agent-i686.exe': executable,
    '../README_pack.md': Buffer.from('doc'),
  })
);
const info = {
  repository: 'kekyo/agent-rover',
  version: '0.10.0',
  gitCommit: 'b'.repeat(40),
  updateProtocol: 0,
  binaries: { i686: { bytes: executable.length, sha256: hash(executable) } },
};
describe('official agent releases', () => {
  it('compares three decimal version components numerically', () => {
    expect(compareVersions('0.9.0', '0.10.0')).toBe(-1);
    expect(compareVersions('1.0.0', '0.10.0')).toBe(1);
    expect(compareVersions('0.10.0', '0.10.0')).toBe(0);
    for (const version of [
      'HEAD',
      '0.10',
      '00.10.0',
      '0.10.0-beta',
      '../0.10.0',
    ])
      expect(() => compareVersions(version, '1.0.0')).toThrow();
  });
  it('checks the repository, tag, ZIP digest and executable hash before installation', () => {
    const digest = 'sha256:' + hash(archive);
    expect(
      checkRelease('0.10.0', 'i686', archive, digest, info).executable
    ).toEqual(executable);
    for (const bad of [
      { ...info, repository: 'someone/project' },
      { ...info, version: '1.0.0' },
      { ...info, binaries: {} },
      {
        ...info,
        binaries: {
          i686: { bytes: executable.length, sha256: 'f'.repeat(64) },
        },
      },
    ])
      expect(() =>
        checkRelease('0.10.0', 'i686', archive, digest, bad)
      ).toThrow();
    expect(() =>
      checkRelease(
        '0.10.0',
        'i686',
        Buffer.concat([archive, Buffer.from('!')]),
        digest,
        info
      )
    ).toThrow();
  });
  it('selects one exact entry in stored and deflated releases and rejects ambiguous or wrong-machine images', () => {
    for (const level of [0, 6] as const)
      expect(
        extractExecutable(
          Buffer.from(zipSync({ 'agent-i686.exe': executable }, { level })),
          'i686'
        )
      ).toEqual(executable);
    expect(() =>
      extractExecutable(
        Buffer.from(
          zipSync({
            'dist/agent-i686.exe': executable,
            'agent-i686.exe': executable,
          })
        ),
        'i686'
      )
    ).toThrow();
    expect(() => extractExecutable(archive, 'amd64')).toThrow();
    expect(() =>
      extractExecutable(
        Buffer.from(zipSync({ 'agent-amd64.exe': executable })),
        'amd64'
      )
    ).toThrow();
  });
});

it('requires explicit consent to lose remote update, including in JSON automation', async () => {
  const { confirmLegacyUpdate } = await import('../src/cli/update.ts');
  const warnings: string[] = [];
  const run = (answer: string | undefined, allowed = false) =>
    confirmLegacyUpdate(
      'xp:39397',
      '0.10.0',
      allowed,
      answer === undefined ? undefined : async () => answer,
      (message) => {
        warnings.push(message);
      }
    );
  await expect(run(undefined)).rejects.toThrow('--allow-legacy-downgrade');
  expect(await run('')).toBe(false);
  expect(await run('n')).toBe(false);
  expect(await run('y')).toBe(true);
  expect(await run(undefined, true)).toBe(true);
  expect(warnings.join(' ')).toMatch(/manual installation on xp:39397/);
});
