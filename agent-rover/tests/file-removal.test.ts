// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';
import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

describe('bounded file removal', () => {
  it('stops retrying when restoration of a changed object fails', async () => {
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === 'file.remove'
          ? {
              code: 'OPERATION_FAILED',
              message: 'delete and rollback failed',
              details: {
                operation: method,
                nativeOperation: 'SetFileInformationByHandle',
                path: 'C:/file',
                osCode: 32,
                reason: 'sharingViolation',
                repairs: [
                  {
                    path: 'C:/file',
                    action: 'clearReadOnly',
                    outcome: 'applied',
                    osCode: 0,
                    restoration: 'failed',
                    restoreOsCode: 5,
                  },
                ],
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      await expect(agent.files.remove('C:/file')).rejects.toMatchObject({
        details: {
          attempts: 1,
          timedOut: false,
          repairs: [{ restoration: 'failed', restoreOsCode: 5 }],
        },
      });
    } finally {
      agent.release();
      await fake.close();
    }
  }, 20000);
  it('removes a file after a transient native lock is released', async () => {
    let failures = 2;
    const killed: number[] = [];
    const fake = await startFakeTcpAgent({
      killedProcessIds: killed,
      beforeRequest: (method) =>
        method === 'file.remove' && failures-- > 0
          ? {
              code: 'OPERATION_FAILED',
              message: 'localized failure',
              details: {
                operation: method,
                nativeOperation: 'DeleteFileW',
                path: 'C:/test.txt',
                osCode: 32,
                reason: 'sharingViolation',
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      await agent.files.writeFile('C:/test.txt', Buffer.from('data'));
      await agent.files.remove('C:/test.txt');
      expect(await agent.files.exists('C:/test.txt')).toBe(false);
      expect(killed).toEqual([]);
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('keeps a locked file and reports deadline exhaustion', async () => {
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === 'file.remove'
          ? {
              code: 'OPERATION_FAILED',
              message: 'locked',
              details: {
                operation: method,
                nativeOperation: 'DeleteFileW',
                path: 'C:/test.txt',
                osCode: 32,
                reason: 'sharingViolation',
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      await agent.files.writeFile('C:/test.txt', Buffer.from('data'));
      const options = { recursive: false, timeoutMs: 0 };
      await expect(
        agent.files.remove('C:/test.txt', options)
      ).rejects.toMatchObject({
        details: {
          path: 'C:/test.txt',
          osCode: 32,
          attempts: 1,
          timedOut: true,
        },
      });
      expect((await agent.files.readFile('C:/test.txt')).toString()).toBe(
        'data'
      );
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('does not retry a known read-only failure', async () => {
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === 'file.remove'
          ? {
              code: 'OPERATION_FAILED',
              message: 'win32Error=32 misleading text',
              details: {
                operation: method,
                nativeOperation: 'DeleteFileW',
                path: 'C:/test.txt',
                osCode: 5,
                reason: 'readOnly',
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      await agent.files.writeFile('C:/test.txt', Buffer.from('data'));
      await expect(agent.files.remove('C:/test.txt')).rejects.toMatchObject({
        details: { reason: 'readOnly', attempts: 1, timedOut: false },
      });
      expect(await agent.files.exists('C:/test.txt')).toBe(true);
    } finally {
      agent.release();
      await fake.close();
    }
  });
});
