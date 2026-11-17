// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { expect, it } from 'vitest';
import type { RemoteProcessSnapshot } from '../src/index';
import { connectTestAgent } from './helpers/test-connection';
import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

const root: RemoteProcessSnapshot = {
  id: 4321,
  name: 'launcher.exe',
  path: 'C:/launcher.exe',
  parentProcessId: null,
  createdAt: '2026-11-17T09:00:00.000Z',
  running: false,
  exitCode: 0,
};
const unrelated: RemoteProcessSnapshot = {
  ...root,
  id: 5678,
  name: 'unrelated.exe',
  path: 'C:/unrelated.exe',
  parentProcessId: root.id,
  createdAt: '2026-11-17T17:00:00.000Z',
  running: true,
  exitCode: null,
};

it('does not include unrelated processes or their existing windows in an empty Job', async () => {
  const unrelatedWindow = { ...defaultFakeWindow, process: unrelated };
  const fake = await startFakeTcpAgent({ windows: [unrelatedWindow] });
  const agent = await connectTestAgent({ host: fake.host, port: fake.port });
  try {
    const process = await agent.processes.launchManaged({ path: root.path });
    fake.setProcesses([root, unrelated]);
    await expect(process.processes()).resolves.toEqual([]);
    await expect(process.windows()).resolves.toEqual([]);
    await expect(
      process.windows({ title: unrelatedWindow.title })
    ).resolves.toEqual([]);
    await process.releaseAsync();
  } finally {
    agent.release();
    await fake.close();
  }
});

it('does not treat a window with an exited root PID as owned', async () => {
  const fake = await startFakeTcpAgent({
    windows: [{ ...defaultFakeWindow, process: root }],
  });
  const agent = await connectTestAgent({ host: fake.host, port: fake.port });
  try {
    const process = await agent.processes.launchManaged({ path: root.path });
    fake.setProcesses([root]);
    await expect(process.windows()).resolves.toEqual([]);
    await process.releaseAsync();
  } finally {
    agent.release();
    await fake.close();
  }
});

it('returns orphaned Job members and windows without parent or creation-time inference', async () => {
  const member = {
    ...unrelated,
    parentProcessId: 9999,
    createdAt: '2026-11-16T00:00:00.000Z',
  };
  const memberWindow = { ...defaultFakeWindow, process: member };
  const fake = await startFakeTcpAgent({ windows: [memberWindow] });
  const agent = await connectTestAgent({ host: fake.host, port: fake.port });
  try {
    const process = await agent.processes.launchManaged({ path: root.path });
    fake.setProcesses([root]);
    fake.setManagedDescendants(root.id, [member]);
    await expect(process.processes()).resolves.toEqual([member]);
    await expect(process.windows()).resolves.toMatchObject([
      { id: memberWindow.id },
    ]);
    await expect(process.snapshot()).resolves.toMatchObject({ running: true });
    // Once the member exits, a recycled PID in the global list is not retained.
    fake.setManagedDescendants(root.id, []);
    fake.setProcesses([root, unrelated]);
    await expect(process.processes()).resolves.toEqual([]);
    await expect(process.windows()).resolves.toEqual([]);
    await process.releaseAsync();
  } finally {
    agent.release();
    await fake.close();
  }
});

it.each(['process.managedProcesses', 'process.managedRunning'])(
  'propagates %s failure instead of reporting an exited Job',
  async (failingMethod) => {
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) =>
        method === failingMethod
          ? { code: 'OPERATION_FAILED', message: 'Job state unavailable.' }
          : undefined,
    });
    const agent = await connectTestAgent({ host: fake.host, port: fake.port });
    try {
      const process = await agent.processes.launchManaged({ path: root.path });
      fake.setProcesses([root]);
      await expect(process.snapshot()).rejects.toThrow(
        'Job state unavailable.'
      );
      await process.releaseAsync();
    } finally {
      agent.release();
      await fake.close();
    }
  }
);
