import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

const root = resolve(import.meta.dirname, '..');
const exec = promisify(execFile);

it.each(['amd64', 'i686'])(
  'keeps the dispatcher responsive while %s socket peers repeatedly exchange data and disconnect',
  async (architecture) => {
    await exec('make', ['-C', 'tests/socket-lifetime', architecture], {
      cwd: root,
      maxBuffer: 8 * 1024 * 1024,
    });
    const result = await exec(
      'wine',
      [resolve(root, `.build/socket-lifetime/runtime-${architecture}.exe`)],
      { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 120000 }
    );
    expect(result.stdout).toContain('PASS');
  },
  180000
);
