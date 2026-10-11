// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { createServer, type Socket } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, it } from 'vitest';
import {
  createTcpFrameDecoder,
  encodeTcpFrame,
  tcpFrameKindJson,
} from '../src/driver/tcp-frame';
import { createConfigurationFixture } from './helpers/configuration-fixture';
import { defaultFakeCapabilities } from './helpers/fake-tcp-agent';

it.each(['arctl', 'arctl-json', 'vitest-forks', 'vitest-threads'])(
  'shows a busy notice before the connection can succeed (%s)',
  async (runner) => {
    const directory = await mkdtemp(
      join(tmpdir(), 'agent-rover-retry-output-')
    );
    const sockets: Socket[] = [];
    let pending: Socket | undefined;
    let observed = false;
    const ready = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from(
        JSON.stringify({
          kind: 'event',
          name: 'agent.ready',
          data: { capabilities: defaultFakeCapabilities },
        })
      ),
    });
    const server = createServer((socket) => {
      sockets.push(socket);
      if (sockets.length === 1) {
        socket.end(
          encodeTcpFrame({
            kind: tcpFrameKindJson,
            payload: Buffer.from(
              JSON.stringify({
                kind: 'event',
                name: 'agent.rejected',
                data: {
                  code: 'AGENT_BUSY',
                  message: 'Another connection is using this agent.',
                },
              })
            ),
          })
        );
        return;
      }
      // Success depends on observing output while waiting, never on elapsed time.
      pending = socket;
      if (observed) socket.write(ready);
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
                  result: [],
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
      throw new Error('Expected TCP address.');
    const notice = `No available agent in section "default". 127.0.0.1:${address.port} (AGENT_BUSY): Another connection is using this agent. Retrying in 0.01s (1/1).\n`;
    const cli = runner.startsWith('arctl');
    let args: string[];
    if (cli) {
      args = [
        fileURLToPath(new URL('../dist/arctl.mjs', import.meta.url)),
        'windows',
        '--timeout',
        '5',
        '--retry-count',
        '1',
        '--retry-interval',
        '0.01',
        ...(runner === 'arctl-json' ? ['--json'] : []),
      ];
    } else {
      await writeFile(join(directory, 'package.json'), '{"type":"module"}');
      await writeFile(
        join(directory, 'vitest.config.ts'),
        'export default { test: { globals: true } };'
      );
      await writeFile(
        join(directory, 'probe.test.ts'),
        `
import { connectRemoteAgent } from ${JSON.stringify(fileURLToPath(new URL('../dist/index.mjs', import.meta.url)))};
it('connects after a visible retry notice', async () => {
  const agent = await connectRemoteAgent({ timeoutMs: 5000, retry: { maxRetries: 1, intervalMs: 10 } });
  try { expect(await agent.windows()).toEqual([]); } finally { agent.release(); }
}, 10000);
`
      );
      args = [
        join(
          dirname(fileURLToPath(import.meta.resolve('vitest/package.json'))),
          'vitest.mjs'
        ),
        'run',
        '--config',
        join(directory, 'vitest.config.ts'),
        '--root',
        directory,
        '--pool',
        runner === 'vitest-forks' ? 'forks' : 'threads',
        '--silent=passed-only',
        '--reporter=default',
      ];
    }
    const fixture = await createConfigurationFixture(
      `[default]\n127.0.0.1:${address.port}=\n`
    );
    const child = spawn(process.execPath, args, {
      cwd: directory,
      env: fixture.environment,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    const exited = once(child, 'close');
    let stdout = '',
      stderr = '';
    const observe = (): void => {
      if (
        !observed &&
        (runner === 'arctl' ? stdout : stderr).includes(notice)
      ) {
        observed = true;
        pending?.write(ready);
      }
    };
    child.stdout.on('data', (bytes) => {
      stdout += bytes;
      observe();
    });
    child.stderr.on('data', (bytes) => {
      stderr += bytes;
      observe();
    });
    try {
      const result = await exited;
      expect(observed, `${stdout}\n${stderr}`).toBe(true);
      expect(result, `${stdout}\n${stderr}`).toEqual([0, null]);
      expect(sockets).toHaveLength(2);
      if (runner === 'arctl-json') {
        expect(JSON.parse(stdout)).toEqual({ command: 'windows', result: [] });
        expect(stderr).toBe(notice);
      } else if (runner === 'arctl') {
        expect(stdout).toBe(`${notice}ID\tPID\tPROCESS\tTITLE\n`);
        expect(stderr).toBe('');
      }
    } finally {
      if (child.exitCode === null && child.signalCode === null) {
        child.kill('SIGKILL');
        await exited;
      }
      for (const socket of sockets) socket.destroy();
      await new Promise<void>((resolve, reject) =>
        server.close((error) => (error ? reject(error) : resolve()))
      );
      await fixture.close();
      await rm(directory, { recursive: true, force: true });
    }
  },
  30000
);
