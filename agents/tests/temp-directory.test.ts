import { execFile } from 'node:child_process';
import { mkdir, mkdtemp, rm } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
const environment = { ...process.env, WINEDEBUG: '-all' };

it('creates owned directories exclusively and preserves native failures on both architectures', async () => {
  await exec('make', ['-C', 'tests/temp-directory', '-j4'], {
    cwd: root,
    maxBuffer: 8 * 1024 * 1024,
  });
  const directory = await mkdtemp(resolve(root, '.build/workspace 日本語 '));
  try {
    for (const arch of ['amd64', 'i686']) {
      const local = resolve(directory, arch);
      await mkdir(local);
      const windows = (await exec('winepath', ['-w', local], { env: environment })).stdout.trim();
      const result = await exec('wine', [resolve(root, `.build/temp-directory/runtime-${arch}.exe`), windows], {
        env: environment,
        timeout: 30000,
      });
      expect(result.stdout).toContain('race passed');
      expect(result.stdout).toContain('denied passed');
      expect(result.stdout).toContain('missing-parent passed');
    }
  } finally { await rm(directory, { recursive: true, force: true }); }
}, 120000);
