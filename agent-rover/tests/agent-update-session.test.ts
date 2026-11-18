// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Recovery and admission on the originally selected endpoint
// Copyright (c) Kouji Matsui. Under MIT.
import { randomUUID } from 'node:crypto';
import { expect, it } from 'vitest';
import { connectAgentForUpdate } from '../src/driver/connection.ts';
import {
  defaultFakeCapabilities,
  startFakeTcpAgent,
} from './helpers/fake-tcp-agent.ts';
const release = {
  version: '1.0.1',
  gitCommit: 'f'.repeat(40),
  architecture: 'amd64' as const,
  sha256: 'a'.repeat(64),
  executable: Buffer.from('fixture'),
  updateProtocol: 1,
};
it.each([true, false])(
  'queries the same endpoint after a lost final response (recorded=%s)',
  async (recorded) => {
    const id = randomUUID(),
      source = 'b'.repeat(64),
      requests: string[] = [];
    const outcome = {
      updateId: id,
      sha256: release.sha256,
      sourceSha256: source,
      status: 'applied',
      actualVersion: '1.0.1',
      message: '',
    };
    const fake = await startFakeTcpAgent({
      capabilities: {
        ...defaultFakeCapabilities,
        agentVersion: '1.0.0',
        architecture: 'amd64',
        features: ['agent.update.v1'],
      },
      disconnectAfterRequest: ['agent.update.apply'],
      respond: (method) => {
        requests.push(method);
        return method === 'agent.update.status' && recorded ? outcome : null;
      },
    });
    try {
      const session = await connectAgentForUpdate(
        {
          kind: 'explicit',
          endpoint: { host: fake.host, port: fake.port, authToken: undefined },
          redact: (text) => text,
        },
        { retry: false }
      );
      try {
        const result = await session.install(id, source, release);
        expect(result.status).toBe(recorded ? 'applied' : 'unconfirmed');
        expect(requests).toEqual([
          'agent.update.prepare',
          'agent.update.apply',
          'agent.update.status',
        ]);
      } finally {
        session.release();
      }
    } finally {
      await fake.close();
    }
  }
);
it('returns the recorded transaction without sending another apply', async () => {
  const id = randomUUID(),
    source = 'b'.repeat(64),
    methods: string[] = [];
  const fake = await startFakeTcpAgent({
    respond: (method) => {
      methods.push(method);
      return {
        updateId: id,
        sha256: release.sha256,
        sourceSha256: source,
        status: 'applied',
        actualVersion: release.version,
        message: '',
      };
    },
  });
  try {
    const s = await connectAgentForUpdate(
      {
        kind: 'explicit',
        endpoint: { host: fake.host, port: fake.port, authToken: undefined },
        redact: (t) => t,
      },
      { retry: false }
    );
    try {
      expect((await s.install(id, source, release)).status).toBe('applied');
      expect(methods).toEqual(['agent.update.prepare']);
    } finally {
      s.release();
    }
  } finally {
    await fake.close();
  }
});

it('waits for a busy selected endpoint even when another section member is available', async () => {
  const source = 'b'.repeat(64),
    id = randomUUID();
  const start = async () => {
    let prepares = 0,
      applies = 0;
    const fake = await startFakeTcpAgent({
      capabilities: {
        ...defaultFakeCapabilities,
        agentInstanceId: 'fixture',
        features: ['agent.update.v1'],
      },
      beforeRequest: (method) =>
        method === 'agent.update.prepare' && ++prepares === 1
          ? { code: 'AGENT_BUSY', message: 'Active test' }
          : undefined,
      respond: (method) => {
        if (method === 'agent.update.apply') {
          applies++;
          return {
            updateId: id,
            sourceSha256: source,
            sha256: release.sha256,
            status: 'applied',
            actualVersion: release.version,
            message: '',
          };
        }
        return null;
      },
    });
    return { fake, counts: () => [prepares, applies] };
  };
  const one = await start(),
    two = await start();
  const endpoints = [one, two].map(({ fake }) => ({
    host: fake.host,
    port: fake.port,
    authToken: undefined,
  }));
  try {
    const s = await connectAgentForUpdate(
      {
        kind: 'section',
        section: { name: 'test', endpoints, redact: (t) => t },
        redact: (t) => t,
      },
      { retry: { maxRetries: 1, intervalMs: 0 }, onRetry: () => {} }
    );
    try {
      expect((await s.install(id, source, release)).status).toBe('applied');
      expect(
        s.endpoint.port === one.fake.port
          ? [one.counts(), two.counts()]
          : [two.counts(), one.counts()]
      ).toEqual([
        [2, 1],
        [0, 0],
      ]);
    } finally {
      s.release();
    }
  } finally {
    await one.fake.close();
    await two.fake.close();
  }
});

it('binds the requested SHA-256 to the authenticated session and update ID', async () => {
  const { createHmac } = await import('node:crypto');
  const id = randomUUID(),
    token = 'session-credential',
    instance = 'same-server',
    source = 'b'.repeat(64);
  let authorization: unknown;
  const fake = await startFakeTcpAgent({
    authToken: token,
    capabilities: { ...defaultFakeCapabilities, agentInstanceId: instance },
    respond: (method, params) => {
      if (
        method === 'agent.update.prepare' &&
        typeof params === 'object' &&
        params !== null &&
        'authorization' in params
      )
        authorization = params.authorization;
      return method === 'agent.update.apply'
        ? {
            updateId: id,
            sourceSha256: source,
            sha256: release.sha256,
            status: 'applied',
            actualVersion: release.version,
            message: '',
          }
        : null;
    },
  });
  try {
    const s = await connectAgentForUpdate(
      {
        kind: 'explicit',
        endpoint: { host: fake.host, port: fake.port, authToken: token },
        redact: (t) => t,
      },
      { retry: false }
    );
    try {
      await s.install(id, source, release);
      expect(authorization).toBe(
        createHmac('sha256', token)
          .update(
            [
              'agent-rover/update/v1',
              instance,
              id,
              source,
              release.version,
              release.architecture,
              release.sha256,
              String(release.executable.length),
              '1',
            ].join('\n')
          )
          .digest('base64url')
      );
    } finally {
      s.release();
    }
  } finally {
    await fake.close();
  }
});
