// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { expect, it } from 'vitest';
import { parseArctlArguments } from '../src/cli/arguments';

it.each(
  [
    [
      '--host',
      'machine',
      'windows',
      '--port',
      '12345',
      '--token',
      'literal=;#,',
    ],
    ['windows', '--host=machine', '--port=12345', '--token=literal=;#,'],
  ].map((args) => ({ args }))
)('restores explicit flags before and after commands: $args', ({ args }) => {
  const parsed = parseArctlArguments(
    args,
    {
      AGENT_ROVER_HOST: 'ignored',
      AGENT_ROVER_PORT: 'invalid',
      AGENT_ROVER_AUTH_TOKEN: 'ignored',
    },
    'test'
  );
  expect(parsed).toMatchObject({
    action: 'execute',
    connection: {
      host: 'machine',
      port: 12345,
      authToken: 'literal=;#,',
      timeoutMs: 30000,
    },
  });
  if (parsed.action === 'execute')
    expect(parsed.connection.section).toBeUndefined();
});

it('restores environment defaults and the legacy default port', () => {
  expect(
    parseArctlArguments(
      ['windows', '--host', 'machine'],
      { AGENT_ROVER_AUTH_TOKEN: 'env-token' },
      'test'
    )
  ).toMatchObject({
    connection: { host: 'machine', port: 39397, authToken: 'env-token' },
  });
  expect(
    parseArctlArguments(
      ['windows'],
      {
        AGENT_ROVER_HOST: 'env-host',
        AGENT_ROVER_PORT: '1234',
        AGENT_ROVER_AUTH_TOKEN: 'env-token',
      },
      'test'
    )
  ).toMatchObject({
    connection: { host: 'env-host', port: 1234, authToken: 'env-token' },
  });
});

it.each(['--host', '--port', '--token'])(
  'rejects combining --section with %s',
  (option) => {
    expect(() =>
      parseArctlArguments(
        ['windows', '--section', 'default', option, '1234'],
        {},
        'test'
      )
    ).toThrow('--section cannot be combined with --host, --port, or --token.');
  }
);

it.each([
  {
    args: ['--port', '1234'],
    message: '--host or AGENT_ROVER_HOST is required.',
  },
  {
    args: ['--token', 'token'],
    message: '--host or AGENT_ROVER_HOST is required.',
  },
  { args: ['--host', ''], message: '--host or AGENT_ROVER_HOST is required.' },
  {
    args: ['--host', '  '],
    message: '--host or AGENT_ROVER_HOST is required.',
  },
  ...['0', '65536', '1.5'].map((port) => ({
    args: ['--host', 'machine', '--port', port],
    message: '--port must be an integer from 1 through 65535.',
  })),
  {
    args: ['--host', 'machine', '--token', ''],
    message: '--token must not be empty.',
  },
])(
  'validates explicit settings without borrowing a configured endpoint: $args',
  ({ args, message }) => {
    expect(() => parseArctlArguments(['windows', ...args], {}, 'test')).toThrow(
      message
    );
  }
);

it('keeps section selection independent of legacy environment values', () => {
  expect(
    parseArctlArguments(
      ['windows', '--section', 'ci.windows'],
      {
        AGENT_ROVER_HOST: 'ignored',
        AGENT_ROVER_PORT: 'invalid',
        AGENT_ROVER_AUTH_TOKEN: 'ignored',
      },
      'test'
    )
  ).toMatchObject({ connection: { section: 'ci.windows' } });
  const parsed = parseArctlArguments(
    ['windows'],
    { AGENT_ROVER_PORT: 'invalid', AGENT_ROVER_AUTH_TOKEN: 'ignored' },
    'test'
  );
  expect(parsed).toMatchObject({ connection: { section: 'default' } });
  if (parsed.action === 'execute')
    expect(parsed.connection.host).toBeUndefined();
});

it('leaves explicit-looking arguments after the launch delimiter with the application', () => {
  const args = [
    '--host',
    'child-host',
    '--token',
    'child-token',
    '--section',
    'child-section',
  ];
  expect(
    parseArctlArguments(['launch', '--', 'C:\\app.exe', ...args], {}, 'test')
  ).toMatchObject({
    connection: { section: 'default' },
    options: { arguments: args },
  });
});
