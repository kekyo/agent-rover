// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { tcpFrameVersion as currentTcpFrameVersion } from '../protocol_version';

/** TCP frame magic written at the start of every agent-rover TCP frame. */
export const tcpFrameMagic = 'TRVR';

/** Current TCP frame protocol version. */
export { tcpFrameVersion } from '../protocol_version';

/** Fixed TCP frame header byte length. */
export const tcpFrameHeaderBytes = 20;

/** JSON protocol message frame kind. */
export const tcpFrameKindJson = 1 as const;

/** Reserved binary payload frame kind. */
export const tcpFrameKindBinary = 2 as const;

/** Ping frame kind. */
export const tcpFrameKindPing = 3 as const;

/** Pong frame kind. */
export const tcpFrameKindPong = 4 as const;

/** Close frame kind. */
export const tcpFrameKindClose = 5 as const;

/** Authentication challenge frame kind. */
export const tcpFrameKindAuthChallenge = 6 as const;

/** Authentication challenge response frame kind. */
export const tcpFrameKindAuthResponse = 7 as const;

/** Maximum JSON payload accepted by default. */
export const defaultTcpFrameMaxPayloadBytes = 16 * 1024 * 1024;

/** Frame kind values used by the TCP frame protocol. */
export type TcpFrameKind =
  | typeof tcpFrameKindAuthChallenge
  | typeof tcpFrameKindAuthResponse
  | typeof tcpFrameKindBinary
  | typeof tcpFrameKindClose
  | typeof tcpFrameKindJson
  | typeof tcpFrameKindPing
  | typeof tcpFrameKindPong;

/** Decoded TCP frame. */
export interface TcpFrame {
  /** Frame kind discriminator. */
  readonly kind: TcpFrameKind;
  /** Frame flags. The current protocol requires this to be zero. */
  readonly flags: number;
  /** Raw frame payload. */
  readonly payload: Buffer;
}

/** TCP frame input accepted by the encoder. */
export interface TcpFrameEncodeInput {
  /** Frame kind discriminator. */
  readonly kind: TcpFrameKind;
  /** Frame flags. The current protocol requires this to be zero. */
  readonly flags?: number;
  /** Raw frame payload. */
  readonly payload: Buffer;
}

/** TCP frame decoder configuration. */
export interface TcpFrameDecoderOptions {
  /** Maximum payload byte length accepted by the decoder. */
  readonly maxPayloadBytes: number;
}

/** Stateful decoder for TCP stream chunks. */
export interface TcpFrameDecoder {
  /** Accepts one TCP data chunk and emits every complete frame. */
  readonly accept: (data: Buffer) => readonly TcpFrame[];
}

interface TcpFrameProtocolError extends Error {
  readonly code: 'PROTOCOL_ERROR';
}

const tcpFrameKinds = new Set<number>([
  tcpFrameKindAuthChallenge,
  tcpFrameKindAuthResponse,
  tcpFrameKindBinary,
  tcpFrameKindClose,
  tcpFrameKindJson,
  tcpFrameKindPing,
  tcpFrameKindPong,
]);

const createTcpFrameProtocolError = (message: string): TcpFrameProtocolError =>
  Object.assign(new Error(message), {
    code: 'PROTOCOL_ERROR' as const,
  });

const validatePayloadLength = (
  value: number,
  maxPayloadBytes: number
): void => {
  if (value > maxPayloadBytes) {
    throw createTcpFrameProtocolError(
      `TCP frame payload exceeds the configured limit: ${String(value)}.`
    );
  }
};

const validateMaxPayloadBytes = (value: number): void => {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff) {
    throw createTcpFrameProtocolError(
      'maxPayloadBytes must be a safe uint32 value.'
    );
  }
};

const validateFrameKind = (value: number): TcpFrameKind => {
  if (!tcpFrameKinds.has(value)) {
    throw createTcpFrameProtocolError(
      `Unsupported TCP frame kind: ${String(value)}.`
    );
  }
  return value as TcpFrameKind;
};

const validateZeroField = (name: string, value: number): void => {
  if (value !== 0) {
    throw createTcpFrameProtocolError(`TCP frame ${name} must be zero.`);
  }
};

const validateFrameHeader = (
  header: Buffer,
  maxPayloadBytes: number
): {
  readonly flags: number;
  readonly kind: TcpFrameKind;
  readonly payloadLength: number;
} => {
  if (header.subarray(0, 4).toString('ascii') !== tcpFrameMagic) {
    throw createTcpFrameProtocolError('Invalid TCP frame magic.');
  }
  const version = header.readUInt16LE(4);
  if (version !== currentTcpFrameVersion) {
    throw createTcpFrameProtocolError(
      `Unsupported TCP frame version: ${String(version)}.`
    );
  }
  const kind = validateFrameKind(header.readUInt16LE(6));
  const flags = header.readUInt32LE(8);
  const payloadLength = header.readUInt32LE(12);
  const reserved = header.readUInt32LE(16);
  validateZeroField('flags', flags);
  validateZeroField('reserved', reserved);
  validatePayloadLength(payloadLength, maxPayloadBytes);
  return {
    flags,
    kind,
    payloadLength,
  };
};

/** Encodes one TCP frame. */
export const encodeTcpFrame = (frame: TcpFrameEncodeInput): Buffer => {
  const flags = frame.flags ?? 0;
  validateFrameKind(frame.kind);
  validateZeroField('flags', flags);
  if (frame.payload.byteLength > 0xffffffff) {
    throw createTcpFrameProtocolError('TCP frame payload is too large.');
  }

  const header = Buffer.alloc(tcpFrameHeaderBytes);
  header.write(tcpFrameMagic, 0, 'ascii');
  header.writeUInt16LE(currentTcpFrameVersion, 4);
  header.writeUInt16LE(frame.kind, 6);
  header.writeUInt32LE(flags, 8);
  header.writeUInt32LE(frame.payload.byteLength, 12);
  header.writeUInt32LE(0, 16);
  return Buffer.concat([header, frame.payload]);
};

/** Creates a stateful TCP frame decoder for one stream. */
export const createTcpFrameDecoder = (
  options: TcpFrameDecoderOptions
): TcpFrameDecoder => {
  validateMaxPayloadBytes(options.maxPayloadBytes);

  let buffered = Buffer.alloc(0);

  return {
    accept: (data): readonly TcpFrame[] => {
      buffered =
        buffered.byteLength === 0
          ? Buffer.from(data)
          : Buffer.concat([buffered, data]);
      const frames: TcpFrame[] = [];

      while (buffered.byteLength >= tcpFrameHeaderBytes) {
        const header = buffered.subarray(0, tcpFrameHeaderBytes);
        const decodedHeader = validateFrameHeader(
          header,
          options.maxPayloadBytes
        );
        const frameLength = tcpFrameHeaderBytes + decodedHeader.payloadLength;
        if (buffered.byteLength < frameLength) {
          break;
        }

        frames.push({
          flags: decodedHeader.flags,
          kind: decodedHeader.kind,
          payload: Buffer.from(
            buffered.subarray(tcpFrameHeaderBytes, frameLength)
          ),
        });
        buffered = buffered.subarray(frameLength);
      }

      return frames;
    },
  };
};
