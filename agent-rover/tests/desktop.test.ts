// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';
import { connectRemoteAgent, type RemoteMonitor } from '../src/index';
import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

const primary: RemoteMonitor = {
  id: 'primary',
  name: 'primary',
  primary: true,
  bounds: { x: 0, y: 0, width: 2560, height: 1440 },
  workArea: { x: 0, y: 0, width: 2560, height: 1400 },
  dpi: 144,
  scaleFactor: 1.5,
};
const left: RemoteMonitor = {
  id: 'left',
  name: 'left',
  primary: false,
  bounds: { x: -1920, y: -200, width: 1920, height: 1080 },
  workArea: { x: -1920, y: -160, width: 1920, height: 1040 },
  dpi: 96,
  scaleFactor: 1,
};

describe('desktop observations', () => {
  it('returns geometry and DPI together and identifies configuration changes', async () => {
    const fake = await startFakeTcpAgent({ monitors: [primary, left] });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const first = await agent.desktop();
      expect(first.bounds).toEqual({
        x: -1920,
        y: -200,
        width: 4480,
        height: 1640,
      });
      expect(first.monitors).toEqual([left, primary]);
      expect(await agent.bounds()).toEqual(first.bounds);
      fake.setMonitors([left, primary]);
      expect((await agent.desktop()).revision).toBe(first.revision);
      fake.setMonitors([left, { ...primary, dpi: 120, scaleFactor: 1.25 }]);
      expect((await agent.desktop()).revision).not.toBe(first.revision);
      fake.setMonitors([
        left,
        { ...primary, workArea: { ...primary.workArea, x: 40, width: 2520 } },
      ]);
      expect((await agent.desktop()).revision).not.toBe(first.revision);
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('preserves unavailable DPI and offscreen or unavailable monitor identity', async () => {
    const fake = await startFakeTcpAgent({
      monitors: [{ ...primary, dpi: null, scaleFactor: null }],
      windows: [
        {
          ...defaultFakeWindow,
          monitorId: null,
          dpi: null,
          dpiAwareness: null,
        },
      ],
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      expect((await agent.desktop()).monitors[0]).toMatchObject({
        dpi: null,
        scaleFactor: null,
      });
      expect((await agent.windows())[0]).toMatchObject({
        monitorId: null,
        dpi: null,
        dpiAwareness: null,
      });
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it.each([
    { dpi: 0, scaleFactor: 1 },
    { dpi: 144, scaleFactor: 1 },
    { dpi: null, scaleFactor: 1 },
    { dpi: 144, scaleFactor: null },
  ])('rejects inconsistent DPI metadata: %j', async (values) => {
    const fake = await startFakeTcpAgent({
      monitors: [{ ...primary, ...values }],
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      await expect(agent.desktop()).rejects.toMatchObject({
        code: 'PROTOCOL_ERROR',
      });
    } finally {
      agent.release();
      await fake.close();
    }
  });
});

describe('placement and diagnostics', () => {
  it('waits for the requested monitor and DPI and resets stability after a failed observation', async () => {
    const placed = {
      ...defaultFakeWindow,
      bounds: { x: 32, y: 32, width: 400, height: 300 },
      monitorId: primary.id,
      dpi: 144,
      dpiAwareness: 'per-monitor-v2' as const,
    };
    const samples = [
      { ...placed, title: 'wrong monitor', monitorId: left.id },
      { ...placed, title: 'wrong DPI', dpi: 96 },
      { ...placed, title: 'first match' },
      null,
      { ...placed, title: 'match after failure' },
      { ...placed, title: 'settled' },
    ];
    let index = 0;
    const fake = await startFakeTcpAgent({
      monitors: [primary, left],
      beforeRequest: (method) => {
        if (method !== 'window.snapshot') return undefined;
        const sample = samples[Math.min(index++, samples.length - 1)];
        if (sample === null)
          return {
            code: 'OPERATION_FAILED',
            message: 'Temporary observation failure.',
          };
        fake.setWindows([sample!]);
        return undefined;
      },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const desktop = await agent.desktop();
      const window = (await agent.windows())[0]!;
      const result = await window.waitForPlacement(
        {
          bounds: placed.bounds,
          monitorId: primary.id,
          dpi: 144,
          desktopRevision: desktop.revision,
        },
        { intervalMs: 1 }
      );
      expect(result).toMatchObject({ ...placed, title: 'settled' });
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('fails when the desktop changes between the placement observations', async () => {
    const fake = await startFakeTcpAgent({
      monitors: [primary],
      beforeRequest: (method) => {
        if (method === 'window.snapshot')
          fake.setMonitors([{ ...primary, dpi: 120, scaleFactor: 1.25 }]);
        return undefined;
      },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const revision = (await agent.desktop()).revision;
      const window = (await agent.windows())[0]!;
      await expect(
        window.waitForPlacement({
          bounds: window.bounds,
          desktopRevision: revision,
        })
      ).rejects.toMatchObject({ code: 'DESKTOP_CHANGED' });
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it.each([
    { ...defaultFakeWindow, minimized: true },
    { ...defaultFakeWindow, visible: false },
    { ...defaultFakeWindow, monitorId: null },
    { ...defaultFakeWindow, dpi: null },
  ])(
    'does not accept unavailable or undisplayed placement: %j',
    async (snapshot) => {
      const fake = await startFakeTcpAgent({ windows: [snapshot] });
      const agent = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
      });
      try {
        const window = (await agent.windows())[0]!;
        await expect(
          window.waitForPlacement(
            { bounds: window.bounds, dpi: 96 },
            { stableIterations: 1, timeoutMs: 0 }
          )
        ).rejects.toMatchObject({ code: 'TIMEOUT' });
      } finally {
        agent.release();
        await fake.close();
      }
    }
  );

  it('rejects invalid placement conditions before waiting', async () => {
    const fake = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const window = (await agent.windows())[0]!;
      for (const expected of [
        {},
        { dpi: 0 },
        { monitorId: '' },
        { bounds: { ...window.bounds, width: -1 } },
      ]) {
        await expect(window.waitForPlacement(expected)).rejects.toMatchObject({
          code: 'INVALID_ARGUMENT',
        });
      }
      await expect(
        window.waitForPlacement({ dpi: 96 }, { stableIterations: 0 })
      ).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
    } finally {
      agent.release();
      await fake.close();
    }
  });

  it('records desktop observations before and after diagnostics acquisition', async () => {
    const fake = await startFakeTcpAgent({
      monitors: [primary],
      beforeRequest: (method) => {
        if (method === 'agent.screenshot') fake.setMonitors([left, primary]);
        return undefined;
      },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const before = await agent.desktop();
      const capture = await agent.diagnostics.capture();
      const after = await agent.desktop();
      expect(before.revision).not.toBe(after.revision);
      expect(capture).toMatchObject({
        desktop: before,
        desktopAfter: after,
        bounds: before.bounds,
        monitors: before.monitors,
      });
      expect(capture.windows[0]).toMatchObject({
        dpi: 96,
        dpiAwareness: 'unaware',
        monitorId: 'monitor-1',
      });
    } finally {
      agent.release();
      await fake.close();
    }
  });
});
