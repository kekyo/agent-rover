// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash } from 'node:crypto';

import type {
  AppWindow,
  AppWindowDescendantsOptions,
  AppWindowProcess,
  AppWindowScreenshot,
  AppWindowSnapshot,
  ConnectRemoteAgentOptions,
  ScreenPoint,
  ScreenRect,
  EventLogEntry,
  EventLogLevel,
  EventLogQuery,
  KeyboardModifier,
  MouseButton,
  RemoteApplicationLaunchOptions,
  RemoteApplicationProcess,
  RemoteClipboard,
  RemoteCursor,
  RemoteAgent,
  RemoteAgentCapabilities,
  RemoteAgentError,
  RemoteAgentErrorCode,
  RemoteAgentScreenshotOptions,
  RemoteDiagnosticsCapture,
  RemoteDiagnosticsCaptureOptions,
  RemoteDiagnosticsWindow,
  RemoteDirectoryEntry,
  RemoteFileStat,
  RemoteStableBoundsWaitOptions,
  RemoteInputOperation,
  RemoteKeyboardPressOptions,
  RemoteMonitor,
  RemoteMouseClickOptions,
  RemoteMouseDragOptions,
  RemoteMouseWheelOptions,
  RemoteProtocolTraceEntry,
  RemoteProcessListOptions,
  RemoteProcessSnapshot,
  RemoteScreenshot,
  RemoteWaitOptions,
  RemoteWindowQuery,
} from '../index';
import { resolveAuthToken } from '../auth';
import { waitForResult } from '../wait';
import {
  createBinaryTransferChunks,
  createBinaryTransferReceiver,
  type JsonValue,
  createPendingRequestTable,
  type ProtocolBinaryTransferChunk,
} from '../protocol';
import { protocolVersion } from '../protocol_version';
import {
  createTcpFrameTransport,
  type ProtocolTransport,
  type ProtocolTransportCallbacks,
} from './transport';

interface ReadyState {
  readonly reject: (error: RemoteAgentError) => void;
  readonly resolve: (capabilities: RemoteAgentCapabilities) => void;
  settled: boolean;
  readonly timer: ReturnType<typeof setTimeout>;
}

interface BinaryTransferReference {
  readonly transferId: string;
  readonly contentType: string;
  readonly totalBytes: number;
  readonly sha256: string;
}

interface WaitingBinaryTransfer {
  readonly reference: BinaryTransferReference;
  readonly reject: (error: RemoteAgentError) => void;
  readonly resolve: (data: Buffer) => void;
  readonly timer: ReturnType<typeof setTimeout>;
}

const defaultTimeoutMs = 30000;
const defaultPasteRestoreDelayMs = 500;
const binaryTransferChunkSize = 64 * 1024;
const maxRecentDiagnosticsOperations = 100;

const sha256Hex = (data: Buffer): string =>
  createHash('sha256').update(data).digest('hex');

const waitForDelay = async (delayMs: number): Promise<void> => {
  await new Promise<void>((resolve) => {
    setTimeout(resolve, delayMs);
  });
};

const createRemoteAgentError = (
  code: RemoteAgentErrorCode,
  message: string
): RemoteAgentError =>
  Object.assign(new Error(message), {
    code,
  });

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const appendRecent = <T>(entries: T[], entry: T): void => {
  entries.push(entry);
  if (entries.length > maxRecentDiagnosticsOperations) {
    entries.splice(0, entries.length - maxRecentDiagnosticsOperations);
  }
};

const shouldRedactTraceKey = (key: string): boolean => {
  const normalized = key.toLowerCase();
  return (
    normalized.includes('token') ||
    normalized === 'data' ||
    normalized === 'database64' ||
    normalized.endsWith('payload')
  );
};

const sanitizeTraceValue = (value: unknown): unknown => {
  if (Array.isArray(value)) {
    return value.map((entry) => sanitizeTraceValue(entry));
  }
  if (isRecord(value)) {
    const sanitized: Record<string, unknown> = {};
    for (const [key, field] of Object.entries(value)) {
      sanitized[key] = shouldRedactTraceKey(key)
        ? '[redacted]'
        : sanitizeTraceValue(field);
    }
    return sanitized;
  }
  return value;
};

const cloneInputOperation = (
  operation: RemoteInputOperation
): RemoteInputOperation =>
  JSON.parse(JSON.stringify(operation)) as RemoteInputOperation;

const copyWindowSnapshot = (window: AppWindowSnapshot): AppWindowSnapshot => ({
  active: window.active,
  bounds: window.bounds,
  className: window.className,
  controlId: window.controlId,
  enabled: window.enabled,
  focused: window.focused,
  id: window.id,
  maximized: window.maximized,
  minimized: window.minimized,
  process: window.process,
  title: window.title,
  visible: window.visible,
});

const parseCapabilities = (
  value: JsonValue | undefined
): RemoteAgentCapabilities => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Agent capabilities must be an object.'
    );
  }
  const protocolVersionValue = value.protocolVersion;
  const platformValue = value.platform;
  const featuresValue = value.features;
  if (typeof protocolVersionValue !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Agent capabilities protocolVersion must be a string.'
    );
  }
  if (platformValue !== 'windows') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Agent capabilities platform is unsupported.'
    );
  }
  if (
    !Array.isArray(featuresValue) ||
    !featuresValue.every((feature) => typeof feature === 'string')
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Agent capabilities features must be a string array.'
    );
  }
  return {
    features: featuresValue,
    platform: platformValue,
    protocolVersion: protocolVersionValue,
  };
};

const readString = (record: Record<string, unknown>, key: string): string => {
  const value = record[key];
  if (typeof value !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `Window field ${key} must be a string.`
    );
  }
  return value;
};

const readNumber = (record: Record<string, unknown>, key: string): number => {
  const value = record[key];
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `Window field ${key} must be a finite number.`
    );
  }
  return value;
};

const parseRect = (value: unknown): ScreenRect => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window bounds must be an object.'
    );
  }
  return {
    height: readNumber(value, 'height'),
    width: readNumber(value, 'width'),
    x: readNumber(value, 'x'),
    y: readNumber(value, 'y'),
  };
};

const parsePoint = (value: unknown): ScreenPoint => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Screen point must be an object.'
    );
  }
  return {
    x: readNumber(value, 'x'),
    y: readNumber(value, 'y'),
  };
};

const parseProcess = (value: unknown): AppWindowProcess => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window process must be an object.'
    );
  }
  return {
    id: readNumber(value, 'id'),
    name: readString(value, 'name'),
  };
};

const parseWindowSnapshot = (value: unknown): AppWindowSnapshot => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window entry must be an object.'
    );
  }
  const visible = value.visible;
  if (typeof visible !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window visible field must be a boolean.'
    );
  }
  const active = value.active;
  const enabled = value.enabled;
  const focused = value.focused;
  const minimized = value.minimized;
  const maximized = value.maximized;
  if (
    typeof active !== 'boolean' ||
    typeof enabled !== 'boolean' ||
    typeof focused !== 'boolean' ||
    typeof minimized !== 'boolean' ||
    typeof maximized !== 'boolean'
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window state fields must be booleans.'
    );
  }
  return {
    active,
    bounds: parseRect(value.bounds),
    className: readString(value, 'className'),
    controlId: readNumber(value, 'controlId'),
    enabled,
    focused,
    id: readString(value, 'id'),
    maximized,
    minimized,
    process: parseProcess(value.process),
    title: readString(value, 'title'),
    visible,
  };
};

const parseWindowArray = (
  value: JsonValue | undefined
): readonly AppWindowSnapshot[] => {
  if (!Array.isArray(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window list result must be an array.'
    );
  }
  return value.map((entry) => parseWindowSnapshot(entry));
};

const parseBinaryTransferReference = (
  value: Record<string, unknown>,
  expectedContentType: string,
  context: string
): BinaryTransferReference => {
  const transferId = value.transferId;
  const contentType = value.contentType;
  const totalBytes = value.totalBytes;
  const sha256 = value.sha256;
  if (
    typeof transferId !== 'string' ||
    typeof contentType !== 'string' ||
    typeof totalBytes !== 'number' ||
    !Number.isSafeInteger(totalBytes) ||
    totalBytes < 0 ||
    typeof sha256 !== 'string'
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `${context} binary transfer metadata is invalid.`
    );
  }
  if (contentType !== expectedContentType) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `${context} binary transfer contentType is invalid.`
    );
  }
  return {
    contentType,
    sha256,
    totalBytes,
    transferId,
  };
};

const parseScreenshot = async (
  value: JsonValue | undefined,
  readBinaryTransfer: (reference: BinaryTransferReference) => Promise<Buffer>
): Promise<RemoteScreenshot> => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Screenshot result must be an object.'
    );
  }
  const imageBase64 = value.imageBase64;
  const clipped = value.clipped;
  if (typeof clipped !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Screenshot result has invalid clipped field.'
    );
  }
  const image =
    typeof imageBase64 === 'string'
      ? Buffer.from(imageBase64, 'base64')
      : await readBinaryTransfer(
          parseBinaryTransferReference(value, 'image/png', 'Screenshot result')
        );
  return {
    bounds: parseRect(value.bounds),
    clipped,
    image,
    visibleBounds: parseRect(value.visibleBounds),
  };
};

const parseMonitor = (value: unknown): RemoteMonitor => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Monitor entry must be an object.'
    );
  }
  const primary = value.primary;
  if (typeof primary !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Monitor primary field must be a boolean.'
    );
  }
  return {
    bounds: parseRect(value.bounds),
    id: readString(value, 'id'),
    name: readString(value, 'name'),
    primary,
    scaleFactor: readNumber(value, 'scaleFactor'),
    workArea: parseRect(value.workArea),
  };
};

const parseMonitorArray = (
  value: JsonValue | undefined
): readonly RemoteMonitor[] => {
  if (!Array.isArray(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Monitor list result must be an array.'
    );
  }
  return value.map((entry) => parseMonitor(entry));
};

const parseCursor = (value: JsonValue | undefined): RemoteCursor => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Cursor result must be an object.'
    );
  }
  const visible = value.visible;
  if (typeof visible !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Cursor visible field must be a boolean.'
    );
  }
  return {
    point: parsePoint(value.point),
    visible,
  };
};

const parseFileReadResult = async (
  value: JsonValue | undefined,
  readBinaryTransfer: (reference: BinaryTransferReference) => Promise<Buffer>
): Promise<Buffer> => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.read result must be an object.'
    );
  }
  if (typeof value.dataBase64 !== 'string') {
    return await readBinaryTransfer(
      parseBinaryTransferReference(
        value,
        'application/octet-stream',
        'file.read result'
      )
    );
  }
  if (typeof value.sha256 !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.read result must include sha256.'
    );
  }
  const data = Buffer.from(value.dataBase64, 'base64');
  if (sha256Hex(data) !== value.sha256) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.read checksum mismatch.'
    );
  }
  return data;
};

const parseFileStat = (value: unknown): RemoteFileStat => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file stat result must be an object.'
    );
  }
  const type = value.type;
  if (type !== 'file' && type !== 'directory' && type !== 'other') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file stat type is invalid.'
    );
  }
  return {
    createdAt: readString(value, 'createdAt'),
    modifiedAt: readString(value, 'modifiedAt'),
    size: readNumber(value, 'size'),
    type,
  };
};

const parseDirectoryEntry = (value: unknown): RemoteDirectoryEntry => ({
  ...parseFileStat(value),
  name: isRecord(value) ? readString(value, 'name') : '',
});

const parseDirectoryEntries = (
  value: JsonValue | undefined
): readonly RemoteDirectoryEntry[] => {
  if (!Array.isArray(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.readdir result must be an array.'
    );
  }
  return value.map((entry) => parseDirectoryEntry(entry));
};

const parseExists = (value: JsonValue | undefined): boolean => {
  if (!isRecord(value) || typeof value.exists !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.exists result must include exists.'
    );
  }
  return value.exists;
};

const parseTempDirectory = (value: JsonValue | undefined): string => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.mkdtemp result must be an object.'
    );
  }
  return readString(value, 'path');
};

const parseApplicationProcess = (
  value: JsonValue | undefined
): RemoteApplicationProcess => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'applications.launch result must be an object.'
    );
  }
  return {
    id: readNumber(value, 'id'),
    name: readString(value, 'name'),
  };
};

const parseProcessSnapshot = (
  value: JsonValue | undefined
): RemoteProcessSnapshot => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process snapshot result must be an object.'
    );
  }
  const running = value.running;
  const exitCode = value.exitCode;
  if (typeof running !== 'boolean') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process snapshot running field must be a boolean.'
    );
  }
  if (
    exitCode !== null &&
    (typeof exitCode !== 'number' || !Number.isFinite(exitCode))
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process snapshot exitCode field must be null or a finite number.'
    );
  }
  return {
    exitCode,
    id: readNumber(value, 'id'),
    name: readString(value, 'name'),
    path: readString(value, 'path'),
    running,
  };
};

const parseProcessSnapshotArray = (
  value: JsonValue | undefined
): readonly RemoteProcessSnapshot[] => {
  if (!Array.isArray(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process.list result must be an array.'
    );
  }
  return value.map((entry) => parseProcessSnapshot(entry as JsonValue));
};

const parseClipboardText = (value: JsonValue | undefined): string => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'clipboard.readText result must be an object.'
    );
  }
  return readString(value, 'text');
};

const isEventLogLevel = (value: unknown): value is EventLogLevel =>
  value === 'Critical' ||
  value === 'Error' ||
  value === 'Information' ||
  value === 'Verbose' ||
  value === 'Warning';

const parseEventLogEntry = (value: unknown): EventLogEntry => {
  if (!isRecord(value) || !isEventLogLevel(value.level)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Event log entry has invalid shape.'
    );
  }
  return {
    id: readNumber(value, 'id'),
    level: value.level,
    message: readString(value, 'message'),
    provider: readString(value, 'provider'),
    timestamp: readString(value, 'timestamp'),
  };
};

const parseEventLogArray = (
  value: JsonValue | undefined
): readonly EventLogEntry[] => {
  if (!Array.isArray(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'eventLogs.read result must be an array.'
    );
  }
  return value.map((entry) => parseEventLogEntry(entry));
};

const eventLogQueryToJson = (query: EventLogQuery | undefined): JsonValue => ({
  ...(query?.maxEntries === undefined ? {} : { maxEntries: query.maxEntries }),
  ...(query?.since === undefined ? {} : { since: query.since }),
  ...(query?.source === undefined ? {} : { source: query.source }),
});

const applicationLaunchOptionsToJson = (
  options: RemoteApplicationLaunchOptions
): JsonValue => ({
  ...(options.arguments === undefined
    ? {}
    : { arguments: [...options.arguments] }),
  ...(options.createNoWindow === undefined
    ? {}
    : { createNoWindow: options.createNoWindow }),
  ...(options.environment === undefined
    ? {}
    : { environment: { ...options.environment } }),
  path: options.path,
  ...(options.stderrPath === undefined
    ? {}
    : { stderrPath: options.stderrPath }),
  ...(options.stdoutPath === undefined
    ? {}
    : { stdoutPath: options.stdoutPath }),
  ...(options.workingDirectory === undefined
    ? {}
    : { workingDirectory: options.workingDirectory }),
});

const processListOptionsToJson = (
  options: RemoteProcessListOptions | undefined
): JsonValue => ({
  ...(options?.name === undefined ? {} : { name: options.name }),
});

const modifiersFromOptions = (
  options:
    | RemoteKeyboardPressOptions
    | RemoteMouseClickOptions
    | RemoteMouseDragOptions
    | undefined
): readonly KeyboardModifier[] => [...(options?.modifiers ?? [])];

const buttonFromOptions = (
  options: RemoteMouseClickOptions | RemoteMouseDragOptions | undefined
): MouseButton => options?.button ?? 'left';

const pointToJson = (point: {
  readonly x: number;
  readonly y: number;
}): JsonValue => ({
  x: point.x,
  y: point.y,
});

const rectToJson = (rect: ScreenRect): JsonValue => ({
  height: rect.height,
  width: rect.width,
  x: rect.x,
  y: rect.y,
});

const agentScreenshotOptionsToJson = (
  options: RemoteAgentScreenshotOptions | undefined
): JsonValue | undefined =>
  options?.rect === undefined
    ? undefined
    : {
        rect: rectToJson(options.rect),
      };

const rectKey = (rect: ScreenRect): string =>
  `${String(rect.x)},${String(rect.y)},${String(rect.width)},${String(
    rect.height
  )}`;

const normalizedProcessName = (name: string): string =>
  name.toLowerCase().replace(/\.exe$/u, '');

const formatWindowCandidate = (window: AppWindowSnapshot): string =>
  `${window.title || '<untitled>'} (${window.process.name || '<unknown>'}:${
    window.process.id
  })`;

const queryLabel = (query: RemoteWindowQuery): string =>
  JSON.stringify({
    ...query,
    titleRegex: query.titleRegex?.toString(),
  });

const windowMatchesQuery = (
  window: AppWindowSnapshot,
  query: RemoteWindowQuery
): boolean => {
  if (query.title !== undefined && window.title !== query.title) {
    return false;
  }
  if (query.titleRegex !== undefined && !query.titleRegex.test(window.title)) {
    return false;
  }
  if (query.processId !== undefined && window.process.id !== query.processId) {
    return false;
  }
  if (query.processName !== undefined) {
    const expected = normalizedProcessName(query.processName);
    if (normalizedProcessName(window.process.name) !== expected) {
      return false;
    }
  }
  if (query.visible !== undefined && window.visible !== query.visible) {
    return false;
  }
  if (query.active !== undefined && window.active !== query.active) {
    return false;
  }
  if (query.className !== undefined && window.className !== query.className) {
    return false;
  }
  if (query.controlId !== undefined && window.controlId !== query.controlId) {
    return false;
  }
  if (query.focused !== undefined && window.focused !== query.focused) {
    return false;
  }
  return true;
};

const inputOperationToJson = (operation: RemoteInputOperation): JsonValue => {
  switch (operation.kind) {
    case 'keyboard.down':
      return {
        key: operation.key,
        kind: operation.kind,
      };
    case 'keyboard.press':
      return {
        key: operation.key,
        kind: operation.kind,
        modifiers: [...operation.modifiers],
      };
    case 'keyboard.type':
      return {
        kind: operation.kind,
        text: operation.text,
      };
    case 'keyboard.up':
      return {
        key: operation.key,
        kind: operation.kind,
      };
    case 'mouse.click':
      return {
        button: operation.button,
        kind: operation.kind,
        modifiers: [...operation.modifiers],
        point: pointToJson(operation.point),
      };
    case 'mouse.drag':
      return {
        button: operation.button,
        from: pointToJson(operation.from),
        kind: operation.kind,
        modifiers: [...operation.modifiers],
        to: pointToJson(operation.to),
      };
    case 'mouse.move':
      return {
        kind: operation.kind,
        point: pointToJson(operation.point),
      };
    case 'mouse.wheel':
      return {
        deltaX: operation.deltaX,
        deltaY: operation.deltaY,
        kind: operation.kind,
        point: operation.point === null ? null : pointToJson(operation.point),
      };
  }
};

const settleReady = (
  state: ReadyState,
  action: 'reject' | 'resolve',
  value: RemoteAgentCapabilities | RemoteAgentError
): void => {
  if (state.settled) {
    return;
  }
  state.settled = true;
  clearTimeout(state.timer);
  if (action === 'resolve') {
    state.resolve(value as RemoteAgentCapabilities);
  } else {
    state.reject(value as RemoteAgentError);
  }
};

const createTransport = (
  options: ConnectRemoteAgentOptions,
  timeoutMs: number,
  authToken: string | undefined,
  callbacks: ProtocolTransportCallbacks
): ProtocolTransport =>
  createTcpFrameTransport({
    callbacks,
    authToken,
    host: options.host,
    port: options.port,
    timeoutMs,
  });

/** Connects to a remote agent-rover agent. */
export const connectRemoteAgent = async (
  options: ConnectRemoteAgentOptions
): Promise<RemoteAgent> => {
  const timeoutMs = options.timeoutMs ?? defaultTimeoutMs;
  const authToken = resolveAuthToken(options.authToken);
  const pending = createPendingRequestTable({
    requestTimeoutMs: timeoutMs,
  });
  const binaryReceiver = createBinaryTransferReceiver();
  const completedBinaryTransfers = new Map<string, Buffer>();
  const waitingBinaryTransfers = new Map<string, WaitingBinaryTransfer[]>();
  let nextBinaryTransferId = 1;
  let disconnected = false;
  let readyState: ReadyState | undefined = undefined;
  let transport: ProtocolTransport | undefined = undefined;
  let socketOpened = false;
  let nextProtocolTraceId = 1;
  const recentInputOperations: RemoteInputOperation[] = [];
  const recentProtocolOperations: RemoteProtocolTraceEntry[] = [];

  const recordProtocolOperation = (
    direction: RemoteProtocolTraceEntry['direction'],
    method: string,
    params: unknown
  ): void => {
    const base = {
      direction,
      id: nextProtocolTraceId,
      method,
      timestamp: new Date().toISOString(),
    };
    nextProtocolTraceId += 1;
    appendRecent(
      recentProtocolOperations,
      params === undefined
        ? base
        : {
            ...base,
            params: sanitizeTraceValue(params),
          }
    );
  };

  const recordInputOperation = (operation: RemoteInputOperation): void => {
    appendRecent(recentInputOperations, cloneInputOperation(operation));
  };

  const rejectBinaryTransferWaiters = (error: RemoteAgentError): void => {
    for (const waiters of waitingBinaryTransfers.values()) {
      for (const waiter of waiters) {
        clearTimeout(waiter.timer);
        waiter.reject(error);
      }
    }
    waitingBinaryTransfers.clear();
  };

  const verifyBinaryTransfer = (
    reference: BinaryTransferReference,
    data: Buffer
  ): void => {
    if (data.byteLength !== reference.totalBytes) {
      throw createRemoteAgentError(
        'PROTOCOL_ERROR',
        `Binary transfer size mismatch: ${reference.transferId}.`
      );
    }
    if (sha256Hex(data) !== reference.sha256) {
      throw createRemoteAgentError(
        'PROTOCOL_ERROR',
        `Binary transfer checksum mismatch: ${reference.transferId}.`
      );
    }
  };

  const resolveBinaryTransferWaiter = (
    waiter: WaitingBinaryTransfer,
    data: Buffer
  ): void => {
    clearTimeout(waiter.timer);
    try {
      verifyBinaryTransfer(waiter.reference, data);
      waiter.resolve(data);
    } catch (error) {
      waiter.reject(
        error instanceof Error
          ? createRemoteAgentError('PROTOCOL_ERROR', error.message)
          : createRemoteAgentError('PROTOCOL_ERROR', 'Invalid binary transfer.')
      );
    }
  };

  const acceptBinaryChunk = (chunk: ProtocolBinaryTransferChunk): void => {
    const result = binaryReceiver.acceptChunk(chunk);
    if (result.state !== 'complete') {
      return;
    }
    const waiters = waitingBinaryTransfers.get(chunk.transferId);
    if (waiters === undefined || waiters.length === 0) {
      waitingBinaryTransfers.delete(chunk.transferId);
      completedBinaryTransfers.set(chunk.transferId, result.data);
      return;
    }
    waitingBinaryTransfers.delete(chunk.transferId);
    for (const waiter of waiters) {
      resolveBinaryTransferWaiter(waiter, result.data);
    }
  };

  const readBinaryTransfer = async (
    reference: BinaryTransferReference
  ): Promise<Buffer> => {
    const completed = completedBinaryTransfers.get(reference.transferId);
    if (completed !== undefined) {
      completedBinaryTransfers.delete(reference.transferId);
      verifyBinaryTransfer(reference, completed);
      return completed;
    }

    return await new Promise<Buffer>((resolve, reject) => {
      const waiter: WaitingBinaryTransfer = {
        reference,
        reject,
        resolve,
        timer: setTimeout(() => {
          const waiters = waitingBinaryTransfers.get(reference.transferId);
          if (waiters !== undefined) {
            const remaining = waiters.filter((entry) => entry !== waiter);
            if (remaining.length === 0) {
              waitingBinaryTransfers.delete(reference.transferId);
            } else {
              waitingBinaryTransfers.set(reference.transferId, remaining);
            }
          }
          reject(
            createRemoteAgentError(
              'PROTOCOL_ERROR',
              `Timed out waiting for binary transfer: ${reference.transferId}.`
            )
          );
        }, timeoutMs),
      };
      waitingBinaryTransfers.set(reference.transferId, [
        ...(waitingBinaryTransfers.get(reference.transferId) ?? []),
        waiter,
      ]);
    });
  };

  const ready = new Promise<RemoteAgentCapabilities>((resolve, reject) => {
    const timer = setTimeout(() => {
      if (readyState !== undefined) {
        settleReady(
          readyState,
          'reject',
          createRemoteAgentError(
            'HANDSHAKE_FAILED',
            'Timed out waiting for agent handshake.'
          )
        );
      }
      void transport?.close();
    }, timeoutMs);
    readyState = {
      reject,
      resolve,
      settled: false,
      timer,
    };
  });

  const preReadyErrorCode = (): RemoteAgentErrorCode =>
    socketOpened ? 'AUTHENTICATION_FAILED' : 'CONNECTION_FAILED';

  transport = createTransport(options, timeoutMs, authToken, {
    onBinaryChunk: (chunk) => {
      try {
        acceptBinaryChunk(chunk);
      } catch (error) {
        const wrapped = createRemoteAgentError(
          'PROTOCOL_ERROR',
          error instanceof Error ? error.message : 'Invalid binary transfer.'
        );
        pending.rejectAll('PROTOCOL_ERROR', wrapped.message);
        rejectBinaryTransferWaiters(wrapped);
        void transport?.close();
      }
    },
    onClose: (message) => {
      disconnected = true;
      const wrapped = createRemoteAgentError(
        readyState?.settled === true ? 'DISCONNECTED' : preReadyErrorCode(),
        message
      );
      if (readyState !== undefined) {
        settleReady(readyState, 'reject', wrapped);
      }
      pending.rejectAll('DISCONNECTED', wrapped.message);
      rejectBinaryTransferWaiters(wrapped);
    },
    onError: (error) => {
      const wrapped = createRemoteAgentError(
        readyState?.settled === true ? 'DISCONNECTED' : preReadyErrorCode(),
        error.message
      );
      if (readyState !== undefined) {
        settleReady(readyState, 'reject', wrapped);
      }
      pending.rejectAll('DISCONNECTED', wrapped.message);
      rejectBinaryTransferWaiters(wrapped);
    },
    onMessage: (message) => {
      try {
        if (message.kind === 'response') {
          pending.acceptResponse(message);
          return;
        }
        if (message.kind === 'event' && message.name === 'agent.ready') {
          const eventData = isRecord(message.data) ? message.data : undefined;
          const capabilities = parseCapabilities(
            isRecord(eventData)
              ? (eventData.capabilities as JsonValue)
              : undefined
          );
          if (capabilities.protocolVersion !== protocolVersion) {
            if (readyState !== undefined) {
              settleReady(
                readyState,
                'reject',
                createRemoteAgentError(
                  'HANDSHAKE_FAILED',
                  `Unsupported protocol version: ${capabilities.protocolVersion}.`
                )
              );
            }
            void transport?.close();
            return;
          }
          if (readyState !== undefined) {
            settleReady(readyState, 'resolve', capabilities);
          }
        }
      } catch (error) {
        if (readyState !== undefined) {
          settleReady(
            readyState,
            'reject',
            createRemoteAgentError(
              'HANDSHAKE_FAILED',
              error instanceof Error ? error.message : 'Invalid handshake.'
            )
          );
        }
        void transport?.close();
      }
    },
    onOpen: () => {
      socketOpened = true;
    },
  });

  await ready;
  const activeTransport = transport;

  const assertConnected = (): void => {
    if (disconnected || !activeTransport.isOpen()) {
      throw createRemoteAgentError(
        'DISCONNECTED',
        'Remote agent connection is closed.'
      );
    }
  };

  const releaseAgent = (): void => {
    if (disconnected) {
      return;
    }
    disconnected = true;
    const wrapped = createRemoteAgentError(
      'DISCONNECTED',
      'Remote agent connection is closed.'
    );
    pending.rejectAll('DISCONNECTED', wrapped.message);
    rejectBinaryTransferWaiters(wrapped);
    void activeTransport.close();
  };

  const requestJson = async (
    method: string,
    params: JsonValue | undefined
  ): Promise<JsonValue | undefined> => {
    assertConnected();
    recordProtocolOperation('request', method, params);
    const request = pending.createRequest(method, params);
    try {
      await activeTransport.send(request.message);
    } catch (error) {
      throw createRemoteAgentError(
        'DISCONNECTED',
        error instanceof Error
          ? `Failed to send protocol message: ${error.message}`
          : 'Failed to send protocol message.'
      );
    }
    return await request.result;
  };

  const sendBinaryTransfer = async (
    contentType: string,
    data: Buffer
  ): Promise<BinaryTransferReference> => {
    const transferId = `client-transfer-${String(nextBinaryTransferId)}`;
    nextBinaryTransferId += 1;
    const reference = {
      contentType,
      sha256: sha256Hex(data),
      totalBytes: data.byteLength,
      transferId,
    };
    recordProtocolOperation('binaryUpload', 'binary.transfer', reference);
    for (const chunk of createBinaryTransferChunks({
      chunkSize: binaryTransferChunkSize,
      contentType,
      data,
      transferId,
    })) {
      try {
        await activeTransport.sendBinaryChunk(chunk);
      } catch (error) {
        throw createRemoteAgentError(
          'DISCONNECTED',
          error instanceof Error
            ? `Failed to send binary transfer: ${error.message}`
            : 'Failed to send binary transfer.'
        );
      }
    }
    return reference;
  };

  const createWindowProxy = (snapshot: AppWindowSnapshot): AppWindow => ({
    ...snapshot,
    activate: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.activate', {
            windowId: snapshot.id,
          })
        )
      ),
    close: async (): Promise<void> => {
      await requestJson('window.close', {
        windowId: snapshot.id,
      });
    },
    children: async (): Promise<readonly AppWindow[]> =>
      parseWindowArray(
        await requestJson('window.children', {
          windowId: snapshot.id,
        })
      ).map((child) => createWindowProxy(child)),
    descendants: async (
      options?: AppWindowDescendantsOptions
    ): Promise<readonly AppWindow[]> =>
      await collectWindowDescendants(
        parseWindowArray(
          await requestJson('window.children', {
            windowId: snapshot.id,
          })
        ).map((child) => createWindowProxy(child)),
        options
      ),
    findDescendants: async (
      query: RemoteWindowQuery
    ): Promise<readonly AppWindow[]> => {
      const descendants = await collectWindowDescendants(
        parseWindowArray(
          await requestJson('window.children', {
            windowId: snapshot.id,
          })
        ).map((child) => createWindowProxy(child)),
        undefined
      );
      return descendants.filter((window) => windowMatchesQuery(window, query));
    },
    focus: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.focus', {
            windowId: snapshot.id,
          })
        )
      ),
    maximize: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.show', {
            state: 'maximized',
            windowId: snapshot.id,
          })
        )
      ),
    minimize: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.show', {
            state: 'minimized',
            windowId: snapshot.id,
          })
        )
      ),
    refresh: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.snapshot', {
            windowId: snapshot.id,
          })
        )
      ),
    restore: async (): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.show', {
            state: 'restored',
            windowId: snapshot.id,
          })
        )
      ),
    screenshot: async (): Promise<AppWindowScreenshot> =>
      await parseScreenshot(
        await requestJson('window.screenshot', {
          windowId: snapshot.id,
        }),
        readBinaryTransfer
      ),
    setBounds: async (bounds): Promise<AppWindow> =>
      createWindowProxy(
        parseWindowSnapshot(
          await requestJson('window.setBounds', {
            bounds: rectToJson(bounds),
            windowId: snapshot.id,
          })
        )
      ),
    waitForClosed: async (options): Promise<void> => {
      await waitForResult(async () => {
        const current = await findWindowById(snapshot.id, true);
        if (current === undefined) {
          return;
        }
        throw new Error(`Window is still present: ${snapshot.id}.`);
      }, options);
    },
    waitForHidden: async (options): Promise<void> => {
      await waitForResult(async () => {
        const current = await findWindowById(snapshot.id, true);
        if (current === undefined || !current.visible) {
          return;
        }
        throw new Error(`Window is still visible: ${snapshot.id}.`);
      }, options);
    },
    waitForStableBounds: async (
      options?: RemoteStableBoundsWaitOptions
    ): Promise<AppWindow> => {
      const requiredIterations = options?.stableIterations ?? 2;
      if (
        !Number.isSafeInteger(requiredIterations) ||
        requiredIterations <= 0
      ) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          'stableIterations must be a positive safe integer.'
        );
      }
      let previousKey: string | undefined = undefined;
      let stableIterations = 0;
      return await waitForResult(async () => {
        const current = await findWindowById(snapshot.id, true);
        if (current === undefined) {
          throw new Error(`Window was closed: ${snapshot.id}.`);
        }
        const currentKey = rectKey(current.bounds);
        if (currentKey === previousKey) {
          stableIterations += 1;
        } else {
          previousKey = currentKey;
          stableIterations = 1;
        }
        if (stableIterations >= requiredIterations) {
          return current;
        }
        throw new Error(`Window bounds are not stable yet: ${snapshot.id}.`);
      }, options);
    },
    waitForVisible: async (options): Promise<AppWindow> =>
      await waitForResult(async () => {
        const current = await findWindowById(snapshot.id, true);
        if (current !== undefined && current.visible) {
          return current;
        }
        throw new Error(`Window is not visible: ${snapshot.id}.`);
      }, options),
  });

  const readDescendantMaxDepth = (
    options: AppWindowDescendantsOptions | undefined
  ): number => {
    if (options?.maxDepth === undefined) {
      return Number.POSITIVE_INFINITY;
    }
    if (!Number.isSafeInteger(options.maxDepth) || options.maxDepth < 0) {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        'maxDepth must be a non-negative safe integer.'
      );
    }
    return options.maxDepth;
  };

  const collectWindowDescendants = async (
    roots: readonly AppWindow[],
    options: AppWindowDescendantsOptions | undefined
  ): Promise<readonly AppWindow[]> => {
    const maxDepth = readDescendantMaxDepth(options);
    const collected: AppWindow[] = [];
    const visit = async (
      windows: readonly AppWindow[],
      depth: number
    ): Promise<void> => {
      if (depth > maxDepth) {
        return;
      }
      for (const window of windows) {
        collected.push(window);
        if (depth < maxDepth) {
          await visit(await window.children(), depth + 1);
        }
      }
    };
    await visit(roots, 1);
    return collected;
  };

  const listTopLevelWindows = async (): Promise<readonly AppWindow[]> =>
    parseWindowArray(await requestJson('agent.windows', undefined)).map(
      (window) => createWindowProxy(window)
    );

  const collectWindows = async (
    roots: readonly AppWindow[],
    includeDescendants: boolean
  ): Promise<readonly AppWindow[]> => {
    if (!includeDescendants) {
      return roots;
    }
    const collected: AppWindow[] = [];
    const visit = async (windows: readonly AppWindow[]): Promise<void> => {
      for (const window of windows) {
        collected.push(window);
        try {
          await visit(await window.children());
        } catch {
          continue;
        }
      }
    };
    await visit(roots);
    return collected;
  };

  const findWindowsByQuery = async (
    query: RemoteWindowQuery
  ): Promise<readonly AppWindow[]> => {
    const topLevelWindows = await listTopLevelWindows();
    const candidates = await collectWindows(
      topLevelWindows,
      query.includeDescendants ?? false
    );
    const matches = candidates.filter((window) =>
      windowMatchesQuery(window, query)
    );
    if (query.strict === true && matches.length !== 1) {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        `Strict window query matched ${String(matches.length)} windows: ${queryLabel(
          query
        )}. Candidates: ${candidates.map(formatWindowCandidate).join(', ')}.`
      );
    }
    return matches;
  };

  const findWindowById = async (
    windowId: string,
    includeDescendants: boolean
  ): Promise<AppWindow | undefined> => {
    const topLevelWindows = await listTopLevelWindows();
    const candidates = await collectWindows(
      topLevelWindows,
      includeDescendants
    );
    return candidates.find((window) => window.id === windowId);
  };

  const waitForWindowByQuery = async (
    query: RemoteWindowQuery,
    options: RemoteWaitOptions | undefined
  ): Promise<AppWindow> =>
    await waitForResult(async () => {
      const topLevelWindows = await listTopLevelWindows();
      const candidates = await collectWindows(
        topLevelWindows,
        query.includeDescendants ?? false
      );
      const matches = candidates.filter((window) =>
        windowMatchesQuery(window, query)
      );
      if (
        matches.length === 1 ||
        (matches.length > 0 && query.strict !== true)
      ) {
        const match = matches[0];
        if (match === undefined) {
          throw new Error('Window query matched no usable window.');
        }
        return match;
      }
      if (matches.length > 1 && query.strict === true) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          `Strict window query matched ${String(matches.length)} windows: ${queryLabel(
            query
          )}.`
        );
      }
      throw new Error(
        `No matching window for ${queryLabel(
          query
        )}. Candidates: ${candidates.map(formatWindowCandidate).join(', ')}.`
      );
    }, options);

  const waitForNoWindowByQuery = async (
    query: RemoteWindowQuery,
    options: RemoteWaitOptions | undefined
  ): Promise<void> => {
    await waitForResult(async () => {
      const matches = await findWindowsByQuery({
        ...query,
        strict: false,
      });
      if (matches.length === 0) {
        return;
      }
      throw new Error(
        `Window query still matched ${String(matches.length)} windows: ${queryLabel(
          query
        )}.`
      );
    }, options);
  };

  const performInput = async (
    operation: RemoteInputOperation
  ): Promise<void> => {
    recordInputOperation(operation);
    await requestJson('input.perform', inputOperationToJson(operation));
  };

  const readClipboardText = async (): Promise<string> =>
    parseClipboardText(await requestJson('clipboard.readText', undefined));

  const writeClipboardText = async (text: string): Promise<void> => {
    await requestJson('clipboard.writeText', {
      text,
    });
  };

  const clearClipboard = async (): Promise<void> => {
    await requestJson('clipboard.clear', undefined);
  };

  const withClipboardText = async <T>(
    text: string,
    operation: () => Promise<T>
  ): Promise<T> => {
    const previousText = await readClipboardText();
    await writeClipboardText(text);

    let result: T | undefined = undefined;
    let operationError: unknown = undefined;
    let operationFailed = false;
    try {
      result = await operation();
    } catch (error) {
      operationFailed = true;
      operationError = error;
    }

    let restoreError: unknown = undefined;
    let restoreFailed = false;
    try {
      await writeClipboardText(previousText);
    } catch (error) {
      restoreFailed = true;
      restoreError = error;
    }

    if (operationFailed) {
      throw operationError;
    }
    if (restoreFailed) {
      throw restoreError;
    }
    return result as T;
  };

  const pasteClipboardText = async (
    text: string,
    restoreClipboard: boolean,
    restoreDelayMs: number
  ): Promise<void> => {
    const pasteOperation = async (): Promise<void> => {
      await performInput({
        key: 'v',
        kind: 'keyboard.press',
        modifiers: ['Control'],
      });
      if (restoreDelayMs > 0) {
        await waitForDelay(restoreDelayMs);
      }
    };
    if (!restoreClipboard) {
      await writeClipboardText(text);
      await performInput({
        key: 'v',
        kind: 'keyboard.press',
        modifiers: ['Control'],
      });
      return;
    }
    await withClipboardText(text, pasteOperation);
  };

  const snapshotProcess = async (
    processId: number
  ): Promise<RemoteProcessSnapshot> =>
    parseProcessSnapshot(
      await requestJson('process.snapshot', {
        processId,
      })
    );

  const waitForProcessExit = async (
    processId: number,
    options: RemoteWaitOptions | undefined
  ): Promise<RemoteProcessSnapshot> =>
    await waitForResult(async () => {
      const snapshot = await snapshotProcess(processId);
      if (!snapshot.running) {
        return snapshot;
      }
      throw new Error(`Process is still running: ${String(processId)}.`);
    }, options);

  const captureDiagnosticsWindow = async (
    window: AppWindow,
    includeDescendants: boolean,
    remainingDepth: number
  ): Promise<RemoteDiagnosticsWindow> => {
    const snapshot = copyWindowSnapshot(window);
    if (!includeDescendants || remainingDepth <= 0) {
      return snapshot;
    }
    const children = await Promise.all(
      (await window.children()).map(
        async (child): Promise<RemoteDiagnosticsWindow> =>
          await captureDiagnosticsWindow(child, true, remainingDepth - 1)
      )
    );
    if (children.length === 0) {
      return snapshot;
    }
    return {
      ...snapshot,
      children,
    };
  };

  const findActiveDiagnosticsWindow = (
    windows: readonly RemoteDiagnosticsWindow[]
  ): AppWindowSnapshot | null => {
    for (const window of windows) {
      if (window.active) {
        return copyWindowSnapshot(window);
      }
      const children = window.children;
      if (children !== undefined) {
        const activeChild = findActiveDiagnosticsWindow(children);
        if (activeChild !== null) {
          return activeChild;
        }
      }
    }
    return null;
  };

  const validateDiagnosticsOptions = (
    options: RemoteDiagnosticsCaptureOptions | undefined
  ): number => {
    const maxDepth = options?.maxDescendantDepth ?? 8;
    if (
      !Number.isFinite(maxDepth) ||
      maxDepth < 0 ||
      !Number.isInteger(maxDepth)
    ) {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        'maxDescendantDepth must be a non-negative integer.'
      );
    }
    return maxDepth;
  };

  const captureDiagnostics = async (
    options?: RemoteDiagnosticsCaptureOptions
  ): Promise<RemoteDiagnosticsCapture> => {
    const maxDescendantDepth = validateDiagnosticsOptions(options);
    const includeDescendants = options?.includeDescendants ?? false;
    const capturedAt = new Date().toISOString();
    const bounds = parseRect(await requestJson('agent.bounds', undefined));
    const screenshot = await parseScreenshot(
      await requestJson(
        'agent.screenshot',
        agentScreenshotOptionsToJson(options?.screenshot)
      ),
      readBinaryTransfer
    );
    const topLevelWindows = await listTopLevelWindows();
    const windows = await Promise.all(
      topLevelWindows.map(
        async (window): Promise<RemoteDiagnosticsWindow> =>
          await captureDiagnosticsWindow(
            window,
            includeDescendants,
            maxDescendantDepth
          )
      )
    );
    const cursor = parseCursor(await requestJson('agent.cursor', undefined));
    const monitors = parseMonitorArray(
      await requestJson('agent.monitors', undefined)
    );
    const eventLogs = parseEventLogArray(
      await requestJson(
        'eventLogs.read',
        eventLogQueryToJson(options?.eventLogs ?? { maxEntries: 50 })
      )
    );

    return {
      activeWindow: findActiveDiagnosticsWindow(windows),
      bounds,
      capturedAt,
      cursor,
      eventLogs,
      inputOperations: recentInputOperations.map((entry) =>
        cloneInputOperation(entry)
      ),
      monitors,
      protocolOperations: recentProtocolOperations.map((entry) =>
        entry.params === undefined
          ? {
              direction: entry.direction,
              id: entry.id,
              method: entry.method,
              timestamp: entry.timestamp,
            }
          : {
              direction: entry.direction,
              id: entry.id,
              method: entry.method,
              params: sanitizeTraceValue(entry.params),
              timestamp: entry.timestamp,
            }
      ),
      screenshot,
      windows,
    };
  };

  return {
    applications: {
      launch: async (options): Promise<RemoteApplicationProcess> =>
        parseApplicationProcess(
          await requestJson(
            'applications.launch',
            applicationLaunchOptionsToJson(options)
          )
        ),
    },
    capabilities: async (): Promise<RemoteAgentCapabilities> => {
      return parseCapabilities(
        await requestJson('agent.capabilities', undefined)
      );
    },
    diagnostics: {
      capture: async (options): Promise<RemoteDiagnosticsCapture> =>
        await captureDiagnostics(options),
    },
    clipboard: {
      clear: async (): Promise<void> => {
        await clearClipboard();
      },
      readText: async (): Promise<string> => await readClipboardText(),
      withText: async <T>(
        text: string,
        operation: () => Promise<T>
      ): Promise<T> => await withClipboardText(text, operation),
      writeText: async (text): Promise<void> => {
        await writeClipboardText(text);
      },
    } satisfies RemoteClipboard,
    bounds: async (): Promise<ScreenRect> =>
      parseRect(await requestJson('agent.bounds', undefined)),
    cursor: async (): Promise<RemoteCursor> =>
      parseCursor(await requestJson('agent.cursor', undefined)),
    eventLogs: {
      read: async (query): Promise<readonly EventLogEntry[]> =>
        parseEventLogArray(
          await requestJson('eventLogs.read', eventLogQueryToJson(query))
        ),
    },
    findWindows: async (query): Promise<readonly AppWindow[]> =>
      await findWindowsByQuery(query),
    files: {
      exists: async (path): Promise<boolean> =>
        parseExists(
          await requestJson('file.exists', {
            path,
          })
        ),
      mkdir: async (path, options): Promise<void> => {
        await requestJson('file.mkdir', {
          path,
          recursive: options?.recursive ?? false,
        });
      },
      mkdtemp: async (prefix): Promise<string> =>
        parseTempDirectory(
          await requestJson('file.mkdtemp', {
            prefix,
          })
        ),
      readFile: async (path): Promise<Buffer> =>
        await parseFileReadResult(
          await requestJson('file.read', {
            path,
          }),
          readBinaryTransfer
        ),
      readdir: async (path): Promise<readonly RemoteDirectoryEntry[]> =>
        parseDirectoryEntries(
          await requestJson('file.readdir', {
            path,
          })
        ),
      remove: async (path, options): Promise<void> => {
        await requestJson('file.remove', {
          path,
          recursive: options?.recursive ?? false,
        });
      },
      rename: async (from, to): Promise<void> => {
        await requestJson('file.rename', {
          from,
          to,
        });
      },
      stat: async (path): Promise<RemoteFileStat> =>
        parseFileStat(
          await requestJson('file.stat', {
            path,
          })
        ),
      writeFile: async (path, data): Promise<void> => {
        const transfer = await sendBinaryTransfer(
          'application/octet-stream',
          data
        );
        await requestJson('file.write', {
          contentType: transfer.contentType,
          path,
          sha256: transfer.sha256,
          totalBytes: transfer.totalBytes,
          transferId: transfer.transferId,
        });
      },
    },
    keyboard: {
      down: async (key): Promise<void> => {
        await performInput({
          key,
          kind: 'keyboard.down',
        });
      },
      press: async (key, options): Promise<void> => {
        await performInput({
          key,
          kind: 'keyboard.press',
          modifiers: modifiersFromOptions(options),
        });
      },
      type: async (text): Promise<void> => {
        await performInput({
          kind: 'keyboard.type',
          text,
        });
      },
      pasteText: async (text, options): Promise<void> => {
        const restoreDelayMs =
          options?.restoreDelayMs ?? defaultPasteRestoreDelayMs;
        if (!Number.isFinite(restoreDelayMs) || restoreDelayMs < 0) {
          throw createRemoteAgentError(
            'INVALID_ARGUMENT',
            'restoreDelayMs must be a non-negative finite number.'
          );
        }
        await pasteClipboardText(
          text,
          options?.restoreClipboard ?? true,
          restoreDelayMs
        );
      },
      up: async (key): Promise<void> => {
        await performInput({
          key,
          kind: 'keyboard.up',
        });
      },
    },
    monitors: async (): Promise<readonly RemoteMonitor[]> =>
      parseMonitorArray(await requestJson('agent.monitors', undefined)),
    mouse: {
      click: async (point, options): Promise<void> => {
        await performInput({
          button: buttonFromOptions(options),
          kind: 'mouse.click',
          modifiers: modifiersFromOptions(options),
          point,
        });
      },
      drag: async (from, to, options): Promise<void> => {
        await performInput({
          button: buttonFromOptions(options),
          from,
          kind: 'mouse.drag',
          modifiers: modifiersFromOptions(options),
          to,
        });
      },
      move: async (point): Promise<void> => {
        await performInput({
          kind: 'mouse.move',
          point,
        });
      },
      wheel: async (options: RemoteMouseWheelOptions): Promise<void> => {
        await performInput({
          deltaX: options.deltaX ?? 0,
          deltaY: options.deltaY ?? 0,
          kind: 'mouse.wheel',
          point: options.point ?? null,
        });
      },
    },
    processes: {
      exists: async (processId): Promise<boolean> =>
        (await snapshotProcess(processId)).running,
      kill: async (processId): Promise<void> => {
        await requestJson('process.kill', {
          processId,
        });
      },
      list: async (options): Promise<readonly RemoteProcessSnapshot[]> =>
        parseProcessSnapshotArray(
          await requestJson('process.list', processListOptionsToJson(options))
        ),
      snapshot: async (processId): Promise<RemoteProcessSnapshot> =>
        await snapshotProcess(processId),
      waitForExit: async (processId, options): Promise<RemoteProcessSnapshot> =>
        await waitForProcessExit(processId, options),
    },
    release: releaseAgent,
    screenshot: async (
      options?: RemoteAgentScreenshotOptions
    ): Promise<RemoteScreenshot> =>
      await parseScreenshot(
        await requestJson(
          'agent.screenshot',
          agentScreenshotOptionsToJson(options)
        ),
        readBinaryTransfer
      ),
    waitForNoWindow: async (query, options): Promise<void> => {
      await waitForNoWindowByQuery(query, options);
    },
    waitForWindow: async (query, options): Promise<AppWindow> =>
      await waitForWindowByQuery(query, options),
    windows: async (): Promise<readonly AppWindow[]> =>
      await listTopLevelWindows(),
    [Symbol.dispose]: releaseAgent,
  };
};
