// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { connect as connectTcpSocket, type Socket } from 'node:net';

import { authChallengeBytes, createAuthChallengeResponse } from '../auth';
import {
  encodeBinaryTransferChunkPayload,
  parseBinaryTransferChunkPayload,
  parseProtocolMessage,
  type ProtocolBinaryTransferChunk,
  type ProtocolMessage,
} from '../protocol';
import {
  createTcpFrameDecoder,
  defaultTcpFrameMaxPayloadBytes,
  encodeTcpFrame,
  tcpFrameKindBinary,
  tcpFrameKindAuthChallenge,
  tcpFrameKindAuthResponse,
  tcpFrameKindClose,
  tcpFrameKindJson,
  tcpFrameKindPing,
  tcpFrameKindPong,
  type TcpFrame,
} from './tcp-frame';

/** Callbacks used by a protocol transport to report peer activity. */
export interface ProtocolTransportCallbacks {
  /** Reports that the underlying socket connection has opened. */
  readonly onOpen: () => void;
  /** Accepts one validated protocol message. */
  readonly onMessage: (message: ProtocolMessage) => void;
  /** Accepts one raw binary transfer chunk. */
  readonly onBinaryChunk: (chunk: ProtocolBinaryTransferChunk) => Promise<void>;
  /** Reports a transport close notification. */
  readonly onClose: (message: string) => void;
  /** Reports a transport or protocol parsing error. */
  readonly onError: (error: Error) => void;
}

/** Connected protocol transport used by the remote agent driver. */
export interface ProtocolTransport {
  /** Sends one JSON protocol message to the peer. */
  readonly send: (message: ProtocolMessage) => Promise<void>;
  /** Sends one raw binary transfer chunk to the peer. */
  readonly sendBinaryChunk: (
    chunk: ProtocolBinaryTransferChunk
  ) => Promise<void>;
  /** Closes the transport. */
  readonly close: () => Promise<void>;
  /** Returns whether the transport can currently send messages. */
  readonly isOpen: () => boolean;
}

/** Options used to create a TCP frame protocol transport. */
export interface TcpFrameTransportOptions {
  /** Token used to answer an authentication challenge. */
  readonly authToken: string | undefined;
  /** Agent host name, FQDN, or IP address. */
  readonly host: string;
  /** Agent TCP port. */
  readonly port: number;
  /** TCP connect and individual write deadline in milliseconds. */
  readonly timeoutMs: number;
  /** Protocol transport callbacks. */
  readonly callbacks: ProtocolTransportCallbacks;
}

const parseTcpJsonFrame = (frame: TcpFrame): ProtocolMessage =>
  parseProtocolMessage(JSON.parse(frame.payload.toString('utf8')));

const writeSocket = async (
  socket: Socket,
  data: Buffer,
  timeoutMs: number
): Promise<void> => {
  if (socket.writableLength + data.length > 32 * 1024 * 1024) {
    socket.destroy();
    throw new Error('TCP outbound queue exceeds 32 MiB.');
  }
  await new Promise<void>((resolve, reject) => {
    let finished = false;
    const complete = (error: Error | undefined): void => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      socket.off('close', onClose);
      socket.off('error', onError);
      if (error) reject(error);
      else resolve();
    };
    const onClose = (): void =>
      complete(new Error('TCP connection closed during write.'));
    const onError = (error: Error): void => complete(error);
    const timer = setTimeout(() => {
      complete(new Error('TCP frame write timed out.'));
      socket.destroy();
    }, timeoutMs);
    socket.once('close', onClose);
    socket.once('error', onError);
    try {
      socket.write(data, (error) => complete(error ?? undefined));
    } catch (error) {
      complete(error instanceof Error ? error : new Error('TCP write failed.'));
    }
  });
};

const writeTcpFrame = async (
  socket: Socket,
  kind: TcpFrame['kind'],
  payload: Buffer,
  timeoutMs: number
): Promise<void> => {
  await writeSocket(
    socket,
    encodeTcpFrame({
      kind,
      payload,
    }),
    timeoutMs
  );
};

const writeTcpPong = (
  socket: Socket,
  callbacks: ProtocolTransportCallbacks,
  timeoutMs: number
): void => {
  void (async (): Promise<void> => {
    try {
      await writeTcpFrame(socket, tcpFrameKindPong, Buffer.alloc(0), timeoutMs);
    } catch (error) {
      callbacks.onError(
        error instanceof Error ? error : new Error('Failed to write TCP pong.')
      );
      socket.destroy();
    }
  })();
};

const closeWithProtocolError = (
  socket: Socket,
  callbacks: ProtocolTransportCallbacks,
  error: Error
): void => {
  callbacks.onError(error);
  socket.destroy();
};

const writeTcpAuthResponse = (
  socket: Socket,
  callbacks: ProtocolTransportCallbacks,
  authToken: string,
  challenge: Buffer,
  timeoutMs: number
): void => {
  void (async (): Promise<void> => {
    try {
      await writeTcpFrame(
        socket,
        tcpFrameKindAuthResponse,
        createAuthChallengeResponse(authToken, challenge),
        timeoutMs
      );
    } catch (error) {
      callbacks.onError(
        error instanceof Error
          ? error
          : new Error('Failed to write TCP auth response.')
      );
      socket.destroy();
    }
  })();
};

/** Creates a TCP-frame-backed protocol transport. */
export const createTcpFrameTransport = (
  options: TcpFrameTransportOptions
): ProtocolTransport => {
  const socket = connectTcpSocket({
    host: options.host,
    port: options.port,
  });
  const decoder = createTcpFrameDecoder({
    maxPayloadBytes: defaultTcpFrameMaxPayloadBytes,
  });
  let open = false;
  let authChallengeAnswered = false;
  let ready = false;
  let closing: Promise<void> | undefined;

  const acceptSocketData = async (data: Buffer): Promise<void> => {
    try {
      for (const frame of decoder.accept(Buffer.from(data))) {
        switch (frame.kind) {
          case tcpFrameKindJson: {
            const message = parseTcpJsonFrame(frame);
            if (message.kind === 'event' && message.name === 'agent.ready')
              ready = true;
            options.callbacks.onMessage(message);
            break;
          }
          case tcpFrameKindPing:
            writeTcpPong(socket, options.callbacks, options.timeoutMs);
            break;
          case tcpFrameKindPong:
            break;
          case tcpFrameKindClose:
            socket.destroy();
            break;
          case tcpFrameKindBinary:
            await options.callbacks.onBinaryChunk(
              parseBinaryTransferChunkPayload(frame.payload)
            );
            break;
          case tcpFrameKindAuthChallenge:
            if (
              authChallengeAnswered ||
              frame.payload.byteLength !== authChallengeBytes
            ) {
              closeWithProtocolError(
                socket,
                options.callbacks,
                new Error('Invalid TCP auth challenge.')
              );
              break;
            }
            if (options.authToken === undefined) {
              closeWithProtocolError(
                socket,
                options.callbacks,
                new Error(
                  'Authentication challenge received but no auth token was provided.'
                )
              );
              break;
            }
            authChallengeAnswered = true;
            writeTcpAuthResponse(
              socket,
              options.callbacks,
              options.authToken,
              frame.payload,
              options.timeoutMs
            );
            break;
          case tcpFrameKindAuthResponse:
            closeWithProtocolError(
              socket,
              options.callbacks,
              new Error('Unexpected TCP auth response.')
            );
            break;
        }
      }
    } catch (error) {
      closeWithProtocolError(
        socket,
        options.callbacks,
        error instanceof Error ? error : new Error('Invalid TCP frame.')
      );
    } finally {
      if (!socket.destroyed) {
        socket.resume();
      }
    }
  };

  socket.setNoDelay(true);
  socket.setTimeout(options.timeoutMs, () => {
    closeWithProtocolError(
      socket,
      options.callbacks,
      new Error('Timed out connecting to TCP frame agent.')
    );
  });
  socket.on('connect', () => {
    if (closing !== undefined) {
      socket.destroy();
      return;
    }
    open = true;
    socket.setTimeout(0);
    options.callbacks.onOpen();
  });
  socket.on('data', (data) => {
    socket.pause();
    void acceptSocketData(Buffer.from(data));
  });
  socket.on('error', (error) => {
    options.callbacks.onError(error);
  });
  socket.on('close', () => {
    open = false;
    options.callbacks.onClose('TCP frame connection closed.');
  });

  return {
    close: async (): Promise<void> => {
      if (closing !== undefined) return await closing;
      if (socket.destroyed) {
        return;
      }
      const canSendClose = open && ready;
      open = false;
      closing = new Promise<void>((resolve) => {
        const timer = setTimeout(
          () => socket.destroy(),
          Math.min(options.timeoutMs, 1000)
        );
        socket.once('close', () => {
          clearTimeout(timer);
          resolve();
        });
        if (canSendClose) {
          socket.end(
            encodeTcpFrame({
              kind: tcpFrameKindClose,
              payload: Buffer.alloc(0),
            })
          );
        } else {
          socket.destroy();
        }
      });
      await closing;
    },
    isOpen: (): boolean => open && !socket.destroyed,
    send: async (message): Promise<void> => {
      if (!open || socket.destroyed) {
        throw new Error('TCP frame socket is not open.');
      }
      await writeTcpFrame(
        socket,
        tcpFrameKindJson,
        Buffer.from(JSON.stringify(message), 'utf8'),
        options.timeoutMs
      );
    },
    sendBinaryChunk: async (chunk): Promise<void> => {
      if (!open || socket.destroyed) {
        throw new Error('TCP frame socket is not open.');
      }
      await writeTcpFrame(
        socket,
        tcpFrameKindBinary,
        encodeBinaryTransferChunkPayload(chunk),
        options.timeoutMs
      );
    },
  };
};
