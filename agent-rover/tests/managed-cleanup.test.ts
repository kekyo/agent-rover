// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';
import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

describe('managed cleanup completion', () => {
  it('keeps output alive until an in-flight completed-output read finishes', async () => {
    let allowRead = false;
    let sawRead: () => void = () => {};
    const readStarted = new Promise<void>((resolve) => {
      sawRead = resolve;
    });
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) => {
        if (method !== 'process.readCaptured' || allowRead) return undefined;
        sawRead();
        return {
          code: 'OPERATION_FAILED',
          message: 'descendant is writing',
          details: {
            operation: method,
            nativeOperation: 'QueryInformationJobObject',
            path: '',
            osCode: 170,
            reason: 'busy',
          },
        };
      },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const child = await agent.processes.launchManaged({
        path: 'test.exe',
        captureStdout: true,
      });
      const reading = child.stdoutText();
      await readStarted;
      const releasing = child.releaseAsync();
      await agent.files.exists('C:/agent-rover-managed-process-fake');
      expect(fake.managedProcessCount()).toBe(1);
      allowRead = true;
      const [output] = await Promise.all([reading, releasing]);
      expect(output).toBe('managed stdout');
      expect(fake.managedProcessCount()).toBe(0);
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(false);
    } finally {
      allowRead = true;
      agent.release();
      await fake.close();
    }
  });
  it('retains resources after native release failure and retries the unfinished release', async () => {
    let fail = true;
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === 'process.releaseManaged' && fail
          ? {
              code: 'OPERATION_FAILED',
              message: 'injected release failure',
              details: {
                operation: method,
                nativeOperation: 'CloseHandle',
                path: '',
                osCode: 6,
                reason: 'unknown',
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const child = await agent.processes.launchManaged({
        path: 'test.exe',
        captureStdout: true,
      });
      await expect(child.releaseAsync()).rejects.toMatchObject({
        code: 'OPERATION_FAILED',
      });
      expect(fake.managedProcessCount()).toBe(1);
      expect(
        await agent.files.exists(
          'C:/agent-rover-managed-process-fake/stdout.log'
        )
      ).toBe(true);
      fail = false;
      await child.releaseAsync();
      expect(fake.managedProcessCount()).toBe(0);
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(false);
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('retries only remaining directory cleanup after native resources have been released', async () => {
    let fail = true;
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === 'file.remove' && fail
          ? {
              code: 'OPERATION_FAILED',
              message: 'permanent removal failure',
              details: {
                operation: method,
                nativeOperation: 'DeleteFileW',
                path: 'C:/agent-rover-managed-process-fake/stdout.log',
                osCode: 87,
                reason: 'unknown',
              },
            }
          : undefined,
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const child = await agent.processes.launchManaged({
        path: 'test.exe',
        captureStdout: true,
      });
      await expect(child.releaseAsync()).rejects.toMatchObject({
        code: 'OPERATION_FAILED',
      });
      expect(fake.managedProcessCount()).toBe(0);
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(true);
      await expect(child.releaseAsync()).rejects.toMatchObject({
        code: 'OPERATION_FAILED',
      });
      fail = false;
      await child.releaseAsync();
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(false);
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('joins concurrent release and async disposal without double releasing resources', async () => {
    const fake = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const child = await agent.processes.launchManaged({
        path: 'test.exe',
        captureStdout: true,
      });
      await Promise.all([
        child.releaseAsync(),
        child[Symbol.asyncDispose](),
        child.releaseAsync(),
      ]);
      expect(fake.managedProcessCount()).toBe(0);
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(false);
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('recovers when native release completed but its response was lost', async () => {
    const fake = await startFakeTcpAgent({
      dropResponses: { 'process.releaseManaged': 1 },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
      timeoutMs: 100,
    });
    try {
      const child = await agent.processes.launchManaged({
        path: 'test.exe',
        captureStdout: true,
      });
      await expect(child.releaseAsync()).rejects.toMatchObject({
        code: 'TIMEOUT',
      });
      expect(fake.managedProcessCount()).toBe(0);
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(true);
      await child.releaseAsync();
      expect(
        await agent.files.exists('C:/agent-rover-managed-process-fake')
      ).toBe(false);
    } finally {
      agent.release();
      await fake.close();
    }
  });
});
