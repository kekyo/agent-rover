// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { createConfigurationFixture } from './helpers/configuration-fixture';
import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

const exec = promisify(execFile);
const executable = fileURLToPath(new URL('../dist/arctl.mjs', import.meta.url));

it.each(['default', 'ci.windows'])(
  'authenticates with the local %s section from the invocation directory',
  async (name) => {
    const fake = await startFakeTcpAgent({ authToken: 'local-token' });
    const fixture = await createConfigurationFixture(
      `[${name}]\n${fake.host}:${fake.port}=wrong-home-token\n`
    );
    try {
      await writeFile(
        join(fixture.workingDirectory, '.agent-rover'),
        `[${name}]\n${fake.host}:${fake.port}=local-token\n`
      );
      const result = await exec(
        process.execPath,
        [
          executable,
          'windows',
          '--json',
          '--no-retry',
          ...(name === 'default' ? [] : ['--section', name]),
        ],
        {
          cwd: fixture.workingDirectory,
          env: fixture.environment,
          timeout: 10000,
        }
      );
      expect(JSON.parse(result.stdout)).toMatchObject({
        command: 'windows',
        result: [{ title: defaultFakeWindow.title }],
      });
      expect(result.stderr).toBe('');
    } finally {
      await fake.close();
      await fixture.close();
    }
  }
);

it('uses the same named home section when only another local section exists', async () => {
  const fake = await startFakeTcpAgent({ authToken: 'home-token' });
  const fixture = await createConfigurationFixture(
    `[ci.windows]\n${fake.host}:${fake.port}=home-token\n`
  );
  try {
    await writeFile(
      join(fixture.workingDirectory, '.agent-rover'),
      `[default]\n${fake.host}:${fake.port}=wrong-local-token\n`
    );
    const result = await exec(
      process.execPath,
      [
        executable,
        'windows',
        '--json',
        '--no-retry',
        '--section',
        'ci.windows',
      ],
      {
        cwd: fixture.workingDirectory,
        env: fixture.environment,
        timeout: 10000,
      }
    );
    expect(JSON.parse(result.stdout)).toMatchObject({
      command: 'windows',
      result: [{ title: defaultFakeWindow.title }],
    });
    expect(result.stderr).toBe('');
  } finally {
    await fake.close();
    await fixture.close();
  }
});

it.each(['[default]\n', '[default]\ninvalid SECRET\n'])(
  'exits with configuration status 2 for invalid local settings: %j',
  async (text) => {
    const fake = await startFakeTcpAgent({});
    const fixture = await createConfigurationFixture(
      `[default]\n${fake.host}:${fake.port}=\n`
    );
    try {
      const path = join(fixture.workingDirectory, '.agent-rover');
      await writeFile(path, text);
      await expect(
        exec(
          process.execPath,
          [executable, 'windows', '--json', '--no-retry'],
          {
            cwd: fixture.workingDirectory,
            env: fixture.environment,
            timeout: 10000,
          }
        )
      ).rejects.toMatchObject({
        code: 2,
        stdout: '',
        stderr: expect.stringContaining(path),
      });
    } finally {
      await fake.close();
      await fixture.close();
    }
  }
);
