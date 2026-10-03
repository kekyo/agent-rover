// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { constants } from 'node:fs';
import { access, lstat, mkdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import type { AppWindow, RemoteAgent } from '../index';

/**
 * Validates a new local capture output and prepares its parent directory.
 * @param output User-supplied local path.
 * @param signal CLI interruption signal.
 * @returns Absolute output path. The final write must still be exclusive.
 */
export const prepareArctlCapture = async (
  output: string,
  signal: AbortSignal
): Promise<string> => {
  const path = resolve(output);
  signal.throwIfAborted();
  try {
    await lstat(path);
    throw new Error(`Output already exists: ${path}`);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error;
  }
  signal.throwIfAborted();
  await mkdir(dirname(path), { recursive: true });
  await access(dirname(path), constants.W_OK);
  return path;
};

/**
 * Resolves an exact current window without activating or restoring it.
 * @param agent Connected agent.
 * @param id User-supplied window ID, or undefined for the whole desktop.
 * @returns The window, or undefined for the whole desktop.
 */
export const resolveArctlWindow = async (
  agent: RemoteAgent,
  id: string | undefined
): Promise<AppWindow | undefined> => {
  if (id === undefined) return undefined;
  const window = (await agent.windows()).find((window) => window.id === id);
  if (window === undefined)
    throw new Error(`Window not found: ${id}. Run arctl windows again.`);
  return window;
};

/**
 * Captures visible pixels and exclusively saves a new PNG.
 * @param agent Connected agent.
 * @param output Local destination path.
 * @param windowId Exact window ID, or undefined for the whole desktop.
 * @param signal CLI interruption signal.
 * @returns Output path and capture metadata.
 */
export const captureArctlScreenshot = async (
  agent: RemoteAgent,
  output: string,
  windowId: string | undefined,
  signal: AbortSignal
) => {
  const path = await prepareArctlCapture(output, signal);
  const window = await resolveArctlWindow(agent, windowId);
  signal.throwIfAborted();
  const { image, bounds, visibleBounds, clipped } =
    window === undefined ? await agent.screenshot() : await window.screenshot();
  signal.throwIfAborted();
  await writeFile(path, image, { flag: 'wx', signal });
  return { path, bounds, visibleBounds, clipped };
};
