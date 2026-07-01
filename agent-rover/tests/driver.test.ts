// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHmac } from 'node:crypto';
import { connect as connectTcpSocket, type Socket } from 'node:net';

import { describe, expect, it } from 'vitest';

import {
  connectRemoteAgent,
  type RemoteApplicationLaunchOptions,
  type RemoteInputOperation,
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

  it.sequential(
    'uses AGENT_ROVER_AUTH_TOKEN when no explicit tcp token is provided',
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

  it.sequential(
    'prefers the explicit tcp token over AGENT_ROVER_AUTH_TOKEN',
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
          id: process.id,
          name: process.name,
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
            feature !== 'process.disposeManaged' &&
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
        killTreeOnDispose: true,
        path: 'fake-managed-process.exe',
        workingDirectory: 'C:/agent-rover',
      });

      expect(process).toMatchObject({
        id: 4321,
        name: 'fake-launched-app',
      });
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
        id: process.id,
        running: true,
      });

      await process.kill();
      await expect(
        process.waitForExit({
          intervalMs: 1,
          timeoutMs: 100,
        })
      ).resolves.toMatchObject({
        exitCode: 1,
        id: process.id,
        running: false,
      });
      await process.dispose();
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
