// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import {
  createTcpFrameDecoder,
  encodeTcpFrame,
  tcpFrameKindAuthChallenge,
  tcpFrameKindAuthResponse,
  tcpFrameKindJson,
  tcpFrameMagic,
  tcpFrameVersion,
} from '../src/driver/tcp-frame';

const captureThrown = (callback: () => void): unknown => {
  try {
    callback();
  } catch (error) {
    return error;
  }
  throw new Error('Expected callback to throw.');
};

describe.concurrent('tcp frame codec', () => {
  it('uses tcp frame protocol version 2', () => {
    expect(tcpFrameVersion).toBe(2);
  });

  it('encodes and decodes a JSON frame header and payload', () => {
    const payload = Buffer.from('{"kind":"event"}', 'utf8');
    const encoded = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload,
    });
    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 1024,
    });

    expect(encoded.subarray(0, 4).toString('ascii')).toBe(tcpFrameMagic);
    expect(encoded.readUInt16LE(4)).toBe(tcpFrameVersion);
    expect(encoded.readUInt16LE(6)).toBe(tcpFrameKindJson);
    expect(encoded.readUInt32LE(12)).toBe(payload.byteLength);
    expect(decoder.accept(encoded)).toEqual([
      {
        flags: 0,
        kind: tcpFrameKindJson,
        payload,
      },
    ]);
  });

  it('waits for partial reads before emitting a complete frame', () => {
    const payload = Buffer.from('partial payload', 'utf8');
    const encoded = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload,
    });
    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 1024,
    });

    expect(decoder.accept(encoded.subarray(0, 7))).toEqual([]);
    expect(decoder.accept(encoded.subarray(7))).toEqual([
      {
        flags: 0,
        kind: tcpFrameKindJson,
        payload,
      },
    ]);
  });

  it('encodes and decodes authentication challenge and response frames', () => {
    const challenge = Buffer.from(
      Array.from({ length: 32 }, (_, index) => index)
    );
    const response = Buffer.from(
      Array.from({ length: 32 }, (_, index) => 255 - index)
    );
    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 1024,
    });

    expect(
      decoder.accept(
        Buffer.concat([
          encodeTcpFrame({
            kind: tcpFrameKindAuthChallenge,
            payload: challenge,
          }),
          encodeTcpFrame({
            kind: tcpFrameKindAuthResponse,
            payload: response,
          }),
        ])
      )
    ).toEqual([
      {
        flags: 0,
        kind: tcpFrameKindAuthChallenge,
        payload: challenge,
      },
      {
        flags: 0,
        kind: tcpFrameKindAuthResponse,
        payload: response,
      },
    ]);
  });

  it('emits multiple frames received in one read', () => {
    const first = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from('first', 'utf8'),
    });
    const second = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from('second', 'utf8'),
    });
    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 1024,
    });

    expect(decoder.accept(Buffer.concat([first, second]))).toEqual([
      {
        flags: 0,
        kind: tcpFrameKindJson,
        payload: Buffer.from('first', 'utf8'),
      },
      {
        flags: 0,
        kind: tcpFrameKindJson,
        payload: Buffer.from('second', 'utf8'),
      },
    ]);
  });

  it('rejects invalid magic, unsupported versions, and oversized payloads', () => {
    const valid = encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from('data', 'utf8'),
    });
    const invalidMagic = Buffer.from(valid);
    invalidMagic[0] = 0x58;
    const unsupportedVersion = Buffer.from(valid);
    unsupportedVersion.writeUInt16LE(tcpFrameVersion + 1, 4);

    expect(
      captureThrown(() => {
        createTcpFrameDecoder({
          maxPayloadBytes: 1024,
        }).accept(invalidMagic);
      })
    ).toMatchObject({
      code: 'PROTOCOL_ERROR',
    });
    expect(
      captureThrown(() => {
        createTcpFrameDecoder({
          maxPayloadBytes: 1024,
        }).accept(unsupportedVersion);
      })
    ).toMatchObject({
      code: 'PROTOCOL_ERROR',
    });
    expect(
      captureThrown(() => {
        createTcpFrameDecoder({
          maxPayloadBytes: 3,
        }).accept(valid);
      })
    ).toMatchObject({
      code: 'PROTOCOL_ERROR',
    });
  });
});
