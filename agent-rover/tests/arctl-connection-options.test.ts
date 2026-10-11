// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { expect, it } from 'vitest';
import { parseArctlArguments } from '../src/cli/arguments';

it('retries busy connections 360 times at ten-second intervals by default', () => {
  expect(
    parseArctlArguments(['windows'], { AGENT_ROVER_HOST: 'localhost' }, 'test')
  ).toMatchObject({
    connection: { retry: { maxRetries: 360, intervalMs: 10000 } },
  });
});

it.each([
  { args: ['--no-retry'], retry: false },
  { args: ['--retry-count', '0'], retry: { maxRetries: 0, intervalMs: 10000 } },
  {
    args: ['--retry-count', '2', '--retry-interval', '0.125'],
    retry: { maxRetries: 2, intervalMs: 125 },
  },
  {
    args: ['--retry-interval', '0'],
    retry: { maxRetries: 360, intervalMs: 0 },
  },
  {
    args: ['--retry-interval', '1.001'],
    retry: { maxRetries: 360, intervalMs: 1001 },
  },
  {
    args: ['--retry-interval', '2147483.647'],
    retry: { maxRetries: 360, intervalMs: 2147483647 },
  },
  {
    args: ['--no-retry', '--retry-count', '2', '--retry-interval', '1'],
    retry: false,
  },
])(
  'accepts retry settings before or after the command: $args',
  ({ args, retry }) => {
    for (const invocation of [
      ['windows', ...args],
      [...args, 'windows'],
    ]) {
      expect(
        parseArctlArguments(
          invocation,
          { AGENT_ROVER_HOST: 'localhost' },
          'test'
        )
      ).toMatchObject({ connection: { retry } });
    }
  }
);

it.each([
  ['--retry-count', '-1'],
  ['--retry-count', '1.5'],
  ['--retry-count', 'NaN'],
  ['--retry-count', '9007199254740992'],
  ['--retry-count', ''],
  ['--retry-interval', '-1'],
  ['--retry-interval', '0.0001'],
  ['--retry-interval', 'Infinity'],
  ['--retry-interval', '2147483.648'],
  ['--retry-interval', ''],
])('rejects invalid retry settings %s %s', (option, value) => {
  expect(() =>
    parseArctlArguments(
      ['windows', option, value],
      { AGENT_ROVER_HOST: 'localhost' },
      'test'
    )
  ).toThrow(option);
});
