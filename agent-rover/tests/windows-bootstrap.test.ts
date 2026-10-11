// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { once } from 'node:events';
import { createServer, type Socket } from 'node:net';
import { afterEach, expect, it, vi } from 'vitest';
import {
  createTcpFrameDecoder,
  encodeTcpFrame,
  tcpFrameKindJson,
} from '../src/driver/tcp-frame';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';

afterEach(() => vi.unstubAllEnvs());

it.each([false, true])(
  'deploys through the configured bootstrap port after a busy rejection: %s',
  async (busy) => {
    const sockets: Socket[] = [];
    const server = createServer((socket) => {
      sockets.push(socket);
      if (busy && sockets.length === 1) {
        socket.end(
          encodeTcpFrame({
            kind: tcpFrameKindJson,
            payload: Buffer.from(
              JSON.stringify({
                kind: 'event',
                name: 'agent.rejected',
                data: {
                  code: 'AGENT_BUSY',
                  message: 'Another driver owns the desktop.',
                },
              })
            ),
          })
        );
        return;
      }
      socket.write(
        encodeTcpFrame({
          kind: tcpFrameKindJson,
          payload: Buffer.from(
            JSON.stringify({ kind: 'event', name: 'agent.ready', data: {} })
          ),
        })
      );
      const decoder = createTcpFrameDecoder({ maxPayloadBytes: 4096 });
      socket.on('data', (bytes) => {
        for (const frame of decoder.accept(bytes)) {
          if (frame.kind !== tcpFrameKindJson) continue;
          const request = JSON.parse(frame.payload.toString());
          socket.write(
            encodeTcpFrame({
              kind: tcpFrameKindJson,
              payload: Buffer.from(
                JSON.stringify({
                  kind: 'response',
                  id: request.id,
                  ok: true,
                  result: request.params,
                })
              ),
            })
          );
        }
      });
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const address = server.address();
    if (address === null || typeof address === 'string')
      throw new Error('Expected a TCP listener.');
    vi.stubEnv('AGENT_ROVER_WIN11_HOST', '127.0.0.1');
    vi.stubEnv('AGENT_ROVER_WIN11_TOKEN', 'bootstrap-test-token');
    vi.stubEnv('AGENT_ROVER_WIN11_PORT', String(address.port));
    try {
      const bootstrap = await connectWindowsBootstrap();
      try {
        expect(await bootstrap.request('fixture.echo', { value: 42 })).toEqual({
          value: 42,
        });
        expect(sockets).toHaveLength(busy ? 2 : 1);
      } finally {
        await bootstrap.close();
      }
    } finally {
      for (const socket of sockets) socket.destroy();
      const closed = once(server, 'close');
      server.close();
      await closed;
    }
  }
);
