import { execFile } from 'node:child_process';
import { mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { it } from 'vitest';

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, '..');
it('bounds a stalled log writer and records dropped or unconfirmed sequences', async () => {
  await mkdir(resolve(root, '.build/log-queue'), { recursive: true });
  await exec('g++', ['-std=c++20', '-Iwindows', 'tests/log-queue.cpp',
    'windows/log_queue.cpp', '-o', '.build/log-queue/test'], { cwd: root });
  await exec(resolve(root, '.build/log-queue/test'));
});
