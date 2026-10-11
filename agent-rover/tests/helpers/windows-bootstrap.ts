// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { randomUUID } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { waitForResult } from '../../src/wait';
import { createTcpFrameTransport } from '../../src/driver/transport';
import {
  createPendingRequestTable,
  createBinaryTransferChunks,
  type JsonValue,
} from '../../src/protocol';

/** Minimal deployment transport, independent of the deployed agent's API version. */
export interface WindowsBootstrap {
  /** Sends a deployment request supported by the preinstalled agent. */
  readonly request: (
    method: string,
    params: JsonValue
  ) => Promise<JsonValue | undefined>;
  /** Uploads a file using binary transfer frames. */
  readonly upload: (path: string, data: Buffer) => Promise<void>;
  /** Releases a deployment Job after its native children actually finish. */
  readonly releaseManaged: (id: number) => Promise<void>;
  /** Closes the deployment connection. */
  readonly close: () => Promise<void>;
}

/** Connects to the explicitly configured Windows 11 test machine. */
export const connectWindowsBootstrap = async (): Promise<WindowsBootstrap> => {
  const host = process.env.AGENT_ROVER_WIN11_HOST;
  const authToken = process.env.AGENT_ROVER_WIN11_TOKEN;
  if (!host || !authToken)
    throw new Error(
      'AGENT_ROVER_WIN11_HOST and AGENT_ROVER_WIN11_TOKEN are required.'
    );
  const port = Number(process.env.AGENT_ROVER_WIN11_PORT ?? '39397');
  if (!Number.isInteger(port) || port < 1 || port > 65535)
    throw new Error(
      'AGENT_ROVER_WIN11_PORT must be an integer from 1 to 65535.'
    );
  for (let retries = 0; ; ++retries) {
    try {
      return await connectWindowsBootstrapOnce(host, port, authToken);
    } catch (error) {
      if (
        !(error instanceof Error) ||
        !('code' in error) ||
        error.code !== 'AGENT_BUSY' ||
        retries >= 360
      )
        throw error;
      console.info(
        `[agent-rover] Windows bootstrap at ${host}:${port} is busy. Retrying in 10s (${retries + 1}/360).`
      );
      await delay(10000);
    }
  }
};

const connectWindowsBootstrapOnce = async (
  host: string,
  port: number,
  authToken: string
): Promise<WindowsBootstrap> => {
  const pending = createPendingRequestTable({ requestTimeoutMs: 30000 });
  let readyResolve: () => void = () => {};
  let readyReject: (error: Error) => void = () => {};
  const ready = new Promise<void>((resolve, reject) => {
    readyResolve = resolve;
    readyReject = reject;
  });
  const timer = setTimeout(
    () => readyReject(new Error('Windows bootstrap handshake timed out.')),
    30000
  );
  const transport = createTcpFrameTransport({
    host,
    authToken,
    port,
    timeoutMs: 30000,
    callbacks: {
      onOpen: () => {},
      onBinaryChunk: async () => {},
      onMessage: (message) => {
        if (message.kind === 'event' && message.name === 'agent.ready')
          readyResolve();
        else if (
          message.kind === 'event' &&
          message.name === 'agent.rejected' &&
          typeof message.data === 'object' &&
          message.data !== null &&
          !Array.isArray(message.data) &&
          message.data.code === 'AGENT_BUSY'
        )
          readyReject(
            Object.assign(new Error('Windows bootstrap agent is busy.'), {
              code: 'AGENT_BUSY',
            })
          );
        else if (message.kind === 'response') pending.acceptResponse(message);
      },
      onError: (error) => {
        readyReject(error);
        pending.rejectAll('DISCONNECTED', error.message);
      },
      onClose: (message) => {
        readyReject(new Error(message));
        pending.rejectAll('DISCONNECTED', message);
      },
    },
  });
  try {
    await ready;
  } catch (error) {
    await transport.close();
    throw error;
  } finally {
    clearTimeout(timer);
  }
  const request = async (
    method: string,
    params: JsonValue
  ): Promise<JsonValue | undefined> => {
    const entry = pending.createRequest(method, params);
    await transport.send(entry.message);
    return await entry.result;
  };
  return {
    request,
    releaseManaged: async (managedProcessId) => {
      await waitForResult(
        async () => {
          try {
            await request('process.releaseManaged', { managedProcessId });
          } catch (error) {
            // The bootstrap version waits only 25 ms per attempt and may report
            // ERROR_BUSY while a terminated agent's helpers are still exiting.
            if (
              error instanceof Error &&
              /win32Error=170\b/u.test(error.message)
            )
              throw new Error('Deployment Job is still exiting.', {
                cause: error,
              });
            throw error;
          }
        },
        { timeoutMs: 10000 }
      );
    },
    close: async () => {
      await transport.close();
    },
    upload: async (path, data) => {
      const transferId = randomUUID();
      const chunks = createBinaryTransferChunks({
        transferId,
        data,
        contentType: 'application/octet-stream',
        chunkSize: 65536,
      });
      for (const chunk of chunks) await transport.sendBinaryChunk(chunk);
      const last = chunks[chunks.length - 1];
      await request('file.write', {
        path,
        transferId,
        totalBytes: data.length,
        sha256: last?.sha256 ?? '',
        contentType: 'application/octet-stream',
      });
    },
  };
};
