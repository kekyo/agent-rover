// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash, randomBytes } from 'node:crypto';
import { createServer, type Server, type Socket } from 'node:net';

import {
  authChallengeBytes,
  authResponseBytes,
  createAuthChallengeResponse,
} from '../../src/auth';
import type {
  AppWindowSnapshot,
  ScreenRect,
  EventLogEntry,
  RemoteApplicationLaunchOptions,
  RemoteApplicationProcess,
  RemoteCursor,
  RemoteAgentCapabilities,
  RemoteInputOperation,
  RemoteManagedProcessLaunchOptions,
  RemoteMonitor,
  RemoteProcessSnapshot,
} from '../../src/index';
import {
  createBinaryTransferChunks,
  encodeBinaryTransferChunkPayload,
  parseBinaryTransferChunkPayload,
  parseProtocolMessage,
  type JsonValue,
  type ProtocolErrorPayload,
} from '../../src/protocol';
import {
  protocolVersion,
  tcpFrameCapabilityId,
  tcpFrameVersion,
} from '../../src/protocol_version';
import {
  createTcpFrameDecoder,
  encodeTcpFrame,
  tcpFrameHeaderBytes,
  tcpFrameKindAuthChallenge,
  tcpFrameKindAuthResponse,
  tcpFrameKindBinary,
  tcpFrameKindJson,
  tcpFrameMagic,
} from '../../src/driver/tcp-frame';

export interface FakeTcpAgentOptions {
  readonly beforeRequest?: (
    method: string,
    params: JsonValue | undefined
  ) => ProtocolErrorPayload | undefined;
  readonly dropResponses?: Readonly<Record<string, number>>;
  readonly authToken?: string;
  readonly capabilities?: RemoteAgentCapabilities;
  readonly childrenByWindowId?: Readonly<
    Record<string, readonly AppWindowSnapshot[]>
  >;
  readonly closedWindowIds?: string[];
  readonly eventLogs?: readonly EventLogEntry[];
  readonly launchedStderr?: string;
  readonly launchedStdout?: string;
  readonly inputOperations?: RemoteInputOperation[];
  readonly initialProcesses?: readonly RemoteProcessSnapshot[];
  readonly killedProcessIds?: number[];
  readonly launchResult?: RemoteApplicationProcess;
  readonly launches?: RemoteApplicationLaunchOptions[];
  readonly lockedRenameFailures?: Readonly<Record<string, number>>;
  readonly killedManagedProcessIds?: number[];
  readonly managedLaunches?: FakeManagedProcessLaunchOptions[];
  readonly releasedManagedProcessIds?: number[];
  readonly protocolVersionOverride?: string;
  readonly screenshotImage?: Buffer;
  readonly videoData?: Buffer;
  readonly videoRequests?: Record<string, unknown>[];
  readonly windows?: readonly AppWindowSnapshot[];
}

export interface FakeTcpAgent {
  readonly managedProcessCount: () => number;
  readonly close: () => Promise<void>;
  readonly host: string;
  readonly port: number;
  readonly requestUsedBase64: () => boolean;
  readonly setProcesses: (processes: readonly RemoteProcessSnapshot[]) => void;
  readonly setWindows: (windows: readonly AppWindowSnapshot[]) => void;
}

export const defaultFakeCapabilities: RemoteAgentCapabilities = {
  features: [
    'capabilities',
    'clipboard.clear',
    'clipboard.readText',
    'clipboard.writeText',
    'agent.bounds',
    'agent.cursor',
    'agent.monitors',
    'agent.screenshot',
    'agent.recordVideo',
    'windows',
    'window.children',
    'applications.launch',
    'input.perform',
    'window.activate',
    'window.close',
    'window.focus',
    'window.setBounds',
    'window.show',
    'window.screenshot',
    'window.recordVideo',
    'window.snapshot',
    'file.exists',
    'file.mkdir',
    'file.mkdtemp',
    'process.createCaptureDirectory',
    'file.manifest',
    'file.read',
    'file.readdir',
    'file.remove',
    'file.rename',
    'file.stat',
    'file.write',
    'process.kill',
    'process.killManaged',
    'process.list',
    'process.launchManaged',
    'process.managedSnapshot',
    'process.managedRunning',
    'process.readCaptured',
    'process.releaseManaged',
    'process.snapshot',
    'eventLogs.read',
    tcpFrameCapabilityId,
    'agent.native-windows',
  ],
  platform: 'windows',
  protocolVersion,
};

export const defaultFakeWindow: AppWindowSnapshot = {
  active: false,
  bounds: {
    height: 480,
    width: 640,
    x: 10,
    y: 20,
  },
  className: 'Notepad',
  controlId: 0,
  enabled: true,
  focused: false,
  id: '0x1001',
  maximized: false,
  minimized: false,
  process: {
    id: 1001,
    name: 'notepad.exe',
    path: 'C:/Windows/System32/notepad.exe',
  },
  title: 'Notepad',
  visible: true,
};

export const defaultFakeChildWindow: AppWindowSnapshot = {
  active: false,
  bounds: {
    height: 24,
    width: 120,
    x: 18,
    y: 52,
  },
  className: 'Button',
  controlId: 1,
  enabled: true,
  focused: false,
  id: '0x1001-child-1',
  maximized: false,
  minimized: false,
  process: {
    id: 1001,
    name: 'notepad.exe',
    path: 'C:/Windows/System32/notepad.exe',
  },
  title: 'OK',
  visible: true,
};

const defaultScreenBounds: ScreenRect = {
  height: 768,
  width: 1024,
  x: 0,
  y: 0,
};

const defaultScreenMonitor: RemoteMonitor = {
  bounds: defaultScreenBounds,
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
};

const defaultScreenCursor: RemoteCursor = {
  point: {
    x: 12,
    y: 34,
  },
  visible: true,
};

const defaultEventLog: EventLogEntry = {
  id: 1,
  level: 'Information',
  message: 'Fake agent started.',
  provider: 'agent-rover',
  timestamp: '2026-06-25T00:00:00.000Z',
};

const defaultLaunchResult: RemoteApplicationProcess = {
  id: 4321,
  name: 'fake-launched-app',
};

export type FakeManagedProcessLaunchOptions =
  RemoteManagedProcessLaunchOptions & {
    readonly stderrPath?: string;
    readonly stdoutPath?: string;
  };

const createFakeProcessSnapshot = (
  process: RemoteApplicationProcess,
  path: string
): RemoteProcessSnapshot => ({
  createdAt: fakeTimestamp,
  exitCode: null,
  id: process.id,
  name: process.name,
  parentProcessId: null,
  path,
  running: true,
});

const sha256Hex = (data: Buffer): string =>
  createHash('sha256').update(data).digest('hex');

const sendTcpProtocolMessage = (socket: Socket, message: JsonValue): void => {
  socket.write(
    encodeTcpFrame({
      kind: tcpFrameKindJson,
      payload: Buffer.from(JSON.stringify(message), 'utf8'),
    })
  );
};

const sendTcpBinaryTransfer = (
  socket: Socket,
  options: {
    readonly contentType: string;
    readonly data: Buffer;
    readonly transferId: string;
  }
): void => {
  for (const chunk of createBinaryTransferChunks({
    chunkSize: 4,
    contentType: options.contentType,
    data: options.data,
    transferId: options.transferId,
  })) {
    socket.write(
      encodeTcpFrame({
        kind: tcpFrameKindBinary,
        payload: encodeBinaryTransferChunkPayload(chunk),
      })
    );
  }
};

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const readScreenRect = (value: unknown): ScreenRect | undefined => {
  if (!isRecord(value)) {
    return undefined;
  }
  const rect = {
    height: value.height,
    width: value.width,
    x: value.x,
    y: value.y,
  };
  if (
    typeof rect.height !== 'number' ||
    typeof rect.width !== 'number' ||
    typeof rect.x !== 'number' ||
    typeof rect.y !== 'number'
  ) {
    return undefined;
  }
  return rect;
};

const normalizePath = (path: string): string => {
  const normalized = path.replace(/\\/gu, '/').replace(/\/+$/u, '');
  return /^[A-Za-z]:$/u.test(normalized) ? `${normalized}/` : normalized;
};

const parentPath = (path: string): string => {
  const normalized = normalizePath(path);
  const index = normalized.lastIndexOf('/');
  return index === -1 ? '' : normalizePath(normalized.slice(0, index));
};

const baseName = (path: string): string => {
  const normalized = normalizePath(path);
  const index = normalized.lastIndexOf('/');
  return index === -1 ? normalized : normalized.slice(index + 1);
};

const isDangerousRemovePath = (path: string): boolean =>
  path.trim() === '' || /^[A-Za-z]:\/?$/u.test(normalizePath(path));

const fakeTimestamp = '2026-06-25T00:00:00.000Z';

const findWindow = (
  windows: readonly AppWindowSnapshot[],
  childrenByWindowId: Readonly<Record<string, readonly AppWindowSnapshot[]>>,
  windowId: string
): AppWindowSnapshot | undefined => {
  for (const window of windows) {
    if (window.id === windowId) {
      return window;
    }
  }
  for (const children of Object.values(childrenByWindowId)) {
    const match = children.find((window) => window.id === windowId);
    if (match !== undefined) {
      return match;
    }
  }
  return undefined;
};

const replaceWindow = (
  windows: AppWindowSnapshot[],
  childrenByWindowId: Record<string, AppWindowSnapshot[]>,
  windowId: string,
  update: (window: AppWindowSnapshot) => AppWindowSnapshot
): AppWindowSnapshot | undefined => {
  const topLevelIndex = windows.findIndex((window) => window.id === windowId);
  if (topLevelIndex !== -1) {
    const current = windows[topLevelIndex];
    if (current === undefined) {
      return undefined;
    }
    const updated = update(current);
    windows[topLevelIndex] = updated;
    return updated;
  }

  for (const [parentId, children] of Object.entries(childrenByWindowId)) {
    const childIndex = children.findIndex((window) => window.id === windowId);
    if (childIndex === -1) {
      continue;
    }
    const current = children[childIndex];
    if (current === undefined) {
      return undefined;
    }
    const updated = update(current);
    childrenByWindowId[parentId] = children.map((child, index) =>
      index === childIndex ? updated : child
    );
    return updated;
  }

  return undefined;
};

const toJson = (value: unknown): JsonValue => value as JsonValue;

export const startFakeTcpAgent = async (
  options: FakeTcpAgentOptions
): Promise<FakeTcpAgent> => {
  const capabilities = {
    ...(options.capabilities ?? defaultFakeCapabilities),
    ...(options.protocolVersionOverride === undefined
      ? {}
      : { protocolVersion: options.protocolVersionOverride }),
  };
  const windows = [...(options.windows ?? [defaultFakeWindow])];
  const childrenByWindowId = Object.fromEntries(
    Object.entries(
      options.childrenByWindowId ?? {
        [defaultFakeWindow.id]: [defaultFakeChildWindow],
      }
    ).map(([windowId, children]) => [windowId, [...children]])
  );
  const eventLogs = options.eventLogs ?? [defaultEventLog];
  const directories = new Set<string>(['C:/']);
  const files = new Map<string, Buffer>();
  const processes = new Map<number, RemoteProcessSnapshot>();
  const managedProcesses = new Map<number, number>();
  const responsesToDrop = new Map(Object.entries(options.dropResponses ?? {}));
  const videoRecordings = new Map<
    string,
    {
      readonly bounds: ScreenRect;
      readonly durationMs: number;
      readonly fps: number;
    }
  >();
  const managedProcessOptions = new Map<
    number,
    FakeManagedProcessLaunchOptions
  >();
  const receivedTransferParts = new Map<string, Buffer[]>();
  const receivedTransfers = new Map<string, Buffer>();
  const sockets = new Set<Socket>();
  let clipboardText = '';
  let nextManagedProcessId = 1;
  let nextVideoRecordingId = 1;
  let sawBase64Write = false;

  for (const process of options.initialProcesses ?? []) {
    processes.set(process.id, process);
  }

  const lockedRenameFailures = new Map(
    Object.entries(options.lockedRenameFailures ?? {}).map(([path, count]) => [
      normalizePath(path),
      count,
    ])
  );

  const ensureDirectory = (path: string): void => {
    const normalized = normalizePath(path);
    if (normalized === '') {
      return;
    }
    const parent = parentPath(normalized);
    if (parent !== normalized) {
      ensureDirectory(parent);
    }
    directories.add(normalized);
  };

  const fileStatJson = (path: string): JsonValue | undefined => {
    const normalized = normalizePath(path);
    const file = files.get(normalized);
    if (file !== undefined) {
      return {
        createdAt: fakeTimestamp,
        modifiedAt: fakeTimestamp,
        size: file.byteLength,
        type: 'file',
      };
    }
    if (directories.has(normalized)) {
      return {
        createdAt: fakeTimestamp,
        modifiedAt: fakeTimestamp,
        size: 0,
        type: 'directory',
      };
    }
    return undefined;
  };

  const writeLaunchedOutputFiles = (params: Record<string, unknown>): void => {
    if (typeof params.stdoutPath === 'string') {
      ensureDirectory(parentPath(params.stdoutPath));
      files.set(
        normalizePath(params.stdoutPath),
        Buffer.from(options.launchedStdout ?? 'managed stdout', 'utf8')
      );
    }
    if (typeof params.stderrPath === 'string') {
      ensureDirectory(parentPath(params.stderrPath));
      files.set(
        normalizePath(params.stderrPath),
        Buffer.from(options.launchedStderr ?? 'managed stderr', 'utf8')
      );
    }
  };

  const relativePath = (root: string, path: string): string => {
    const normalizedRoot = normalizePath(root);
    const normalizedPath = normalizePath(path);
    if (normalizedPath === normalizedRoot) {
      return '';
    }
    return normalizedPath.slice(normalizedRoot.length + 1);
  };

  const manifestEntries = (root: string): readonly JsonValue[] => {
    const normalizedRoot = normalizePath(root);
    const entries = [
      ...[...directories]
        .filter((entry) => entry.startsWith(`${normalizedRoot}/`))
        .map((entry) => ({
          modifiedAt: fakeTimestamp,
          path: relativePath(normalizedRoot, entry),
          size: 0,
          type: 'directory',
        })),
      ...[...files.entries()]
        .filter(([entry]) => entry.startsWith(`${normalizedRoot}/`))
        .map(([entry, data]) => ({
          modifiedAt: fakeTimestamp,
          path: relativePath(normalizedRoot, entry),
          sha256: sha256Hex(data),
          size: data.byteLength,
          type: 'file',
        })),
    ];
    return entries
      .filter((entry) => entry.path !== '')
      .sort((left, right) => left.path.localeCompare(right.path))
      .map((entry) => toJson(entry));
  };

  const server: Server = createServer((socket) => {
    sockets.add(socket);
    socket.once('close', () => {
      sockets.delete(socket);
    });

    const decoder = createTcpFrameDecoder({
      maxPayloadBytes: 16 * 1024 * 1024,
    });
    const requiredAuthToken = options.authToken;
    let authenticated = requiredAuthToken === undefined;
    const authChallenge = authenticated
      ? Buffer.alloc(0)
      : randomBytes(authChallengeBytes);
    let bufferedAuth = Buffer.alloc(0);
    let authPayloadLength: number | undefined = undefined;

    const sendAuthChallenge = (): void => {
      socket.write(
        encodeTcpFrame({
          kind: tcpFrameKindAuthChallenge,
          payload: authChallenge,
        })
      );
    };

    const sendReady = (): void => {
      sendTcpProtocolMessage(socket, {
        data: {
          capabilities: toJson(capabilities),
          protocolVersion: capabilities.protocolVersion,
        },
        kind: 'event',
        name: 'agent.ready',
      });
    };

    const sendSuccess = (id: string, result: JsonValue | undefined): void => {
      sendTcpProtocolMessage(socket, {
        ...(result === undefined ? {} : { result }),
        id,
        kind: 'response',
        ok: true,
      });
    };

    const sendFailure = (id: string, message: string): void => {
      sendTcpProtocolMessage(socket, {
        error: {
          code: 'PROTOCOL_ERROR',
          message,
        },
        id,
        kind: 'response',
        ok: false,
      });
    };

    const handleJsonRequest = (
      id: string,
      method: string,
      params: JsonValue | undefined
    ): void => {
      const failure = options.beforeRequest?.(method, params);
      if (failure !== undefined) {
        sendTcpProtocolMessage(socket, {
          id,
          kind: 'response',
          ok: false,
          error: failure,
        });
        return;
      }
      const recordParams = isRecord(params) ? params : {};
      switch (method) {
        case 'agent.capabilities':
          sendSuccess(id, toJson(capabilities));
          return;
        case 'clipboard.readText':
          sendSuccess(id, {
            text: clipboardText,
          });
          return;
        case 'clipboard.writeText': {
          const text = recordParams.text;
          if (typeof text !== 'string') {
            sendFailure(id, 'clipboard.writeText requires text.');
            return;
          }
          clipboardText = text;
          sendSuccess(id, null);
          return;
        }
        case 'clipboard.clear':
          clipboardText = '';
          sendSuccess(id, null);
          return;
        case 'agent.bounds':
          sendSuccess(id, toJson(defaultScreenBounds));
          return;
        case 'agent.monitors':
          sendSuccess(id, toJson([defaultScreenMonitor]));
          return;
        case 'agent.cursor':
          sendSuccess(id, toJson(defaultScreenCursor));
          return;
        case 'agent.screenshot': {
          const rect =
            recordParams.rect === undefined
              ? defaultScreenBounds
              : readScreenRect(recordParams.rect);
          if (rect === undefined) {
            sendFailure(id, 'agent.screenshot rect is invalid.');
            return;
          }
          const screenshotImage = Buffer.from('fake screen png bytes');
          const transferId = `${id}-screen-screenshot`;
          sendTcpBinaryTransfer(socket, {
            contentType: 'image/png',
            data: screenshotImage,
            transferId,
          });
          sendSuccess(id, {
            bounds: toJson(rect),
            clipped: false,
            contentType: 'image/png',
            sha256: sha256Hex(screenshotImage),
            totalBytes: screenshotImage.byteLength,
            transferId,
            visibleBounds: toJson(rect),
          });
          return;
        }
        case 'agent.recordVideo': {
          const durationMs = recordParams.durationMs;
          const fps = recordParams.fps;
          const rect =
            recordParams.rect === undefined
              ? defaultScreenBounds
              : readScreenRect(recordParams.rect);
          if (
            typeof durationMs !== 'number' ||
            typeof fps !== 'number' ||
            rect === undefined
          ) {
            sendFailure(id, 'agent.recordVideo parameters are invalid.');
            return;
          }
          const recordingId = `video-${String(nextVideoRecordingId)}`;
          nextVideoRecordingId += 1;
          videoRecordings.set(recordingId, {
            bounds: rect,
            durationMs,
            fps,
          });
          options.videoRequests?.push({ ...recordParams });
          sendSuccess(id, { recordingId });
          return;
        }
        case 'agent.windows':
          sendSuccess(id, toJson(windows));
          return;
        case 'window.children': {
          const windowId = recordParams.windowId;
          sendSuccess(
            id,
            toJson(
              typeof windowId === 'string'
                ? (childrenByWindowId[windowId] ?? [])
                : []
            )
          );
          return;
        }
        case 'window.snapshot': {
          const windowId = recordParams.windowId;
          const window =
            typeof windowId === 'string'
              ? findWindow(windows, childrenByWindowId, windowId)
              : undefined;
          if (window === undefined) {
            sendFailure(id, 'window.snapshot requires a known windowId.');
            return;
          }
          sendSuccess(id, toJson(window));
          return;
        }
        case 'window.activate':
        case 'window.focus': {
          const windowId = recordParams.windowId;
          if (typeof windowId !== 'string') {
            sendFailure(id, `${method} requires windowId.`);
            return;
          }
          for (const window of windows) {
            replaceWindow(windows, childrenByWindowId, window.id, (entry) => ({
              ...entry,
              active: false,
            }));
          }
          const updated = replaceWindow(
            windows,
            childrenByWindowId,
            windowId,
            (window) => ({
              ...window,
              active: true,
              minimized: false,
            })
          );
          if (updated === undefined) {
            sendFailure(id, `${method} requires a known windowId.`);
            return;
          }
          sendSuccess(id, toJson(updated));
          return;
        }
        case 'window.show': {
          const windowId = recordParams.windowId;
          const state = recordParams.state;
          if (
            typeof windowId !== 'string' ||
            (state !== 'minimized' &&
              state !== 'maximized' &&
              state !== 'restored')
          ) {
            sendFailure(id, 'window.show requires windowId and state.');
            return;
          }
          const updated = replaceWindow(
            windows,
            childrenByWindowId,
            windowId,
            (window) => ({
              ...window,
              maximized: state === 'maximized',
              minimized: state === 'minimized',
            })
          );
          if (updated === undefined) {
            sendFailure(id, 'window.show requires a known windowId.');
            return;
          }
          sendSuccess(id, toJson(updated));
          return;
        }
        case 'window.setBounds': {
          const windowId = recordParams.windowId;
          const bounds = recordParams.bounds;
          if (typeof windowId !== 'string' || !isRecord(bounds)) {
            sendFailure(id, 'window.setBounds requires windowId and bounds.');
            return;
          }
          const nextBounds = {
            height: bounds.height,
            width: bounds.width,
            x: bounds.x,
            y: bounds.y,
          };
          if (
            typeof nextBounds.height !== 'number' ||
            typeof nextBounds.width !== 'number' ||
            typeof nextBounds.x !== 'number' ||
            typeof nextBounds.y !== 'number'
          ) {
            sendFailure(id, 'window.setBounds bounds are invalid.');
            return;
          }
          const updated = replaceWindow(
            windows,
            childrenByWindowId,
            windowId,
            (window) => ({
              ...window,
              bounds: nextBounds,
              maximized: false,
              minimized: false,
            })
          );
          if (updated === undefined) {
            sendFailure(id, 'window.setBounds requires a known windowId.');
            return;
          }
          sendSuccess(id, toJson(updated));
          return;
        }
        case 'input.perform':
          options.inputOperations?.push(params as RemoteInputOperation);
          sendSuccess(id, null);
          return;
        case 'applications.launch':
          options.launches?.push(
            recordParams as unknown as RemoteApplicationLaunchOptions
          );
          {
            const process = options.launchResult ?? defaultLaunchResult;
            const path =
              typeof recordParams.path === 'string' ? recordParams.path : '';
            writeLaunchedOutputFiles(recordParams);
            processes.set(process.id, createFakeProcessSnapshot(process, path));
            sendSuccess(id, toJson(process));
          }
          return;
        case 'process.launchManaged':
          options.managedLaunches?.push(
            recordParams as unknown as FakeManagedProcessLaunchOptions
          );
          {
            const process = options.launchResult ?? defaultLaunchResult;
            const managedProcessId = nextManagedProcessId;
            nextManagedProcessId += 1;
            const path =
              typeof recordParams.path === 'string' ? recordParams.path : '';
            const launchOptions =
              recordParams as unknown as FakeManagedProcessLaunchOptions;
            writeLaunchedOutputFiles(recordParams);
            processes.set(process.id, createFakeProcessSnapshot(process, path));
            managedProcesses.set(managedProcessId, process.id);
            managedProcessOptions.set(managedProcessId, launchOptions);
            sendSuccess(id, {
              id: process.id,
              managedProcessId,
              name: process.name,
              stderrPath:
                typeof recordParams.stderrPath === 'string'
                  ? recordParams.stderrPath
                  : null,
              stdoutPath:
                typeof recordParams.stdoutPath === 'string'
                  ? recordParams.stdoutPath
                  : null,
            });
          }
          return;
        case 'process.managedRunning': {
          const processId = managedProcesses.get(
            Number(recordParams.managedProcessId)
          );
          sendSuccess(
            id,
            processId !== undefined &&
              processes.get(processId)?.running === true
          );
          return;
        }
        case 'process.managedSnapshot': {
          const managedProcessId = recordParams.managedProcessId;
          if (typeof managedProcessId !== 'number') {
            sendFailure(
              id,
              'process.managedSnapshot requires managedProcessId.'
            );
            return;
          }
          const processId = managedProcesses.get(managedProcessId);
          if (processId === undefined) {
            sendFailure(
              id,
              'process.managedSnapshot requires a known process.'
            );
            return;
          }
          sendSuccess(
            id,
            toJson(
              processes.get(processId) ?? {
                createdAt: null,
                exitCode: null,
                id: processId,
                name: '',
                parentProcessId: null,
                path: '',
                running: false,
              }
            )
          );
          return;
        }
        case 'process.killManaged': {
          const managedProcessId = recordParams.managedProcessId;
          if (typeof managedProcessId !== 'number') {
            sendFailure(id, 'process.killManaged requires managedProcessId.');
            return;
          }
          const processId = managedProcesses.get(managedProcessId);
          if (processId === undefined) {
            sendFailure(id, 'process.killManaged requires a known process.');
            return;
          }
          options.killedManagedProcessIds?.push(managedProcessId);
          options.killedProcessIds?.push(processId);
          const current = processes.get(processId);
          processes.set(processId, {
            createdAt: current?.createdAt ?? null,
            exitCode: 1,
            id: processId,
            name: current?.name ?? '',
            parentProcessId: current?.parentProcessId ?? null,
            path: current?.path ?? '',
            running: false,
          });
          sendSuccess(id, null);
          return;
        }
        case 'process.releaseManaged': {
          const managedProcessId = recordParams.managedProcessId;
          if (typeof managedProcessId !== 'number') {
            sendFailure(
              id,
              'process.releaseManaged requires managedProcessId.'
            );
            return;
          }
          const processId = managedProcesses.get(managedProcessId);
          if (processId === undefined) {
            sendSuccess(id, null);
            return;
          }
          options.releasedManagedProcessIds?.push(managedProcessId);
          const launchOptions = managedProcessOptions.get(managedProcessId);
          const current = processes.get(processId);
          if (launchOptions?.killTreeOnRelease === true) {
            processes.set(processId, {
              createdAt: current?.createdAt ?? null,
              exitCode: 1,
              id: processId,
              name: current?.name ?? '',
              parentProcessId: current?.parentProcessId ?? null,
              path: current?.path ?? '',
              running: false,
            });
          }
          managedProcesses.delete(managedProcessId);
          managedProcessOptions.delete(managedProcessId);
          const dropped = responsesToDrop.get(method) ?? 0;
          if (dropped > 0) {
            responsesToDrop.set(method, dropped - 1);
            return;
          }
          sendSuccess(id, null);
          return;
        }
        case 'process.snapshot': {
          const processId = recordParams.processId;
          if (typeof processId !== 'number') {
            sendFailure(id, 'process.snapshot requires processId.');
            return;
          }
          sendSuccess(
            id,
            toJson(
              processes.get(processId) ?? {
                createdAt: null,
                exitCode: null,
                id: processId,
                name: '',
                parentProcessId: null,
                path: '',
                running: false,
              }
            )
          );
          return;
        }
        case 'process.list': {
          const name = recordParams.name;
          const entries = [...processes.values()].filter(
            (process) =>
              process.running &&
              (typeof name !== 'string' || process.name === name)
          );
          sendSuccess(id, toJson(entries));
          return;
        }
        case 'process.kill': {
          const processId = recordParams.processId;
          if (typeof processId !== 'number') {
            sendFailure(id, 'process.kill requires processId.');
            return;
          }
          const current = processes.get(processId);
          options.killedProcessIds?.push(processId);
          processes.set(processId, {
            createdAt: current?.createdAt ?? null,
            exitCode: 1,
            id: processId,
            name: current?.name ?? '',
            parentProcessId: current?.parentProcessId ?? null,
            path: current?.path ?? '',
            running: false,
          });
          sendSuccess(id, null);
          return;
        }
        case 'window.close': {
          const windowId = recordParams.windowId;
          if (typeof windowId === 'string') {
            options.closedWindowIds?.push(windowId);
          }
          sendSuccess(id, null);
          return;
        }
        case 'window.screenshot': {
          const windowId = recordParams.windowId;
          const window =
            typeof windowId === 'string'
              ? findWindow(windows, childrenByWindowId, windowId)
              : undefined;
          const bounds = window?.bounds ?? defaultFakeWindow.bounds;
          const screenshotImage =
            options.screenshotImage ?? Buffer.from('fake png bytes');
          const transferId = `${id}-screenshot`;
          sendTcpBinaryTransfer(socket, {
            contentType: 'image/png',
            data: screenshotImage,
            transferId,
          });
          sendSuccess(id, {
            bounds: toJson(bounds),
            clipped: false,
            contentType: 'image/png',
            sha256: sha256Hex(screenshotImage),
            totalBytes: screenshotImage.byteLength,
            transferId,
            visibleBounds: toJson(bounds),
          });
          return;
        }
        case 'window.recordVideo': {
          const durationMs = recordParams.durationMs;
          const fps = recordParams.fps;
          const windowId = recordParams.windowId;
          const window =
            typeof windowId === 'string'
              ? findWindow(windows, childrenByWindowId, windowId)
              : undefined;
          if (
            typeof durationMs !== 'number' ||
            typeof fps !== 'number' ||
            window === undefined
          ) {
            sendFailure(id, 'window.recordVideo parameters are invalid.');
            return;
          }
          const recordingId = `video-${String(nextVideoRecordingId)}`;
          nextVideoRecordingId += 1;
          videoRecordings.set(recordingId, {
            bounds: window.bounds,
            durationMs,
            fps,
          });
          options.videoRequests?.push({ ...recordParams });
          sendSuccess(id, { recordingId });
          return;
        }
        case 'video.result': {
          const recordingId = recordParams.recordingId;
          const recording =
            typeof recordingId === 'string'
              ? videoRecordings.get(recordingId)
              : undefined;
          if (recording === undefined || typeof recordingId !== 'string') {
            sendFailure(id, 'Video recording was not found.');
            return;
          }
          videoRecordings.delete(recordingId);
          const videoData =
            options.videoData ?? Buffer.from('fake h264 mp4 bytes');
          const transferId = `${id}-video`;
          sendSuccess(id, {
            clipped: false,
            codec: 'h264',
            contentType: 'video/mp4',
            droppedFrames: 0,
            durationMs: recording.durationMs,
            finalBounds: toJson(recording.bounds),
            fps: recording.fps,
            frameCount: Math.ceil(
              (recording.durationMs * recording.fps) / 1000
            ),
            initialBounds: toJson(recording.bounds),
            sha256: sha256Hex(videoData),
            totalBytes: videoData.byteLength,
            transferId,
          });
          sendTcpBinaryTransfer(socket, {
            contentType: 'video/mp4',
            data: videoData,
            transferId,
          });
          return;
        }
        case 'file.write': {
          sawBase64Write = sawBase64Write || 'dataBase64' in recordParams;
          const path = recordParams.path;
          const transferId = recordParams.transferId;
          const dataBase64 = recordParams.dataBase64;
          if (typeof path === 'string' && typeof transferId === 'string') {
            const upload = receivedTransfers.get(transferId);
            if (upload !== undefined) {
              ensureDirectory(parentPath(path));
              files.set(normalizePath(path), upload);
            }
          } else if (
            typeof path === 'string' &&
            typeof dataBase64 === 'string'
          ) {
            ensureDirectory(parentPath(path));
            files.set(normalizePath(path), Buffer.from(dataBase64, 'base64'));
          }
          sendSuccess(id, null);
          return;
        }
        case 'file.exists': {
          const path = recordParams.path;
          const exists =
            typeof path === 'string' &&
            (files.has(normalizePath(path)) ||
              directories.has(normalizePath(path)));
          sendSuccess(id, {
            exists,
          });
          return;
        }
        case 'file.stat': {
          const path = recordParams.path;
          const stat =
            typeof path === 'string' ? fileStatJson(path) : undefined;
          if (stat === undefined) {
            sendFailure(id, 'file.stat requires an existing path.');
            return;
          }
          sendSuccess(id, stat);
          return;
        }
        case 'file.mkdir': {
          const path = recordParams.path;
          if (typeof path !== 'string') {
            sendFailure(id, 'file.mkdir requires path.');
            return;
          }
          ensureDirectory(path);
          sendSuccess(id, null);
          return;
        }
        case 'file.readdir': {
          const path = recordParams.path;
          if (
            typeof path !== 'string' ||
            !directories.has(normalizePath(path))
          ) {
            sendFailure(id, 'file.readdir requires directory path.');
            return;
          }
          const directory = normalizePath(path);
          const entries = [
            ...[...directories]
              .filter((entry) => parentPath(entry) === directory)
              .map((entry) => ({
                ...(fileStatJson(entry) as Record<string, unknown>),
                name: baseName(entry),
              })),
            ...[...files.keys()]
              .filter((entry) => parentPath(entry) === directory)
              .map((entry) => ({
                ...(fileStatJson(entry) as Record<string, unknown>),
                name: baseName(entry),
              })),
          ];
          sendSuccess(id, toJson(entries));
          return;
        }
        case 'file.manifest': {
          const path = recordParams.path;
          if (
            typeof path !== 'string' ||
            !directories.has(normalizePath(path))
          ) {
            sendFailure(id, 'file.manifest requires directory path.');
            return;
          }
          sendSuccess(id, {
            entries: toJson(manifestEntries(path)),
          });
          return;
        }
        case 'file.rename': {
          const from = recordParams.from;
          const to = recordParams.to;
          if (typeof from !== 'string' || typeof to !== 'string') {
            sendFailure(id, 'file.rename requires from and to.');
            return;
          }
          const normalizedFrom = normalizePath(from);
          const normalizedTo = normalizePath(to);
          const remainingLockedFailures =
            lockedRenameFailures.get(normalizedTo) ?? 0;
          if (remainingLockedFailures > 0) {
            lockedRenameFailures.set(normalizedTo, remainingLockedFailures - 1);
            sendTcpProtocolMessage(socket, {
              id,
              kind: 'response',
              ok: false,
              error: {
                code: 'OPERATION_FAILED',
                message: 'The target is locked.',
                details: {
                  operation: method,
                  nativeOperation: 'MoveFileExW',
                  path: normalizedTo,
                  osCode: 32,
                  reason: 'sharingViolation',
                },
              },
            });
            return;
          }
          const file = files.get(normalizedFrom);
          if (file !== undefined) {
            ensureDirectory(parentPath(normalizedTo));
            files.delete(normalizedFrom);
            files.set(normalizedTo, file);
          } else if (directories.has(normalizedFrom)) {
            directories.delete(normalizedFrom);
            directories.add(normalizedTo);
          } else {
            sendFailure(id, 'file.rename requires an existing source.');
            return;
          }
          sendSuccess(id, null);
          return;
        }
        case 'process.createCaptureDirectory':
        case 'file.mkdtemp': {
          const prefix =
            method === 'process.createCaptureDirectory'
              ? 'C:/agent-rover-managed-process-'
              : recordParams.prefix;
          if (typeof prefix !== 'string') {
            sendFailure(id, 'file.mkdtemp requires prefix.');
            return;
          }
          const path = `${prefix}fake`;
          ensureDirectory(path);
          sendSuccess(id, {
            path,
          });
          return;
        }
        case 'file.remove': {
          const path = recordParams.path;
          const recursive = recordParams.recursive === true;
          if (typeof path !== 'string' || isDangerousRemovePath(path)) {
            sendFailure(id, 'file.remove rejects dangerous path.');
            return;
          }
          const normalized = normalizePath(path);
          if (
            !files.has(normalized) &&
            !directories.has(normalized) &&
            recordParams.ignoreMissing !== true
          ) {
            sendTcpProtocolMessage(socket, {
              id,
              kind: 'response',
              ok: false,
              error: {
                code: 'OPERATION_FAILED',
                message: 'The target is absent.',
                details: {
                  operation: method,
                  nativeOperation: 'GetFileAttributesW',
                  path,
                  osCode: 2,
                  reason: 'notFound',
                },
              },
            });
            return;
          }
          if (files.delete(normalized)) {
            sendSuccess(id, null);
            return;
          }
          if (directories.has(normalized)) {
            const hasChildren =
              [...files.keys()].some((entry) =>
                entry.startsWith(`${normalized}/`)
              ) ||
              [...directories].some(
                (entry) =>
                  entry !== normalized && entry.startsWith(`${normalized}/`)
              );
            if (hasChildren && !recursive) {
              sendFailure(id, 'file.remove requires recursive.');
              return;
            }
            for (const entry of [...files.keys()]) {
              if (entry.startsWith(`${normalized}/`)) {
                files.delete(entry);
              }
            }
            for (const entry of [...directories]) {
              if (entry === normalized || entry.startsWith(`${normalized}/`)) {
                directories.delete(entry);
              }
            }
          }
          sendSuccess(id, null);
          return;
        }
        case 'process.readCaptured':
        case 'file.read': {
          const launch = managedProcessOptions.get(
            Number(recordParams.managedProcessId)
          );
          const path =
            method === 'file.read'
              ? recordParams.path
              : recordParams.stream === 'stderr'
                ? launch?.stderrPath
                : launch?.stdoutPath;
          const data =
            typeof path === 'string'
              ? (files.get(normalizePath(path)) ?? Buffer.alloc(0))
              : Buffer.alloc(0);
          const transferId = `${id}-file`;
          sendTcpBinaryTransfer(socket, {
            contentType: 'application/octet-stream',
            data,
            transferId,
          });
          sendSuccess(id, {
            contentType: 'application/octet-stream',
            sha256: sha256Hex(data),
            totalBytes: data.byteLength,
            transferId,
          });
          return;
        }
        case 'eventLogs.read': {
          const source = recordParams.source;
          const maxEntries = recordParams.maxEntries;
          const filtered =
            typeof source === 'string'
              ? eventLogs.filter((entry) => entry.provider === source)
              : [...eventLogs];
          sendSuccess(
            id,
            toJson(
              typeof maxEntries === 'number'
                ? filtered.slice(0, maxEntries)
                : filtered
            )
          );
          return;
        }
        default:
          sendFailure(id, `Unsupported method: ${method}.`);
      }
    };

    const acceptNormalData = (data: Buffer): void => {
      for (const frame of decoder.accept(data)) {
        if (frame.kind === tcpFrameKindBinary) {
          const chunk = parseBinaryTransferChunkPayload(frame.payload);
          const parts = receivedTransferParts.get(chunk.transferId) ?? [];
          parts.push(chunk.data);
          receivedTransferParts.set(chunk.transferId, parts);
          if (chunk.final) {
            receivedTransfers.set(chunk.transferId, Buffer.concat(parts));
            receivedTransferParts.delete(chunk.transferId);
          }
          continue;
        }
        if (frame.kind !== tcpFrameKindJson) {
          continue;
        }
        const message = parseProtocolMessage(
          JSON.parse(frame.payload.toString('utf8'))
        );
        if (message.kind === 'request') {
          handleJsonRequest(message.id, message.method, message.params);
        }
      }
    };

    const acceptAuthData = (data: Buffer): void => {
      if (requiredAuthToken === undefined) {
        socket.destroy();
        return;
      }
      bufferedAuth = Buffer.concat([bufferedAuth, data]);
      if (authPayloadLength === undefined) {
        if (bufferedAuth.byteLength < tcpFrameHeaderBytes) {
          return;
        }
        const header = bufferedAuth.subarray(0, tcpFrameHeaderBytes);
        if (
          header.subarray(0, 4).toString('ascii') !== tcpFrameMagic ||
          header.readUInt16LE(4) !== tcpFrameVersion ||
          header.readUInt16LE(6) !== tcpFrameKindAuthResponse ||
          header.readUInt32LE(8) !== 0 ||
          header.readUInt32LE(16) !== 0
        ) {
          socket.destroy();
          return;
        }
        authPayloadLength = header.readUInt32LE(12);
        if (authPayloadLength !== authResponseBytes) {
          socket.destroy();
          return;
        }
      }

      const authFrameLength = tcpFrameHeaderBytes + authPayloadLength;
      if (bufferedAuth.byteLength < authFrameLength) {
        return;
      }

      const actualResponse = bufferedAuth.subarray(
        tcpFrameHeaderBytes,
        authFrameLength
      );
      const expectedResponse = createAuthChallengeResponse(
        requiredAuthToken,
        authChallenge
      );
      if (!actualResponse.equals(expectedResponse)) {
        socket.destroy();
        return;
      }

      authenticated = true;
      const remaining = bufferedAuth.subarray(authFrameLength);
      bufferedAuth = Buffer.alloc(0);
      sendReady();
      if (remaining.byteLength > 0) {
        acceptNormalData(remaining);
      }
    };

    if (authenticated) {
      sendReady();
    } else {
      sendAuthChallenge();
    }

    socket.on('data', (data) => {
      if (authenticated) {
        acceptNormalData(Buffer.from(data));
      } else {
        acceptAuthData(Buffer.from(data));
      }
    });
  });

  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      resolve();
    });
  });

  const address = server.address();
  if (address === null || typeof address === 'string') {
    throw new Error('Expected a TCP address.');
  }

  return {
    managedProcessCount: () => managedProcesses.size,
    close: async (): Promise<void> => {
      for (const socket of sockets) {
        socket.destroy();
      }
      await new Promise<void>((resolve, reject) => {
        server.close((error) => {
          if (error !== undefined) {
            reject(error);
          } else {
            resolve();
          }
        });
      });
    },
    host: '127.0.0.1',
    port: address.port,
    requestUsedBase64: (): boolean => sawBase64Write,
    setProcesses: (nextProcesses): void => {
      processes.clear();
      for (const process of nextProcesses) {
        processes.set(process.id, process);
      }
    },
    setWindows: (nextWindows): void => {
      windows.splice(0, windows.length, ...nextWindows);
    },
  };
};
