// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { EventEmitter } from 'node:events';
import type { Socket } from 'node:net';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { connectRemoteAgent } from '../src/index';
import { encodeTcpFrame, tcpFrameKindJson } from '../src/driver/tcp-frame';
import { defaultFakeCapabilities } from './helpers/fake-tcp-agent';

const state = vi.hoisted(() => ({
  sockets: [] as Socket[],
  open: true,
  respond: undefined as ((socket: Socket) => void) | undefined,
}));

vi.mock('node:net', async (original) => ({
  ...(await original<typeof import('node:net')>()),
  connect: () => {
    const socket = Object.assign(new EventEmitter(), {
      destroyed: false,
      writableLength: 0,
      setNoDelay: vi.fn(),
      setTimeout: vi.fn(),
      pause: vi.fn(),
      resume: vi.fn(),
      write: vi.fn(),
      end: () => socket.destroy(),
      destroy: () => {
        if (!socket.destroyed) {
          socket.destroyed = true;
          queueMicrotask(() => socket.emit('close'));
        }
        return socket;
      },
    }) as unknown as Socket;
    state.sockets.push(socket);
    queueMicrotask(() => {
      if (state.open) socket.emit('connect');
      state.respond?.(socket);
    });
    return socket;
  },
}));

const event = (socket: Socket, name: string, data: unknown): void => {
  socket.emit(
    'data',
    encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from(JSON.stringify({ kind: 'event', name, data })),
    })
  );
};

beforeEach(() => {
  vi.useFakeTimers();
  state.sockets = [];
  state.open = true;
  state.respond = (socket) =>
    event(socket, 'agent.rejected', {
      code: 'AGENT_BUSY',
      message: 'Another connection is using this agent.',
    });
});
afterEach(() => {
  for (const socket of state.sockets) socket.destroy();
  vi.useRealTimers();
});

it.each([false, { maxRetries: 0 }])(
  'reports a busy agent without retrying when retry is %j',
  async (retry) => {
    const rejected = expect(
      connectRemoteAgent({
        host: 'test.invalid',
        port: 1,
        timeoutMs: 100,
        retry,
      })
    ).rejects.toMatchObject({
      code: 'AGENT_BUSY',
      message: 'Another connection is using this agent.',
    });
    await Promise.all([rejected, vi.advanceTimersByTimeAsync(100)]);
    expect(state.sockets).toHaveLength(1);
    expect(state.sockets[0]!.destroyed).toBe(true);
    expect(vi.getTimerCount()).toBe(0);
  }
);

it('retries at the configured interval and connects when the agent is free', async () => {
  const rejectBusy = state.respond!;
  state.respond = (socket) => {
    if (state.sockets.length < 3) rejectBusy(socket);
    else
      event(socket, 'agent.ready', { capabilities: defaultFakeCapabilities });
  };
  const connecting = connectRemoteAgent({
    host: 'test.invalid',
    port: 1,
    retry: { maxRetries: 2, intervalMs: 250 },
  });
  const observed = (async () => {
    try {
      return await connecting;
    } catch (error) {
      return error;
    }
  })();
  await vi.advanceTimersByTimeAsync(0);
  expect(state.sockets[0]!.destroyed).toBe(true);
  await vi.advanceTimersByTimeAsync(249);
  expect(state.sockets).toHaveLength(1);
  await vi.advanceTimersByTimeAsync(1);
  expect(state.sockets).toHaveLength(2);
  expect(state.sockets[1]!.destroyed).toBe(true);
  await vi.advanceTimersByTimeAsync(250);
  expect(state.sockets).toHaveLength(3);
  await expect(observed).resolves.toHaveProperty('release');
  const agent = await connecting;
  agent.release();
  await vi.advanceTimersByTimeAsync(0);
  expect(vi.getTimerCount()).toBe(0);
});

it('defaults to 360 retries at ten-second intervals, then reports AGENT_BUSY', async () => {
  const connecting = connectRemoteAgent({ host: 'test.invalid', port: 1 });
  const observed = (async () => {
    try {
      return await connecting;
    } catch (error) {
      return error;
    }
  })();
  await vi.advanceTimersByTimeAsync(0);
  expect(state.sockets).toHaveLength(1);
  await vi.advanceTimersByTimeAsync(9999);
  expect(state.sockets).toHaveLength(1);
  await vi.advanceTimersByTimeAsync(1);
  expect(state.sockets).toHaveLength(2);
  await vi.advanceTimersByTimeAsync(359 * 10000);
  await expect(observed).resolves.toMatchObject({ code: 'AGENT_BUSY' });
  expect(state.sockets).toHaveLength(361);
  expect(state.sockets.every((socket) => socket.destroyed)).toBe(true);
  expect(vi.getTimerCount()).toBe(0);
});

it.each(['network', 'authentication', 'invalid rejection'])(
  'does not retry a %s failure',
  async (failure) => {
    state.open = failure !== 'network';
    state.respond = (socket) => {
      if (failure === 'network')
        socket.emit('error', new Error('ECONNREFUSED'));
      else if (failure === 'authentication') socket.destroy();
      else
        event(socket, 'agent.rejected', {
          code: 'unknown',
          message: 'invalid',
        });
    };
    const rejected = expect(
      connectRemoteAgent({ host: 'test.invalid', port: 1, timeoutMs: 100 })
    ).rejects.toMatchObject({
      code:
        failure === 'network'
          ? 'CONNECTION_FAILED'
          : failure === 'invalid rejection'
            ? 'HANDSHAKE_FAILED'
            : 'AUTHENTICATION_FAILED',
    });
    await Promise.all([rejected, vi.advanceTimersByTimeAsync(100)]);
    expect(state.sockets).toHaveLength(1);
    expect(state.sockets[0]!.destroyed).toBe(true);
    expect(vi.getTimerCount()).toBe(0);
  }
);

it.each([
  { maxRetries: -1 },
  { maxRetries: 1.5 },
  { maxRetries: Infinity },
  { intervalMs: -1 },
  { intervalMs: NaN },
  { intervalMs: 2147483648 },
])(
  'rejects invalid retry settings before opening a socket: %j',
  async (retry) => {
    const rejected = expect(
      connectRemoteAgent({
        host: 'test.invalid',
        port: 1,
        timeoutMs: 100,
        retry,
      })
    ).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
    await Promise.all([rejected, vi.advanceTimersByTimeAsync(100)]);
    expect(state.sockets).toHaveLength(0);
  }
);
