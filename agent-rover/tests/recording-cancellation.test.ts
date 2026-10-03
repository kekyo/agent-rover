// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createDeferred } from 'async-primitives';
import { expect, it, vi } from 'vitest';
import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

it.each(['release', 'disconnect'] as const)(
  'ends an acknowledged recording wait on %s',
  async (mode) => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    const started = createDeferred<void>();
    const requests: string[] = [];
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) => {
        requests.push(method);
        if (method === 'agent.recordVideo') started.resolve();
        return undefined;
      },
    });
    const agent = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    let outcome: unknown;
    let closed = false;
    const recording = (async () => {
      try {
        outcome = await agent.recordVideo(600000);
      } catch (error) {
        outcome = error;
      }
    })();
    try {
      await started.promise;
      // A later response on the same ordered connection proves that the start
      // response was consumed. The recording is now waiting for its duration.
      await agent.capabilities();
      if (mode === 'release') agent.release();
      else {
        await fake.close();
        closed = true;
        await expect(agent.capabilities()).rejects.toMatchObject({
          code: 'DISCONNECTED',
        });
      }
      await vi.advanceTimersByTimeAsync(0);
      expect(outcome).toMatchObject({ code: 'DISCONNECTED' });
      expect(
        requests.filter((method) => method === 'agent.recordVideo')
      ).toHaveLength(1);
      expect(requests).not.toContain('video.result');
    } finally {
      agent.release();
      if (!closed) await fake.close();
      await vi.runAllTimersAsync();
      await recording;
      vi.useRealTimers();
    }
  }
);
