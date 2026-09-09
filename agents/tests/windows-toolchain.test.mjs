import { execFile } from 'node:child_process';
import {
  copyFile,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from 'node:fs/promises';
import { resolve } from 'node:path';
import { promisify } from 'node:util';

import { expect, it } from 'vitest';

const exec = promisify(execFile);
const agentsRoot = resolve(import.meta.dirname, '..');
const toolchain = resolve(agentsRoot, 'scripts/windows-toolchain.sh');

it('requires preparation after the image definition changes', async () => {
  const buildDirectory = resolve(agentsRoot, '.build');
  await mkdir(buildDirectory, { recursive: true });
  const temporary = await mkdtemp(resolve(buildDirectory, 'toolchain test-'));
  try {
    await mkdir(resolve(temporary, 'agents/scripts'), { recursive: true });
    await mkdir(resolve(temporary, 'agents/toolchain'), { recursive: true });
    const launcher = resolve(temporary, 'agents/scripts/windows-toolchain.sh');
    await copyFile(toolchain, launcher);
    const definition = await readFile(
      resolve(agentsRoot, 'toolchain/Containerfile'),
      'utf8'
    );
    await writeFile(
      resolve(temporary, 'agents/toolchain/Containerfile'),
      `${definition}\n# Changed definition: ${temporary}\n`
    );
    await expect(
      exec(launcher, ['x86_64-w64-mingw32-g++-win32', '--version'], {
        cwd: temporary,
      })
    ).rejects.toMatchObject({
      code: 1,
      stderr: expect.stringContaining('Run ./prereq.sh'),
    });
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
}, 30_000);

it('reuses the prepared image and selects GCC 12 with the Win32 thread model', async () => {
  const { stdout: image } = await exec(toolchain, ['--image']);
  const inspect = async () =>
    (
      await exec('podman', [
        'image',
        'inspect',
        '--format',
        '{{.Id}}',
        image.trim(),
      ])
    ).stdout;
  const original = await inspect();
  for (let index = 0; index < 2; index += 1) {
    const result = await exec(resolve(agentsRoot, '../prereq.sh'));
    expect(result.stdout).toContain('Reusing Windows toolchain:');
    expect(await inspect()).toBe(original);
  }
  for (const target of ['x86_64', 'i686']) {
    const { stderr } = await exec(toolchain, [
      `${target}-w64-mingw32-g++-win32`,
      '-v',
    ]);
    expect(stderr).toContain('Thread model: win32');
    expect(stderr).toContain('gcc version 12-win32');
  }
}, 30_000);

it('builds standalone agents without known post-XP runtime imports', async () => {
  await exec('make', ['-j4'], { cwd: agentsRoot, maxBuffer: 8 * 1024 * 1024 });
  for (const architecture of ['amd64', 'i686']) {
    const { stdout } = await exec(
      toolchain,
      ['x86_64-w64-mingw32-objdump', '-p', `dist/agent-${architecture}.exe`],
      {
        cwd: agentsRoot,
      }
    );
    const libraries = [...stdout.matchAll(/DLL Name: (\S+)/gu)].map((match) =>
      match[1].toLowerCase()
    );
    expect(libraries.length).toBeGreaterThan(0);
    expect(
      libraries.filter(
        (name) =>
          ![
            'advapi32.dll',
            'gdi32.dll',
            'kernel32.dll',
            'msvcrt.dll',
            'ole32.dll',
            'psapi.dll',
            'user32.dll',
            'ws2_32.dll',
          ].includes(name)
      )
    ).toEqual([]);
    // Inspect PE imports, not strings: optional APIs may still be loaded dynamically.
    const imports = [
      ...stdout.matchAll(/^\s+[\da-f]+\s+\d+\s+(\S+)\s*$/gmu),
    ].map((match) => match[1]);
    expect(imports).toContain('GetCurrentThreadId');
    expect(
      imports.filter((name) =>
        /^(GetThreadId|GetTickCount64|InitializeCriticalSectionEx|InitializeConditionVariable|SleepConditionVariableCS|SleepConditionVariableSRW|WakeConditionVariable|WakeAllConditionVariable|InitializeSRWLock|AcquireSRWLockExclusive|ReleaseSRWLockExclusive|InitOnceExecuteOnce|GetDpiForWindow|GetDpiForMonitor|SetThreadDpiAwarenessContext)$/.test(
          name
        )
      )
    ).toEqual([]);
  }
}, 180_000);
