// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import {
  constants as fileConstants,
  copyFile as copyLocalFile,
  mkdir as makeLocalDirectory,
  readdir as readLocalDirectory,
  readFile as readLocalFile,
  stat as statLocalPath,
  writeFile as writeLocalFile,
} from 'node:fs/promises';
import { once } from 'node:events';
import {
  dirname as localDirname,
  join as joinLocalPath,
  relative as relativeLocalPath,
  resolve as resolveLocalPath,
} from 'node:path';

import { createDeferred, delay } from 'async-primitives';

import type {
  AppWindow,
  AppWindowDescendantsOptions,
  AppWindowProcess,
  AppWindowScreenshot,
  AppWindowSnapshot,
  AppWindowVideoCaptureOptions,
  CapturedVideoMetadata,
  CapturedVideoResult,
  CapturedVideoStream,
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
  RemoteAgentVideoCaptureOptions,
  RemoteDiagnosticsCapture,
  RemoteDiagnosticsCaptureOptions,
  RemoteDiagnosticsWindow,
  RemoteDirectoryDownloadOptions,
  RemoteDirectoryDownloadResult,
  RemoteDirectoryEntry,
  RemoteDirectoryManifestEntry,
  RemoteDirectorySyncOptions,
  RemoteDirectorySyncResult,
  RemoteFileStat,
  RemoteFileType,
  RemoteInteractionSession,
  RemoteInteractionSessionOptions,
  RemoteStableBoundsWaitOptions,
  RemoteInputOperation,
  RemoteKeyboardPressOptions,
  RemoteMonitor,
  RemoteMouseButtonOptions,
  RemoteMouseClickOptions,
  RemoteMouseDragOptions,
  RemoteMouseWheelOptions,
  RemoteCleanupOptions,
  RemoteManagedProcess,
  RemoteManagedProcessLaunchOptions,
  RemoteManagedProcessSnapshot,
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
  createResourceDeadline,
  operationDetails,
  retryResourceOperation,
} from './resource-operation';
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
import {
  createFileBinaryTransferReceiver,
  removeCompletedFileBinaryTransfer,
  type CompletedFileBinaryTransfer,
} from './file-binary-transfer';

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

interface HeldInteractionInput {
  readonly id: string;
  readonly release: RemoteInputOperation;
}

interface WaitingBinaryTransfer {
  readonly reference: BinaryTransferReference;
  readonly reject: (error: RemoteAgentError) => void;
  readonly resolve: (data: Buffer) => void;
  readonly timer: ReturnType<typeof setTimeout>;
}

interface WaitingFileBinaryTransfer {
  readonly reference: BinaryTransferReference;
  readonly reject: (error: RemoteAgentError) => void;
  readonly resolve: (transfer: CompletedFileBinaryTransfer) => void;
  readonly timer: ReturnType<typeof setTimeout>;
}

interface ParsedVideoResult {
  readonly metadata: CapturedVideoMetadata;
  readonly transfer: BinaryTransferReference;
}

interface ManagedProcessLaunchResult {
  readonly managedProcessId: number;
  readonly process: RemoteApplicationProcess;
  readonly stdoutPath: string | undefined;
  readonly stderrPath: string | undefined;
}

interface LocalDirectoryManifestEntry extends RemoteDirectoryManifestEntry {
  readonly absolutePath: string;
}

interface DirectoryFilter {
  readonly included: (path: string, type: RemoteFileType) => boolean;
  readonly excluded: (path: string, type: RemoteFileType) => boolean;
}

interface DirectorySyncCounters {
  uploadedFiles: number;
  skippedFiles: number;
  createdDirectories: number;
  deletedFiles: number;
  deletedDirectories: number;
  bytesUploaded: number;
}

interface DirectoryDownloadCounters {
  downloadedFiles: number;
  createdDirectories: number;
  bytesDownloaded: number;
}

interface LockedFileRetryOptions {
  readonly policy: 'fail' | 'retry' | 'killRelatedProcessesAndRetry';
  readonly relatedProcessPaths: readonly string[];
}

const defaultTimeoutMs = 30000;
const defaultPasteRestoreDelayMs = 500;
const binaryTransferChunkSize = 64 * 1024;
const maxRecentDiagnosticsOperations = 100;
const lockedFileRetryAttempts = 5;
const lockedFileRetryDelayMs = 100;

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

const normalizeDirectoryRelativePath = (path: string): string =>
  path.replace(/\\/gu, '/').replace(/^\/+/u, '').replace(/\/+$/u, '');

const normalizeRemotePath = (path: string): string => {
  const normalized = path.replace(/\\/gu, '/').replace(/\/+$/u, '');
  return /^[A-Za-z]:$/u.test(normalized) ? `${normalized}/` : normalized;
};

const joinRemotePath = (root: string, relativePath: string): string => {
  const normalizedRoot = normalizeRemotePath(root);
  const normalizedRelative = normalizeDirectoryRelativePath(relativePath);
  if (normalizedRelative === '') {
    return normalizedRoot;
  }
  if (normalizedRoot.endsWith('/')) {
    return `${normalizedRoot}${normalizedRelative}`;
  }
  return `${normalizedRoot}/${normalizedRelative}`;
};

const normalizeLocalRelativePath = (root: string, path: string): string =>
  normalizeDirectoryRelativePath(relativeLocalPath(root, path));

const escapeRegExpCharacter = (character: string): string =>
  character.replace(/[\\^$.*+?()[\]{}|]/gu, '\\$&');

const globToRegExp = (pattern: string): RegExp => {
  const normalized = normalizeDirectoryRelativePath(pattern);
  let source = '^';
  for (let index = 0; index < normalized.length; index += 1) {
    const character = normalized[index];
    if (character === '*') {
      if (normalized[index + 1] === '*') {
        source += '.*';
        index += 1;
      } else {
        source += '[^/]*';
      }
    } else if (character === '?') {
      source += '[^/]';
    } else if (character !== undefined) {
      source += escapeRegExpCharacter(character);
    }
  }
  source += '$';
  return new RegExp(source, 'u');
};

const pathMatchesAnyPattern = (
  path: string,
  type: RemoteFileType,
  patterns: readonly RegExp[]
): boolean => {
  const normalized = normalizeDirectoryRelativePath(path);
  const candidates =
    type === 'directory' ? [normalized, `${normalized}/`] : [normalized];
  return patterns.some((pattern) =>
    candidates.some((candidate) => pattern.test(candidate))
  );
};

const createDirectoryFilter = (
  include: readonly string[] | undefined,
  exclude: readonly string[] | undefined
): DirectoryFilter => {
  const includePatterns = (include ?? []).map((pattern) =>
    globToRegExp(pattern)
  );
  const excludePatterns = (exclude ?? []).map((pattern) =>
    globToRegExp(pattern)
  );
  return {
    excluded: (path, type): boolean =>
      pathMatchesAnyPattern(path, type, excludePatterns),
    included: (path, type): boolean =>
      (includePatterns.length === 0 ||
        pathMatchesAnyPattern(path, type, includePatterns)) &&
      !pathMatchesAnyPattern(path, type, excludePatterns),
  };
};

const buildLocalDirectoryManifest = async (
  root: string,
  filter: DirectoryFilter
): Promise<readonly LocalDirectoryManifestEntry[]> => {
  const entries: LocalDirectoryManifestEntry[] = [];
  const visit = async (directory: string): Promise<void> => {
    for (const name of await readLocalDirectory(directory)) {
      const absolutePath = joinLocalPath(directory, name);
      const stat = await statLocalPath(absolutePath);
      const relativePath = normalizeLocalRelativePath(root, absolutePath);
      if (stat.isDirectory()) {
        if (filter.excluded(relativePath, 'directory')) {
          continue;
        }
        if (filter.included(relativePath, 'directory')) {
          entries.push({
            absolutePath,
            modifiedAt: stat.mtime.toISOString(),
            path: relativePath,
            size: 0,
            type: 'directory',
          });
        }
        await visit(absolutePath);
      } else if (stat.isFile() && filter.included(relativePath, 'file')) {
        const data = await readLocalFile(absolutePath);
        entries.push({
          absolutePath,
          modifiedAt: stat.mtime.toISOString(),
          path: relativePath,
          sha256: sha256Hex(data),
          size: data.byteLength,
          type: 'file',
        });
      }
    }
  };
  await visit(root);
  return entries.sort((left, right) => left.path.localeCompare(right.path));
};

const isLockedFileError = (error: unknown): boolean => {
  if (!(error instanceof Error)) {
    return false;
  }
  const message = error.message.toLowerCase();
  return (
    message.includes('win32error=32') ||
    message.includes('win32error=33') ||
    message.includes('win32error=5') ||
    message.includes('sharing violation') ||
    message.includes('lock violation') ||
    message.includes('access is denied') ||
    message.includes('access denied')
  );
};

const pathIsUnderRemotePrefix = (path: string, prefix: string): boolean => {
  const normalizedPath = normalizeRemotePath(path).toLowerCase();
  const normalizedPrefix = normalizeRemotePath(prefix).toLowerCase();
  return (
    normalizedPath === normalizedPrefix ||
    normalizedPath.startsWith(
      normalizedPrefix.endsWith('/') ? normalizedPrefix : `${normalizedPrefix}/`
    )
  );
};

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
  const path = value.path;
  if (path !== undefined && path !== null && typeof path !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Window process path field must be a string or null.'
    );
  }
  return {
    id: readNumber(value, 'id'),
    name: readString(value, 'name'),
    path: typeof path === 'string' ? path : '',
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

const readVideoInteger = (
  value: Record<string, unknown>,
  key: string,
  minimum: number
): number => {
  const field = value[key];
  if (
    typeof field !== 'number' ||
    !Number.isSafeInteger(field) ||
    field < minimum
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `Video result field ${key} is invalid.`
    );
  }
  return field;
};

const parseVideoResult = (value: JsonValue | undefined): ParsedVideoResult => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Video result must be an object.'
    );
  }
  if (
    value.contentType !== 'video/mp4' ||
    value.codec !== 'h264' ||
    typeof value.clipped !== 'boolean'
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Video result format metadata is invalid.'
    );
  }
  return {
    metadata: {
      clipped: value.clipped,
      codec: value.codec,
      contentType: value.contentType,
      droppedFrames: readVideoInteger(value, 'droppedFrames', 0),
      durationMs: readVideoInteger(value, 'durationMs', 0),
      finalBounds: parseRect(value.finalBounds),
      fps: readVideoInteger(value, 'fps', 1),
      frameCount: readVideoInteger(value, 'frameCount', 0),
      initialBounds: parseRect(value.initialBounds),
    },
    transfer: parseBinaryTransferReference(value, 'video/mp4', 'Video result'),
  };
};

const parseVideoRecordingId = (value: JsonValue | undefined): string => {
  if (!isRecord(value) || typeof value.recordingId !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Video recording start result is invalid.'
    );
  }
  return value.recordingId;
};

const validateVideoDuration = (durationMs: number): void => {
  if (
    !Number.isSafeInteger(durationMs) ||
    durationMs <= 0 ||
    durationMs > 0xffffffff
  ) {
    throw createRemoteAgentError(
      'INVALID_ARGUMENT',
      'durationMs must be a positive integer no greater than 4294967295.'
    );
  }
};

const validateVideoOptions = (options: VideoCaptureOptions): void => {
  if (
    !Number.isSafeInteger(options.fps) ||
    options.fps < 1 ||
    options.fps > 240
  ) {
    throw createRemoteAgentError(
      'INVALID_ARGUMENT',
      'fps must be an integer from 1 through 240.'
    );
  }
  if (
    !Number.isSafeInteger(options.quality) ||
    options.quality < 1 ||
    options.quality > 100
  ) {
    throw createRemoteAgentError(
      'INVALID_ARGUMENT',
      'quality must be an integer from 1 through 100.'
    );
  }
};

interface VideoCaptureOptions {
  readonly fps: number;
  readonly quality: number;
}

const normalizeVideoOptions = (
  options: AppWindowVideoCaptureOptions | RemoteAgentVideoCaptureOptions
): VideoCaptureOptions => {
  const normalized = {
    fps: options.fps ?? 60,
    quality: options.quality ?? 90,
  };
  validateVideoOptions(normalized);
  return normalized;
};

const validateVideoRect = (rect: ScreenRect): void => {
  if (
    !Number.isSafeInteger(rect.x) ||
    !Number.isSafeInteger(rect.y) ||
    !Number.isSafeInteger(rect.width) ||
    !Number.isSafeInteger(rect.height) ||
    rect.width <= 0 ||
    rect.height <= 0
  ) {
    throw createRemoteAgentError(
      'INVALID_ARGUMENT',
      'Video capture rect must contain integer coordinates and positive dimensions.'
    );
  }
};

const createCapturedVideoStream = (
  transfer: CompletedFileBinaryTransfer,
  metadata: CapturedVideoMetadata
): CapturedVideoStream => {
  const stream = createReadStream(transfer.path);
  let releasePromise: Promise<void> | undefined = undefined;
  const releaseAsync = async (): Promise<void> => {
    if (releasePromise !== undefined) {
      await releasePromise;
      return;
    }
    releasePromise = (async (): Promise<void> => {
      if (!stream.closed) {
        const closed = once(stream, 'close');
        stream.destroy();
        try {
          await closed;
        } catch {
          // Cleanup must continue even when a stream error caused closure.
        }
      }
      await removeCompletedFileBinaryTransfer(transfer);
    })();
    await releasePromise;
  };
  return Object.assign(stream, metadata, {
    releaseAsync,
    [Symbol.asyncDispose]: releaseAsync,
  }) as CapturedVideoStream;
};

const persistCapturedVideo = async (
  transfer: CompletedFileBinaryTransfer,
  metadata: CapturedVideoMetadata,
  outputPath: string
): Promise<CapturedVideoResult> => {
  const path = resolveLocalPath(outputPath);
  try {
    await copyLocalFile(transfer.path, path, fileConstants.COPYFILE_EXCL);
  } catch (error) {
    await removeCompletedFileBinaryTransfer(transfer);
    const code =
      isRecord(error) && typeof error.code === 'string'
        ? error.code
        : undefined;
    throw createRemoteAgentError(
      code === 'EEXIST' ? 'INVALID_ARGUMENT' : 'PROTOCOL_ERROR',
      code === 'EEXIST'
        ? `Video output path already exists: ${path}.`
        : `Failed to persist video output: ${path}.`
    );
  }
  await removeCompletedFileBinaryTransfer(transfer);
  return {
    ...metadata,
    path,
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

const parseDirectoryManifestEntry = (
  value: unknown
): RemoteDirectoryManifestEntry => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.manifest entry must be an object.'
    );
  }
  const type = value.type;
  if (type !== 'file' && type !== 'directory' && type !== 'other') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.manifest entry type is invalid.'
    );
  }
  const sha256 = readOptionalString(value, 'sha256');
  return {
    ...(sha256 === undefined ? {} : { sha256 }),
    modifiedAt: readString(value, 'modifiedAt'),
    path: normalizeDirectoryRelativePath(readString(value, 'path')),
    size: readNumber(value, 'size'),
    type,
  };
};

const parseDirectoryManifest = (
  value: JsonValue | undefined
): readonly RemoteDirectoryManifestEntry[] => {
  if (!isRecord(value) || !Array.isArray(value.entries)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'file.manifest result must include entries.'
    );
  }
  return value.entries.map((entry) => parseDirectoryManifestEntry(entry));
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

const readOptionalString = (
  record: Record<string, unknown>,
  key: string
): string | undefined => {
  const value = record[key];
  if (value === null || value === undefined) {
    return undefined;
  }
  if (typeof value !== 'string') {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      `${key} must be a string or null.`
    );
  }
  return value;
};

const parseManagedProcessLaunchResult = (
  value: JsonValue | undefined
): ManagedProcessLaunchResult => {
  if (!isRecord(value)) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process.launchManaged result must be an object.'
    );
  }
  return {
    managedProcessId: readNumber(value, 'managedProcessId'),
    process: {
      id: readNumber(value, 'id'),
      name: readString(value, 'name'),
    },
    stderrPath: readOptionalString(value, 'stderrPath'),
    stdoutPath: readOptionalString(value, 'stdoutPath'),
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
  const parentProcessId = value.parentProcessId;
  const createdAt = value.createdAt;
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
  if (
    parentProcessId !== undefined &&
    parentProcessId !== null &&
    (typeof parentProcessId !== 'number' || !Number.isFinite(parentProcessId))
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process snapshot parentProcessId field must be null or a finite number.'
    );
  }
  if (
    createdAt !== undefined &&
    createdAt !== null &&
    typeof createdAt !== 'string'
  ) {
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'process snapshot createdAt field must be null or a string.'
    );
  }
  return {
    createdAt: typeof createdAt === 'string' ? createdAt : null,
    exitCode,
    id: readNumber(value, 'id'),
    name: readString(value, 'name'),
    parentProcessId:
      typeof parentProcessId === 'number' ? parentProcessId : null,
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

const managedProcessLaunchOptionsToJson = (
  options: RemoteManagedProcessLaunchOptions,
  stdoutPath: string | undefined,
  stderrPath: string | undefined
): JsonValue => ({
  ...(options.arguments === undefined
    ? {}
    : { arguments: [...options.arguments] }),
  ...(options.captureStderr === undefined
    ? {}
    : { captureStderr: options.captureStderr }),
  ...(options.captureStdout === undefined
    ? {}
    : { captureStdout: options.captureStdout }),
  ...(options.createNoWindow === undefined
    ? {}
    : { createNoWindow: options.createNoWindow }),
  ...(options.environment === undefined
    ? {}
    : { environment: { ...options.environment } }),
  killTreeOnRelease: options.killTreeOnRelease ?? true,
  path: options.path,
  ...(stderrPath === undefined ? {} : { stderrPath }),
  ...(stdoutPath === undefined ? {} : { stdoutPath }),
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
  options:
    | RemoteMouseButtonOptions
    | RemoteMouseClickOptions
    | RemoteMouseDragOptions
    | undefined
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
    case 'mouse.down':
      return {
        button: operation.button,
        kind: operation.kind,
        point: operation.point === null ? null : pointToJson(operation.point),
      };
    case 'mouse.move':
      return {
        kind: operation.kind,
        point: pointToJson(operation.point),
      };
    case 'mouse.up':
      return {
        button: operation.button,
        kind: operation.kind,
        point: operation.point === null ? null : pointToJson(operation.point),
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
  const fileBinaryReceiver = createFileBinaryTransferReceiver();
  const completedBinaryTransfers = new Map<string, Buffer>();
  const completedFileBinaryTransfers = new Map<
    string,
    CompletedFileBinaryTransfer
  >();
  const waitingBinaryTransfers = new Map<string, WaitingBinaryTransfer[]>();
  const waitingFileBinaryTransfers = new Map<
    string,
    WaitingFileBinaryTransfer[]
  >();
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

  const rejectFileBinaryTransferWaiters = (error: RemoteAgentError): void => {
    for (const waiters of waitingFileBinaryTransfers.values()) {
      for (const waiter of waiters) {
        clearTimeout(waiter.timer);
        waiter.reject(error);
      }
    }
    waitingFileBinaryTransfers.clear();
  };

  const cleanupFileBinaryTransfers = async (): Promise<void> => {
    const completed = [...completedFileBinaryTransfers.values()];
    completedFileBinaryTransfers.clear();
    for (const transfer of completed) {
      await removeCompletedFileBinaryTransfer(transfer);
    }
    await fileBinaryReceiver.releaseAsync();
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

  const acceptBinaryChunk = async (
    chunk: ProtocolBinaryTransferChunk
  ): Promise<void> => {
    if (chunk.contentType === 'video/mp4') {
      const result = await fileBinaryReceiver.acceptChunk(chunk);
      if (result === undefined) {
        return;
      }
      const waiters = waitingFileBinaryTransfers.get(chunk.transferId);
      if (waiters === undefined || waiters.length === 0) {
        waitingFileBinaryTransfers.delete(chunk.transferId);
        completedFileBinaryTransfers.set(chunk.transferId, result);
        return;
      }
      waitingFileBinaryTransfers.delete(chunk.transferId);
      for (const waiter of waiters) {
        clearTimeout(waiter.timer);
        if (
          waiter.reference.contentType !== result.contentType ||
          waiter.reference.totalBytes !== result.totalBytes ||
          waiter.reference.sha256 !== result.sha256
        ) {
          await removeCompletedFileBinaryTransfer(result);
          waiter.reject(
            createRemoteAgentError(
              'PROTOCOL_ERROR',
              `Video binary transfer metadata mismatch: ${chunk.transferId}.`
            )
          );
        } else {
          waiter.resolve(result);
        }
      }
      return;
    }
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

  const readFileBinaryTransfer = async (
    reference: BinaryTransferReference
  ): Promise<CompletedFileBinaryTransfer> => {
    const completed = completedFileBinaryTransfers.get(reference.transferId);
    if (completed !== undefined) {
      completedFileBinaryTransfers.delete(reference.transferId);
      if (
        reference.contentType !== completed.contentType ||
        reference.totalBytes !== completed.totalBytes ||
        reference.sha256 !== completed.sha256
      ) {
        await removeCompletedFileBinaryTransfer(completed);
        throw createRemoteAgentError(
          'PROTOCOL_ERROR',
          `Video binary transfer metadata mismatch: ${reference.transferId}.`
        );
      }
      return completed;
    }

    const controller = new AbortController();
    const transferTimeoutMs = Math.max(
      timeoutMs,
      defaultTimeoutMs + Math.ceil(reference.totalBytes / (1024 * 1024)) * 1000
    );
    const timer = setTimeout(() => {
      controller.abort();
    }, transferTimeoutMs);
    const deferred = createDeferred<CompletedFileBinaryTransfer>(
      controller.signal
    );
    const waiter: WaitingFileBinaryTransfer = {
      reference,
      reject: (error): void => {
        deferred.reject(error);
      },
      resolve: (transfer): void => {
        deferred.resolve(transfer);
      },
      timer,
    };
    waitingFileBinaryTransfers.set(reference.transferId, [
      ...(waitingFileBinaryTransfers.get(reference.transferId) ?? []),
      waiter,
    ]);
    try {
      return await deferred.promise;
    } catch (error) {
      const waiters = waitingFileBinaryTransfers.get(reference.transferId);
      if (waiters !== undefined) {
        const remaining = waiters.filter((entry) => entry !== waiter);
        if (remaining.length === 0) {
          waitingFileBinaryTransfers.delete(reference.transferId);
        } else {
          waitingFileBinaryTransfers.set(reference.transferId, remaining);
        }
      }
      await fileBinaryReceiver.cancel(reference.transferId);
      if (controller.signal.aborted) {
        throw createRemoteAgentError(
          'PROTOCOL_ERROR',
          `Timed out waiting for video transfer: ${reference.transferId}.`
        );
      }
      throw error;
    } finally {
      clearTimeout(timer);
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
    onBinaryChunk: async (chunk): Promise<void> => {
      try {
        await acceptBinaryChunk(chunk);
      } catch (error) {
        const wrapped = createRemoteAgentError(
          'PROTOCOL_ERROR',
          error instanceof Error ? error.message : 'Invalid binary transfer.'
        );
        pending.rejectAll('PROTOCOL_ERROR', wrapped.message);
        rejectBinaryTransferWaiters(wrapped);
        rejectFileBinaryTransferWaiters(wrapped);
        await cleanupFileBinaryTransfers();
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
      rejectFileBinaryTransferWaiters(wrapped);
      void cleanupFileBinaryTransfers();
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
      rejectFileBinaryTransferWaiters(wrapped);
      void cleanupFileBinaryTransfers();
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

  const connectedCapabilities = await ready;
  const activeTransport = transport;
  const connectedFeatures = new Set(connectedCapabilities.features);

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
    rejectFileBinaryTransferWaiters(wrapped);
    void cleanupFileBinaryTransfers();
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

  const receiveRecordedVideo = async (
    method: 'agent.recordVideo' | 'window.recordVideo',
    durationMs: number,
    outputPath: string | undefined,
    options: AppWindowVideoCaptureOptions | RemoteAgentVideoCaptureOptions,
    target: JsonValue
  ): Promise<CapturedVideoResult | CapturedVideoStream> => {
    validateVideoDuration(durationMs);
    if (outputPath !== undefined && outputPath.trim() === '') {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        'Video output path must not be empty.'
      );
    }
    const normalized = normalizeVideoOptions(options);
    const recordingId = parseVideoRecordingId(
      await requestJson(method, {
        durationMs,
        fps: normalized.fps,
        quality: normalized.quality,
        ...(isRecord(target) ? target : {}),
      })
    );

    await delay(durationMs);
    const video = parseVideoResult(
      await requestJson('video.result', {
        recordingId,
      })
    );
    const transfer = await readFileBinaryTransfer(video.transfer);
    return outputPath === undefined
      ? createCapturedVideoStream(transfer, video.metadata)
      : await persistCapturedVideo(transfer, video.metadata, outputPath);
  };

  const createWindowRecordVideo = (
    snapshot: AppWindowSnapshot
  ): AppWindow['recordVideo'] =>
    (async (
      durationMs: number,
      outputPathOrOptions?: string | AppWindowVideoCaptureOptions,
      pathOptions?: AppWindowVideoCaptureOptions
    ): Promise<CapturedVideoResult | CapturedVideoStream> => {
      const outputPath =
        typeof outputPathOrOptions === 'string'
          ? outputPathOrOptions
          : undefined;
      const options =
        typeof outputPathOrOptions === 'string'
          ? (pathOptions ?? {})
          : (outputPathOrOptions ?? {});
      const tracking = options.tracking ?? 'followWindow';
      if (tracking !== 'followWindow' && tracking !== 'initialBounds') {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          'tracking must be followWindow or initialBounds.'
        );
      }
      return await receiveRecordedVideo(
        'window.recordVideo',
        durationMs,
        outputPath,
        options,
        {
          tracking,
          windowId: snapshot.id,
        }
      );
    }) as AppWindow['recordVideo'];

  const recordAgentVideo = (async (
    durationMs: number,
    outputPathOrOptions?: string | RemoteAgentVideoCaptureOptions,
    pathOptions?: RemoteAgentVideoCaptureOptions
  ): Promise<CapturedVideoResult | CapturedVideoStream> => {
    const outputPath =
      typeof outputPathOrOptions === 'string' ? outputPathOrOptions : undefined;
    const options =
      typeof outputPathOrOptions === 'string'
        ? (pathOptions ?? {})
        : (outputPathOrOptions ?? {});
    if (options.rect !== undefined) {
      validateVideoRect(options.rect);
    }
    return await receiveRecordedVideo(
      'agent.recordVideo',
      durationMs,
      outputPath,
      options,
      options.rect === undefined ? {} : { rect: rectToJson(options.rect) }
    );
  }) as RemoteAgent['recordVideo'];

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
    recordVideo: createWindowRecordVideo(snapshot),
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

  const createInteractionSession = async (
    options: RemoteInteractionSessionOptions | undefined
  ): Promise<RemoteInteractionSession> => {
    const restoreCursor = options?.restoreCursor ?? true;
    const initialCursor = restoreCursor
      ? parseCursor(await requestJson('agent.cursor', undefined))
      : undefined;
    const heldIds = new Set<string>();
    const heldInputs: HeldInteractionInput[] = [];
    let released = false;
    let releasePromise: Promise<void> | undefined = undefined;

    const assertActive = (): void => {
      if (released) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          'Interaction session has already been released.'
        );
      }
    };

    const removeHeldInput = (id: string): void => {
      const index = heldInputs.findIndex((entry) => entry.id === id);
      if (index >= 0) {
        heldInputs.splice(index, 1);
      }
    };

    const releaseOwnedInputs = async (): Promise<void> => {
      let firstError: unknown = undefined;
      for (const input of [...heldInputs].reverse()) {
        if (!heldIds.has(input.id)) {
          continue;
        }
        try {
          await performInput(input.release);
          heldIds.delete(input.id);
          removeHeldInput(input.id);
        } catch (error) {
          firstError ??= error;
        }
      }
      if (initialCursor !== undefined) {
        try {
          await performInput({
            kind: 'mouse.move',
            point: initialCursor.point,
          });
        } catch (error) {
          firstError ??= error;
        }
      }
      if (firstError !== undefined) {
        throw firstError;
      }
    };

    const releaseAsync = async (): Promise<void> => {
      if (releasePromise !== undefined) {
        await releasePromise;
        return;
      }
      if (released) {
        return;
      }
      released = true;
      releasePromise = releaseOwnedInputs();
      await releasePromise;
    };

    return {
      [Symbol.asyncDispose]: releaseAsync,
      keyboard: {
        down: async (key): Promise<void> => {
          assertActive();
          const id = `keyboard:${key}`;
          if (heldIds.has(id)) {
            return;
          }
          await performInput({
            key,
            kind: 'keyboard.down',
          });
          heldIds.add(id);
          heldInputs.push({
            id,
            release: {
              key,
              kind: 'keyboard.up',
            },
          });
        },
        press: async (key, pressOptions): Promise<void> => {
          assertActive();
          await performInput({
            key,
            kind: 'keyboard.press',
            modifiers: modifiersFromOptions(pressOptions),
          });
        },
        up: async (key): Promise<void> => {
          assertActive();
          const id = `keyboard:${key}`;
          if (!heldIds.has(id)) {
            return;
          }
          await performInput({
            key,
            kind: 'keyboard.up',
          });
          heldIds.delete(id);
          removeHeldInput(id);
        },
      },
      mouse: {
        click: async (point, clickOptions): Promise<void> => {
          assertActive();
          await performInput({
            button: buttonFromOptions(clickOptions),
            kind: 'mouse.click',
            modifiers: modifiersFromOptions(clickOptions),
            point,
          });
        },
        down: async (buttonOptions): Promise<void> => {
          assertActive();
          const button = buttonFromOptions(buttonOptions);
          const id = `mouse:${button}`;
          if (heldIds.has(id)) {
            return;
          }
          await performInput({
            button,
            kind: 'mouse.down',
            point: buttonOptions?.point ?? null,
          });
          heldIds.add(id);
          heldInputs.push({
            id,
            release: {
              button,
              kind: 'mouse.up',
              point: null,
            },
          });
        },
        drag: async (from, to, dragOptions): Promise<void> => {
          assertActive();
          await performInput({
            button: buttonFromOptions(dragOptions),
            from,
            kind: 'mouse.drag',
            modifiers: modifiersFromOptions(dragOptions),
            to,
          });
        },
        move: async (point): Promise<void> => {
          assertActive();
          await performInput({
            kind: 'mouse.move',
            point,
          });
        },
        up: async (buttonOptions): Promise<void> => {
          assertActive();
          const button = buttonFromOptions(buttonOptions);
          const id = `mouse:${button}`;
          if (!heldIds.has(id)) {
            return;
          }
          await performInput({
            button,
            kind: 'mouse.up',
            point: buttonOptions?.point ?? null,
          });
          heldIds.delete(id);
          removeHeldInput(id);
        },
        wheel: async (wheelOptions): Promise<void> => {
          assertActive();
          await performInput({
            deltaX: wheelOptions.deltaX ?? 0,
            deltaY: wheelOptions.deltaY ?? 0,
            kind: 'mouse.wheel',
            point: wheelOptions.point ?? null,
          });
        },
      },
      pause: async (durationMs): Promise<void> => {
        assertActive();
        if (!Number.isFinite(durationMs) || durationMs < 0) {
          throw createRemoteAgentError(
            'INVALID_ARGUMENT',
            'durationMs must be a non-negative finite number.'
          );
        }
        await waitForDelay(durationMs);
      },
      releaseAsync,
    };
  };

  const withInteractionSession = async <T>(
    operation: (session: RemoteInteractionSession) => Promise<T>,
    options: RemoteInteractionSessionOptions | undefined
  ): Promise<T> => {
    const session = await createInteractionSession(options);
    try {
      const result = await operation(session);
      await session.releaseAsync();
      return result;
    } catch (error) {
      try {
        await session.releaseAsync();
      } catch {
        // Preserve the operation error. Successful operations still surface release errors.
      }
      throw error;
    }
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

  const makeRemoteTempDirectory = async (prefix: string): Promise<string> =>
    parseTempDirectory(
      await requestJson('file.mkdtemp', {
        prefix,
      })
    );

  const readRemoteFile = async (path: string): Promise<Buffer> =>
    await parseFileReadResult(
      await requestJson('file.read', {
        path,
      }),
      readBinaryTransfer
    );

  const remotePathExists = async (path: string): Promise<boolean> =>
    parseExists(
      await requestJson('file.exists', {
        path,
      })
    );

  const makeRemoteDirectory = async (path: string): Promise<void> => {
    await requestJson('file.mkdir', {
      path,
      recursive: true,
    });
  };

  const readRemoteDirectoryManifest = async (
    path: string
  ): Promise<readonly RemoteDirectoryManifestEntry[]> =>
    parseDirectoryManifest(
      await requestJson('file.manifest', {
        path,
      })
    );

  const writeRemoteFile = async (path: string, data: Buffer): Promise<void> => {
    const transfer = await sendBinaryTransfer('application/octet-stream', data);
    await requestJson('file.write', {
      contentType: transfer.contentType,
      path,
      sha256: transfer.sha256,
      totalBytes: transfer.totalBytes,
      transferId: transfer.transferId,
    });
  };

  const removeRemotePath = async (
    path: string,
    recursive: boolean
  ): Promise<void> => {
    await requestJson('file.remove', {
      path,
      recursive,
    });
  };

  const renameRemotePath = async (from: string, to: string): Promise<void> => {
    await requestJson('file.rename', {
      from,
      to,
    });
  };

  const killRelatedProcesses = async (
    relatedProcessPaths: readonly string[]
  ): Promise<void> => {
    const processes = parseProcessSnapshotArray(
      await requestJson('process.list', {})
    );
    const related = processes.filter(
      (process) =>
        process.running &&
        process.path !== '' &&
        relatedProcessPaths.some((path) =>
          pathIsUnderRemotePrefix(process.path, path)
        )
    );
    for (const process of related) {
      await requestJson('process.kill', {
        processId: process.id,
      });
      await waitForProcessExit(process.id, {
        intervalMs: 50,
        timeoutMs: 5000,
      });
    }
  };

  const withLockedFileRetry = async <T>(
    retryOptions: LockedFileRetryOptions,
    operation: () => Promise<T>
  ): Promise<T> => {
    let killedProcesses = false;
    for (let attempt = 0; attempt < lockedFileRetryAttempts; attempt += 1) {
      try {
        return await operation();
      } catch (error) {
        if (
          retryOptions.policy === 'fail' ||
          !isLockedFileError(error) ||
          attempt === lockedFileRetryAttempts - 1
        ) {
          throw error;
        }
        if (
          retryOptions.policy === 'killRelatedProcessesAndRetry' &&
          !killedProcesses
        ) {
          await killRelatedProcesses(retryOptions.relatedProcessPaths);
          killedProcesses = true;
        }
        await waitForDelay(lockedFileRetryDelayMs);
      }
    }
    throw createRemoteAgentError(
      'PROTOCOL_ERROR',
      'Locked file retry exhausted.'
    );
  };

  const createRemoteManifestMap = (
    entries: readonly RemoteDirectoryManifestEntry[]
  ): Map<string, RemoteDirectoryManifestEntry> =>
    new Map(
      entries.map((entry) => [
        normalizeDirectoryRelativePath(entry.path),
        {
          ...entry,
          path: normalizeDirectoryRelativePath(entry.path),
        },
      ])
    );

  const createLocalManifestMap = (
    entries: readonly LocalDirectoryManifestEntry[]
  ): Map<string, LocalDirectoryManifestEntry> =>
    new Map(entries.map((entry) => [entry.path, entry]));

  const directoryDepth = (path: string): number =>
    path === '' ? 0 : path.split('/').length;

  const removeRemoteManifestEntry = async (
    remoteRoot: string,
    entry: RemoteDirectoryManifestEntry,
    retryOptions: LockedFileRetryOptions
  ): Promise<void> => {
    await withLockedFileRetry(retryOptions, async () => {
      await removeRemotePath(
        joinRemotePath(remoteRoot, entry.path),
        entry.type === 'directory'
      );
    });
  };

  const syncDirectory = async (
    options: RemoteDirectorySyncOptions
  ): Promise<RemoteDirectorySyncResult> => {
    const mode = options.mode ?? 'mirror';
    if (mode !== 'mirror' && mode !== 'update') {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        'syncDirectory mode must be mirror or update.'
      );
    }
    const checksum = options.checksum ?? 'sha256';
    if (checksum !== 'sha256') {
      throw createRemoteAgentError(
        'INVALID_ARGUMENT',
        'syncDirectory checksum must be sha256.'
      );
    }
    const policy = options.onLockedFile ?? 'retry';
    const retryOptions: LockedFileRetryOptions = {
      policy,
      relatedProcessPaths:
        options.relatedProcessPaths ??
        (policy === 'killRelatedProcessesAndRetry' ? [options.remotePath] : []),
    };
    const deleteExtraneous = options.deleteExtraneous ?? mode === 'mirror';
    const filter = createDirectoryFilter(options.include, options.exclude);
    const localManifest = await buildLocalDirectoryManifest(
      options.localPath,
      filter
    );
    const counters: DirectorySyncCounters = {
      bytesUploaded: 0,
      createdDirectories: 0,
      deletedDirectories: 0,
      deletedFiles: 0,
      skippedFiles: 0,
      uploadedFiles: 0,
    };

    if (!(await remotePathExists(options.remotePath))) {
      await makeRemoteDirectory(options.remotePath);
      counters.createdDirectories += 1;
    }

    const remoteManifest = await readRemoteDirectoryManifest(
      options.remotePath
    );
    const remoteEntries = remoteManifest.map((entry) => ({
      ...entry,
      path: normalizeDirectoryRelativePath(entry.path),
    }));
    const includedRemoteEntries = remoteEntries.filter((entry) =>
      filter.included(entry.path, entry.type)
    );
    const excludedRemoteEntries = remoteEntries.filter((entry) =>
      filter.excluded(entry.path, entry.type)
    );
    const remoteMap = createRemoteManifestMap(includedRemoteEntries);
    const localMap = createLocalManifestMap(localManifest);

    for (const directory of localManifest
      .filter((entry) => entry.type === 'directory')
      .sort(
        (left, right) => directoryDepth(left.path) - directoryDepth(right.path)
      )) {
      const remoteEntry = remoteMap.get(directory.path);
      if (remoteEntry?.type === 'directory') {
        continue;
      }
      if (remoteEntry !== undefined) {
        await removeRemoteManifestEntry(
          options.remotePath,
          remoteEntry,
          retryOptions
        );
      }
      await withLockedFileRetry(retryOptions, async () => {
        await makeRemoteDirectory(
          joinRemotePath(options.remotePath, directory.path)
        );
      });
      counters.createdDirectories += 1;
    }

    let uploadIndex = 0;
    for (const file of localManifest.filter((entry) => entry.type === 'file')) {
      const remoteEntry = remoteMap.get(file.path);
      if (
        remoteEntry?.type === 'file' &&
        remoteEntry.size === file.size &&
        remoteEntry.sha256 === file.sha256
      ) {
        counters.skippedFiles += 1;
        continue;
      }
      if (remoteEntry !== undefined && remoteEntry.type === 'directory') {
        await removeRemoteManifestEntry(
          options.remotePath,
          remoteEntry,
          retryOptions
        );
      }
      const remoteFilePath = joinRemotePath(options.remotePath, file.path);
      const remoteTempPath = `${remoteFilePath}.agent-rover-${String(
        Date.now()
      )}-${String(uploadIndex)}.tmp`;
      uploadIndex += 1;
      const data = await readLocalFile(file.absolutePath);
      await withLockedFileRetry(retryOptions, async () => {
        await writeRemoteFile(remoteTempPath, data);
        await renameRemotePath(remoteTempPath, remoteFilePath);
      });
      counters.uploadedFiles += 1;
      counters.bytesUploaded += data.byteLength;
    }

    if (deleteExtraneous) {
      const hasExcludedDescendant = (
        entry: RemoteDirectoryManifestEntry
      ): boolean =>
        excludedRemoteEntries.some((excluded) =>
          excluded.path.startsWith(`${entry.path}/`)
        );
      for (const entry of includedRemoteEntries.filter(
        (candidate) =>
          candidate.type !== 'directory' && !localMap.has(candidate.path)
      )) {
        await removeRemoteManifestEntry(
          options.remotePath,
          entry,
          retryOptions
        );
        counters.deletedFiles += 1;
      }
      for (const entry of includedRemoteEntries
        .filter(
          (candidate) =>
            candidate.type === 'directory' &&
            !localMap.has(candidate.path) &&
            !hasExcludedDescendant(candidate)
        )
        .sort(
          (left, right) =>
            directoryDepth(right.path) - directoryDepth(left.path)
        )) {
        await removeRemoteManifestEntry(
          options.remotePath,
          entry,
          retryOptions
        );
        counters.deletedDirectories += 1;
      }
    }

    return counters;
  };

  const downloadDirectory = async (
    options: RemoteDirectoryDownloadOptions
  ): Promise<RemoteDirectoryDownloadResult> => {
    const counters: DirectoryDownloadCounters = {
      bytesDownloaded: 0,
      createdDirectories: 0,
      downloadedFiles: 0,
    };
    if (!(await remotePathExists(options.remotePath))) {
      if (options.ignoreMissing === true) {
        return counters;
      }
      await readRemoteDirectoryManifest(options.remotePath);
    }

    const filter = createDirectoryFilter(options.include, options.exclude);
    const remoteManifest = (
      await readRemoteDirectoryManifest(options.remotePath)
    )
      .map((entry) => ({
        ...entry,
        path: normalizeDirectoryRelativePath(entry.path),
      }))
      .filter((entry) => filter.included(entry.path, entry.type));

    await makeLocalDirectory(options.localPath, {
      recursive: true,
    });
    counters.createdDirectories += 1;

    for (const directory of remoteManifest
      .filter((entry) => entry.type === 'directory')
      .sort(
        (left, right) => directoryDepth(left.path) - directoryDepth(right.path)
      )) {
      await makeLocalDirectory(
        joinLocalPath(options.localPath, ...directory.path.split('/')),
        {
          recursive: true,
        }
      );
      counters.createdDirectories += 1;
    }

    for (const file of remoteManifest.filter(
      (entry) => entry.type === 'file'
    )) {
      const localFilePath = joinLocalPath(
        options.localPath,
        ...file.path.split('/')
      );
      await makeLocalDirectory(localDirname(localFilePath), {
        recursive: true,
      });
      const data = await readRemoteFile(
        joinRemotePath(options.remotePath, file.path)
      );
      await writeLocalFile(localFilePath, data);
      counters.downloadedFiles += 1;
      counters.bytesDownloaded += data.byteLength;
    }

    return counters;
  };

  const createManagedProcessCapturePaths = async (
    options: RemoteManagedProcessLaunchOptions
  ): Promise<{
    readonly stderrPath: string | undefined;
    readonly stdoutPath: string | undefined;
    readonly tempDirectory: string | undefined;
  }> => {
    const captureStdout = options.captureStdout === true;
    const captureStderr = options.captureStderr === true;
    if (!captureStdout && !captureStderr) {
      return {
        stderrPath: undefined,
        stdoutPath: undefined,
        tempDirectory: undefined,
      };
    }

    const tempDirectory = await makeRemoteTempDirectory(
      'C:/agent-rover-managed-process-'
    );
    const normalizedDirectory = tempDirectory.replace(/[\\/]+$/u, '');
    return {
      stderrPath: captureStderr
        ? `${normalizedDirectory}/stderr.log`
        : undefined,
      stdoutPath: captureStdout
        ? `${normalizedDirectory}/stdout.log`
        : undefined,
      tempDirectory,
    };
  };

  const managedLaunchOptionsToApplicationOptions = (
    options: RemoteManagedProcessLaunchOptions,
    stdoutPath: string | undefined,
    stderrPath: string | undefined
  ): RemoteApplicationLaunchOptions => ({
    ...(options.arguments === undefined
      ? {}
      : { arguments: options.arguments }),
    ...(options.createNoWindow === undefined
      ? {}
      : { createNoWindow: options.createNoWindow }),
    ...(options.environment === undefined
      ? {}
      : { environment: options.environment }),
    path: options.path,
    ...(stderrPath === undefined ? {} : { stderrPath }),
    ...(stdoutPath === undefined ? {} : { stdoutPath }),
    ...(options.workingDirectory === undefined
      ? {}
      : { workingDirectory: options.workingDirectory }),
  });

  const createManagedProcessProxy = (options: {
    readonly baselineWindowIds: ReadonlySet<string>;
    readonly killTreeOnRelease: boolean;
    readonly managedProcessId: number | undefined;
    readonly nativeManaged: boolean;
    readonly process: RemoteApplicationProcess;
    readonly stderrPath: string | undefined;
    readonly stdoutPath: string | undefined;
    readonly tempDirectory: string | undefined;
  }): RemoteManagedProcess => {
    let released = false;
    let releaseStarted = false;
    let nativeReleased = false;
    let treeReleased = false;
    let releasing: Promise<void> | undefined;
    const activeReads = new Set<Promise<string>>();
    const knownProcessIds = new Set<number>([options.process.id]);

    const assertNotReleased = (): void => {
      if (released || releaseStarted) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          'Managed process has already been released.'
        );
      }
    };

    const nativeManagedProcessId = (): number => {
      const managedProcessId = options.managedProcessId;
      if (managedProcessId === undefined) {
        throw createRemoteAgentError(
          'PROTOCOL_ERROR',
          'Managed process id is unavailable.'
        );
      }
      return managedProcessId;
    };

    const capturedPath = (
      label: 'stderr' | 'stdout',
      path: string | undefined
    ): string => {
      if (path === undefined) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          `Managed process ${label} was not captured.`
        );
      }
      return path;
    };

    const rootSnapshot = async (): Promise<RemoteProcessSnapshot> => {
      if (!options.nativeManaged) {
        return await snapshotProcess(options.process.id);
      }
      return parseProcessSnapshot(
        await requestJson('process.managedSnapshot', {
          managedProcessId: nativeManagedProcessId(),
        })
      );
    };

    const processCreationMs = (
      process: RemoteProcessSnapshot
    ): number | undefined => {
      if (process.createdAt === null) {
        return undefined;
      }
      const parsed = Date.parse(process.createdAt);
      return Number.isFinite(parsed) ? parsed : undefined;
    };

    const processBelongsToRootEra = (
      root: RemoteProcessSnapshot,
      process: RemoteProcessSnapshot
    ): boolean => {
      const rootCreatedAtMs = processCreationMs(root);
      const processCreatedAtMs = processCreationMs(process);
      return (
        rootCreatedAtMs === undefined ||
        processCreatedAtMs === undefined ||
        processCreatedAtMs >= rootCreatedAtMs
      );
    };

    const listRunningProcesses = async (): Promise<
      readonly RemoteProcessSnapshot[]
    > =>
      parseProcessSnapshotArray(
        await requestJson('process.list', processListOptionsToJson(undefined))
      ).filter((process) => process.running);

    const collectTrackedProcesses = async (
      root: RemoteProcessSnapshot
    ): Promise<readonly RemoteProcessSnapshot[]> => {
      const runningProcesses = await listRunningProcesses();
      const trackedIds = new Set(knownProcessIds);
      trackedIds.add(root.id);
      const trackedProcesses = new Map<number, RemoteProcessSnapshot>();
      let changed = true;
      while (changed) {
        changed = false;
        for (const process of runningProcesses) {
          if (process.id === root.id || process.parentProcessId === null) {
            continue;
          }
          if (!trackedIds.has(process.parentProcessId)) {
            continue;
          }
          if (!processBelongsToRootEra(root, process)) {
            continue;
          }
          trackedProcesses.set(process.id, process);
          if (!trackedIds.has(process.id)) {
            trackedIds.add(process.id);
            changed = true;
          }
        }
      }
      for (const processId of trackedIds) {
        knownProcessIds.add(processId);
      }
      return [...trackedProcesses.values()].sort(
        (left, right) => left.id - right.id
      );
    };

    const snapshot = async (): Promise<RemoteManagedProcessSnapshot> => {
      const root = await rootSnapshot();
      const trackedProcesses = await collectTrackedProcesses(root);
      const nativeRunning = options.nativeManaged
        ? await requestJson('process.managedRunning', {
            managedProcessId: nativeManagedProcessId(),
          })
        : false;
      if (typeof nativeRunning !== 'boolean')
        throw createRemoteAgentError(
          'PROTOCOL_ERROR',
          'Invalid managed process running state.'
        );
      return {
        processes: trackedProcesses,
        root,
        running:
          nativeRunning ||
          root.running ||
          trackedProcesses.some((process) => process.running),
      };
    };

    const processTreeDepth = (
      process: RemoteProcessSnapshot,
      processes: ReadonlyMap<number, RemoteProcessSnapshot>
    ): number => {
      let depth = 0;
      let parentProcessId = process.parentProcessId;
      const visited = new Set<number>();
      while (
        parentProcessId !== null &&
        processes.has(parentProcessId) &&
        !visited.has(parentProcessId)
      ) {
        visited.add(parentProcessId);
        depth += 1;
        parentProcessId =
          processes.get(parentProcessId)?.parentProcessId ?? null;
      }
      return depth;
    };

    const killProcessIfRunning = async (
      process: RemoteProcessSnapshot
    ): Promise<void> => {
      try {
        await requestJson('process.kill', {
          processId: process.id,
        });
      } catch (error) {
        const current = await snapshotProcess(process.id);
        if (current.running) {
          throw error;
        }
      }
    };

    const killTrackedProcessTree = async (): Promise<void> => {
      const current = await snapshot();
      const targets = [...current.processes, current.root].filter(
        (process) => process.running
      );
      const processMap = new Map(
        targets.map((process) => [process.id, process])
      );
      for (const process of targets.sort(
        (left, right) =>
          processTreeDepth(right, processMap) -
          processTreeDepth(left, processMap)
      )) {
        await killProcessIfRunning(process);
      }
    };

    const kill = async (): Promise<void> => {
      assertNotReleased();
      if (options.nativeManaged) {
        await requestJson('process.killManaged', {
          managedProcessId: nativeManagedProcessId(),
        });
      } else {
        await killTrackedProcessTree();
      }
    };

    const waitForExit = async (
      waitOptions?: RemoteWaitOptions
    ): Promise<RemoteManagedProcessSnapshot> =>
      await waitForResult(async () => {
        const current = await snapshot();
        if (!current.running) {
          return current;
        }
        throw new Error(
          `Managed process is still running: ${String(options.process.id)}.`
        );
      }, waitOptions);

    const readCapturedText = async (
      label: 'stderr' | 'stdout',
      path: string | undefined,
      readOptions: RemoteCleanupOptions | undefined
    ): Promise<string> => {
      assertNotReleased();
      const resolvedPath = capturedPath(label, path);
      const deadline = createResourceDeadline(readOptions?.timeoutMs);
      const reading = retryResourceOperation(
        deadline,
        (error) =>
          ['busy', 'sharingViolation', 'lockViolation'].includes(
            operationDetails(error)?.reason ?? ''
          ),
        async () => {
          const data = options.nativeManaged
            ? await parseFileReadResult(
                await requestJson('process.readCaptured', {
                  managedProcessId: nativeManagedProcessId(),
                  stream: label,
                }),
                readBinaryTransfer
              )
            : await readRemoteFile(resolvedPath);
          return data.toString('utf8');
        }
      );
      activeReads.add(reading);
      try {
        return await reading;
      } finally {
        activeReads.delete(reading);
      }
    };

    const releaseNative = async (): Promise<void> => {
      await requestJson('process.releaseManaged', {
        managedProcessId: nativeManagedProcessId(),
      });
    };

    const hasFallbackWindowSelector = (
      query: RemoteWindowQuery | undefined
    ): boolean =>
      query !== undefined &&
      (query.title !== undefined ||
        query.titleRegex !== undefined ||
        query.processId !== undefined ||
        query.processName !== undefined ||
        query.className !== undefined ||
        query.controlId !== undefined);

    const managedWindows = async (
      query: RemoteWindowQuery | undefined
    ): Promise<readonly AppWindow[]> => {
      assertNotReleased();
      const current = await snapshot();
      const topLevelWindows = await listTopLevelWindows();
      const candidates = await collectWindows(
        topLevelWindows,
        query?.includeDescendants ?? false
      );
      const effectiveQuery = query ?? {};
      const trackedProcessIds = new Set([
        current.root.id,
        ...current.processes.map((process) => process.id),
      ]);
      const trackedMatches = candidates.filter(
        (window) =>
          trackedProcessIds.has(window.process.id) &&
          windowMatchesQuery(window, effectiveQuery)
      );
      const matches =
        trackedMatches.length > 0 || !hasFallbackWindowSelector(query)
          ? trackedMatches
          : candidates.filter(
              (window) =>
                !trackedProcessIds.has(window.process.id) &&
                !options.baselineWindowIds.has(window.id) &&
                windowMatchesQuery(window, effectiveQuery)
            );
      if (query?.strict === true && matches.length !== 1) {
        throw createRemoteAgentError(
          'INVALID_ARGUMENT',
          `Strict window query matched ${String(matches.length)} windows: ${queryLabel(
            query
          )}. Candidates: ${candidates.map(formatWindowCandidate).join(', ')}.`
        );
      }
      return matches;
    };

    const waitForWindow = async (
      query: RemoteWindowQuery,
      waitOptions?: RemoteWaitOptions
    ): Promise<AppWindow> =>
      await waitForResult(async () => {
        const matches = await managedWindows(query);
        if (
          matches.length === 1 ||
          (matches.length > 0 && query.strict !== true)
        ) {
          const match = matches[0];
          if (match === undefined) {
            throw new Error('Managed window query matched no usable window.');
          }
          return match;
        }
        throw new Error(
          `No matching managed window for ${queryLabel(
            query
          )}. Candidates: ${matches.map(formatWindowCandidate).join(', ')}.`
        );
      }, waitOptions);

    const waitForNoWindow = async (
      query?: RemoteWindowQuery,
      waitOptions?: RemoteWaitOptions
    ): Promise<void> => {
      await waitForResult(async () => {
        const matches = await managedWindows({
          ...(query ?? {}),
          strict: false,
        });
        if (matches.length === 0) {
          return;
        }
        throw new Error(
          `Managed window query still matched ${String(matches.length)} windows: ${queryLabel(
            query ?? {}
          )}.`
        );
      }, waitOptions);
    };

    const releaseAsync = async (
      releaseOptions?: RemoteCleanupOptions
    ): Promise<void> => {
      if (released) return;
      if (releasing !== undefined) {
        await releasing;
        return;
      }
      const deadline = createResourceDeadline(releaseOptions?.timeoutMs);
      releaseStarted = true;
      const attempt = async (): Promise<void> => {
        if (activeReads.size > 0) {
          let timer: ReturnType<typeof setTimeout> | undefined;
          const timeout = new Promise<never>((_, reject) => {
            timer = setTimeout(
              () =>
                reject(
                  Object.assign(
                    new Error('Capture read is still in progress.'),
                    {
                      code: 'OPERATION_FAILED',
                      details: {
                        operation: 'process.releaseManaged',
                        nativeOperation: '',
                        path: options.tempDirectory ?? '',
                        osCode: null,
                        reason: 'busy',
                        stage: 'captureRead',
                        timedOut: true,
                      },
                    }
                  )
                ),
              Math.max(0, deadline.expiresAt - performance.now())
            );
          });
          try {
            await Promise.race([Promise.allSettled([...activeReads]), timeout]);
          } finally {
            clearTimeout(timer);
          }
        }
        if (options.nativeManaged) {
          if (!nativeReleased) {
            await retryResourceOperation(
              deadline,
              (error) =>
                ['busy', 'sharingViolation', 'lockViolation'].includes(
                  operationDetails(error)?.reason ?? ''
                ),
              releaseNative
            );
            nativeReleased = true;
          }
        } else if (!treeReleased) {
          if (options.killTreeOnRelease) {
            await killTrackedProcessTree();
            await retryResourceOperation(
              deadline,
              (error) => operationDetails(error)?.reason === 'busy',
              async () => {
                if ((await snapshot()).running)
                  throw Object.assign(
                    new Error('Managed process tree has not exited.'),
                    {
                      code: 'OPERATION_FAILED',
                      details: {
                        operation: 'process.releaseManaged',
                        nativeOperation: '',
                        path: options.process.name,
                        osCode: null,
                        reason: 'busy',
                        stage: 'processExit',
                      },
                    }
                  );
              }
            );
          }
          treeReleased = true;
        }
        if (options.tempDirectory !== undefined)
          await removeRemotePath(options.tempDirectory, true);
        released = true;
      };
      releasing = attempt();
      try {
        await releasing;
      } finally {
        releasing = undefined;
      }
    };

    return {
      id: options.process.id,
      kill,
      name: options.process.name,
      processes: async (): Promise<readonly RemoteProcessSnapshot[]> => {
        assertNotReleased();
        return (await snapshot()).processes;
      },
      releaseAsync,
      rootSnapshot: async () => {
        assertNotReleased();
        return await rootSnapshot();
      },
      snapshot: async () => {
        assertNotReleased();
        return await snapshot();
      },
      stderrText: async (readOptions): Promise<string> =>
        await readCapturedText('stderr', options.stderrPath, readOptions),
      stdoutText: async (readOptions): Promise<string> =>
        await readCapturedText('stdout', options.stdoutPath, readOptions),
      waitForNoWindow,
      waitForWindow,
      waitForExit,
      windows: managedWindows,
      [Symbol.asyncDispose]: async () => {
        await releaseAsync();
      },
    };
  };

  const launchManagedProcess = async (
    options: RemoteManagedProcessLaunchOptions
  ): Promise<RemoteManagedProcess> => {
    const capturePaths = await createManagedProcessCapturePaths(options);
    const baselineWindowIds = new Set(
      (await listTopLevelWindows()).map((window) => window.id)
    );
    const killTreeOnRelease = options.killTreeOnRelease ?? true;
    const nativeManaged = connectedFeatures.has('process.launchManaged');
    if (nativeManaged) {
      const launched = parseManagedProcessLaunchResult(
        await requestJson(
          'process.launchManaged',
          managedProcessLaunchOptionsToJson(
            options,
            capturePaths.stdoutPath,
            capturePaths.stderrPath
          )
        )
      );
      return createManagedProcessProxy({
        baselineWindowIds,
        killTreeOnRelease,
        managedProcessId: launched.managedProcessId,
        nativeManaged: true,
        process: launched.process,
        stderrPath: launched.stderrPath ?? capturePaths.stderrPath,
        stdoutPath: launched.stdoutPath ?? capturePaths.stdoutPath,
        tempDirectory: capturePaths.tempDirectory,
      });
    }

    const process = parseApplicationProcess(
      await requestJson(
        'applications.launch',
        applicationLaunchOptionsToJson(
          managedLaunchOptionsToApplicationOptions(
            options,
            capturePaths.stdoutPath,
            capturePaths.stderrPath
          )
        )
      )
    );
    return createManagedProcessProxy({
      baselineWindowIds,
      killTreeOnRelease,
      managedProcessId: undefined,
      nativeManaged: false,
      process,
      stderrPath: capturePaths.stderrPath,
      stdoutPath: capturePaths.stdoutPath,
      tempDirectory: capturePaths.tempDirectory,
    });
  };

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
      downloadDirectory: async (
        options
      ): Promise<RemoteDirectoryDownloadResult> =>
        await downloadDirectory(options),
      exists: async (path): Promise<boolean> => await remotePathExists(path),
      mkdir: async (path, options): Promise<void> => {
        await requestJson('file.mkdir', {
          path,
          recursive: options?.recursive ?? false,
        });
      },
      mkdtemp: async (prefix): Promise<string> =>
        await makeRemoteTempDirectory(prefix),
      readFile: async (path): Promise<Buffer> => await readRemoteFile(path),
      readdir: async (path): Promise<readonly RemoteDirectoryEntry[]> =>
        parseDirectoryEntries(
          await requestJson('file.readdir', {
            path,
          })
        ),
      remove: async (path, options): Promise<void> => {
        await removeRemotePath(path, options?.recursive ?? false);
      },
      rename: async (from, to): Promise<void> => {
        await renameRemotePath(from, to);
      },
      stat: async (path): Promise<RemoteFileStat> =>
        parseFileStat(
          await requestJson('file.stat', {
            path,
          })
        ),
      syncDirectory: async (options): Promise<RemoteDirectorySyncResult> =>
        await syncDirectory(options),
      writeFile: async (path, data): Promise<void> => {
        await writeRemoteFile(path, data);
      },
    },
    interaction: {
      start: async (
        options?: RemoteInteractionSessionOptions
      ): Promise<RemoteInteractionSession> =>
        await createInteractionSession(options),
      with: async <T>(
        operation: (session: RemoteInteractionSession) => Promise<T>,
        options?: RemoteInteractionSessionOptions
      ): Promise<T> => await withInteractionSession(operation, options),
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
      down: async (options): Promise<void> => {
        await performInput({
          button: buttonFromOptions(options),
          kind: 'mouse.down',
          point: options?.point ?? null,
        });
      },
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
      up: async (options): Promise<void> => {
        await performInput({
          button: buttonFromOptions(options),
          kind: 'mouse.up',
          point: options?.point ?? null,
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
      launchManaged: async (options): Promise<RemoteManagedProcess> =>
        await launchManagedProcess(options),
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
    recordVideo: recordAgentVideo,
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
