// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

describe.concurrent('file transfer and event logs', () => {
  it('writes and reads a remote file through the agent', async () => {
    const fakeAgent = await startFakeTcpAgent({});

    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile(
        'C:/agent-rover/mock-artifact.txt',
        Buffer.from('hello remote file')
      );

      expect(
        await agent.files.readFile('C:/agent-rover/mock-artifact.txt')
      ).toEqual(Buffer.from('hello remote file'));
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('manages remote filesystem entries', async () => {
    const fakeAgent = await startFakeTcpAgent({});

    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.mkdir('C:/agent-rover/work/logs', {
        recursive: true,
      });
      await agent.files.writeFile(
        'C:/agent-rover/work/logs/output.txt',
        Buffer.from('log output')
      );

      await expect(
        agent.files.exists('C:/agent-rover/work/logs/output.txt')
      ).resolves.toBe(true);
      await expect(
        agent.files.stat('C:/agent-rover/work/logs/output.txt')
      ).resolves.toMatchObject({
        size: 10,
        type: 'file',
      });
      await expect(
        agent.files.readdir('C:/agent-rover/work/logs')
      ).resolves.toMatchObject([
        {
          name: 'output.txt',
          type: 'file',
        },
      ]);

      await agent.files.rename(
        'C:/agent-rover/work/logs/output.txt',
        'C:/agent-rover/work/logs/renamed.txt'
      );
      await expect(
        agent.files.exists('C:/agent-rover/work/logs/output.txt')
      ).resolves.toBe(false);
      await expect(
        agent.files.exists('C:/agent-rover/work/logs/renamed.txt')
      ).resolves.toBe(true);

      const tempDirectory = await agent.files.mkdtemp(
        'C:/agent-rover/work/temp-'
      );
      await expect(agent.files.stat(tempDirectory)).resolves.toMatchObject({
        type: 'directory',
      });

      await agent.files.remove('C:/agent-rover/work', {
        recursive: true,
      });
      await expect(agent.files.exists('C:/agent-rover/work')).resolves.toBe(
        false
      );
      await expect(
        agent.files.remove('C:/', {
          recursive: true,
        })
      ).rejects.toMatchObject({
        code: 'PROTOCOL_ERROR',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('reads event log entries from the agent', async () => {
    const fakeAgent = await startFakeTcpAgent({
      eventLogs: [
        {
          id: 1,
          level: 'Information',
          message: 'Fake agent started.',
          provider: 'agent-rover',
          timestamp: '2026-06-25T00:00:00.000Z',
        },
      ],
    });

    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(
        agent.eventLogs.read({
          maxEntries: 1,
          source: 'agent-rover',
        })
      ).resolves.toEqual([
        {
          id: 1,
          level: 'Information',
          message: 'Fake agent started.',
          provider: 'agent-rover',
          timestamp: '2026-06-25T00:00:00.000Z',
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });
});
