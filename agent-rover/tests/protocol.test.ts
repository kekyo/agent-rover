// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it, vi } from 'vitest';

import {
  createBinaryTransferChunks,
  createPendingRequestTable,
  createTransferChunks,
  createTransferReceiver,
  encodeBinaryTransferChunkPayload,
  parseBinaryTransferChunkPayload,
  type ProtocolTransferChunkMessage,
  parseProtocolMessage,
  protocolVersion,
} from '../src/protocol';

const captureThrown = (callback: () => void): unknown => {
  try {
    callback();
  } catch (error) {
    return error;
  }
  throw new Error('Expected callback to throw.');
};

describe('protocol message validation', () => {
  it('accepts request and response messages with matching protocol shape', () => {
    expect(
      parseProtocolMessage({
        id: 'req-1',
        kind: 'request',
        method: 'agent.capabilities',
        params: { includeFeatures: true },
      })
    ).toEqual({
      id: 'req-1',
      kind: 'request',
      method: 'agent.capabilities',
      params: { includeFeatures: true },
    });

    expect(
      parseProtocolMessage({
        id: 'req-1',
        kind: 'response',
        ok: true,
        result: { protocolVersion },
      })
    ).toEqual({
      id: 'req-1',
      kind: 'response',
      ok: true,
      result: { protocolVersion },
    });
  });

  it('rejects invalid protocol messages with a machine-readable error', () => {
    expect(
      captureThrown(() => {
        parseProtocolMessage({
          id: '',
          kind: 'request',
          method: 'agent.capabilities',
        });
      })
    ).toMatchObject({
      code: 'PROTOCOL_ERROR',
    });
  });
});

describe('pending request table', () => {
  it('resolves the request that matches a response id', async () => {
    const table = createPendingRequestTable({
      requestTimeoutMs: 30000,
    });
    const request = table.createRequest('agent.capabilities', {});

    table.acceptResponse({
      id: request.message.id,
      kind: 'response',
      ok: true,
      result: { protocolVersion },
    });

    await expect(request.result).resolves.toEqual({ protocolVersion });
    expect(table.pendingCount()).toBe(0);
  });

  it('fails pending requests when the transport disconnects', async () => {
    const table = createPendingRequestTable({
      requestTimeoutMs: 30000,
    });
    const request = table.createRequest('agent.capabilities', {});

    table.rejectAll('DISCONNECTED', 'Transport closed.');

    await expect(request.result).rejects.toMatchObject({
      code: 'DISCONNECTED',
    });
    expect(table.pendingCount()).toBe(0);
  });

  it('uses deterministic fake time for request timeout handling', async () => {
    vi.useFakeTimers();
    try {
      const table = createPendingRequestTable({
        requestTimeoutMs: 25,
      });
      const request = table.createRequest('agent.capabilities', {});
      const expectation = expect(request.result).rejects.toMatchObject({
        code: 'TIMEOUT',
      });

      await vi.advanceTimersByTimeAsync(25);
      await expectation;

      expect(table.pendingCount()).toBe(0);
    } finally {
      vi.useRealTimers();
    }
  });
});

describe('chunk transfer protocol', () => {
  it('reassembles chunks and verifies the final checksum', () => {
    const data = Buffer.from('remote file data');
    const receiver = createTransferReceiver();
    const chunks = createTransferChunks({
      chunkSize: 5,
      data,
      transferId: 'transfer-1',
    });
    let completed: Buffer | undefined = undefined;

    for (const chunk of chunks) {
      const result = receiver.acceptChunk(chunk);
      if (result.state === 'complete') {
        completed = result.data;
      }
    }

    expect(completed).toEqual(data);
  });

  it('rejects a transfer when the checksum does not match', () => {
    const data = Buffer.from('remote file data');
    const receiver = createTransferReceiver();
    const chunks = createTransferChunks({
      chunkSize: 6,
      data,
      transferId: 'transfer-1',
    });
    const finalChunk = chunks[chunks.length - 1];
    if (finalChunk === undefined) {
      throw new Error('Expected at least one transfer chunk.');
    }
    const tamperedFinal: ProtocolTransferChunkMessage = {
      ...finalChunk,
      sha256: '0'.repeat(64),
    };

    for (const chunk of chunks.slice(0, -1)) {
      receiver.acceptChunk(chunk);
    }

    expect(
      captureThrown(() => {
        receiver.acceptChunk(tamperedFinal);
      })
    ).toMatchObject({
      code: 'CHECKSUM_MISMATCH',
    });
  });

  it('cancels a transfer before accepting more chunks', () => {
    const data = Buffer.from('remote file data');
    const receiver = createTransferReceiver();
    const chunks = createTransferChunks({
      chunkSize: 5,
      data,
      transferId: 'transfer-1',
    });
    const firstChunk = chunks[0];
    const secondChunk = chunks[1];
    if (firstChunk === undefined || secondChunk === undefined) {
      throw new Error('Expected at least two transfer chunks.');
    }

    receiver.acceptChunk(firstChunk);
    receiver.cancel('transfer-1');

    expect(
      captureThrown(() => {
        receiver.acceptChunk(secondChunk);
      })
    ).toMatchObject({
      code: 'TRANSFER_CANCELLED',
    });
  });
});

describe('binary transfer frame payload', () => {
  it('round-trips raw chunk bytes without base64 encoding the payload', () => {
    const data = Buffer.from([0x00, 0x01, 0x02, 0xf0, 0xff]);
    const [chunk] = createBinaryTransferChunks({
      chunkSize: 1024,
      contentType: 'application/octet-stream',
      data,
      transferId: 'binary-transfer-1',
    });
    if (chunk === undefined) {
      throw new Error('Expected one binary transfer chunk.');
    }

    const payload = encodeBinaryTransferChunkPayload(chunk);
    const parsed = parseBinaryTransferChunkPayload(payload);

    expect(parsed).toMatchObject({
      contentType: 'application/octet-stream',
      final: true,
      sequence: 0,
      sha256:
        'b3d1a66eb6bf2c51419b9741bbeb06bd856b50f4ffdbc4a4407ed617932d6f9a',
      totalBytes: data.byteLength,
      transferId: 'binary-transfer-1',
    });
    expect(parsed.data).toEqual(data);
    expect(payload.subarray(payload.byteLength - data.byteLength)).toEqual(
      data
    );
  });
});
