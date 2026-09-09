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
