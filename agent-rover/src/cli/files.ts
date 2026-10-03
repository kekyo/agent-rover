// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { lstat, mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, win32 } from 'node:path';
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
 * Copies files through the existing SDK, leaving completed writes in place.
 * @param agent Connected agent.
 * @param direction Upload or download.
 * @param source Exact source path.
 * @param destination Exact destination path.
 * @param recursive Copy directory contents instead of a single file.
 * @param signal Stops subsequent operations after CLI interruption.
 * @returns Completed transfer totals.
 */
export const transferArctlFiles = async (
  agent: RemoteAgent,
  direction: 'put' | 'get',
  source: string,
  destination: string,
  recursive: boolean,
  signal: AbortSignal
): Promise<ArctlTransferResult> => {
  signal.throwIfAborted();
  if (recursive) {
    const sourceType =
      direction === 'put'
        ? await localType(source)
        : (await agent.files.stat(source)).type;
    if (sourceType !== 'directory')
      throw new Error(`Recursive source must be a directory: ${source}`);
    const destinationType =
      direction === 'put'
        ? (await agent.files.exists(destination))
          ? (await agent.files.stat(destination)).type
          : undefined
        : await localType(destination);
    if (destinationType !== undefined && destinationType !== 'directory')
      throw new Error(`Destination is not a directory: ${destination}`);
    signal.throwIfAborted();
    if (direction === 'put')
      await agent.files.mkdir(destination, { recursive: true });
    else await mkdir(destination, { recursive: true });
    const entries =
      direction === 'put'
        ? (await readdir(source, { withFileTypes: true })).map((entry) => ({
            name: entry.name,
            type: entry.isDirectory()
              ? 'directory'
              : entry.isFile()
                ? 'file'
                : 'other',
          }))
        : await agent.files.readdir(source);
    let files = 0;
    let directories = 1;
    let bytes = 0;
    for (const entry of [...entries].sort((left, right) =>
      left.name.localeCompare(right.name)
    )) {
      // Windows directory entries are single names. Reject path components
      // before combining them with either the local or the remote destination.
      if (
        entry.name === '.' ||
        entry.name === '..' ||
        /[\\/:\0]/u.test(entry.name) ||
        entry.name === ''
      )
        throw new Error(`Unsupported directory entry name: ${entry.name}`);
      if (entry.type !== 'file' && entry.type !== 'directory')
        throw new Error(`Unsupported file type: ${entry.name}`);
      signal.throwIfAborted();
      const copied = await transferArctlFiles(
        agent,
        direction,
        direction === 'put'
          ? join(source, entry.name)
          : win32.join(source, entry.name),
        direction === 'put'
          ? win32.join(destination, entry.name)
          : join(destination, entry.name),
        entry.type === 'directory',
        signal
      );
      files += copied.files;
      directories += copied.directories;
      bytes += copied.bytes;
    }
    return { source, destination, files, directories, bytes };
  }
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
