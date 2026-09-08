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

/** Object that owns an external resource and can be released asynchronously. */
export interface AsyncReleaseable extends AsyncDisposable {
  /**
   * Asynchronously releases the owned resource.
   *
   * @remarks Implementations must be idempotent and expose the same operation
   * through `Symbol.asyncDispose`.
   */
  readonly releaseAsync: () => Promise<void>;
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

/** Mouse button state options. */
export interface RemoteMouseButtonOptions {
  /** Mouse button to press or release. */
  readonly button?: MouseButton;
  /** Optional point to move to before changing the button state. */
  readonly point?: ScreenPoint;
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
  /** Presses a mouse button without releasing it. */
  readonly down: (options?: RemoteMouseButtonOptions) => Promise<void>;
  /** Releases a mouse button. */
  readonly up: (options?: RemoteMouseButtonOptions) => Promise<void>;
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

/** Options for starting a remote interaction session. */
export interface RemoteInteractionSessionOptions {
  /** Whether to restore the cursor position captured at session start. */
  readonly restoreCursor?: boolean;
}

/** Remote keyboard controller scoped to one interaction session. */
export interface RemoteInteractionKeyboard {
  /** Presses one key without releasing it. */
  readonly down: (key: string) => Promise<void>;
  /** Presses and releases one key. */
  readonly press: (
    key: string,
    options?: RemoteKeyboardPressOptions
  ) => Promise<void>;
  /** Releases one key pressed by this session. */
  readonly up: (key: string) => Promise<void>;
}

/** Remote mouse controller scoped to one interaction session. */
export interface RemoteInteractionMouse {
  /** Moves the mouse cursor. */
  readonly move: (point: ScreenPoint) => Promise<void>;
  /** Presses a mouse button without releasing it. */
  readonly down: (options?: RemoteMouseButtonOptions) => Promise<void>;
  /** Releases a mouse button pressed by this session. */
  readonly up: (options?: RemoteMouseButtonOptions) => Promise<void>;
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

/** Releaseable remote keyboard and mouse interaction session. */
export interface RemoteInteractionSession extends AsyncReleaseable {
  /** Keyboard operations owned by this session. */
  readonly keyboard: RemoteInteractionKeyboard;
  /** Mouse operations owned by this session. */
  readonly mouse: RemoteInteractionMouse;
  /** Waits without changing the current interaction state. */
  readonly pause: (durationMs: number) => Promise<void>;
}

/** Remote interaction session factory. */
export interface RemoteInteractions {
  /** Starts a releaseable keyboard and mouse interaction session. */
  readonly start: (
    options?: RemoteInteractionSessionOptions
  ) => Promise<RemoteInteractionSession>;
  /** Runs an operation in a releaseable keyboard and mouse session. */
  readonly with: <T>(
    operation: (session: RemoteInteractionSession) => Promise<T>,
    options?: RemoteInteractionSessionOptions
  ) => Promise<T>;
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
      readonly kind: 'mouse.down';
      /** Button to press. */
      readonly button: MouseButton;
      /** Point to move to before pressing, or null to keep current position. */
      readonly point: ScreenPoint | null;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.move';
      /** Target point. */
      readonly point: ScreenPoint;
    }
  | {
      /** Operation discriminator. */
      readonly kind: 'mouse.up';
      /** Button to release. */
      readonly button: MouseButton;
      /** Point to move to before releasing, or null to keep current position. */
      readonly point: ScreenPoint | null;
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
  /** Full executable path when the platform can resolve it. */
  readonly path: string;
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

/** Options used to launch a managed remote process. */
export interface RemoteManagedProcessLaunchOptions {
  /** Executable, script, or document path resolved on the agent machine. */
  readonly path: string;
  /** Command-line arguments passed to the process. */
  readonly arguments?: readonly string[];
  /** Working directory used for the launched process. */
  readonly workingDirectory?: string;
  /** Environment variables added or overridden for the launched process. */
  readonly environment?: Readonly<Record<string, string>>;
  /** Whether standard output should be captured for `stdoutText()`. */
  readonly captureStdout?: boolean;
  /** Whether standard error should be captured for `stderrText()`. */
  readonly captureStderr?: boolean;
  /** Whether the process should be created without a console window. */
  readonly createNoWindow?: boolean;
  /**
   * Whether asynchronous release should terminate the tracked process tree.
   *
   * @remarks Default is true. `kill()` always terminates the tracked process tree.
   */
  readonly killTreeOnRelease?: boolean;
}

/** Logical managed application snapshot. */
export interface RemoteManagedProcessSnapshot {
  /** Current root process state. */
  readonly root: RemoteProcessSnapshot;
  /** Running descendant processes tracked for this managed process. */
  readonly processes: readonly RemoteProcessSnapshot[];
  /** Whether the root or any tracked descendant process is still running. */
  readonly running: boolean;
}

/** Managed remote process handle. */
export interface RemoteManagedProcess
  extends RemoteApplicationProcess, AsyncReleaseable {
  /**
   * Completes owned cleanup, preserving unfinished stages after failure.
   * @param options Overall cleanup deadline, defaulting to 10000 milliseconds.
   * @returns Completion of process termination, owned handle release and temporary cleanup.
   * @remarks Concurrent calls join the current release. A failed release can be retried.
   */
  readonly releaseAsync: (options?: RemoteCleanupOptions) => Promise<void>;
  /** Reads the current root process state. */
  readonly rootSnapshot: () => Promise<RemoteProcessSnapshot>;
  /** Reads the current logical application state. */
  readonly snapshot: () => Promise<RemoteManagedProcessSnapshot>;
  /** Lists running descendant processes tracked for this managed process. */
  readonly processes: () => Promise<readonly RemoteProcessSnapshot[]>;
  /** Finds top-level windows related to this managed process. */
  readonly windows: (
    query?: RemoteWindowQuery
  ) => Promise<readonly AppWindow[]>;
  /** Waits until a related window matching the query is available. */
  readonly waitForWindow: (
    query: RemoteWindowQuery,
    options?: RemoteWaitOptions
  ) => Promise<AppWindow>;
  /** Waits until no related windows match the query. */
  readonly waitForNoWindow: (
    query?: RemoteWindowQuery,
    options?: RemoteWaitOptions
  ) => Promise<void>;
  /** Terminates the tracked process tree. */
  readonly kill: () => Promise<void>;
  /** Waits until the root and tracked descendants exit. */
  readonly waitForExit: (
    options?: RemoteWaitOptions
  ) => Promise<RemoteManagedProcessSnapshot>;
  /**
   * Reads a live snapshot, or complete UTF-8 stdout after the root exits.
   * @param options Deadline for descendant writers and capture availability.
   * @returns Captured output, including the final bytes after completion.
   */
  readonly stdoutText: (options?: RemoteCleanupOptions) => Promise<string>;
  /**
   * Reads a live snapshot, or complete UTF-8 stderr after the root exits.
   * @param options Deadline for descendant writers and capture availability.
   * @returns Captured error output, including the final bytes after completion.
   */
  readonly stderrText: (options?: RemoteCleanupOptions) => Promise<string>;
}

/** Remote process snapshot. */
export interface RemoteProcessSnapshot {
  /** Operating system process id. */
  readonly id: number;
  /** Process executable name when available. */
  readonly name: string;
  /** Full executable path when available. */
  readonly path: string;
  /** Parent operating system process id when available. */
  readonly parentProcessId: number | null;
  /** Process creation timestamp as an ISO string when available. */
  readonly createdAt: string | null;
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
  /** Launches a process and returns a managed lifecycle handle. */
  readonly launchManaged: (
    options: RemoteManagedProcessLaunchOptions
  ) => Promise<RemoteManagedProcess>;
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
  /** Captures this window as an H.264 MP4 video. */
  readonly recordVideo: {
    /**
     * Captures video and persists it to a host-side path.
     *
     * @param durationMs Capture duration in milliseconds.
     * @param outputPath Destination path on the host running this driver.
     * @param options Video capture options.
     * @return Metadata for the persisted MP4 file.
     */
    (
      durationMs: number,
      outputPath: string,
      options?: AppWindowVideoCaptureOptions
    ): Promise<CapturedVideoResult>;
    /**
     * Captures video into a host-side temporary file.
     *
     * @param durationMs Capture duration in milliseconds.
     * @param options Video capture options.
     * @return A readable stream that deletes its temporary file when released.
     */
    (
      durationMs: number,
      options?: AppWindowVideoCaptureOptions
    ): Promise<CapturedVideoStream>;
  };
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

/** Window position behavior used while recording video. */
export type WindowVideoTracking = 'followWindow' | 'initialBounds';

/** Common options for recording an H.264 MP4 video. */
export interface VideoCaptureOptions {
  /**
   * Encoded frames per second.
   *
   * @remarks Default is 60.
   */
  readonly fps?: number;
  /**
   * Encoder quality from 1 through 100.
   *
   * @remarks Default is 90. A value of 100 does not make H.264 lossless.
   */
  readonly quality?: number;
}

/** Options for recording an application window. */
export interface AppWindowVideoCaptureOptions extends VideoCaptureOptions {
  /**
   * Whether the capture rectangle follows the window while it moves.
   *
   * @remarks Default is `followWindow`.
   */
  readonly tracking?: WindowVideoTracking;
}

/** Options for recording the remote screen. */
export interface RemoteAgentVideoCaptureOptions extends VideoCaptureOptions {
  /** Optional fixed screen rectangle to record. */
  readonly rect?: ScreenRect;
}

/** Metadata shared by temporary and persisted video capture results. */
export interface CapturedVideoMetadata {
  /** MIME type of the captured video. */
  readonly contentType: 'video/mp4';
  /** Video codec stored in the MP4 container. */
  readonly codec: 'h264';
  /** Actual capture duration in milliseconds. */
  readonly durationMs: number;
  /** Nominal encoded frame rate. */
  readonly fps: number;
  /** Number of encoded frames. */
  readonly frameCount: number;
  /** Number of capture deadlines missed while recording. */
  readonly droppedFrames: number;
  /** Capture bounds at the beginning of recording. */
  readonly initialBounds: ScreenRect;
  /** Capture bounds at the end of recording. */
  readonly finalBounds: ScreenRect;
  /** Whether any frame was clipped by the screen or fixed encoded bounds. */
  readonly clipped: boolean;
}

/** Result for a video persisted at a caller-supplied host path. */
export interface CapturedVideoResult extends CapturedVideoMetadata {
  /** Absolute path of the persisted host-side MP4 file. */
  readonly path: string;
}

/** Temporary-file-backed video stream owned by the caller. */
export interface CapturedVideoStream
  extends CapturedVideoMetadata, NodeJS.ReadableStream, AsyncReleaseable {}

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
  /** Synchronizes a local directory to the agent machine. */
  readonly syncDirectory: (
    options: RemoteDirectorySyncOptions
  ) => Promise<RemoteDirectorySyncResult>;
  /** Downloads a remote directory from the agent machine. */
  readonly downloadDirectory: (
    options: RemoteDirectoryDownloadOptions
  ) => Promise<RemoteDirectoryDownloadResult>;
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

/** Directory synchronization mode. */
export type RemoteDirectorySyncMode = 'mirror' | 'update';

/** Directory synchronization checksum algorithm. */
export type RemoteDirectoryChecksum = 'sha256';

/** Policy used when a remote file operation fails because a file is locked. */
export type RemoteLockedFilePolicy =
  'fail' | 'retry' | 'killRelatedProcessesAndRetry';

/** Recursive directory manifest entry. */
export interface RemoteDirectoryManifestEntry {
  /** Relative path using forward slashes. */
  readonly path: string;
  /** Entry type. */
  readonly type: RemoteFileType;
  /** Size in bytes. */
  readonly size: number;
  /** Last modification timestamp as an ISO string. */
  readonly modifiedAt: string;
  /** SHA-256 digest for file entries when requested and available. */
  readonly sha256?: string;
}

/** Options for synchronizing a local directory to the agent machine. */
export interface RemoteDirectorySyncOptions {
  /** Local source directory. */
  readonly localPath: string;
  /** Remote destination directory. */
  readonly remotePath: string;
  /** Synchronization mode. */
  readonly mode?: RemoteDirectorySyncMode;
  /** Checksum algorithm used for file comparison. */
  readonly checksum?: RemoteDirectoryChecksum;
  /** Whether remote entries missing from the local source should be deleted. */
  readonly deleteExtraneous?: boolean;
  /** Include glob patterns matched against forward-slash relative paths. */
  readonly include?: readonly string[];
  /** Exclude glob patterns matched against forward-slash relative paths. */
  readonly exclude?: readonly string[];
  /** Policy used when remote files are locked. */
  readonly onLockedFile?: RemoteLockedFilePolicy;
  /** Remote process executable path prefixes related to locked files. */
  readonly relatedProcessPaths?: readonly string[];
}

/** Result returned after synchronizing a directory to the agent machine. */
export interface RemoteDirectorySyncResult {
  /** Number of files uploaded or replaced. */
  readonly uploadedFiles: number;
  /** Number of unchanged files skipped. */
  readonly skippedFiles: number;
  /** Number of directories created on the agent machine. */
  readonly createdDirectories: number;
  /** Number of extraneous remote files deleted. */
  readonly deletedFiles: number;
  /** Number of extraneous remote directories deleted. */
  readonly deletedDirectories: number;
  /** Number of uploaded bytes. */
  readonly bytesUploaded: number;
}

/** Options for downloading a remote directory from the agent machine. */
export interface RemoteDirectoryDownloadOptions {
  /** Remote source directory. */
  readonly remotePath: string;
  /** Local destination directory. */
  readonly localPath: string;
  /** Whether a missing remote source should be ignored. */
  readonly ignoreMissing?: boolean;
  /** Include glob patterns matched against forward-slash relative paths. */
  readonly include?: readonly string[];
  /** Exclude glob patterns matched against forward-slash relative paths. */
  readonly exclude?: readonly string[];
}

/** Result returned after downloading a remote directory. */
export interface RemoteDirectoryDownloadResult {
  /** Number of files downloaded. */
  readonly downloadedFiles: number;
  /** Number of directories created locally. */
  readonly createdDirectories: number;
  /** Number of downloaded bytes. */
  readonly bytesDownloaded: number;
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
  'Critical' | 'Error' | 'Information' | 'Verbose' | 'Warning';

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

/** Remote or process artifact to save with diagnostics. */
export type RemoteDiagnosticsAttachment =
  | {
      /** Attachment kind discriminator. */
      readonly kind: 'remoteFile';
      /** Stable attachment name used under the attachments directory. */
      readonly name: string;
      /** Remote file path. */
      readonly path: string;
      /** Whether attachment failures should be recorded instead of thrown. */
      readonly optional?: boolean;
      /** Content type recorded in the diagnostics manifest. */
      readonly contentType?: string;
    }
  | {
      /** Attachment kind discriminator. */
      readonly kind: 'remoteDirectory';
      /** Stable attachment name used under the attachments directory. */
      readonly name: string;
      /** Remote directory path. */
      readonly path: string;
      /** Whether attachment failures should be recorded instead of thrown. */
      readonly optional?: boolean;
    }
  | {
      /** Attachment kind discriminator. */
      readonly kind: 'managedProcess';
      /** Stable attachment name used under the attachments directory. */
      readonly name: string;
      /** Managed process whose captured stdout and stderr should be saved. */
      readonly process: RemoteManagedProcess;
      /** Whether attachment failures should be recorded instead of thrown. */
      readonly optional?: boolean;
    };

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
  /** Additional remote or process artifacts saved with the diagnostics bundle. */
  readonly attachments?: readonly RemoteDiagnosticsAttachment[];
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
  /** Releaseable keyboard and mouse interaction session API. */
  readonly interaction: RemoteInteractions;
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
  /** Captures the whole screen or a screen rectangle as an H.264 MP4 video. */
  readonly recordVideo: {
    /**
     * Captures video and persists it to a host-side path.
     *
     * @param durationMs Capture duration in milliseconds.
     * @param outputPath Destination path on the host running this driver.
     * @param options Video capture options.
     * @return Metadata for the persisted MP4 file.
     */
    (
      durationMs: number,
      outputPath: string,
      options?: RemoteAgentVideoCaptureOptions
    ): Promise<CapturedVideoResult>;
    /**
     * Captures video into a host-side temporary file.
     *
     * @param durationMs Capture duration in milliseconds.
     * @param options Video capture options.
     * @return A readable stream that deletes its temporary file when released.
     */
    (
      durationMs: number,
      options?: RemoteAgentVideoCaptureOptions
    ): Promise<CapturedVideoStream>;
  };
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
  | 'OPERATION_FAILED'
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
  /** Structured process or file failure, when reported by the operation. */
  readonly details?: RemoteOperationErrorDetails;
}

/** Native process or file failure information, independent of the message language. */
export interface RemoteOperationErrorDetails {
  /** Requested protocol operation. */
  readonly operation: string;
  /** Failing operating-system API. */
  readonly nativeOperation: string;
  /** Actual failing file or directory, or empty for a non-file operation. */
  readonly path: string;
  /** Native error code, or null when the failure has no OS code. */
  readonly osCode: number | null;
  /** Observed cause; accessDenied does not by itself identify a lock or a permanent denial. */
  readonly reason:
    | 'unknown'
    | 'sharingViolation'
    | 'lockViolation'
    | 'accessDenied'
    | 'readOnly'
    | 'notFound'
    | 'directoryNotEmpty'
    | 'busy'
    | 'unsupported'
    | 'invalidArgument';
  /** Incomplete cleanup stage, when applicable. */
  readonly stage?: string;
  /** Attempts made by the current retry operation. */
  readonly attempts?: number;
  /** Milliseconds elapsed since the outermost operation started. */
  readonly elapsedMs?: number;
  /** Whether the operation's deadline was reached. */
  readonly timedOut?: boolean;
}

/** Deadline options for process cleanup and completed-output reads. */
export interface RemoteCleanupOptions {
  /** Overall deadline in milliseconds; zero permits one immediate attempt. Default is 10000. */
  readonly timeoutMs?: number;
}

export { connectRemoteAgent } from './driver/connection';
export { saveDiagnostics, withDiagnostics } from './diagnostics';
export { compareImages } from './image';
