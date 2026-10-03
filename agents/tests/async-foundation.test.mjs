import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { beforeAll, expect, it } from 'vitest';

const exec = promisify(execFile);
const agentsRoot = resolve(import.meta.dirname, '..');
const toolchain = resolve(agentsRoot, 'scripts/windows-toolchain.sh');
const architectures = ['amd64', 'i686'];

beforeAll(async () => {
  await exec('make', ['-C', 'tests/async-foundation', '-j4', 'all'], {
    cwd: agentsRoot,
    maxBuffer: 4 * 1024 * 1024,
  });
}, 180_000);

it('builds cardio with the XP toolchain without post-XP runtime imports', async () => {
  for (const architecture of architectures) {
    for (const probe of ['core', 'handles', 'runtime']) {
      const { stdout } = await exec(
        toolchain,
        [
          'x86_64-w64-mingw32-objdump',
          '-p',
          `.build/async-foundation/${probe}-${architecture}.exe`,
        ],
        { cwd: agentsRoot, maxBuffer: 16 * 1024 * 1024 }
      );
      const imports = [
        ...stdout.matchAll(/^\s+[\da-f]+\s+\d+\s+(\S+)\s*$/gmu),
      ].map((match) => match[1]);
      expect(imports).toContain('GetCurrentThreadId');
      expect(
        imports.filter((name) =>
          /^(CancelIoEx|CancelSynchronousIo|GetQueuedCompletionStatusEx|GetOverlappedResultEx|GetThreadId|GetTickCount64|InitializeCriticalSectionEx|InitializeConditionVariable|SleepConditionVariableCS|SleepConditionVariableSRW|WakeConditionVariable|WakeAllConditionVariable|InitializeSRWLock|AcquireSRWLockExclusive|ReleaseSRWLockExclusive|InitOnceExecuteOnce|WaitOnAddress)$/u.test(
            name
          )
        )
      ).toEqual([]);
      const libraries = [...stdout.matchAll(/DLL Name: (\S+)/gu)].map((match) =>
        match[1].toLowerCase()
      );
      expect(
        libraries.filter(
          (name) =>
            ![
              'kernel32.dll',
              'msvcrt.dll',
              'user32.dll',
              'ws2_32.dll',
            ].includes(name)
        )
      ).toEqual([]);
    }
  }
}, 60_000);

it('keeps sockets independent and completes cancellation before reuse under a nested Win32 loop', async () => {
  for (const architecture of architectures) {
    for (const probe of ['core', 'handles', 'runtime']) {
      const { stdout } = await exec(
        'wine',
        [
          resolve(
            agentsRoot,
            `.build/async-foundation/${probe}-${architecture}.exe`
          ),
        ],
        { env: { ...process.env, WINEDEBUG: '-all' }, timeout: 90_000 }
      );
      if (probe === 'runtime')
        expect(stdout).toContain('nested message loop passed');
    }
  }
}, 180_000);
