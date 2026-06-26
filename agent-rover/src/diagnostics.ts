// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import type {
  EventLogEntry,
  RemoteAgent,
  RemoteDiagnosticsArtifact,
  RemoteDiagnosticsCapture,
  RemoteDiagnosticsSaveResult,
  SaveDiagnosticsOptions,
  WithDiagnosticsOptions,
} from './index';

const manifestFileName = 'manifest.json';
const screenshotFileName = 'screen.png';
const windowsFileName = 'windows.json';
const eventLogTextFileName = 'event-log.txt';
const protocolTraceFileName = 'protocol-trace.json';
const inputOperationsFileName = 'input-operations.json';

const jsonText = (value: unknown): string =>
  `${JSON.stringify(value, null, 2)}\n`;

const formatEventLogEntry = (entry: EventLogEntry): string =>
  [
    `[${entry.timestamp}] ${entry.level} ${entry.provider}#${String(entry.id)}`,
    entry.message,
  ].join('\n');

const eventLogText = (entries: readonly EventLogEntry[]): string =>
  entries.map((entry) => formatEventLogEntry(entry)).join('\n\n');

const resolveCapture = async (
  options: SaveDiagnosticsOptions
): Promise<RemoteDiagnosticsCapture> => {
  if (options.capture !== undefined) {
    return options.capture;
  }
  if (options.agent !== undefined) {
    return await options.agent.diagnostics.capture(options.captureOptions);
  }
  throw new Error('saveDiagnostics requires capture or agent.');
};

const manifestFromCapture = (
  capture: RemoteDiagnosticsCapture,
  artifacts: readonly RemoteDiagnosticsArtifact[]
): unknown => ({
  activeWindow: capture.activeWindow,
  artifacts,
  capturedAt: capture.capturedAt,
  cursor: capture.cursor,
  screen: {
    bounds: capture.bounds,
    monitors: capture.monitors,
    screenshot: {
      artifact: screenshotFileName,
      bounds: capture.screenshot.bounds,
      clipped: capture.screenshot.clipped,
      contentType: 'image/png',
      size: capture.screenshot.image.byteLength,
      visibleBounds: capture.screenshot.visibleBounds,
    },
  },
  eventLogCount: capture.eventLogs.length,
  inputOperationCount: capture.inputOperations.length,
  protocolOperationCount: capture.protocolOperations.length,
  windowCount: capture.windows.length,
});

/**
 * Saves diagnostics artifacts to a local directory.
 *
 * @param directory Local directory that receives the artifact files.
 * @param options Existing capture or connected agent capture options.
 * @return Saved artifact paths and metadata.
 */
export const saveDiagnostics = async (
  directory: string,
  options: SaveDiagnosticsOptions
): Promise<RemoteDiagnosticsSaveResult> => {
  const capture = await resolveCapture(options);
  await mkdir(directory, {
    recursive: true,
  });

  const artifacts: readonly RemoteDiagnosticsArtifact[] = [
    {
      contentType: 'image/png',
      kind: 'screenScreenshot',
      path: screenshotFileName,
    },
    {
      contentType: 'application/json',
      kind: 'windows',
      path: windowsFileName,
    },
    {
      contentType: 'text/plain; charset=utf-8',
      kind: 'eventLogText',
      path: eventLogTextFileName,
    },
    {
      contentType: 'application/json',
      kind: 'protocolTrace',
      path: protocolTraceFileName,
    },
    {
      contentType: 'application/json',
      kind: 'inputOperations',
      path: inputOperationsFileName,
    },
  ];

  const manifestPath = join(directory, manifestFileName);
  const screenshotPath = join(directory, screenshotFileName);
  await writeFile(screenshotPath, capture.screenshot.image);
  await writeFile(join(directory, windowsFileName), jsonText(capture.windows));
  await writeFile(
    join(directory, eventLogTextFileName),
    `${eventLogText(capture.eventLogs)}\n`
  );
  await writeFile(
    join(directory, protocolTraceFileName),
    jsonText(capture.protocolOperations)
  );
  await writeFile(
    join(directory, inputOperationsFileName),
    jsonText(capture.inputOperations)
  );
  await writeFile(
    manifestPath,
    jsonText(manifestFromCapture(capture, artifacts))
  );

  return {
    artifacts,
    directory,
    manifestPath,
    screenshotPath,
  };
};

/**
 * Runs an operation and saves diagnostics if the operation throws.
 *
 * @param agent Connected agent used to capture diagnostics.
 * @param directory Local directory that receives diagnostics artifacts.
 * @param operation Operation to run.
 * @param options Diagnostics capture options.
 * @return The operation result when it succeeds.
 */
export const withDiagnostics = async <T>(
  agent: RemoteAgent,
  directory: string,
  operation: () => Promise<T>,
  options?: WithDiagnosticsOptions
): Promise<T> => {
  try {
    return await operation();
  } catch (error) {
    try {
      await saveDiagnostics(
        directory,
        options?.captureOptions === undefined
          ? {
              agent,
            }
          : {
              agent,
              captureOptions: options.captureOptions,
            }
      );
    } catch {
      // Preserve the original test failure. Diagnostics failures are secondary.
    }
    throw error;
  }
};
