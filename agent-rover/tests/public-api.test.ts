// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, expectTypeOf, it } from 'vitest';

import {
  connectRemoteAgent,
  packageName,
  type CapturedVideoResult,
  type CapturedVideoStream,
} from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

describe('public package entry', () => {
  it('exposes the package name from the library entry point', () => {
    expect(packageName).toBe('agent-rover');
  });

  it('exposes directory synchronization methods from the file system API', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      expect(agent.files.syncDirectory).toEqual(expect.any(Function));
      expect(agent.files.downloadDirectory).toEqual(expect.any(Function));
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('exposes distinct video stream and persisted-file overloads', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      expectTypeOf(() => agent.recordVideo(1)).returns.toEqualTypeOf<
        Promise<CapturedVideoStream>
      >();
      expectTypeOf(() =>
        agent.recordVideo(1, 'capture.mp4')
      ).returns.toEqualTypeOf<Promise<CapturedVideoResult>>();
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });
});
