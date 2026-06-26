// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import { toPass, waitForResult } from '../src/testing';

describe.concurrent('wait helpers', () => {
  it('retries until a probe returns a result', async () => {
    let attempts = 0;

    const result = await waitForResult(
      async () => {
        attempts += 1;
        if (attempts < 3) {
          throw new Error('not ready');
        }
        return 'ready';
      },
      { intervalMs: 1, timeoutMs: 1000 }
    );

    expect(result).toBe('ready');
    expect(attempts).toBe(3);
  });

  it('retries assertion blocks until they pass', async () => {
    let attempts = 0;

    await toPass(
      () => {
        attempts += 1;
        expect(attempts).toBeGreaterThanOrEqual(3);
      },
      { intervalMs: 1, timeoutMs: 1000 }
    );

    expect(attempts).toBe(3);
  });

  it('reports the last retryable error when the timeout expires', async () => {
    await expect(
      waitForResult(
        () => {
          throw new Error('last failure');
        },
        {
          intervalMs: 1,
          message: 'custom wait failed.',
          timeoutMs: 5,
        }
      )
    ).rejects.toMatchObject({
      code: 'TIMEOUT',
      message: expect.stringContaining('last failure'),
    });
  });

  it('does not retry non-retryable remote agent errors', async () => {
    let attempts = 0;

    await expect(
      waitForResult(
        () => {
          attempts += 1;
          throw Object.assign(new Error('bad protocol'), {
            code: 'PROTOCOL_ERROR',
          });
        },
        { intervalMs: 1, timeoutMs: 1000 }
      )
    ).rejects.toMatchObject({
      code: 'PROTOCOL_ERROR',
    });
    expect(attempts).toBe(1);
  });
});
