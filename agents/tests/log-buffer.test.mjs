import { execFile } from 'node:child_process';
import { mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
it('retains only the latest 1000 bounded records and delivers logs without console output', async () => {
  await mkdir(resolve(root, '.build/log-buffer'), { recursive: true });
  await exec(
    'g++',
    [
      '-std=c++20',
      '-Iwindows',
      'tests/log-buffer.cpp',
      'windows/agent_log.cpp',
      '-o',
      '.build/log-buffer/test',
    ],
    { cwd: root }
  );
  const { stdout, stderr } = await exec(
    resolve(root, '.build/log-buffer/test')
  );
  expect(stdout).toBe('');
  expect(stderr).toBe('');
});
