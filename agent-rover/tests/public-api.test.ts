// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import { connectRemoteAgent, packageName } from '../src/index';
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
});
