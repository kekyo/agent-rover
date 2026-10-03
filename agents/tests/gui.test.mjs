import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
it('operates the actual log controls with bounded rows, resizing and tray commands', async () => {
  await exec('make', ['version-header'], { cwd: root });
  await exec('make', ['-C', 'tests/gui', '-j4', 'all'], {
    cwd: root,
    maxBuffer: 4 * 1024 * 1024,
  });
  for (const architecture of ['amd64', 'i686']) {
    const { stdout } = await exec(
      'wine',
      [resolve(root, `.build/gui/viewer-${architecture}.exe`)],
      { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 60000 }
    );
    expect(stdout).toContain('hide/reopen, exit passed');
  }
}, 180000);
