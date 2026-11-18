// agent-rover - Incremental native build metadata regression
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import {
  copyFile,
  mkdir,
  mkdtemp,
  rm,
  stat,
  writeFile,
} from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';

it('updates both native banners in the first build after a commit and preserves unchanged outputs', async () => {
  const exec = promisify(execFile);
  const agents = resolve(import.meta.dirname, '..');
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-version-'));
  const screwUp = resolve(agents, '../node_modules/.bin/screw-up');
  try {
    await mkdir(join(directory, 'toolchain'));
    await mkdir(join(directory, 'windows'));
    for (const path of [
      'Makefile',
      'package.json',
      'version.h.in',
      'toolchain/toolchain.mk',
      'windows/version_banner.cpp',
      'windows/version_banner.h',
    ]) {
      await copyFile(join(agents, path), join(directory, path));
    }
    await writeFile(join(directory, '.gitignore'), '.build/\ndist/\n');
    await writeFile(
      join(directory, 'windows/driver.cpp'),
      '#include "version_banner.h"\n#include <iostream>\nint main() { std::cout << agent_rover::BuildAgentVersionText(); }\n'
    );
    await exec(
      'git',
      ['init', '--quiet', '--initial-branch=feature/version-fixture'],
      {
        cwd: directory,
      }
    );
    await exec('git', ['add', '.'], { cwd: directory });
    // Exercise the real dependency graph with a host driver; no Windows runtime
    // or consumer application is needed to observe the compiled version text.
    const build = async () =>
      await exec(
        'make',
        [
          '-j4',
          'all',
          `SCREW_UP=${screwUp}`,
          'AMD64_CXX=g++',
          'I686_CXX=g++',
          'TOOLCHAIN_DEPENDENCIES=',
          'CXXFLAGS=-std=c++20 -Wall -Wextra -Werror',
          'LDFLAGS=',
          'LDLIBS=',
          'AMD64_OBJECTS=.build/obj/amd64/version_banner.o .build/obj/amd64/driver.o',
          'I686_OBJECTS=.build/obj/i686/version_banner.o .build/obj/i686/driver.o',
        ],
        { cwd: directory }
      );
    for (const message of [
      'chore: initial fixture',
      'chore: metadata update',
    ]) {
      await exec(
        'git',
        [
          '-c',
          'user.name=agent-rover test',
          '-c',
          'user.email=agent-rover-test@example.invalid',
          '-c',
          'core.hooksPath=/dev/null',
          'commit',
          '--quiet',
          '--no-gpg-sign',
          '--allow-empty',
          '-m',
          message,
        ],
        { cwd: directory }
      );
      const expected = (
        await exec(
          screwUp,
          ['format', '--expression', '{version}-{git.commit.hash}'],
          {
            cwd: directory,
          }
        )
      ).stdout.trim();
      await build();
      for (const arch of ['amd64', 'i686']) {
        const output = await exec(join(directory, `dist/agent-${arch}.exe`));
        expect(output.stdout, `${arch}: ${message}`).toBe(expected);
      }
    }
    const outputs = ['amd64', 'i686'].map((arch) =>
      join(directory, `dist/agent-${arch}.exe`)
    );
    const before = await Promise.all(
      outputs.map(async (path) => await stat(path, { bigint: true }))
    );
    await build();
    const after = await Promise.all(
      outputs.map(async (path) => await stat(path, { bigint: true }))
    );
    expect(after.map(({ mtimeNs }) => mtimeNs)).toEqual(
      before.map(({ mtimeNs }) => mtimeNs)
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}, 30000);
