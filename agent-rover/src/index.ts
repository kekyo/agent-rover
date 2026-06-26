// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

/** Package name used by generated metadata and smoke tests. */
export const packageName = 'agent-rover';

/** Object that owns an external resource and can be released explicitly. */
export interface Releaseable extends Disposable {
  /**
   * Releases the owned resource.
   *
   * @remarks Implementations must be idempotent and expose the same operation
   * through `Symbol.dispose`.
   */
  readonly release: () => void;
}

/**
 * Screen-relative rectangle in physical screen pixels.
 * @remarks The origin is the virtual screen origin, so multi-monitor setups may
 * report negative x or y coordinates.
 */
export interface ScreenRect {
  /** Left coordinate in physical pixels. */
  readonly x: number;
  /** Top coordinate in physical pixels. */
  readonly y: number;
  /** Rectangle width in physical pixels. */
  readonly width: number;
  /** Rectangle height in physical pixels. */
  readonly height: number;
}

/** Screen-relative point in physical screen pixels. */
export interface ScreenPoint {
  /** X coordinate in physical pixels. */
  readonly x: number;
  /** Y coordinate in physical pixels. */
  readonly y: number;
}

/** Mouse button names accepted by remote input operations. */
export type MouseButton = 'left' | 'middle' | 'right';

/** Keyboard modifier names accepted by remote input operations. */
export type KeyboardModifier = 'Alt' | 'Control' | 'Meta' | 'Shift';

/** Mouse click options. */
export interface RemoteMouseClickOptions {
  /** Mouse button to click. */
  readonly button?: MouseButton;
  /** Modifiers held while the click is performed. */
  readonly modifiers?: readonly KeyboardModifier[];
}

/** Mouse drag options. */
export interface RemoteMouseDragOptions extends RemoteMouseClickOptions {}

/** Mouse wheel options. */
export interface RemoteMouseWheelOptions {
  /** Horizontal wheel delta. */
  readonly deltaX?: number;
  /** Vertical wheel delta. */
  readonly deltaY?: number;
  /** Optional point to move to before wheeling. */
  readonly point?: ScreenPoint;
}

/** Keyboard press options. */
export interface RemoteKeyboardPressOptions {
  /** Modifiers held while the key is pressed. */
  readonly modifiers?: readonly KeyboardModifier[];
}

/** Keyboard pasteText helper options. */
export interface RemoteKeyboardPasteTextOptions {
  /** Whether to restore the previous clipboard text after paste. */
  readonly restoreClipboard?: boolean;
  /** Delay before restoring clipboard text after issuing paste input. */
  readonly restoreDelayMs?: number;
}

/** Remote mouse controller. */
export interface RemoteMouse {
  /** Moves the mouse cursor. */
  readonly move: (point: ScreenPoint) => Promise<void>;
  /** Clicks a mouse button. */
  readonly click: (
    point: ScreenPoint,
    options?: RemoteMouseClickOptions
  ) => Promise<void>;
  /** Drags from one point to another. */
  readonly drag: (
    from: ScreenPoint,
    to: ScreenPoint,
    options?: RemoteMouseDragOptions
  ) => Promise<void>;
  /** Sends a mouse wheel operation. */
  readonly wheel: (options: RemoteMouseWheelOptions) => Promise<void>;
}

/** Remote keyboard controller. */
export interface RemoteKeyboard {
  /** Presses and releases one key. */
  readonly press: (
    key: string,
    options?: RemoteKeyboardPressOptions
  ) => Promise<void>;
  /** Presses one key without releasing it. */
  readonly down: (key: string) => Promise<void>;
  /** Releases one key. */
  readonly up: (key: string) => Promise<void>;
  /** Types text through the active keyboard focus. */
  readonly type: (text: string) => Promise<void>;
  /** Pastes text through the active keyboard focus using the clipboard. */
  readonly pasteText: (
    text: string,
    options?: RemoteKeyboardPasteTextOptions
  ) => Promise<void>;
}

/** Remote clipboard text API. */
export interface RemoteClipboard {
  /** Reads CF_UNICODETEXT clipboard text, or an empty string when unavailable. */
  readonly readText: () => Promise<string>;
  /** Replaces the clipboard with CF_UNICODETEXT text. */
  readonly writeText: (text: string) => Promise<void>;
  /** Clears the clipboard. */
  readonly clear: () => Promise<void>;
  /**
   * Temporarily replaces clipboard text while an operation runs.
   *
   * @remarks The previous text is restored after the operation settles.
   */
  readonly withText: <T>(
    text: string,
    operation: () => Promise<T>
  ) => Promise<T>;
}

/** Input operation transported to an agent platform backend. */
export type RemoteInputOperation =
  | {
      /** Operation discriminator. */
      readonly kind: 'keyboard.down';
      /** Key name. */
      readonly key: string;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'keyboard.press';
      /** Key name. */
      readonly key: string;
      /** Modifiers held while pressing the key. */
      readonly modifiers: readonly KeyboardModifier[];
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'keyboard.type';
      /** Text to type. */
      readonly text: string;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'keyboard.up';
      /** Key name. */
      readonly key: string;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.click';
      /** Click point. */
      readonly point: ScreenPoint;
      /** Button to click. */
      readonly button: MouseButton;
      /** Modifiers held while clicking. */
      readonly modifiers: readonly KeyboardModifier[];
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.drag';
      /** Drag start point. */
      readonly from: ScreenPoint;
      /** Drag end point. */
      readonly to: ScreenPoint;
      /** Button held while dragging. */
      readonly button: MouseButton;
      /** Modifiers held while dragging. */
      readonly modifiers: readonly KeyboardModifier[];
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.move';
      /** Target point. */
      readonly point: ScreenPoint;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.wheel';
      /** Horizontal wheel delta. */
      readonly deltaX: number;
      /** Vertical wheel delta. */
      readonly deltaY: number;
      /** Point to move to before wheeling, or null to keep current position. */
      readonly point: ScreenPoint | null;
    };

/** Capability values reported by a remote agent. */
export interface RemoteAgentCapabilities {
  /** Protocol version spoken by the connected agent. */
  readonly protocolVersion: string;
  /** Platform backend reported by the connected agent. */
  readonly platform: 'windows';
  /** Feature names supported by the connected agent. */
  readonly features: readonly string[];
}

/** Remote top-level or descendant application window. */
export interface AppWindowSnapshot {
  /** Stable agent-side identifier for this window. */
  readonly id: string;
  /** Current window title. */
  readonly title: string;
  /** Platform window class name. */
  readonly className: string;
  /** Current window bounds. */
  readonly bounds: ScreenRect;
  /** Whether the platform reports the window as visible. */
  readonly visible: boolean;
  /** Whether this window or its root owner is currently active. */
  readonly active: boolean;
  /** Whether this window currently has keyboard focus. */
  readonly focused: boolean;
  /** Whether the platform reports the window as enabled for input. */
  readonly enabled: boolean;
  /** Whether this window is currently minimized. */
  readonly minimized: boolean;
  /** Whether this window is currently maximized. */
  readonly maximized: boolean;
  /** Platform control id, or 0 when unavailable. */
  readonly controlId: number;
  /** Process metadata for the owning process. */
  readonly process: AppWindowProcess;
}

/** Process metadata associated with an application window. */
export interface AppWindowProcess {
  /** Operating system process id. */
  readonly id: number;
  /** Process executable name when the platform can resolve it. */
  readonly name: string;
}

/** Options used to launch an application on the remote agent. */
export interface RemoteApplicationLaunchOptions {
  /** Executable, script, or document path resolved on the agent machine. */
  readonly path: string;
  /** Command-line arguments passed to the application. */
  readonly arguments?: readonly string[];
  /** Working directory used for the launched process. */
  readonly workingDirectory?: string;
  /** Environment variables added or overridden for the launched process. */
  readonly environment?: Readonly<Record<string, string>>;
  /** Optional path receiving standard output. */
  readonly stdoutPath?: string;
  /** Optional path receiving standard error. */
  readonly stderrPath?: string;
  /** Whether the process should be created without a console window. */
  readonly createNoWindow?: boolean;
}

/** Process metadata returned after a remote application is launched. */
export interface RemoteApplicationProcess {
  /** Operating system process id. */
  readonly id: number;
  /** Process executable name when the platform can resolve it. */
  readonly name: string;
}

/** Remote process snapshot. */
export interface RemoteProcessSnapshot {
  /** Operating system process id. */
  readonly id: number;
  /** Process executable name when available. */
  readonly name: string;
  /** Full executable path when available. */
  readonly path: string;
  /** Whether the process is still running. */
  readonly running: boolean;
  /** Exit code when known and the process has exited. */
  readonly exitCode: number | null;
}

/** Options for listing processes. */
export interface RemoteProcessListOptions {
  /** Optional executable name filter. */
  readonly name?: string;
}

/** Options used by remote agent wait helpers. */
export interface RemoteWaitOptions {
  /**
   * Maximum time to retry in milliseconds.
   *
   * @remarks Default is 10000.
   */
  readonly timeoutMs?: number;
  /**
   * Delay between retry attempts in milliseconds.
   *
   * @remarks Default is 50.
   */
  readonly intervalMs?: number;
  /** Message included in the timeout error. */
  readonly message?: string;
}

/** Query used to find windows from the current window snapshot. */
export interface RemoteWindowQuery {
  /** Exact window title to match. */
  readonly title?: string;
  /** Regular expression tested against the window title. */
  readonly titleRegex?: RegExp;
  /** Owning process id to match. */
  readonly processId?: number;
  /** Owning process executable name to match case-insensitively. */
  readonly processName?: string;
  /** Required visibility state. */
  readonly visible?: boolean;
  /** Required active state. */
  readonly active?: boolean;
  /** Platform control class name to match when available. */
  readonly className?: string;
  /** Platform control id to match. */
  readonly controlId?: number;
  /** Required keyboard focus state. */
  readonly focused?: boolean;
  /** Whether descendant windows should be searched in addition to top-level windows. */
  readonly includeDescendants?: boolean;
  /** Whether the query must match exactly one window. */
  readonly strict?: boolean;
}

/** Options used while waiting for a window bounds to become stable. */
export interface RemoteStableBoundsWaitOptions extends RemoteWaitOptions {
  /**
   * Number of consecutive matching snapshots required before success.
   *
   * @remarks Default is 2.
   */
  readonly stableIterations?: number;
}

/** Options for recursively listing window descendants. */
export interface AppWindowDescendantsOptions {
  /** Maximum descendant depth to include. */
  readonly maxDepth?: number;
}

/** Remote application launcher API. */
export interface RemoteApplications {
  /** Launches an application inside the remote user session. */
  readonly launch: (
    options: RemoteApplicationLaunchOptions
  ) => Promise<RemoteApplicationProcess>;
}

/** Remote process control API. */
export interface RemoteProcesses {
  /** Reads a process snapshot. */
  readonly snapshot: (processId: number) => Promise<RemoteProcessSnapshot>;
  /** Tests whether a process is running. */
  readonly exists: (processId: number) => Promise<boolean>;
  /** Lists running processes. */
  readonly list: (
    options?: RemoteProcessListOptions
  ) => Promise<readonly RemoteProcessSnapshot[]>;
  /** Terminates a process. */
  readonly kill: (processId: number) => Promise<void>;
  /** Waits until a process exits. */
  readonly waitForExit: (
    processId: number,
    options?: RemoteWaitOptions
  ) => Promise<RemoteProcessSnapshot>;
}

/** Remote top-level or descendant application window. */
export interface AppWindow extends AppWindowSnapshot {
  /** Brings this window to the foreground and activates it. */
  readonly activate: () => Promise<AppWindow>;
  /** Lists direct child windows for this window. */
  readonly children: () => Promise<readonly AppWindow[]>;
  /** Lists descendant windows recursively. */
  readonly descendants: (
    options?: AppWindowDescendantsOptions
  ) => Promise<readonly AppWindow[]>;
  /** Finds descendant windows matching a query. */
  readonly findDescendants: (
    query: RemoteWindowQuery
  ) => Promise<readonly AppWindow[]>;
  /** Captures this window bounds as a PNG screenshot. */
  readonly screenshot: () => Promise<AppWindowScreenshot>;
  /** Sets keyboard focus to this window when the platform allows it. */
  readonly focus: () => Promise<AppWindow>;
  /** Refreshes this window snapshot. */
  readonly refresh: () => Promise<AppWindow>;
  /** Minimizes this window. */
  readonly minimize: () => Promise<AppWindow>;
  /** Maximizes this window. */
  readonly maximize: () => Promise<AppWindow>;
  /** Restores this window from minimized or maximized state. */
  readonly restore: () => Promise<AppWindow>;
  /** Moves and resizes this window. */
  readonly setBounds: (bounds: ScreenRect) => Promise<AppWindow>;
  /** Waits until this window is visible. */
  readonly waitForVisible: (options?: RemoteWaitOptions) => Promise<AppWindow>;
  /** Waits until this window is hidden or closed. */
  readonly waitForHidden: (options?: RemoteWaitOptions) => Promise<void>;
  /** Waits until this window is closed. */
  readonly waitForClosed: (options?: RemoteWaitOptions) => Promise<void>;
  /** Waits until this window reports stable bounds. */
  readonly waitForStableBounds: (
    options?: RemoteStableBoundsWaitOptions
  ) => Promise<AppWindow>;
  /** Requests that this window closes. */
  readonly close: () => Promise<void>;
}

/** PNG screenshot captured for an application window. */
export interface AppWindowScreenshot {
  /** PNG image buffer. */
  readonly image: Buffer;
  /** Window bounds used for capture. */
  readonly bounds: ScreenRect;
  /** Visible bounds included in the screenshot. */
  readonly visibleBounds: ScreenRect;
  /** Whether capture bounds were clipped by the platform. */
  readonly clipped: boolean;
}

/** Options for capturing a remote agent screenshot. */
export interface RemoteAgentScreenshotOptions {
  /** Optional screen rectangle to capture. */
  readonly rect?: ScreenRect;
}

/** PNG screenshot captured from the remote agent. */
export interface RemoteScreenshot {
  /** PNG image buffer. */
  readonly image: Buffer;
  /** Requested capture bounds. */
  readonly bounds: ScreenRect;
  /** Visible bounds included in the screenshot. */
  readonly visibleBounds: ScreenRect;
  /** Whether capture bounds were clipped by the platform. */
  readonly clipped: boolean;
}

/** Remote monitor geometry. */
export interface RemoteMonitor {
  /** Stable monitor identifier within the current screen session. */
  readonly id: string;
  /** Platform monitor name when available. */
  readonly name: string;
  /** Monitor bounds in physical screen pixels. */
  readonly bounds: ScreenRect;
  /** Work area excluding taskbars and system toolbars. */
  readonly workArea: ScreenRect;
  /** Whether this monitor is the primary monitor. */
  readonly primary: boolean;
  /** Monitor scale factor relative to 96 DPI. */
  readonly scaleFactor: number;
}

/** Remote cursor state. */
export interface RemoteCursor {
  /** Cursor position in physical screen pixels. */
  readonly point: ScreenPoint;
  /** Whether the cursor is currently visible. */
  readonly visible: boolean;
}

/** Options for PNG image comparison. */
export interface ImageComparisonOptions {
  /** Pixelmatch threshold from 0 to 1. */
  readonly threshold?: number;
  /** Maximum allowed mismatched pixels. */
  readonly maxDiffPixels?: number;
  /** Maximum allowed mismatched pixel ratio. */
  readonly maxDiffRatio?: number;
}

/** PNG image comparison result. */
export interface ImageComparisonResult {
  /** Whether the image comparison passed the supplied tolerances. */
  readonly pass: boolean;
  /** Number of mismatched pixels. */
  readonly diffPixels: number;
  /** Mismatched pixel ratio from 0 to 1. */
  readonly diffRatio: number;
  /** Number of compared pixels. */
  readonly totalPixels: number;
}

/** Remote file transfer API. */
export interface RemoteFileSystem {
  /** Writes a complete file to the agent machine. */
  readonly writeFile: (path: string, data: Buffer) => Promise<void>;
  /** Reads a complete file from the agent machine. */
  readonly readFile: (path: string) => Promise<Buffer>;
  /** Tests whether a path exists. */
  readonly exists: (path: string) => Promise<boolean>;
  /** Reads file or directory metadata. */
  readonly stat: (path: string) => Promise<RemoteFileStat>;
  /** Creates a directory. */
  readonly mkdir: (path: string, options?: RemoteMkdirOptions) => Promise<void>;
  /** Lists direct directory entries. */
  readonly readdir: (path: string) => Promise<readonly RemoteDirectoryEntry[]>;
  /** Removes a file or directory. */
  readonly remove: (
    path: string,
    options?: RemoteRemoveOptions
  ) => Promise<void>;
  /** Renames or moves a file or directory. */
  readonly rename: (from: string, to: string) => Promise<void>;
  /** Creates a temporary directory from a prefix and returns its path. */
  readonly mkdtemp: (prefix: string) => Promise<string>;
}

/** Remote file type. */
export type RemoteFileType = 'directory' | 'file' | 'other';

/** Remote file or directory metadata. */
export interface RemoteFileStat {
  /** Entry type. */
  readonly type: RemoteFileType;
  /** Size in bytes. */
  readonly size: number;
  /** Creation timestamp as an ISO string. */
  readonly createdAt: string;
  /** Last modification timestamp as an ISO string. */
  readonly modifiedAt: string;
}

/** Remote directory entry. */
export interface RemoteDirectoryEntry extends RemoteFileStat {
  /** Entry name. */
  readonly name: string;
}

/** Directory creation options. */
export interface RemoteMkdirOptions {
  /** Whether parent directories should be created. */
  readonly recursive?: boolean;
}

/** Remove options. */
export interface RemoteRemoveOptions {
  /** Whether directory contents should be removed recursively. */
  readonly recursive?: boolean;
}

/** Event log severity label. */
export type EventLogLevel =
  | 'Critical'
  | 'Error'
  | 'Information'
  | 'Verbose'
  | 'Warning';

/** Event log entry returned by an agent. */
export interface EventLogEntry {
  /** Event id. */
  readonly id: number;
  /** Provider or source name. */
  readonly provider: string;
  /** Event severity label. */
  readonly level: EventLogLevel;
  /** Event timestamp as an ISO string. */
  readonly timestamp: string;
  /** Rendered event message. */
  readonly message: string;
}

/** Event log query options. */
export interface EventLogQuery {
  /** Provider/source filter. */
  readonly source?: string;
  /** Maximum entries to return. */
  readonly maxEntries?: number;
  /** Lower timestamp bound as an ISO string. */
  readonly since?: string;
}

/** Remote event log API. */
export interface RemoteEventLogs {
  /** Reads event logs from the agent machine. */
  readonly read: (query?: EventLogQuery) => Promise<readonly EventLogEntry[]>;
}

/** Direction of a protocol trace entry captured for diagnostics. */
export type RemoteProtocolTraceDirection = 'binaryUpload' | 'request';

/** Sanitized protocol operation metadata captured for diagnostics. */
export interface RemoteProtocolTraceEntry {
  /** Monotonic sequence number within the current connection. */
  readonly id: number;
  /** Trace timestamp as an ISO string. */
  readonly timestamp: string;
  /** Protocol operation direction. */
  readonly direction: RemoteProtocolTraceDirection;
  /** Protocol method or binary transfer marker. */
  readonly method: string;
  /** Sanitized operation parameters or metadata. */
  readonly params?: unknown;
}

/** Window snapshot tree captured for diagnostics. */
export interface RemoteDiagnosticsWindow extends AppWindowSnapshot {
  /** Direct child windows when descendant capture is enabled. */
  readonly children?: readonly RemoteDiagnosticsWindow[];
}

/** Options for collecting diagnostics from a connected agent. */
export interface RemoteDiagnosticsCaptureOptions {
  /** Screenshot capture options. */
  readonly screenshot?: RemoteAgentScreenshotOptions;
  /** Event log query used during diagnostics capture. */
  readonly eventLogs?: EventLogQuery;
  /** Whether child window trees should be included. */
  readonly includeDescendants?: boolean;
  /** Maximum descendant depth when child window trees are included. */
  readonly maxDescendantDepth?: number;
}

/** Complete in-memory diagnostics capture. */
export interface RemoteDiagnosticsCapture {
  /** Capture timestamp as an ISO string. */
  readonly capturedAt: string;
  /** Remote agent bounds. */
  readonly bounds: ScreenRect;
  /** Screen screenshot. */
  readonly screenshot: RemoteScreenshot;
  /** Top-level window snapshots, optionally with child trees. */
  readonly windows: readonly RemoteDiagnosticsWindow[];
  /** Active window snapshot when one is known. */
  readonly activeWindow: AppWindowSnapshot | null;
  /** Cursor state at capture time. */
  readonly cursor: RemoteCursor;
  /** Monitor list at capture time. */
  readonly monitors: readonly RemoteMonitor[];
  /** Event log entries collected during capture. */
  readonly eventLogs: readonly EventLogEntry[];
  /** Sanitized recent protocol operation metadata. */
  readonly protocolOperations: readonly RemoteProtocolTraceEntry[];
  /** Recent input operations issued through this connection. */
  readonly inputOperations: readonly RemoteInputOperation[];
}

/** Remote diagnostics API. */
export interface RemoteDiagnostics {
  /** Captures the current agent diagnostics bundle in memory. */
  readonly capture: (
    options?: RemoteDiagnosticsCaptureOptions
  ) => Promise<RemoteDiagnosticsCapture>;
}

/** Saved diagnostics artifact metadata. */
export interface RemoteDiagnosticsArtifact {
  /** Artifact kind. */
  readonly kind: string;
  /** Artifact path relative to the diagnostics directory. */
  readonly path: string;
  /** Artifact content type. */
  readonly contentType: string;
}

/** Result returned after diagnostics artifacts are saved. */
export interface RemoteDiagnosticsSaveResult {
  /** Directory containing all diagnostics artifacts. */
  readonly directory: string;
  /** Absolute path to the manifest JSON file. */
  readonly manifestPath: string;
  /** Absolute path to the screen screenshot file. */
  readonly screenshotPath: string;
  /** Saved artifact metadata. */
  readonly artifacts: readonly RemoteDiagnosticsArtifact[];
}

/** Options for saving diagnostics artifacts. */
export interface SaveDiagnosticsOptions {
  /** Existing capture to save. */
  readonly capture?: RemoteDiagnosticsCapture;
  /** Agent used to capture diagnostics when capture is omitted. */
  readonly agent?: RemoteAgent;
  /** Capture options used when agent is supplied. */
  readonly captureOptions?: RemoteDiagnosticsCaptureOptions;
}

/** Options for wrapping an operation with failure diagnostics. */
export interface WithDiagnosticsOptions {
  /** Capture options used when the wrapped operation fails. */
  readonly captureOptions?: RemoteDiagnosticsCaptureOptions;
}

/** Connected remote agent session. */
export interface RemoteAgent extends Releaseable {
  /** Reads capabilities reported by the connected agent. */
  readonly capabilities: () => Promise<RemoteAgentCapabilities>;
  /** Remote diagnostics API. */
  readonly diagnostics: RemoteDiagnostics;
  /** Remote clipboard text API. */
  readonly clipboard: RemoteClipboard;
  /** Remote application launcher API. */
  readonly applications: RemoteApplications;
  /** Remote process control API. */
  readonly processes: RemoteProcesses;
  /** Remote file transfer API. */
  readonly files: RemoteFileSystem;
  /** Remote event log API. */
  readonly eventLogs: RemoteEventLogs;
  /** Remote keyboard input controller. */
  readonly keyboard: RemoteKeyboard;
  /** Remote mouse input controller. */
  readonly mouse: RemoteMouse;
  /** Lists top-level application windows. */
  readonly windows: () => Promise<readonly AppWindow[]>;
  /** Captures the whole screen or a screen rectangle as PNG. */
  readonly screenshot: (
    options?: RemoteAgentScreenshotOptions
  ) => Promise<RemoteScreenshot>;
  /** Reads the virtual screen bounds. */
  readonly bounds: () => Promise<ScreenRect>;
  /** Lists monitors in the current screen session. */
  readonly monitors: () => Promise<readonly RemoteMonitor[]>;
  /** Reads the current cursor state. */
  readonly cursor: () => Promise<RemoteCursor>;
  /** Finds windows matching a query. */
  readonly findWindows: (
    query: RemoteWindowQuery
  ) => Promise<readonly AppWindow[]>;
  /** Waits until exactly one matching window is available unless strict is false. */
  readonly waitForWindow: (
    query: RemoteWindowQuery,
    options?: RemoteWaitOptions
  ) => Promise<AppWindow>;
  /** Waits until no windows match a query. */
  readonly waitForNoWindow: (
    query: RemoteWindowQuery,
    options?: RemoteWaitOptions
  ) => Promise<void>;
}

/** Options used to connect to a remote agent. */
export interface ConnectRemoteAgentOptions {
  /** Agent host name, FQDN, or IP address. */
  readonly host: string;
  /** Agent TCP port. */
  readonly port: number;
  /** Token printed by the agent process. Overrides AGENT_ROVER_AUTH_TOKEN. */
  readonly authToken?: string;
  /** Connection and request timeout in milliseconds. */
  readonly timeoutMs?: number;
}

/** Stable error codes raised by the remote agent driver. */
export type RemoteAgentErrorCode =
  | 'AUTHENTICATION_FAILED'
  | 'CONNECTION_FAILED'
  | 'DISCONNECTED'
  | 'HANDSHAKE_FAILED'
  | 'INVALID_ARGUMENT'
  | 'PROTOCOL_ERROR';

/** Error raised by the remote agent driver. */
export interface RemoteAgentError extends Error {
  /** Stable machine-readable error code. */
  readonly code: RemoteAgentErrorCode;
}

export { connectRemoteAgent } from './driver/connection';
export { saveDiagnostics, withDiagnostics } from './diagnostics';
export { compareImages } from './image';
