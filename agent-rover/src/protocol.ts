// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash } from 'node:crypto';

export { protocolVersion } from './protocol_version';

/** JSON value allowed in protocol control messages. */
export type JsonValue =
  | null
  | boolean
  | number
  | string
  | readonly JsonValue[]
  | { readonly [key: string]: JsonValue };

/** Stable machine-readable protocol error codes. */
export type ProtocolErrorCode =
  | 'OPERATION_FAILED'
  | 'AUTHENTICATION_FAILED'
  | 'CHECKSUM_MISMATCH'
  | 'DISCONNECTED'
  | 'PROTOCOL_ERROR'
  | 'TIMEOUT'
  | 'TRANSFER_CANCELLED';

/** Error object raised by protocol helpers. */
export interface ProtocolRuntimeError extends Error {
  /** Stable machine-readable error code. */
  readonly code: ProtocolErrorCode;
  /** Structured native failure, when available. */
  readonly details?: import('./index').RemoteOperationErrorDetails;
}

/** Error payload transported over the wire. */
export interface ProtocolErrorPayload {
  /** Stable machine-readable error code. */
  readonly code: ProtocolErrorCode;
  /** Human-readable diagnostic message. */
  readonly message: string;
  /** Structured native failure, independent of the diagnostic message. */
  readonly details?: import('./index').RemoteOperationErrorDetails;
}

/** Request message sent over the control channel. */
export interface ProtocolRequestMessage {
  /** Message kind discriminator. */
  readonly kind: 'request';
  /** Request id used to pair this request with its response. */
  readonly id: string;
  /** Protocol method name. */
  readonly method: string;
  /** JSON method parameters. */
  readonly params?: JsonValue;
}

/** Successful response message sent over the control channel. */
export interface ProtocolSuccessResponseMessage {
  /** Message kind discriminator. */
  readonly kind: 'response';
  /** Request id this response resolves. */
  readonly id: string;
  /** Indicates successful completion. */
  readonly ok: true;
  /** JSON result payload. */
  readonly result?: JsonValue;
}

/** Failed response message sent over the control channel. */
export interface ProtocolFailureResponseMessage {
  /** Message kind discriminator. */
  readonly kind: 'response';
  /** Request id this response rejects. */
  readonly id: string;
  /** Indicates failed completion. */
  readonly ok: false;
  /** Error payload. */
  readonly error: ProtocolErrorPayload;
}

/** Response message sent over the control channel. */
export type ProtocolResponseMessage =
  ProtocolSuccessResponseMessage | ProtocolFailureResponseMessage;

/** Event message sent over the control channel. */
export interface ProtocolEventMessage {
  /** Message kind discriminator. */
  readonly kind: 'event';
  /** Event name. */
  readonly name: string;
  /** JSON event payload. */
  readonly data?: JsonValue;
}

/** Chunk message used by image and file transfer payloads. */
export interface ProtocolTransferChunkMessage {
  /** Message kind discriminator. */
  readonly kind: 'transfer.chunk';
  /** Transfer id shared by all chunks for one payload. */
  readonly transferId: string;
  /** Zero-based contiguous chunk sequence number. */
  readonly sequence: number;
  /** Base64-encoded chunk payload. */
  readonly dataBase64: string;
  /** Whether this is the final chunk. */
  readonly final: boolean;
  /** Total payload byte count, required on final chunks. */
  readonly totalBytes?: number;
  /** SHA-256 hex checksum, required on final chunks. */
  readonly sha256?: string;
}

/** Cancel message used to stop an in-flight transfer. */
export interface ProtocolTransferCancelMessage {
  /** Message kind discriminator. */
  readonly kind: 'transfer.cancel';
  /** Transfer id to cancel. */
  readonly transferId: string;
}

/** Any validated protocol message. */
export type ProtocolMessage =
  | ProtocolEventMessage
  | ProtocolRequestMessage
  | ProtocolResponseMessage
  | ProtocolTransferCancelMessage
  | ProtocolTransferChunkMessage;

/** Request entry returned by the pending request table. */
export interface PendingRequest {
  /** Wire message to send to the peer. */
  readonly message: ProtocolRequestMessage;
  /** Promise resolved or rejected by a matching response. */
  readonly result: Promise<JsonValue | undefined>;
}

/** Pending request table configuration. */
export interface PendingRequestTableOptions {
  /** Timeout applied to each request. */
  readonly requestTimeoutMs: number;
}

/** Utility that maps request ids to response promises. */
export interface PendingRequestTable {
  /** Creates a pending request and its result promise. */
  readonly createRequest: (
    method: string,
    params: JsonValue | undefined
  ) => PendingRequest;
  /** Accepts a validated response and resolves or rejects the matching request. */
  readonly acceptResponse: (response: ProtocolResponseMessage) => void;
  /** Rejects every pending request with the supplied error. */
  readonly rejectAll: (code: ProtocolErrorCode, message: string) => void;
  /** Current pending request count. */
  readonly pendingCount: () => number;
}

/** Options for splitting a binary payload into transfer chunks. */
export interface CreateTransferChunksOptions {
  /** Transfer id used for every generated chunk. */
  readonly transferId: string;
  /** Binary payload to split. */
  readonly data: Buffer;
  /** Maximum bytes per chunk. */
  readonly chunkSize: number;
}

/** Options for splitting a binary TCP frame transfer into raw chunks. */
export interface CreateBinaryTransferChunksOptions {
  /** Transfer id used for every generated chunk. */
  readonly transferId: string;
  /** MIME type carried by the transfer metadata. */
  readonly contentType: string;
  /** Binary payload to split. */
  readonly data: Buffer;
  /** Maximum bytes per chunk. */
  readonly chunkSize: number;
}

/** Raw binary chunk transported inside a TCP kind=2 frame. */
export interface ProtocolBinaryTransferChunk {
  /** Transfer id shared by all chunks for one payload. */
  readonly transferId: string;
  /** Zero-based contiguous chunk sequence number. */
  readonly sequence: number;
  /** Raw chunk payload bytes. */
  readonly data: Buffer;
  /** Whether this is the final chunk. */
  readonly final: boolean;
  /** MIME type carried by this transfer. */
  readonly contentType: string;
  /** Total payload byte count, required on final chunks. */
  readonly totalBytes?: number;
  /** SHA-256 hex checksum, required on final chunks. */
  readonly sha256?: string;
}

/** Receiver for raw binary TCP transfer chunks. */
export interface BinaryTransferReceiver {
  /** Accepts one binary transfer chunk. */
  readonly acceptChunk: (
    chunk: ProtocolBinaryTransferChunk
  ) => TransferReceiveResult;
  /** Cancels one transfer id. */
  readonly cancel: (transferId: string) => void;
}

/** Result returned when a transfer chunk is accepted. */
export type TransferReceiveResult =
  | {
      /** Transfer is still waiting for more chunks. */
      readonly state: 'pending';
    }
  | {
      /** Transfer completed and checksum verification passed. */
      readonly state: 'complete';
      /** Reassembled binary payload. */
      readonly data: Buffer;
    };

/** Receiver for base64 chunk transfer messages. */
export interface TransferReceiver {
  /** Accepts one transfer chunk. */
  readonly acceptChunk: (
    chunk: ProtocolTransferChunkMessage
  ) => TransferReceiveResult;
  /** Cancels one transfer id. */
  readonly cancel: (transferId: string) => void;
}

interface PendingEntry {
  readonly reject: (error: ProtocolRuntimeError) => void;
  readonly resolve: (value: JsonValue | undefined) => void;
  readonly timer: ReturnType<typeof setTimeout>;
}

interface TransferState {
  readonly chunks: Buffer[];
  nextSequence: number;
}

const hasOwn = (value: object, key: string): boolean =>
  Object.prototype.hasOwnProperty.call(value, key);

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const createProtocolRuntimeError = (
  code: ProtocolErrorCode,
  message: string
): ProtocolRuntimeError =>
  Object.assign(new Error(message), {
    code,
  });

const isJsonValue = (value: unknown): value is JsonValue => {
  if (
    value === null ||
    typeof value === 'boolean' ||
    typeof value === 'string'
  ) {
    return true;
  }
  if (typeof value === 'number') {
    return Number.isFinite(value);
  }
  if (Array.isArray(value)) {
    return value.every((item) => isJsonValue(item));
  }
  if (isRecord(value)) {
    return Object.values(value).every((item) => isJsonValue(item));
  }
  return false;
};

const requireString = (
  record: Record<string, unknown>,
  key: string
): string => {
  const value = record[key];
  if (typeof value !== 'string' || value.length === 0) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      `Protocol field ${key} must be a non-empty string.`
    );
  }
  return value;
};

const requireBoolean = (
  record: Record<string, unknown>,
  key: string
): boolean => {
  const value = record[key];
  if (typeof value !== 'boolean') {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      `Protocol field ${key} must be a boolean.`
    );
  }
  return value;
};

const requireInteger = (
  record: Record<string, unknown>,
  key: string
): number => {
  const value = record[key];
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      `Protocol field ${key} must be a non-negative safe integer.`
    );
  }
  return value;
};

const readOptionalJsonValue = (
  record: Record<string, unknown>,
  key: string
): JsonValue | undefined => {
  if (!hasOwn(record, key)) {
    return undefined;
  }
  const value = record[key];
  if (!isJsonValue(value)) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      `Protocol field ${key} must be a JSON value.`
    );
  }
  return value;
};

const readErrorPayload = (value: unknown): ProtocolErrorPayload => {
  if (!isRecord(value)) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'Protocol response error must be an object.'
    );
  }
  let details: import('./index').RemoteOperationErrorDetails | undefined;
  if (value.details !== undefined) {
    const raw = value.details;
    if (
      !isRecord(raw) ||
      !(
        raw.osCode === null ||
        (typeof raw.osCode === 'number' &&
          Number.isSafeInteger(raw.osCode) &&
          raw.osCode >= 0)
      )
    ) {
      throw createProtocolRuntimeError(
        'PROTOCOL_ERROR',
        'Invalid native error details.'
      );
    }
    const reason = requireString(raw, 'reason');
    if (
      ![
        'unknown',
        'sharingViolation',
        'lockViolation',
        'accessDenied',
        'permissionDenied',
        'readOnly',
        'notFound',
        'directoryNotEmpty',
        'busy',
        'unsupported',
        'invalidArgument',
      ].includes(reason)
    ) {
      throw createProtocolRuntimeError(
        'PROTOCOL_ERROR',
        'Invalid native error reason.'
      );
    }
    details = {
      operation: requireString(raw, 'operation'),
      nativeOperation:
        typeof raw.nativeOperation === 'string' ? raw.nativeOperation : '',
      path: typeof raw.path === 'string' ? raw.path : '',
      osCode: raw.osCode as number | null,
      reason: reason as import('./index').RemoteOperationErrorDetails['reason'],
      ...(typeof raw.stage === 'string' ? { stage: raw.stage } : {}),
      ...(Array.isArray(raw.repairs)
        ? {
            repairs: raw.repairs.map((repair) => {
              if (
                !isRecord(repair) ||
                !['clearReadOnly', 'grantDelete'].includes(
                  String(repair.action)
                ) ||
                !['applied', 'failed', 'skipped'].includes(
                  String(repair.outcome)
                ) ||
                !['notNeeded', 'restored', 'failed'].includes(
                  String(repair.restoration)
                ) ||
                typeof repair.osCode !== 'number' ||
                typeof repair.restoreOsCode !== 'number'
              )
                throw createProtocolRuntimeError(
                  'PROTOCOL_ERROR',
                  'Invalid cleanup repair details.'
                );
              return {
                path: requireString(repair, 'path'),
                action: repair.action,
                outcome: repair.outcome,
                osCode: repair.osCode,
                restoration: repair.restoration,
                restoreOsCode: repair.restoreOsCode,
              } as import('./index').RemoteCleanupRepair;
            }),
          }
        : {}),
    };
  }
  return {
    code: requireString(value, 'code') as ProtocolErrorCode,
    message: requireString(value, 'message'),
    ...(details === undefined ? {} : { details }),
  };
};

const readRequest = (
  record: Record<string, unknown>
): ProtocolRequestMessage => {
  const id = requireString(record, 'id');
  const method = requireString(record, 'method');
  const params = readOptionalJsonValue(record, 'params');
  return params === undefined
    ? {
        id,
        kind: 'request',
        method,
      }
    : {
        id,
        kind: 'request',
        method,
        params,
      };
};

const readResponse = (
  record: Record<string, unknown>
): ProtocolResponseMessage => {
  const id = requireString(record, 'id');
  const ok = requireBoolean(record, 'ok');
  if (ok) {
    const result = readOptionalJsonValue(record, 'result');
    return result === undefined
      ? {
          id,
          kind: 'response',
          ok: true,
        }
      : {
          id,
          kind: 'response',
          ok: true,
          result,
        };
  }
  return {
    error: readErrorPayload(record.error),
    id,
    kind: 'response',
    ok: false,
  };
};

const readEvent = (record: Record<string, unknown>): ProtocolEventMessage => {
  const name = requireString(record, 'name');
  const data = readOptionalJsonValue(record, 'data');
  return data === undefined
    ? {
        kind: 'event',
        name,
      }
    : {
        data,
        kind: 'event',
        name,
      };
};

const readTransferChunk = (
  record: Record<string, unknown>
): ProtocolTransferChunkMessage => {
  const transferId = requireString(record, 'transferId');
  const sequence = requireInteger(record, 'sequence');
  const dataBase64 = requireString(record, 'dataBase64');
  const final = requireBoolean(record, 'final');
  const totalBytes = hasOwn(record, 'totalBytes')
    ? requireInteger(record, 'totalBytes')
    : undefined;
  const sha256 = hasOwn(record, 'sha256')
    ? requireString(record, 'sha256')
    : undefined;
  return {
    dataBase64,
    final,
    kind: 'transfer.chunk',
    sequence,
    ...(sha256 === undefined ? {} : { sha256 }),
    ...(totalBytes === undefined ? {} : { totalBytes }),
    transferId,
  };
};

const readTransferCancel = (
  record: Record<string, unknown>
): ProtocolTransferCancelMessage => ({
  kind: 'transfer.cancel',
  transferId: requireString(record, 'transferId'),
});

const readBinaryTransferMetadata = (
  value: unknown
): Omit<ProtocolBinaryTransferChunk, 'data'> => {
  if (!isRecord(value)) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'Binary transfer metadata must be an object.'
    );
  }
  const totalBytes = hasOwn(value, 'totalBytes')
    ? requireInteger(value, 'totalBytes')
    : undefined;
  const sha256 = hasOwn(value, 'sha256')
    ? requireString(value, 'sha256')
    : undefined;
  return {
    contentType: requireString(value, 'contentType'),
    final: requireBoolean(value, 'final'),
    sequence: requireInteger(value, 'sequence'),
    ...(sha256 === undefined ? {} : { sha256 }),
    ...(totalBytes === undefined ? {} : { totalBytes }),
    transferId: requireString(value, 'transferId'),
  };
};

const sha256Hex = (data: Buffer): string =>
  createHash('sha256').update(data).digest('hex');

/** Parses and validates one decoded JSON protocol message. */
export const parseProtocolMessage = (input: unknown): ProtocolMessage => {
  if (!isRecord(input)) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'Protocol message must be an object.'
    );
  }

  const kind = requireString(input, 'kind');
  switch (kind) {
    case 'event':
      return readEvent(input);
    case 'request':
      return readRequest(input);
    case 'response':
      return readResponse(input);
    case 'transfer.cancel':
      return readTransferCancel(input);
    case 'transfer.chunk':
      return readTransferChunk(input);
    default:
      throw createProtocolRuntimeError(
        'PROTOCOL_ERROR',
        `Unsupported protocol message kind: ${kind}.`
      );
  }
};

/** Creates a table that pairs outgoing requests with incoming responses. */
export const createPendingRequestTable = (
  options: PendingRequestTableOptions
): PendingRequestTable => {
  if (
    !Number.isSafeInteger(options.requestTimeoutMs) ||
    options.requestTimeoutMs <= 0
  ) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'requestTimeoutMs must be a positive safe integer.'
    );
  }

  let nextRequestId = 1;
  const pending = new Map<string, PendingEntry>();

  const removeEntry = (id: string): PendingEntry | undefined => {
    const entry = pending.get(id);
    if (entry === undefined) {
      return undefined;
    }
    pending.delete(id);
    clearTimeout(entry.timer);
    return entry;
  };

  return {
    acceptResponse: (response): void => {
      const entry = removeEntry(response.id);
      if (entry === undefined) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          `No pending request for response id: ${response.id}.`
        );
      }

      if (response.ok) {
        entry.resolve(response.result);
      } else {
        entry.reject(
          Object.assign(
            createProtocolRuntimeError(
              response.error.code,
              response.error.message
            ),
            response.error.details === undefined
              ? {}
              : { details: response.error.details }
          )
        );
      }
    },
    createRequest: (method, params): PendingRequest => {
      const id = `req-${String(nextRequestId)}`;
      nextRequestId += 1;

      let resolveResult: ((value: JsonValue | undefined) => void) | undefined =
        undefined;
      let rejectResult: ((error: ProtocolRuntimeError) => void) | undefined =
        undefined;

      const result = new Promise<JsonValue | undefined>((resolve, reject) => {
        resolveResult = resolve;
        rejectResult = reject;
      });
      if (resolveResult === undefined || rejectResult === undefined) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          'Failed to create pending request promise.'
        );
      }

      const timer = setTimeout(() => {
        const entry = removeEntry(id);
        if (entry !== undefined) {
          entry.reject(
            createProtocolRuntimeError(
              'TIMEOUT',
              `Request timed out: ${method}.`
            )
          );
        }
      }, options.requestTimeoutMs);

      pending.set(id, {
        reject: rejectResult,
        resolve: resolveResult,
        timer,
      });

      return {
        message:
          params === undefined
            ? {
                id,
                kind: 'request',
                method,
              }
            : {
                id,
                kind: 'request',
                method,
                params,
              },
        result,
      };
    },
    pendingCount: (): number => pending.size,
    rejectAll: (code, message): void => {
      for (const [id, entry] of pending) {
        pending.delete(id);
        clearTimeout(entry.timer);
        entry.reject(createProtocolRuntimeError(code, message));
      }
    },
  };
};

/** Splits a binary payload into validated base64 transfer chunks. */
export const createTransferChunks = (
  options: CreateTransferChunksOptions
): readonly ProtocolTransferChunkMessage[] => {
  if (!Number.isSafeInteger(options.chunkSize) || options.chunkSize <= 0) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'chunkSize must be a positive safe integer.'
    );
  }

  const chunks: ProtocolTransferChunkMessage[] = [];
  const checksum = sha256Hex(options.data);
  const totalBytes = options.data.byteLength;
  const chunkCount = Math.max(1, Math.ceil(totalBytes / options.chunkSize));

  for (let sequence = 0; sequence < chunkCount; sequence += 1) {
    const start = sequence * options.chunkSize;
    const end = Math.min(start + options.chunkSize, totalBytes);
    const final = sequence === chunkCount - 1;
    chunks.push({
      dataBase64: options.data.subarray(start, end).toString('base64'),
      final,
      kind: 'transfer.chunk',
      sequence,
      ...(final
        ? {
            sha256: checksum,
            totalBytes,
          }
        : {}),
      transferId: options.transferId,
    });
  }

  return chunks;
};

/** Splits a binary payload into raw TCP binary transfer chunks. */
export const createBinaryTransferChunks = (
  options: CreateBinaryTransferChunksOptions
): readonly ProtocolBinaryTransferChunk[] => {
  if (!Number.isSafeInteger(options.chunkSize) || options.chunkSize <= 0) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'chunkSize must be a positive safe integer.'
    );
  }

  const chunks: ProtocolBinaryTransferChunk[] = [];
  const checksum = sha256Hex(options.data);
  const totalBytes = options.data.byteLength;
  const chunkCount = Math.max(1, Math.ceil(totalBytes / options.chunkSize));

  for (let sequence = 0; sequence < chunkCount; sequence += 1) {
    const start = sequence * options.chunkSize;
    const end = Math.min(start + options.chunkSize, totalBytes);
    const final = sequence === chunkCount - 1;
    chunks.push({
      contentType: options.contentType,
      data: options.data.subarray(start, end),
      final,
      sequence,
      ...(final
        ? {
            sha256: checksum,
            totalBytes,
          }
        : {}),
      transferId: options.transferId,
    });
  }

  return chunks;
};

/** Encodes one binary transfer chunk as a TCP kind=2 frame payload. */
export const encodeBinaryTransferChunkPayload = (
  chunk: ProtocolBinaryTransferChunk
): Buffer => {
  const metadata = Buffer.from(
    JSON.stringify({
      contentType: chunk.contentType,
      final: chunk.final,
      sequence: chunk.sequence,
      ...(chunk.sha256 === undefined ? {} : { sha256: chunk.sha256 }),
      ...(chunk.totalBytes === undefined
        ? {}
        : { totalBytes: chunk.totalBytes }),
      transferId: chunk.transferId,
    }),
    'utf8'
  );
  const header = Buffer.alloc(4);
  header.writeUInt32LE(metadata.byteLength, 0);
  return Buffer.concat([header, metadata, chunk.data]);
};

/** Decodes one TCP kind=2 frame payload into a binary transfer chunk. */
export const parseBinaryTransferChunkPayload = (
  payload: Buffer
): ProtocolBinaryTransferChunk => {
  if (payload.byteLength < 4) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'Binary transfer frame payload is too short.'
    );
  }
  const metadataLength = payload.readUInt32LE(0);
  const metadataStart = 4;
  const dataStart = metadataStart + metadataLength;
  if (metadataLength === 0 || dataStart > payload.byteLength) {
    throw createProtocolRuntimeError(
      'PROTOCOL_ERROR',
      'Binary transfer metadata length is invalid.'
    );
  }

  const metadata = readBinaryTransferMetadata(
    JSON.parse(payload.subarray(metadataStart, dataStart).toString('utf8'))
  );
  return {
    ...metadata,
    data: payload.subarray(dataStart),
  };
};

/** Creates a receiver for chunk transfer messages. */
export const createTransferReceiver = (): TransferReceiver => {
  const transfers = new Map<string, TransferState>();
  const cancelled = new Set<string>();

  const getState = (chunk: ProtocolTransferChunkMessage): TransferState => {
    const current = transfers.get(chunk.transferId);
    if (current !== undefined) {
      return current;
    }
    if (chunk.sequence !== 0) {
      throw createProtocolRuntimeError(
        'PROTOCOL_ERROR',
        `First transfer chunk must use sequence 0: ${chunk.transferId}.`
      );
    }
    const created = {
      chunks: [],
      nextSequence: 0,
    };
    transfers.set(chunk.transferId, created);
    return created;
  };

  return {
    acceptChunk: (chunk): TransferReceiveResult => {
      if (cancelled.has(chunk.transferId)) {
        throw createProtocolRuntimeError(
          'TRANSFER_CANCELLED',
          `Transfer was cancelled: ${chunk.transferId}.`
        );
      }

      const state = getState(chunk);
      if (chunk.sequence !== state.nextSequence) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          `Unexpected transfer sequence for ${chunk.transferId}.`
        );
      }

      state.chunks.push(Buffer.from(chunk.dataBase64, 'base64'));
      state.nextSequence += 1;

      if (!chunk.final) {
        return {
          state: 'pending',
        };
      }

      if (chunk.totalBytes === undefined || chunk.sha256 === undefined) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          'Final transfer chunk must include totalBytes and sha256.'
        );
      }

      const data = Buffer.concat(state.chunks);
      transfers.delete(chunk.transferId);

      if (data.byteLength !== chunk.totalBytes) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          `Transfer size mismatch for ${chunk.transferId}.`
        );
      }
      if (sha256Hex(data) !== chunk.sha256) {
        throw createProtocolRuntimeError(
          'CHECKSUM_MISMATCH',
          `Transfer checksum mismatch for ${chunk.transferId}.`
        );
      }

      return {
        data,
        state: 'complete',
      };
    },
    cancel: (transferId): void => {
      transfers.delete(transferId);
      cancelled.add(transferId);
    },
  };
};

/** Creates a receiver for raw TCP binary transfer chunks. */
export const createBinaryTransferReceiver = (): BinaryTransferReceiver => {
  const transfers = new Map<string, TransferState>();
  const cancelled = new Set<string>();

  const getState = (chunk: ProtocolBinaryTransferChunk): TransferState => {
    const current = transfers.get(chunk.transferId);
    if (current !== undefined) {
      return current;
    }
    if (chunk.sequence !== 0) {
      throw createProtocolRuntimeError(
        'PROTOCOL_ERROR',
        `First binary transfer chunk must use sequence 0: ${chunk.transferId}.`
      );
    }
    const created = {
      chunks: [],
      nextSequence: 0,
    };
    transfers.set(chunk.transferId, created);
    return created;
  };

  return {
    acceptChunk: (chunk): TransferReceiveResult => {
      if (cancelled.has(chunk.transferId)) {
        throw createProtocolRuntimeError(
          'TRANSFER_CANCELLED',
          `Binary transfer was cancelled: ${chunk.transferId}.`
        );
      }

      const state = getState(chunk);
      if (chunk.sequence !== state.nextSequence) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          `Unexpected binary transfer sequence for ${chunk.transferId}.`
        );
      }

      state.chunks.push(chunk.data);
      state.nextSequence += 1;

      if (!chunk.final) {
        return {
          state: 'pending',
        };
      }

      if (chunk.totalBytes === undefined || chunk.sha256 === undefined) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          'Final binary transfer chunk must include totalBytes and sha256.'
        );
      }

      const data = Buffer.concat(state.chunks);
      transfers.delete(chunk.transferId);

      if (data.byteLength !== chunk.totalBytes) {
        throw createProtocolRuntimeError(
          'PROTOCOL_ERROR',
          `Binary transfer size mismatch for ${chunk.transferId}.`
        );
      }
      if (sha256Hex(data) !== chunk.sha256) {
        throw createProtocolRuntimeError(
          'CHECKSUM_MISMATCH',
          `Binary transfer checksum mismatch for ${chunk.transferId}.`
        );
      }

      return {
        data,
        state: 'complete',
      };
    },
    cancel: (transferId): void => {
      transfers.delete(transferId);
      cancelled.add(transferId);
    },
  };
};
