// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, readdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import type {
  EventLogEntry,
  RemoteAgent,
  RemoteDiagnosticsAttachment,
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
const attachmentsDirectoryName = 'attachments';

interface RemoteDiagnosticsAttachmentError {
  readonly kind: string;
  readonly name: string;
  readonly message: string;
}

const jsonText = (value: unknown): string =>
  `${JSON.stringify(value, null, 2)}\n`;

const formatEventLogEntry = (entry: EventLogEntry): string =>
  [
    `[${entry.timestamp}] ${entry.level} ${entry.provider}#${String(entry.id)}`,
    entry.message,
  ].join('\n');

const eventLogText = (entries: readonly EventLogEntry[]): string =>
  entries.map((entry) => formatEventLogEntry(entry)).join('\n\n');

const safePathSegment = (name: string): string => {
  const normalized = name.trim().replace(/[^A-Za-z0-9._-]+/gu, '-');
  return normalized === '' ? 'attachment' : normalized.slice(0, 80);
};

const remoteBaseName = (path: string): string => {
  const normalized = path.replace(/\\/gu, '/').replace(/\/+$/u, '');
  const separator = normalized.lastIndexOf('/');
  const name = separator === -1 ? normalized : normalized.slice(separator + 1);
  return safePathSegment(name);
};

const listLocalArtifactFiles = async (
  root: string,
  relativeRoot: string
): Promise<readonly string[]> => {
  const output: string[] = [];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const relativePath = `${relativeRoot}/${entry.name}`;
    const absolutePath = join(root, entry.name);
    if (entry.isDirectory()) {
      output.push(
        ...(await listLocalArtifactFiles(absolutePath, relativePath))
      );
    } else if (entry.isFile()) {
      output.push(relativePath);
    }
  }
  return output;
};

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
  artifacts: readonly RemoteDiagnosticsArtifact[],
  attachmentErrors: readonly RemoteDiagnosticsAttachmentError[]
): unknown => ({
  activeWindow: capture.activeWindow,
  attachmentErrors,
  artifacts,
  capturedAt: capture.capturedAt,
  cursor: capture.cursor,
  screen: {
    desktop: capture.desktop,
    desktopAfter: capture.desktopAfter,
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

const saveRemoteFileAttachment = async (
  directory: string,
  agent: RemoteAgent,
  attachment: Extract<RemoteDiagnosticsAttachment, { kind: 'remoteFile' }>
): Promise<readonly RemoteDiagnosticsArtifact[]> => {
  if (!(await agent.files.exists(attachment.path))) {
    throw new Error(`Remote attachment file is missing: ${attachment.path}`);
  }
  const safeName = safePathSegment(attachment.name);
  const fileName = remoteBaseName(attachment.path);
  const relativePath = `${attachmentsDirectoryName}/${safeName}/${fileName}`;
  await mkdir(join(directory, attachmentsDirectoryName, safeName), {
    recursive: true,
  });
  await writeFile(
    join(directory, attachmentsDirectoryName, safeName, fileName),
    await agent.files.readFile(attachment.path)
  );
  return [
    {
      contentType: attachment.contentType ?? 'application/octet-stream',
      kind: 'remoteFile',
      path: relativePath,
    },
  ];
};

const saveRemoteDirectoryAttachment = async (
  directory: string,
  agent: RemoteAgent,
  attachment: Extract<RemoteDiagnosticsAttachment, { kind: 'remoteDirectory' }>
): Promise<readonly RemoteDiagnosticsArtifact[]> => {
  if (!(await agent.files.exists(attachment.path))) {
    throw new Error(
      `Remote attachment directory is missing: ${attachment.path}`
    );
  }
  const safeName = safePathSegment(attachment.name);
  const localPath = join(directory, attachmentsDirectoryName, safeName);
  await agent.files.downloadDirectory({
    localPath,
    remotePath: attachment.path,
  });
  return (
    await listLocalArtifactFiles(
      localPath,
      `${attachmentsDirectoryName}/${safeName}`
    )
  ).map((path) => ({
    contentType: 'application/octet-stream',
    kind: 'remoteDirectory',
    path,
  }));
};

const saveManagedProcessAttachment = async (
  directory: string,
  attachment: Extract<RemoteDiagnosticsAttachment, { kind: 'managedProcess' }>
): Promise<readonly RemoteDiagnosticsArtifact[]> => {
  const safeName = safePathSegment(attachment.name);
  const attachmentDirectory = join(
    directory,
    attachmentsDirectoryName,
    safeName
  );
  await mkdir(attachmentDirectory, {
    recursive: true,
  });
  const stdoutPath = `${attachmentsDirectoryName}/${safeName}/stdout.txt`;
  const stderrPath = `${attachmentsDirectoryName}/${safeName}/stderr.txt`;
  await writeFile(
    join(attachmentDirectory, 'stdout.txt'),
    await attachment.process.stdoutText(),
    'utf8'
  );
  await writeFile(
    join(attachmentDirectory, 'stderr.txt'),
    await attachment.process.stderrText(),
    'utf8'
  );
  return [
    {
      contentType: 'text/plain; charset=utf-8',
      kind: 'managedProcessStdout',
      path: stdoutPath,
    },
    {
      contentType: 'text/plain; charset=utf-8',
      kind: 'managedProcessStderr',
      path: stderrPath,
    },
  ];
};

const saveAttachment = async (
  directory: string,
  options: SaveDiagnosticsOptions,
  attachment: RemoteDiagnosticsAttachment
): Promise<readonly RemoteDiagnosticsArtifact[]> => {
  if (attachment.kind === 'managedProcess') {
    return await saveManagedProcessAttachment(directory, attachment);
  }
  const agent = options.agent;
  if (agent === undefined) {
    throw new Error('Remote diagnostics attachment requires agent.');
  }
  if (attachment.kind === 'remoteFile') {
    return await saveRemoteFileAttachment(directory, agent, attachment);
  }
  return await saveRemoteDirectoryAttachment(directory, agent, attachment);
};

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

  const baseArtifacts: readonly RemoteDiagnosticsArtifact[] = [
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
  const attachmentArtifacts: RemoteDiagnosticsArtifact[] = [];
  const attachmentErrors: RemoteDiagnosticsAttachmentError[] = [];

  for (const attachment of options.attachments ?? []) {
    try {
      attachmentArtifacts.push(
        ...(await saveAttachment(directory, options, attachment))
      );
    } catch (error) {
      attachmentErrors.push({
        kind: attachment.kind,
        message: error instanceof Error ? error.message : 'Attachment failed.',
        name: attachment.name,
      });
    }
  }
  const artifacts = [...baseArtifacts, ...attachmentArtifacts];

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
    jsonText(manifestFromCapture(capture, artifacts, attachmentErrors))
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
