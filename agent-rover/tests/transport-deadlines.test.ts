// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { EventEmitter } from 'node:events';
import type { Socket } from 'node:net';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { createTcpFrameTransport } from '../src/driver/transport';
import { connectRemoteAgent } from '../src/index';
import { encodeTcpFrame, tcpFrameKindJson } from '../src/driver/tcp-frame';
import { defaultFakeCapabilities } from './helpers/fake-tcp-agent';
import { protocolVersion } from '../src/protocol_version';

const state = vi.hoisted(() => ({ socket: undefined as Socket | undefined }));
vi.mock('node:net', async (original) => ({
  ...(await original<typeof import('node:net')>()),
  connect: () => state.socket!,
}));

let socket: Socket;
beforeEach(() => {
  vi.useFakeTimers();
  const events = new EventEmitter();
  socket = Object.assign(events, {
    destroyed: false,
    writableLength: 0,
    setNoDelay: vi.fn(),
    setTimeout: vi.fn(),
    pause: vi.fn(),
    resume: vi.fn(),
    write: vi.fn(() => false),
    end: vi.fn(),
    destroy: vi.fn(() => {
      Object.assign(socket, { destroyed: true });
      queueMicrotask(() => events.emit('close'));
      return socket;
    }),
  }) as unknown as Socket;
  state.socket = socket;
});
afterEach(() => {
  socket.destroy();
  vi.useRealTimers();
});
const ready = () => {
  socket.emit('connect');
  socket.emit(
    'data',
    encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from(
        JSON.stringify({
          kind: 'event',
          name: 'agent.ready',
          data: { capabilities: defaultFakeCapabilities, protocolVersion },
        })
      ),
    })
  );
};
const transport = () =>
  createTcpFrameTransport({
    host: 'test.invalid',
    port: 1,
    authToken: undefined,
    timeoutMs: 100,
    callbacks: {
      onOpen: () => {},
      onMessage: () => {},
      onBinaryChunk: async () => {},
      onClose: () => {},
      onError: () => {},
    },
  });

it('ends a send whose native write callback never completes', async () => {
  const connection = transport();
  ready();
  const rejected = expect(
    connection.send({ id: '1', kind: 'request', method: 'agent.capabilities' })
  ).rejects.toThrow(/timed out/iu);
  await vi.advanceTimersByTimeAsync(100);
  await rejected;
  expect(socket.destroyed).toBe(true);
}, 3000);

it('finishes close when a peer never finishes its half of the connection', async () => {
  const connection = transport();
  ready();
  const closing = connection.close();
  await vi.advanceTimersByTimeAsync(100);
  await closing;
  expect(socket.destroyed).toBe(true);
}, 3000);

it('closes before authentication without sending an application Close frame', async () => {
  const connection = transport();
  socket.emit('connect');
  const closing = connection.close();
  await vi.advanceTimersByTimeAsync(100);
  await closing;
  expect(socket.end).not.toHaveBeenCalled();
  expect(socket.destroyed).toBe(true);
}, 3000);

it('observes request timeout during a stalled send and disconnects the session', async () => {
  const connecting = connectRemoteAgent({
    host: 'test.invalid',
    port: 1,
    timeoutMs: 100,
  });
  ready();
  const agent = await connecting;
  const rejected = expect(agent.capabilities()).rejects.toMatchObject({
    code: 'TIMEOUT',
  });
  await vi.advanceTimersByTimeAsync(100);
  await rejected;
  await expect(agent.capabilities()).rejects.toMatchObject({
    code: 'DISCONNECTED',
  });
  await vi.advanceTimersByTimeAsync(100);
  expect(socket.destroyed).toBe(true);
}, 3000);
