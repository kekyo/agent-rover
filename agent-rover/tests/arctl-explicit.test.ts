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

it.each(['flags', 'environment'])(
  'connects using legacy %s despite invalid local and home configuration',
  async (source) => {
    const token = 'explicit-secret-token';
    const fake = await startFakeTcpAgent({
      authToken: token,
      windows: [{ ...defaultFakeWindow, title: `Window ${token}` }],
    });
    const fixture = await createConfigurationFixture(
      'invalid home configuration'
    );
    try {
      await writeFile(
        join(fixture.workingDirectory, '.agent-rover'),
        'invalid local configuration'
      );
      const result = await exec(
        process.execPath,
        [
          executable,
          'windows',
          '--json',
          ...(source === 'flags'
            ? [
                '--host',
                fake.host,
                '--port',
                String(fake.port),
                '--token',
                token,
              ]
            : []),
        ],
        {
          cwd: fixture.workingDirectory,
          env: {
            ...fixture.environment,
            AGENT_ROVER_HOST:
              source === 'environment' ? fake.host : 'ignored.invalid',
            AGENT_ROVER_PORT:
              source === 'environment' ? String(fake.port) : 'invalid',
            AGENT_ROVER_AUTH_TOKEN:
              source === 'environment' ? token : 'ignored-token',
          },
          timeout: 10000,
        }
      );
      expect(JSON.parse(result.stdout)).toMatchObject({
        command: 'windows',
        result: [{ title: 'Window [redacted]' }],
      });
      expect(result.stderr).toBe('');
    } finally {
      await fake.close();
      await fixture.close();
    }
  }
);

it('uses a named section even when legacy environment defaults are present', async () => {
  const fake = await startFakeTcpAgent({ authToken: 'configured-token' });
  const fixture = await createConfigurationFixture(
    `[ci.windows]\n${fake.host}:${fake.port}=configured-token\n`
  );
  try {
    const result = await exec(
      process.execPath,
      [
        executable,
        'windows',
        '--section',
        'ci.windows',
        '--json',
        '--no-retry',
      ],
      {
        cwd: fixture.workingDirectory,
        env: {
          ...fixture.environment,
          AGENT_ROVER_HOST: 'ignored.invalid',
          AGENT_ROVER_PORT: 'invalid',
          AGENT_ROVER_AUTH_TOKEN: 'ignored-token',
        },
        timeout: 10000,
      }
    );
    expect(JSON.parse(result.stdout)).toMatchObject({
      command: 'windows',
      result: [{ title: defaultFakeWindow.title }],
    });
  } finally {
    await fake.close();
    await fixture.close();
  }
});

it.each(['--host', '--port', '--token'])(
  'reports a section/%s conflict as argument status 2',
  async (option) => {
    const fixture = await createConfigurationFixture(
      'invalid home configuration'
    );
    try {
      await expect(
        exec(
          process.execPath,
          [executable, 'windows', '--section', 'default', option, 'SECRET'],
          {
            cwd: fixture.workingDirectory,
            env: fixture.environment,
            timeout: 10000,
          }
        )
      ).rejects.toMatchObject({
        code: 2,
        stdout: '',
        stderr: expect.stringContaining('--section cannot be combined'),
      });
    } finally {
      await fixture.close();
    }
  }
);

it.each(
  [['--token', 'SECRET'], ['--token=SECRET']].map((tokenArgs) => ({
    tokenArgs,
  }))
)(
  'redacts credentials even when argument parsing fails: $tokenArgs',
  async ({ tokenArgs }) => {
    const fixture = await createConfigurationFixture(
      'invalid home configuration'
    );
    try {
      await expect(
        exec(
          process.execPath,
          [executable, ...tokenArgs, 'windows', '--unknown=SECRET'],
          {
            cwd: fixture.workingDirectory,
            env: fixture.environment,
            timeout: 10000,
          }
        )
      ).rejects.toMatchObject({
        code: 2,
        stderr: expect.not.stringContaining('SECRET'),
      });
    } finally {
      await fixture.close();
    }
  }
);

it('returns an explicit authentication failure without falling back or retrying', async () => {
  const fake = await startFakeTcpAgent({ authToken: 'correct-token' });
  const fixture = await createConfigurationFixture(
    `[default]\n${fake.host}:${fake.port}=correct-token\n`
  );
  try {
    await expect(
      exec(
        process.execPath,
        [
          executable,
          'windows',
          '--host',
          fake.host,
          '--port',
          String(fake.port),
          '--token',
          'wrong-token',
          '--retry-count',
          '2',
          '--retry-interval',
          '0',
        ],
        {
          cwd: fixture.workingDirectory,
          env: fixture.environment,
          timeout: 10000,
        }
      )
    ).rejects.toMatchObject({
      code: 1,
      stdout: '',
      stderr: expect.stringContaining('AUTHENTICATION_FAILED'),
    });
  } finally {
    await fake.close();
    await fixture.close();
  }
});
