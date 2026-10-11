import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { beforeAll, expect, it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
beforeAll(async () => {
  await exec('make', ['-j4'], { cwd: root, maxBuffer: 8 * 1024 * 1024 });
  await exec('make', ['-C', 'tests/gui', '-j4', 'all'], {
    cwd: root,
    maxBuffer: 4 * 1024 * 1024,
  });
}, 180000);

it.each(
  ['amd64', 'i686'].flatMap((architecture) =>
    ['viewer', 'copy', 'icon', 'no-auth'].map((mode) => [architecture, mode])
  )
)(
  'operates the actual %s viewer in %s mode',
  async (architecture, mode) => {
    const { stdout } = await exec(
      'wine',
      [
        resolve(root, `.build/gui/viewer-${architecture}.exe`),
        mode,
        `dist\\agent-${architecture}.exe`,
      ],
      { cwd: root, env: { ...process.env, WINEDEBUG: '-all' }, timeout: 60000 }
    );
    expect(stdout).toContain('passed');
  },
  180000
);
