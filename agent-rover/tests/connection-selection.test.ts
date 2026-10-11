// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { afterEach, expect, it, vi } from 'vitest';
import { selectConnection } from '../src/driver/selection';
import type { ConnectionSection } from '../src/driver/configuration';

const section: ConnectionSection = {
  name: 'test',
  endpoints: ['a', 'b', 'c'].map((host, index) => ({
    host,
    port: 100 + index,
    authToken: `token-${host}`,
  })),
  redact: (text) => text.replace(/token-[abc]/gu, '[redacted]'),
};
afterEach(() => {
  vi.restoreAllMocks();
  vi.useRealTimers();
});

it('shuffles whole pairs, advances without delays, and stops at the first success', async () => {
  vi.useFakeTimers();
  vi.spyOn(Math, 'random').mockReturnValue(0);
  const attempt = vi.fn(async (endpoint) => {
    if (endpoint.host !== 'c')
      throw Object.assign(new Error('busy'), { code: 'AGENT_BUSY' });
    return 'connected';
  });
  const onRetry = vi.fn();
  await expect(
    selectConnection(section, { retry: false, onRetry }, attempt)
  ).resolves.toBe('connected');
  expect(attempt.mock.calls.map(([endpoint]) => endpoint)).toEqual([
    section.endpoints[1],
    section.endpoints[2],
  ]);
  expect(section.endpoints.map(({ host }) => host)).toEqual(['a', 'b', 'c']);
  expect(onRetry).not.toHaveBeenCalled();
  expect(vi.getTimerCount()).toBe(0);
});

it('waits once per failed round and shuffles again before the next round', async () => {
  vi.useFakeTimers();
  vi.spyOn(Math, 'random')
    .mockReturnValueOnce(0)
    .mockReturnValueOnce(0)
    .mockReturnValue(0.99);
  const attempt = vi.fn(async () => {
    if (attempt.mock.calls.length <= 3)
      throw Object.assign(new Error('failed'), {
        code: 'AUTHENTICATION_FAILED',
      });
    return 'connected';
  });
  const onRetry = vi.fn();
  const connecting = selectConnection(
    section,
    { retry: { maxRetries: 1, intervalMs: 250 }, onRetry },
    attempt
  );
  await vi.advanceTimersByTimeAsync(0);
  expect(attempt).toHaveBeenCalledTimes(3);
  expect(onRetry).toHaveBeenCalledTimes(1);
  await vi.advanceTimersByTimeAsync(249);
  expect(attempt).toHaveBeenCalledTimes(3);
  await vi.advanceTimersByTimeAsync(1);
  await expect(connecting).resolves.toBe('connected');
  expect(attempt).toHaveBeenNthCalledWith(4, section.endpoints[0]);
  expect(vi.getTimerCount()).toBe(0);
});

it.each([
  'AGENT_BUSY',
  'CONNECTION_FAILED',
  'AUTHENTICATION_FAILED',
  'HANDSHAKE_FAILED',
])(
  'tries every candidate even with retries disabled after %s',
  async (code) => {
    const onRetry = vi.fn();
    const attempt = vi.fn(async () => {
      throw Object.assign(new Error('failed token-a'), { code });
    });
    await expect(
      selectConnection(section, { retry: false, onRetry }, attempt)
    ).rejects.toMatchObject({
      code,
      message: expect.stringContaining('[redacted]'),
    });
    expect(attempt).toHaveBeenCalledTimes(3);
    expect(onRetry).not.toHaveBeenCalled();
  }
);

it('reports every endpoint failure at exhaustion without exposing credentials', async () => {
  vi.useFakeTimers();
  const onRetry = vi.fn();
  const attempt = vi.fn(async ({ host, authToken }) => {
    throw Object.assign(new Error(`failure ${authToken}`), {
      code: host === 'a' ? 'AGENT_BUSY' : 'CONNECTION_FAILED',
    });
  });
  const rejected = expect(
    selectConnection(
      section,
      { retry: { maxRetries: 2, intervalMs: 10 }, onRetry },
      attempt
    )
  ).rejects.toMatchObject({
    code: 'CONNECTION_FAILED',
    message: expect.stringMatching(/a:100.*|b:101.*|c:102/u),
  });
  await vi.advanceTimersByTimeAsync(20);
  await rejected;
  expect(attempt).toHaveBeenCalledTimes(9);
  expect(onRetry).toHaveBeenCalledTimes(2);
  for (const [message] of onRetry.mock.calls) {
    for (const address of ['a:100', 'b:101', 'c:102'])
      expect(message).toContain(address);
    expect(message).not.toContain('token-');
  }
});
