// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHmac } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { connect as connectTcpSocket, type Socket } from 'node:net';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

import {
  connectRemoteAgent,
  type AsyncReleaseable,
  type AppWindowSnapshot,
  type RemoteApplicationLaunchOptions,
  type RemoteInputOperation,
  type RemoteProcessSnapshot,
} from '../src/index';
import {
  createTcpFrameDecoder,
  encodeTcpFrame,
  tcpFrameKindAuthChallenge,
  tcpFrameKindAuthResponse,
  tcpFrameHeaderBytes,
  tcpFrameKindJson,
  tcpFrameMagic,
  tcpFrameVersion,
  type TcpFrame,
} from '../src/driver/tcp-frame';
import { authChallengePrefix, protocolVersion } from '../src/protocol_version';
import {
  defaultFakeCapabilities,
  defaultFakeChildWindow,
  defaultFakeWindow,
  type FakeManagedProcessLaunchOptions,
  startFakeTcpAgent,
} from './helpers/fake-tcp-agent';

const sendTcpProtocolMessage = (socket: Socket, message: unknown): void => {
  socket.write(
    encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from(JSON.stringify(message), 'utf8'),
    })
  );
};

const createAuthResponse = (token: string, challenge: Buffer): Buffer => {
  const hmac = createHmac('sha256', Buffer.from(token, 'utf8'));
  hmac.update(Buffer.concat([authChallengePrefix, challenge]));
  return hmac.digest();
};

const fakeProcessSnapshot = (
  overrides: Partial<RemoteProcessSnapshot> & {
    readonly id: number;
  }
): RemoteProcessSnapshot => ({
  createdAt: '2026-06-25T00:00:00.000Z',
  exitCode: null,
  id: overrides.id,
  name: `process-${String(overrides.id)}.exe`,
  parentProcessId: null,
  path: `C:/agent-rover/process-${String(overrides.id)}.exe`,
  running: true,
  ...overrides,
});

const readTcpFrame = async (socket: Socket): Promise<TcpFrame> =>
  await new Promise<TcpFrame>((resolve, reject) => {
    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 1024,
    });
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error('Timed out waiting for a TCP frame.'));
    }, 1000);
    const cleanup = (): void => {
      clearTimeout(timer);
      socket.off('data', onData);
      socket.off('error', onError);
      socket.off('close', onClose);
    };
    const onData = (data: Buffer): void => {
      for (const frame of decoder.accept(Buffer.from(data))) {
        cleanup();
        resolve(frame);
        return;
      }
    };
    const onError = (error: Error): void => {
      cleanup();
      reject(error);
    };
    const onClose = (): void => {
      cleanup();
      reject(new Error('TCP socket closed before a frame was received.'));
    };
    socket.on('data', onData);
    socket.once('error', onError);
    socket.once('close', onClose);
  });

const waitForSocketClose = async (socket: Socket): Promise<void> =>
  await new Promise<void>((resolve) => {
    if (socket.destroyed) {
      resolve();
      return;
    }
    socket.once('close', () => {
      resolve();
    });
  });

const waitForClientConnect = async (socket: Socket): Promise<void> =>
  await new Promise<void>((resolve, reject) => {
    socket.once('error', reject);
    socket.once('connect', () => {
      resolve();
    });
  });

describe.concurrent('remote agent connection api', () => {
  it('connects to a tcp frame agent and reads capabilities', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.capabilities()).resolves.toEqual(
        defaultFakeCapabilities
      );
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('authenticates to a tcp frame agent with a challenge response', async () => {
    const fakeAgent = await startFakeTcpAgent({
      authToken: 'correct-token',
    });
    const agent = await connectRemoteAgent({
      authToken: 'correct-token',
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.capabilities()).resolves.toMatchObject({
        platform: 'windows',
        protocolVersion,
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('authenticates with an explicit token even when no auth is required', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      authToken: 'unused-token',
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.capabilities()).resolves.toMatchObject({
        platform: 'windows',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it(
    'uses AGENT_ROVER_AUTH_TOKEN when no explicit tcp token is provided',
    { concurrent: false },
    async () => {
      const original = process.env.AGENT_ROVER_AUTH_TOKEN;
      process.env.AGENT_ROVER_AUTH_TOKEN = 'env-token';
      const fakeAgent = await startFakeTcpAgent({
        authToken: 'env-token',
      });
      try {
        const agent = await connectRemoteAgent({
          host: fakeAgent.host,
          port: fakeAgent.port,
        });
        try {
          await expect(agent.capabilities()).resolves.toMatchObject({
            platform: 'windows',
          });
        } finally {
          agent.release();
        }
      } finally {
        if (original === undefined) {
          delete process.env.AGENT_ROVER_AUTH_TOKEN;
        } else {
          process.env.AGENT_ROVER_AUTH_TOKEN = original;
        }
        await fakeAgent.close();
      }
    }
  );

  it(
    'prefers the explicit tcp token over AGENT_ROVER_AUTH_TOKEN',
    { concurrent: false },
    async () => {
      const original = process.env.AGENT_ROVER_AUTH_TOKEN;
      process.env.AGENT_ROVER_AUTH_TOKEN = 'wrong-env-token';
      const fakeAgent = await startFakeTcpAgent({
        authToken: 'explicit-token',
      });
      try {
        const agent = await connectRemoteAgent({
          authToken: 'explicit-token',
          host: fakeAgent.host,
          port: fakeAgent.port,
        });
        try {
          await expect(agent.capabilities()).resolves.toMatchObject({
            platform: 'windows',
          });
        } finally {
          agent.release();
        }
      } finally {
        if (original === undefined) {
          delete process.env.AGENT_ROVER_AUTH_TOKEN;
        } else {
          process.env.AGENT_ROVER_AUTH_TOKEN = original;
        }
        await fakeAgent.close();
      }
    }
  );

  it('rejects tcp connections with missing or incorrect tokens', async () => {
    const missingTokenAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    try {
      await expect(
        connectRemoteAgent({
          host: missingTokenAgent.host,
          port: missingTokenAgent.port,
          timeoutMs: 1000,
        })
      ).rejects.toMatchObject({
        code: 'AUTHENTICATION_FAILED',
      });
    } finally {
      await missingTokenAgent.close();
    }

    const wrongTokenAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    try {
      await expect(
        connectRemoteAgent({
          authToken: 'wrong',
          host: wrongTokenAgent.host,
          port: wrongTokenAgent.port,
          timeoutMs: 1000,
        })
      ).rejects.toMatchObject({
        code: 'AUTHENTICATION_FAILED',
      });
    } finally {
      await wrongTokenAgent.close();
    }
  });

  it('sends a challenge before accepting tcp auth responses', async () => {
    const fakeAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    const client = connectTcpSocket({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await waitForClientConnect(client);
      const challenge = await readTcpFrame(client);
      expect(challenge.kind).toBe(tcpFrameKindAuthChallenge);
      expect(challenge.payload.byteLength).toBe(32);
      expect(challenge.payload.toString('utf8')).not.toBe('required');

      client.write(
        encodeTcpFrame({
          kind: tcpFrameKindAuthResponse,
          payload: createAuthResponse('required', challenge.payload),
        })
      );

      const ready = await readTcpFrame(client);
      expect(ready.kind).toBe(tcpFrameKindJson);
    } finally {
      client.destroy();
      await fakeAgent.close();
    }
  });

  it('disconnects tcp clients that answer a challenge with a non-response frame', async () => {
    const fakeAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    const client = connectTcpSocket({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await waitForClientConnect(client);
      const challenge = await readTcpFrame(client);
      expect(challenge.kind).toBe(tcpFrameKindAuthChallenge);

      sendTcpProtocolMessage(client, {
        id: 'req-before-auth',
        kind: 'request',
        method: 'agent.capabilities',
      });
      await waitForSocketClose(client);
      expect(client.destroyed).toBe(true);
    } finally {
      client.destroy();
      await fakeAgent.close();
    }
  });

  it('disconnects tcp auth attempts before reading invalid response payloads', async () => {
    const fakeAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    const client = connectTcpSocket({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await waitForClientConnect(client);
      const challenge = await readTcpFrame(client);
      expect(challenge.kind).toBe(tcpFrameKindAuthChallenge);

      const header = Buffer.alloc(tcpFrameHeaderBytes);
      header.write(tcpFrameMagic, 0, 'ascii');
      header.writeUInt16LE(tcpFrameVersion, 4);
      header.writeUInt16LE(tcpFrameKindAuthResponse, 6);
      header.writeUInt32LE(0, 8);
      header.writeUInt32LE(33, 12);
      header.writeUInt32LE(0, 16);
      client.write(header);

      await waitForSocketClose(client);
      expect(client.bytesWritten).toBe(tcpFrameHeaderBytes);
    } finally {
      client.destroy();
      await fakeAgent.close();
    }
  });

  it('disconnects tcp auth attempts with incorrect response digests', async () => {
    const fakeAgent = await startFakeTcpAgent({
      authToken: 'required',
    });
    const client = connectTcpSocket({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await waitForClientConnect(client);
      const challenge = await readTcpFrame(client);
      expect(challenge.kind).toBe(tcpFrameKindAuthChallenge);

      const frame = encodeTcpFrame({
        kind: tcpFrameKindAuthResponse,
        payload: Buffer.from('x'.repeat(32), 'utf8'),
      });
      for (const byte of frame) {
        client.write(Buffer.from([byte]));
      }

      await waitForSocketClose(client);
      expect(client.bytesWritten).toBe(52);
    } finally {
      client.destroy();
      await fakeAgent.close();
    }
  });

  it('uses TCP binary frames for file and screenshot payloads', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const filePath = 'C:/agent-rover/binary.bin';
      const fileData = Buffer.from([0, 1, 2, 3, 4, 250, 251, 252, 253, 254]);

      await agent.files.writeFile(filePath, fileData);
      await expect(agent.files.readFile(filePath)).resolves.toEqual(fileData);

      const [window] = await agent.windows();
      if (window === undefined) {
        throw new Error('Expected fake window.');
      }
      await expect(window.screenshot()).resolves.toMatchObject({
        image: Buffer.from('fake png bytes'),
      });
      expect(fakeAgent.requestUsedBase64()).toBe(false);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('captures window video into a releaseable readable stream', async () => {
    const videoData = Buffer.from('fake streamed h264 mp4');
    const videoRequests: Record<string, unknown>[] = [];
    const fakeAgent = await startFakeTcpAgent({ videoData, videoRequests });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const [window] = await agent.windows();
      if (window === undefined) {
        throw new Error('Expected fake window.');
      }
      const video = await window.recordVideo(1, {
        fps: 30,
        quality: 82,
        tracking: 'initialBounds',
      });
      const chunks: Buffer[] = [];
      for await (const chunk of video) {
        chunks.push(Buffer.from(chunk));
      }

      expect(Buffer.concat(chunks)).toEqual(videoData);
      expect(video).toMatchObject({
        clipped: false,
        codec: 'h264',
        contentType: 'video/mp4',
        droppedFrames: 0,
        durationMs: 1,
        fps: 30,
      });
      expect(video.releaseAsync).toEqual(expect.any(Function));
      expect(video[Symbol.asyncDispose]).toEqual(expect.any(Function));
      await video.releaseAsync();
      await video[Symbol.asyncDispose]();
      expect(videoRequests).toEqual([
        {
          durationMs: 1,
          fps: 30,
          quality: 82,
          tracking: 'initialBounds',
          windowId: defaultFakeWindow.id,
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('persists screen video directly to a new host path', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-video-test-'));
    const outputPath = join(directory, 'capture.mp4');
    const videoData = Buffer.from('fake persisted h264 mp4');
    const fakeAgent = await startFakeTcpAgent({ videoData });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const result = await agent.recordVideo(1, outputPath, {
        fps: 24,
        quality: 75,
        rect: { height: 240, width: 320, x: 5, y: 6 },
      });

      expect(result).toMatchObject({
        codec: 'h264',
        contentType: 'video/mp4',
        path: resolve(outputPath),
      });
      expect(result).not.toHaveProperty('pipe');
      await expect(readFile(outputPath)).resolves.toEqual(videoData);
    } finally {
      agent.release();
      await fakeAgent.close();
      await rm(directory, { force: true, recursive: true });
    }
  });

  it('does not overwrite an existing video output file', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-video-test-'));
    const outputPath = join(directory, 'capture.mp4');
    const original = Buffer.from('keep existing file');
    await writeFile(outputPath, original);
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.recordVideo(1, outputPath)).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
      await expect(readFile(outputPath)).resolves.toEqual(original);
    } finally {
      agent.release();
      await fakeAgent.close();
      await rm(directory, { force: true, recursive: true });
    }
  });

  it('validates video capture options at the public API boundary', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.recordVideo(0)).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
      await expect(agent.recordVideo(1, { fps: 0 })).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
      await expect(
        agent.recordVideo(1, { quality: 101 })
      ).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('reports connection failures with a stable error code', async () => {
    await expect(
      connectRemoteAgent({
        host: '127.0.0.1',
        port: 0,
        timeoutMs: 1000,
      })
    ).rejects.toMatchObject({
      code: 'CONNECTION_FAILED',
    });
  });

  it('rejects agents that speak a different protocol version', async () => {
    const fakeAgent = await startFakeTcpAgent({
      protocolVersionOverride: 'legacy',
    });
    try {
      await expect(
        connectRemoteAgent({
          host: fakeAgent.host,
          port: fakeAgent.port,
        })
      ).rejects.toMatchObject({
        code: 'HANDSHAKE_FAILED',
      });
    } finally {
      await fakeAgent.close();
    }
  });

  it('rejects operations after release', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    expect(agent.release).toEqual(expect.any(Function));
    expect(agent[Symbol.dispose]).toEqual(expect.any(Function));
    agent[Symbol.dispose]();

    try {
      await expect(agent.capabilities()).rejects.toMatchObject({
        code: 'DISCONNECTED',
      });
    } finally {
      await fakeAgent.close();
    }
  });

  it('lists top-level windows and traverses descendants', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const windows = await agent.windows();
      const firstWindow = windows[0];
      if (firstWindow === undefined) {
        throw new Error('Expected a fake top-level window.');
      }

      expect(firstWindow).toMatchObject(defaultFakeWindow);

      const children = await firstWindow.children();
      expect(children).toHaveLength(1);
      expect(children[0]).toMatchObject(defaultFakeChildWindow);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('reads control metadata and searches window descendants', async () => {
    const rootWindow = {
      ...defaultFakeWindow,
      className: 'Notepad',
      controlId: 0,
      focused: false,
    };
    const editWindow = {
      ...defaultFakeChildWindow,
      className: 'Edit',
      controlId: 15,
      focused: true,
      id: '0x1001-edit',
      title: 'Document',
    };
    const buttonWindow = {
      ...defaultFakeChildWindow,
      bounds: {
        height: 24,
        width: 80,
        x: 22,
        y: 82,
      },
      className: 'Button',
      controlId: 1,
      focused: false,
      id: '0x1001-edit-ok',
      title: 'OK',
    };
    const fakeAgent = await startFakeTcpAgent({
      childrenByWindowId: {
        [rootWindow.id]: [editWindow],
        [editWindow.id]: [buttonWindow],
      },
      windows: [rootWindow],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const [root] = await agent.windows();
      if (root === undefined) {
        throw new Error('Expected a fake root window.');
      }
      expect(root).toMatchObject({
        className: 'Notepad',
        controlId: 0,
        focused: false,
      });

      await expect(root.descendants()).resolves.toMatchObject([
        {
          className: 'Edit',
          focused: true,
          id: editWindow.id,
        },
        {
          className: 'Button',
          controlId: 1,
          id: buttonWindow.id,
        },
      ]);
      await expect(
        root.findDescendants({
          className: 'Edit',
          focused: true,
        })
      ).resolves.toMatchObject([
        {
          id: editWindow.id,
        },
      ]);
      await expect(
        agent.findWindows({
          className: 'Button',
          includeDescendants: true,
          strict: true,
        })
      ).resolves.toMatchObject([
        {
          controlId: 1,
          id: buttonWindow.id,
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('sends ordered keyboard and mouse input operations', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.keyboard.down('Shift');
      await agent.mouse.drag(
        { x: 10, y: 20 },
        { x: 30, y: 40 },
        {
          button: 'left',
          modifiers: ['Shift'],
        }
      );
      await agent.keyboard.up('Shift');
      await agent.mouse.wheel({
        deltaY: -120,
        point: { x: 30, y: 40 },
      });

      expect(operations).toEqual([
        {
          key: 'Shift',
          kind: 'keyboard.down',
        },
        {
          button: 'left',
          from: { x: 10, y: 20 },
          kind: 'mouse.drag',
          modifiers: ['Shift'],
          to: { x: 30, y: 40 },
        },
        {
          key: 'Shift',
          kind: 'keyboard.up',
        },
        {
          deltaX: 0,
          deltaY: -120,
          kind: 'mouse.wheel',
          point: { x: 30, y: 40 },
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('sends low-level mouse button state operations', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.mouse.down();
      await agent.mouse.up({
        button: 'right',
        point: { x: 12, y: 34 },
      });
      await agent.mouse.down({
        button: 'middle',
        point: { x: 56, y: 78 },
      });
      await agent.mouse.up({
        button: 'middle',
      });

      expect(operations).toEqual([
        {
          button: 'left',
          kind: 'mouse.down',
          point: null,
        },
        {
          button: 'right',
          kind: 'mouse.up',
          point: { x: 12, y: 34 },
        },
        {
          button: 'middle',
          kind: 'mouse.down',
          point: { x: 56, y: 78 },
        },
        {
          button: 'middle',
          kind: 'mouse.up',
          point: null,
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('releases interaction session input state in reverse order', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const session = await agent.interaction.start();
      await session.keyboard.down('Shift');
      await session.mouse.down({
        button: 'left',
        point: { x: 10, y: 20 },
      });
      await session.keyboard.down('Shift');
      await session.mouse.down({
        button: 'left',
        point: { x: 99, y: 99 },
      });
      await session.keyboard.down('Control');
      await session.mouse.down({
        button: 'right',
      });
      await session.mouse.up({
        button: 'left',
      });
      await session.keyboard.up('Control');
      await session.releaseAsync();

      expect(operations).toEqual([
        {
          key: 'Shift',
          kind: 'keyboard.down',
        },
        {
          button: 'left',
          kind: 'mouse.down',
          point: { x: 10, y: 20 },
        },
        {
          key: 'Control',
          kind: 'keyboard.down',
        },
        {
          button: 'right',
          kind: 'mouse.down',
          point: null,
        },
        {
          button: 'left',
          kind: 'mouse.up',
          point: null,
        },
        {
          key: 'Control',
          kind: 'keyboard.up',
        },
        {
          button: 'right',
          kind: 'mouse.up',
          point: null,
        },
        {
          key: 'Shift',
          kind: 'keyboard.up',
        },
        {
          kind: 'mouse.move',
          point: { x: 12, y: 34 },
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('releases interaction sessions when scoped operations fail', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(
        agent.interaction.with(async (session) => {
          await session.keyboard.down('Alt');
          await session.mouse.down({
            button: 'middle',
          });
          throw new Error('expected interaction failure');
        })
      ).rejects.toThrow('expected interaction failure');

      expect(operations).toEqual([
        {
          key: 'Alt',
          kind: 'keyboard.down',
        },
        {
          button: 'middle',
          kind: 'mouse.down',
          point: null,
        },
        {
          button: 'middle',
          kind: 'mouse.up',
          point: null,
        },
        {
          key: 'Alt',
          kind: 'keyboard.up',
        },
        {
          kind: 'mouse.move',
          point: { x: 12, y: 34 },
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('makes interaction session release idempotent and rejects later operations', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const session = await agent.interaction.start({
        restoreCursor: false,
      });
      await session.mouse.down();
      await session.releaseAsync();
      await session[Symbol.asyncDispose]();

      expect(operations).toEqual([
        {
          button: 'left',
          kind: 'mouse.down',
          point: null,
        },
        {
          button: 'left',
          kind: 'mouse.up',
          point: null,
        },
      ]);
      await expect(session.mouse.move({ x: 1, y: 2 })).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('manages clipboard text and pastes through the keyboard helper', async () => {
    const operations: RemoteInputOperation[] = [];
    const fakeAgent = await startFakeTcpAgent({
      inputOperations: operations,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.clipboard.writeText('initial clipboard text');
      await expect(agent.clipboard.readText()).resolves.toBe(
        'initial clipboard text'
      );

      await agent.clipboard.writeText('previous clipboard text');
      await expect(
        agent.clipboard.withText('scoped clipboard text', async () => {
          await expect(agent.clipboard.readText()).resolves.toBe(
            'scoped clipboard text'
          );
          return 'operation result';
        })
      ).resolves.toBe('operation result');
      await expect(agent.clipboard.readText()).resolves.toBe(
        'previous clipboard text'
      );

      await expect(
        agent.clipboard.withText('failing clipboard text', async () => {
          throw new Error('expected operation failure');
        })
      ).rejects.toThrow('expected operation failure');
      await expect(agent.clipboard.readText()).resolves.toBe(
        'previous clipboard text'
      );

      await agent.keyboard.pasteText('pasted clipboard text', {
        restoreDelayMs: 0,
      });
      await expect(agent.clipboard.readText()).resolves.toBe(
        'previous clipboard text'
      );
      expect(operations.at(-1)).toEqual({
        key: 'v',
        kind: 'keyboard.press',
        modifiers: ['Control'],
      });

      await agent.clipboard.clear();
      await expect(agent.clipboard.readText()).resolves.toBe('');
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('launches applications and closes windows through the remote agent', async () => {
    const launches: RemoteApplicationLaunchOptions[] = [];
    const closedWindowIds: string[] = [];
    const fakeAgent = await startFakeTcpAgent({
      closedWindowIds,
      launches,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(
        agent.applications.launch({
          arguments: ['C:/agent-rover/app input.txt'],
          path: 'C:/agent-rover/app.exe',
          workingDirectory: 'C:/agent-rover',
        })
      ).resolves.toEqual({
        id: 4321,
        name: 'fake-launched-app',
      });

      const [window] = await agent.windows();
      if (window === undefined) {
        throw new Error('Expected a fake window.');
      }
      await window.close();

      expect(launches).toEqual([
        {
          arguments: ['C:/agent-rover/app input.txt'],
          path: 'C:/agent-rover/app.exe',
          workingDirectory: 'C:/agent-rover',
        },
      ]);
      expect(closedWindowIds).toEqual([defaultFakeWindow.id]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('tracks launched processes and waits for exit', async () => {
    const launches: RemoteApplicationLaunchOptions[] = [];
    const fakeAgent = await startFakeTcpAgent({
      launches,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.applications.launch({
        arguments: ['--mode', 'test'],
        createNoWindow: true,
        environment: {
          AGENT_ROVER_PROCESS: 'enabled',
        },
        path: 'fake-process.exe',
        stderrPath: 'C:/agent-rover/stderr.log',
        stdoutPath: 'C:/agent-rover/stdout.log',
      });
      expect(launches[0]).toMatchObject({
        arguments: ['--mode', 'test'],
        createNoWindow: true,
        environment: {
          AGENT_ROVER_PROCESS: 'enabled',
        },
        path: 'fake-process.exe',
        stderrPath: 'C:/agent-rover/stderr.log',
        stdoutPath: 'C:/agent-rover/stdout.log',
      });

      await expect(agent.processes.exists(process.id)).resolves.toBe(true);
      await expect(agent.processes.snapshot(process.id)).resolves.toMatchObject(
        {
          createdAt: '2026-06-25T00:00:00.000Z',
          id: process.id,
          name: process.name,
          parentProcessId: null,
          running: true,
        }
      );
      await expect(
        agent.processes.list({
          name: process.name,
        })
      ).resolves.toMatchObject([
        {
          id: process.id,
          running: true,
        },
      ]);

      await agent.processes.kill(process.id);
      await expect(
        agent.processes.waitForExit(process.id, {
          intervalMs: 1,
          timeoutMs: 100,
        })
      ).resolves.toMatchObject({
        exitCode: 1,
        id: process.id,
        running: false,
      });
      await expect(agent.processes.exists(process.id)).resolves.toBe(false);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('launches managed processes with captured output and cleanup', async () => {
    const launches: RemoteApplicationLaunchOptions[] = [];
    const fakeAgent = await startFakeTcpAgent({
      capabilities: {
        ...defaultFakeCapabilities,
        features: defaultFakeCapabilities.features.filter(
          (feature) =>
            feature !== 'process.launchManaged' &&
            feature !== 'process.killManaged' &&
            feature !== 'process.releaseManaged' &&
            !feature.startsWith('process.managed')
        ),
      },
      launches,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        arguments: ['--mode', 'managed'],
        captureStderr: true,
        captureStdout: true,
        createNoWindow: true,
        environment: {
          AGENT_ROVER_MANAGED_PROCESS: 'enabled',
        },
        killTreeOnRelease: true,
        path: 'fake-managed-process.exe',
        workingDirectory: 'C:/agent-rover',
      });

      expect(process).toMatchObject({
        id: 4321,
        name: 'fake-launched-app',
      });
      expect(process).not.toHaveProperty('dispose');
      expect(process.releaseAsync).toEqual(expect.any(Function));
      expect(process[Symbol.asyncDispose]).toEqual(expect.any(Function));
      expect(launches[0]).toMatchObject({
        arguments: ['--mode', 'managed'],
        createNoWindow: true,
        environment: {
          AGENT_ROVER_MANAGED_PROCESS: 'enabled',
        },
        path: 'fake-managed-process.exe',
        workingDirectory: 'C:/agent-rover',
      });
      expect(launches[0]?.stdoutPath).toMatch(
        /^C:\/agent-rover-managed-process-fake\/stdout\.log$/u
      );
      expect(launches[0]?.stderrPath).toMatch(
        /^C:\/agent-rover-managed-process-fake\/stderr\.log$/u
      );

      await expect(process.stdoutText()).resolves.toBe('managed stdout');
      await expect(process.stderrText()).resolves.toBe('managed stderr');
      await expect(process.snapshot()).resolves.toMatchObject({
        root: {
          id: process.id,
          running: true,
        },
        running: true,
      });

      await process.kill();
      await expect(
        process.waitForExit({
          intervalMs: 1,
          timeoutMs: 100,
        })
      ).resolves.toMatchObject({
        root: {
          exitCode: 1,
          id: process.id,
        },
        running: false,
      });
      const releasable: AsyncReleaseable = process;
      await releasable.releaseAsync();
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('uses native managed process protocol when available', async () => {
    const killedProcessIds: number[] = [];
    const launches: RemoteApplicationLaunchOptions[] = [];
    const managedLaunches: FakeManagedProcessLaunchOptions[] = [];
    const releasedManagedProcessIds: number[] = [];
    const fakeAgent = await startFakeTcpAgent({
      killedProcessIds,
      launches,
      managedLaunches,
      releasedManagedProcessIds,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        arguments: ['--native-managed'],
        captureStderr: true,
        captureStdout: true,
        killTreeOnRelease: true,
        path: 'fake-native-managed-process.exe',
      });

      expect(launches).toEqual([]);
      expect(managedLaunches[0]).toMatchObject({
        arguments: ['--native-managed'],
        captureStderr: true,
        captureStdout: true,
        killTreeOnRelease: true,
        path: 'fake-native-managed-process.exe',
      });
      expect(managedLaunches[0]?.stdoutPath).toBe(
        'C:/agent-rover-managed-process-fake/stdout.log'
      );
      expect(managedLaunches[0]?.stderrPath).toBe(
        'C:/agent-rover-managed-process-fake/stderr.log'
      );

      await expect(process.stdoutText()).resolves.toBe('managed stdout');
      await expect(process.stderrText()).resolves.toBe('managed stderr');
      await expect(process.snapshot()).resolves.toMatchObject({
        root: {
          id: 4321,
          running: true,
        },
        running: true,
      });

      await process.kill();
      expect(killedProcessIds).toEqual([4321]);
      await expect(
        process.waitForExit({
          intervalMs: 1,
          timeoutMs: 100,
        })
      ).resolves.toMatchObject({
        root: {
          exitCode: 1,
          id: 4321,
        },
        running: false,
      });

      const releasable: AsyncReleaseable = process;
      await releasable[Symbol.asyncDispose]();
      expect(releasedManagedProcessIds).toEqual([1]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('defaults managed release cleanup to killing the launched process tree', async () => {
    const killedProcessIds: number[] = [];
    const launches: RemoteApplicationLaunchOptions[] = [];
    const fakeAgent = await startFakeTcpAgent({
      capabilities: {
        ...defaultFakeCapabilities,
        features: defaultFakeCapabilities.features.filter(
          (feature) =>
            feature !== 'process.launchManaged' &&
            feature !== 'process.killManaged' &&
            feature !== 'process.releaseManaged' &&
            !feature.startsWith('process.managed')
        ),
      },
      killedProcessIds,
      launches,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        path: 'fake-default-cleanup.exe',
      });

      await process.releaseAsync();

      expect(launches[0]).toMatchObject({
        path: 'fake-default-cleanup.exe',
      });
      expect(killedProcessIds).toEqual([process.id]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('leaves the launched process running when release tree cleanup is disabled', async () => {
    const killedProcessIds: number[] = [];
    const fakeAgent = await startFakeTcpAgent({
      killedProcessIds,
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        killTreeOnRelease: false,
        path: 'fake-leave-running.exe',
      });

      await process.releaseAsync();

      expect(killedProcessIds).toEqual([]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('finds managed windows owned by descendant processes', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        path: 'fake-bootstrap.exe',
      });
      const childProcess = fakeProcessSnapshot({
        id: 9876,
        name: 'fake-gui.exe',
        parentProcessId: process.id,
        path: 'C:/agent-rover/fake-gui.exe',
      });
      const childWindow: AppWindowSnapshot = {
        ...defaultFakeWindow,
        id: '0x9876',
        process: {
          id: childProcess.id,
          name: childProcess.name,
          path: childProcess.path,
        },
        title: 'Child GUI',
      };
      fakeAgent.setProcesses([
        fakeProcessSnapshot({
          id: process.id,
          name: process.name,
          path: 'C:/agent-rover/fake-bootstrap.exe',
        }),
        childProcess,
      ]);
      fakeAgent.setWindows([childWindow]);

      await expect(
        process.waitForWindow(
          {
            title: 'Child GUI',
            visible: true,
          },
          {
            intervalMs: 1,
            timeoutMs: 1000,
          }
        )
      ).resolves.toMatchObject({
        id: childWindow.id,
        process: {
          id: childProcess.id,
        },
      });
      await expect(process.windows()).resolves.toMatchObject([
        {
          id: childWindow.id,
        },
      ]);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('keeps managed apps running while descendants remain after the launcher exits', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        path: 'fake-short-launcher.exe',
      });
      const exitedRoot = fakeProcessSnapshot({
        exitCode: 0,
        id: process.id,
        name: process.name,
        path: 'C:/agent-rover/fake-short-launcher.exe',
        running: false,
      });
      const childProcess = fakeProcessSnapshot({
        id: 2468,
        name: 'fake-child.exe',
        parentProcessId: process.id,
        path: 'C:/agent-rover/fake-child.exe',
      });
      fakeAgent.setProcesses([exitedRoot, childProcess]);

      await expect(process.rootSnapshot()).resolves.toMatchObject({
        id: process.id,
        running: false,
      });
      await expect(process.snapshot()).resolves.toMatchObject({
        processes: [
          {
            id: childProcess.id,
            running: true,
          },
        ],
        root: {
          id: process.id,
          running: false,
        },
        running: true,
      });

      fakeAgent.setProcesses([
        exitedRoot,
        {
          ...childProcess,
          exitCode: 1,
          running: false,
        },
      ]);
      await expect(
        process.waitForExit({
          intervalMs: 1,
          timeoutMs: 1000,
        })
      ).resolves.toMatchObject({
        running: false,
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('waits for managed windows that appear after a CLI-style launch', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        createNoWindow: true,
        path: 'fake-cli-with-late-ui.exe',
      });
      const pendingWindow = process.waitForWindow(
        {
          title: 'Late UI',
          visible: true,
        },
        {
          intervalMs: 1,
          timeoutMs: 1000,
        }
      );

      fakeAgent.setWindows([
        {
          ...defaultFakeWindow,
          id: '0x4321-late',
          process: {
            id: process.id,
            name: process.name,
            path: 'C:/agent-rover/fake-cli-with-late-ui.exe',
          },
          title: 'Late UI',
        },
      ]);

      await expect(pendingWindow).resolves.toMatchObject({
        id: '0x4321-late',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('rejects strict managed fallback window queries with candidate diagnostics', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const process = await agent.processes.launchManaged({
        path: 'fake-window-forwarder.exe',
      });
      fakeAgent.setWindows([
        {
          ...defaultFakeWindow,
          id: '0x7001',
          process: {
            id: 7001,
            name: 'singleton.exe',
            path: 'C:/agent-rover/singleton.exe',
          },
          title: 'Forwarded UI',
        },
        {
          ...defaultFakeWindow,
          id: '0x7002',
          process: {
            id: 7002,
            name: 'singleton.exe',
            path: 'C:/agent-rover/singleton.exe',
          },
          title: 'Forwarded UI',
        },
      ]);

      await expect(
        process.waitForWindow(
          {
            strict: true,
            title: 'Forwarded UI',
          },
          {
            intervalMs: 1,
            timeoutMs: 100,
          }
        )
      ).rejects.toThrow(/Strict window query matched 2 windows/u);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('preserves outer, visible frame, and client screen rectangles', async () => {
    const expected = {
      ...defaultFakeWindow,
      bounds: { x: -300, y: 20, width: 640, height: 480 },
      frameBounds: { x: -292, y: 20, width: 624, height: 472 },
      clientBounds: { x: -292, y: 51, width: 624, height: 441 },
    };
    const fakeAgent = await startFakeTcpAgent({ windows: [expected] });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const [window] = await agent.windows();
      expect(window).toMatchObject(expected);
      expect(await window!.refresh()).toMatchObject(expected);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('controls window activation, state, bounds, and snapshots', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const [window] = await agent.windows();
      if (window === undefined) {
        throw new Error('Expected a fake window.');
      }

      await expect(window.activate()).resolves.toMatchObject({
        active: true,
        id: defaultFakeWindow.id,
      });
      await expect(window.focus()).resolves.toMatchObject({
        active: true,
        id: defaultFakeWindow.id,
      });
      await expect(window.minimize()).resolves.toMatchObject({
        minimized: true,
      });
      await expect(window.restore()).resolves.toMatchObject({
        minimized: false,
      });
      await expect(window.maximize()).resolves.toMatchObject({
        maximized: true,
      });
      await expect(
        window.setBounds({
          height: 360,
          width: 480,
          x: 44,
          y: 55,
        })
      ).resolves.toMatchObject({
        bounds: {
          height: 360,
          width: 480,
          x: 44,
          y: 55,
        },
      });
      await expect(window.refresh()).resolves.toMatchObject({
        id: defaultFakeWindow.id,
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('finds and waits for windows with query helpers', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const delayedWindow = {
        ...defaultFakeWindow,
        title: 'Delayed Notepad',
      };
      const pendingWindow = agent.waitForWindow(
        {
          strict: true,
          title: 'Delayed Notepad',
        },
        {
          intervalMs: 1,
          timeoutMs: 1000,
        }
      );
      fakeAgent.setWindows([delayedWindow]);

      const window = await pendingWindow;
      expect(window).toMatchObject({
        id: defaultFakeWindow.id,
        title: 'Delayed Notepad',
      });

      await expect(
        agent.findWindows({
          processName: 'notepad.exe',
        })
      ).resolves.toHaveLength(1);
      await expect(
        agent.findWindows({
          includeDescendants: true,
          title: defaultFakeChildWindow.title,
        })
      ).resolves.toHaveLength(1);
      await expect(
        window.waitForStableBounds({
          intervalMs: 1,
          timeoutMs: 1000,
        })
      ).resolves.toMatchObject({
        bounds: delayedWindow.bounds,
      });

      const pendingClosed = window.waitForClosed({
        intervalMs: 1,
        timeoutMs: 1000,
      });
      fakeAgent.setWindows([]);
      await expect(pendingClosed).resolves.toBeUndefined();
      await expect(
        agent.waitForNoWindow(
          {
            title: 'Delayed Notepad',
          },
          {
            intervalMs: 1,
            timeoutMs: 1000,
          }
        )
      ).resolves.toBeUndefined();
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('rejects strict window queries with multiple matches', async () => {
    const fakeAgent = await startFakeTcpAgent({
      windows: [
        {
          ...defaultFakeWindow,
          id: '0x2001',
          title: 'Duplicate',
        },
        {
          ...defaultFakeWindow,
          id: '0x2002',
          title: 'Duplicate',
        },
      ],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(
        agent.findWindows({
          strict: true,
          title: 'Duplicate',
        })
      ).rejects.toMatchObject({
        code: 'INVALID_ARGUMENT',
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('preserves monitor DPI separately from target window DPI', async () => {
    const monitor = {
      id: 'left',
      name: 'left',
      primary: false,
      bounds: { x: -1920, y: -200, width: 1920, height: 1080 },
      workArea: { x: -1920, y: -160, width: 1920, height: 1040 },
      dpi: 144,
      scaleFactor: 1.5,
    };
    const window = {
      ...defaultFakeWindow,
      monitorId: 'left',
      dpi: 96,
      dpiAwareness: 'unaware' as const,
    };
    const fakeAgent = await startFakeTcpAgent({
      monitors: [monitor],
      windows: [window],
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      expect(await agent.monitors()).toEqual([monitor]);
      expect((await agent.windows())[0]).toMatchObject(window);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('reads screen geometry, cursor state, and screenshots', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await expect(agent.bounds()).resolves.toEqual({
        height: 768,
        width: 1024,
        x: 0,
        y: 0,
      });
      await expect(agent.monitors()).resolves.toEqual([
        {
          bounds: {
            height: 768,
            width: 1024,
            x: 0,
            y: 0,
          },
          id: 'monitor-1',
          name: 'DISPLAY1',
          primary: true,
          scaleFactor: 1,
          dpi: 96,
          workArea: {
            height: 728,
            width: 1024,
            x: 0,
            y: 0,
          },
        },
      ]);
      await expect(agent.cursor()).resolves.toEqual({
        point: {
          x: 12,
          y: 34,
        },
        visible: true,
      });
      await expect(
        agent.screenshot({
          rect: {
            height: 100,
            width: 120,
            x: 10,
            y: 20,
          },
        })
      ).resolves.toMatchObject({
        bounds: {
          height: 100,
          width: 120,
          x: 10,
          y: 20,
        },
        image: Buffer.from('fake screen png bytes'),
      });
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });
});
