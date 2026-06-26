import { execFile } from 'node:child_process';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { promisify } from 'node:util';

import { afterEach, describe, expect, it } from 'vitest';

import { createWindowsAgentArchive } from '../scripts/pack-windows.mjs';

const execFileAsync = promisify(execFile);
const agentsRoot = resolve(import.meta.dirname, '..');

const readUInt16 = (buffer, offset) => buffer.readUInt16LE(offset);
const readUInt32 = (buffer, offset) => buffer.readUInt32LE(offset);

const readZipEntries = async (archivePath) => {
  const buffer = await readFile(archivePath);
  let endOfCentralDirectory = -1;

  for (let index = buffer.length - 22; index >= 0; index -= 1) {
    if (readUInt32(buffer, index) === 0x06054b50) {
      endOfCentralDirectory = index;
      break;
    }
  }

  expect(endOfCentralDirectory).toBeGreaterThanOrEqual(0);

  const entryCount = readUInt16(buffer, endOfCentralDirectory + 10);
  const centralDirectoryOffset = readUInt32(buffer, endOfCentralDirectory + 16);
  const entries = new Map();
  let offset = centralDirectoryOffset;

  for (let index = 0; index < entryCount; index += 1) {
    expect(readUInt32(buffer, offset)).toBe(0x02014b50);

    const compressedSize = readUInt32(buffer, offset + 20);
    const fileNameLength = readUInt16(buffer, offset + 28);
    const extraLength = readUInt16(buffer, offset + 30);
    const commentLength = readUInt16(buffer, offset + 32);
    const localHeaderOffset = readUInt32(buffer, offset + 42);
    const fileNameOffset = offset + 46;
    const name = buffer
      .subarray(fileNameOffset, fileNameOffset + fileNameLength)
      .toString('utf8');

    expect(readUInt32(buffer, localHeaderOffset)).toBe(0x04034b50);

    const localFileNameLength = readUInt16(buffer, localHeaderOffset + 26);
    const localExtraLength = readUInt16(buffer, localHeaderOffset + 28);
    const contentOffset =
      localHeaderOffset + 30 + localFileNameLength + localExtraLength;
    const content = buffer.subarray(contentOffset, contentOffset + compressedSize);

    entries.set(name, content);
    offset += 46 + fileNameLength + extraLength + commentLength;
  }

  return entries;
};

describe('windows agent packaging', () => {
  const temporaryDirectories = [];

  afterEach(async () => {
    for (const directory of temporaryDirectories.splice(0)) {
      await rm(directory, { recursive: true, force: true });
    }
  });

  it('builds short Windows agent executable names', async () => {
    const result = await execFileAsync('make', ['print-output'], {
      cwd: agentsRoot,
    });

    expect(result.stdout.trim().split(/\r?\n/u)).toEqual([
      'dist/agent-amd64.exe',
      'dist/agent-i686.exe',
    ]);
  });

  it('creates a versioned Windows archive with agent binaries and documents', async () => {
    const repoRoot = await mkdtemp(join(tmpdir(), 'agent-rover-pack-'));
    temporaryDirectories.push(repoRoot);

    const fakeAgentsRoot = join(repoRoot, 'agents');
    const dist = join(fakeAgentsRoot, 'dist');
    await mkdir(dist, { recursive: true });
    await writeFile(join(repoRoot, 'LICENSE'), 'license text');
    await writeFile(join(repoRoot, 'README_pack.md'), 'readme text');
    await writeFile(join(dist, 'agent-amd64.exe'), 'amd64 binary');
    await writeFile(join(dist, 'agent-i686.exe'), 'i686 binary');
    await writeFile(join(dist, 'agent-rover-agent-amd64.exe'), 'legacy binary');

    const archivePath = await createWindowsAgentArchive({
      repoRoot,
      agentsRoot: fakeAgentsRoot,
      version: '1.2.3',
    });

    expect(archivePath).toBe(
      join(repoRoot, 'artifacts', 'agent-rover-windows-1.2.3.zip')
    );

    const entries = await readZipEntries(archivePath);

    expect([...entries.keys()].sort()).toEqual([
      'LICENSE',
      'README_pack.md',
      'agent-amd64.exe',
      'agent-i686.exe',
    ]);
    expect(entries.get('agent-amd64.exe').toString('utf8')).toBe('amd64 binary');
    expect(entries.get('agent-i686.exe').toString('utf8')).toBe('i686 binary');
    expect(entries.get('LICENSE').toString('utf8')).toBe('license text');
    expect(entries.get('README_pack.md').toString('utf8')).toBe('readme text');
    expect(dirname(archivePath)).toBe(join(repoRoot, 'artifacts'));
  });
});
