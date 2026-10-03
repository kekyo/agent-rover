import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
it('keeps queue time inside finite operation-specific deadlines', async () => {
  await exec('g++', ['-std=c++20', '-Iwindows', 'tests/operation-limits.cpp', '-o', '.build/operation-limits'], { cwd: root });
  await exec(resolve(root, '.build/operation-limits'));
});
it('bounds file allocation and cancellation without freeing a running codec', async () => {
  await exec('make', ['-C', 'tests/io', '-j4', 'all'], { cwd: root, maxBuffer: 4 * 1024 * 1024 });
  for (const architecture of ['amd64', 'i686']) {
    for (const mode of ['file', 'video']) {
      const { stdout } = await exec('wine', [resolve(root, `.build/io/bounded-${architecture}.exe`), mode],
        { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 15000 });
      expect(stdout).toContain('bounded native operation passed');
    }
  }
}, 180000);
