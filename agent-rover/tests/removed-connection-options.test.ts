// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { expect, it } from 'vitest';
import { parseArctlArguments } from '../src/cli/arguments';
import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

it.each(['--host', '--port', '--token'])(
  'rejects the removed CLI option %s',
  (option) => {
    expect(() =>
      parseArctlArguments(
        [
          'windows',
          '--host',
          'localhost',
          option,
          option === '--port' ? '39397' : 'value',
        ],
        'test'
      )
    ).toThrow();
  }
);

it('rejects direct endpoint options in the SDK', async () => {
  const server = await startFakeTcpAgent({});
  try {
    await expect(
      (async () => {
        const agent = await connectRemoteAgent({
          host: server.host,
          port: server.port,
        } as unknown as Parameters<typeof connectRemoteAgent>[0]);
        agent.release();
      })()
    ).rejects.toThrow();
  } finally {
    await server.close();
  }
});
