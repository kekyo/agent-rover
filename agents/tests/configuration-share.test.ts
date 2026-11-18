import { execFile } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

it('accepts an existing UNC share root even when path attributes are unavailable', async () => {
  const exec = promisify(execFile);
  const root = resolve(import.meta.dirname, '..');
  await exec('make', ['-C', 'tests/configuration', '-j4', '../../.build/configuration/share-amd64.exe', '../../.build/configuration/share-i686.exe'], { cwd: root, maxBuffer: 8 * 1024 * 1024 });
  const directory = await mkdtemp(resolve(root, '.build/share-root-'));
  const environment = { ...process.env, WINEDEBUG: '-all' };
  try {
    const windows = (await exec('winepath', ['-w', directory], { env: environment })).stdout.trim();
    for (const arch of ['amd64', 'i686']) {
      const result = await exec('wine', [resolve(root, `.build/configuration/share-${arch}.exe`), windows], { env: environment, timeout: 30000 });
      expect(result.stdout).toContain('UNC share root passed');
    }
  } finally { await rm(directory, { recursive: true, force: true }); }
}, 120000);
