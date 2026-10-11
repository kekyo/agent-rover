// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { readFile } from 'node:fs/promises';
import { afterEach, expect, it, vi } from 'vitest';
import {
  connectRemoteAgent,
  type ConnectRemoteAgentOptions,
} from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

vi.mock('node:fs/promises', async (original) => ({
  ...(await original<typeof import('node:fs/promises')>()),
  readFile: vi.fn(async () => {
    throw new Error(
      'Configuration reads are forbidden in explicit connections.'
    );
  }),
}));
afterEach(() => {
  vi.clearAllMocks();
  vi.unstubAllEnvs();
});

it.each(['argument', 'environment', 'unauthenticated', 'empty'])(
  'connects with legacy host/port/authToken settings without reading configuration (%s)',
  async (source) => {
    const authenticated = source === 'argument' || source === 'environment';
    const fake = await startFakeTcpAgent(
      authenticated ? { authToken: 'explicit-token' } : {}
    );
    vi.stubEnv(
      'AGENT_ROVER_AUTH_TOKEN',
      source === 'environment'
        ? 'explicit-token'
        : source === 'unauthenticated'
          ? undefined
          : 'ignored-environment-token'
    );
    try {
      const agent = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
        retry: false,
        ...(source === 'argument'
          ? { authToken: 'explicit-token' }
          : source === 'empty'
            ? { authToken: '' }
            : {}),
      });
      try {
        expect((await agent.capabilities()).platform).toBe('windows');
      } finally {
        agent.release();
      }
      expect(readFile).not.toHaveBeenCalled();
    } finally {
      await fake.close();
    }
  }
);

it.each([{ host: 'localhost' }, { port: 39397 }, { authToken: 'SECRET' }])(
  'rejects combining a section with explicit options before reading files: %j',
  async (explicit) => {
    await expect(
      connectRemoteAgent({ section: 'default', ...explicit })
    ).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
      message: 'section cannot be combined with host, port, or authToken.',
    });
    expect(readFile).not.toHaveBeenCalled();
  }
);

it.each([
  { host: 'localhost' },
  { port: 39397 },
  { authToken: 'SECRET' },
  { host: '', port: 39397 },
  { host: 'localhost', port: 0 },
  { host: 'localhost', port: 65536 },
  { host: 'localhost', port: 1.5 },
  { host: 'localhost', port: 39397, authToken: 123 },
])(
  'rejects incomplete or invalid explicit settings without reading configuration: %j',
  async (options) => {
    await expect(
      connectRemoteAgent(options as unknown as ConnectRemoteAgentOptions)
    ).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
    expect(readFile).not.toHaveBeenCalled();
  }
);
