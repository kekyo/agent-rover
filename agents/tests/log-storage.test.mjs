import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
it('rotates bounded files without deleting active writers and preserves async UTF-8 appends', async () => {
  await exec('make', ['-C', 'tests/log-storage', '-j4', 'all'], { cwd: root, maxBuffer: 4 * 1024 * 1024 });
  for (const architecture of ['amd64', 'i686']) {
    const { stdout } = await exec('wine', [resolve(root, `.build/log-storage/storage-${architecture}.exe`)],
      { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 120000 });
    expect(stdout).toContain('rotation, active writer, UTF-8, offsets and failure passed');
  }
}, 240000);
