// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { lstat, mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';
import type { RemoteAgent } from '../index';

/** Completed file transfer totals. */
export interface ArctlTransferResult {
  /** Source path as supplied by the caller. */ readonly source: string;
  /** Exact destination path supplied by the caller. */ readonly destination: string;
  /** Number of completed files. */ readonly files: number;
  /** Number of copied directories, including a recursive root. */ readonly directories: number;
  /** Number of file content bytes transferred. */ readonly bytes: number;
}

const localType = async (
  path: string
): Promise<'file' | 'directory' | 'other' | undefined> => {
  try {
    const entry = await lstat(path);
    return entry.isFile()
      ? 'file'
      : entry.isDirectory()
        ? 'directory'
        : 'other';
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') return undefined;
    throw error;
  }
};

/**
 * Copies a file through the existing SDK, leaving completed writes in place.
 * @param agent Connected agent.
 * @param direction Upload or download.
 * @param source Exact source path.
 * @param destination Exact destination path.
 * @param signal Stops subsequent operations after CLI interruption.
 * @returns Completed transfer totals.
 */
export const transferArctlFiles = async (
  agent: RemoteAgent,
  direction: 'put' | 'get',
  source: string,
  destination: string,
  signal: AbortSignal
): Promise<ArctlTransferResult> => {
  let data: Buffer;
  if (direction === 'put') {
    if ((await localType(source)) !== 'file')
      throw new Error(`Source must be a regular file: ${source}`);
    if (
      (await agent.files.exists(destination)) &&
      (await agent.files.stat(destination)).type !== 'file'
    )
      throw new Error(`Destination is not a file: ${destination}`);
    data = await readFile(source, { signal });
    signal.throwIfAborted();
    await agent.files.writeFile(destination, data);
  } else {
    if ((await agent.files.stat(source)).type !== 'file')
      throw new Error(`Source must be a regular file: ${source}`);
    const type = await localType(destination);
    if (type !== undefined && type !== 'file')
      throw new Error(`Destination is not a file: ${destination}`);
    data = await agent.files.readFile(source);
    signal.throwIfAborted();
    await mkdir(dirname(destination), { recursive: true });
    signal.throwIfAborted();
    await writeFile(destination, data, { signal });
  }
  return { source, destination, files: 1, directories: 0, bytes: data.length };
};
