// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import type { RemoteAgent, RemoteApplicationLaunchOptions } from '../index';

/**
 * Starts an application without tying its lifetime to the CLI connection.
 * @param agent Connected agent.
 * @param options Remote executable, arguments, environment, and log paths.
 * @returns Process identifier and executable name.
 */
export const launchArctlApplication = async (
  agent: RemoteAgent,
  options: RemoteApplicationLaunchOptions
) => {
  const process = await agent.applications.launch(options);
  return { pid: process.id, name: process.name };
};

/** A visible top-level window listed by arctl. */
export interface ListedWindow {
  /** Current native window identifier. */
  readonly id: string;
  /** Owning operating system process ID. */
  readonly pid: number;
  /** Owning executable name. */
  readonly processName: string;
  /** Current window title. */
  readonly title: string;
}

/**
 * Lists visible windows without changing their state.
 * @param agent Connected remote agent.
 * @returns Window information suitable for text or JSON output.
 */
export const listArctlWindows = async (
  agent: RemoteAgent
): Promise<readonly ListedWindow[]> =>
  (await agent.windows())
    .filter((window) => window.visible)
    .map((window) => ({
      id: window.id,
      pid: window.process.id,
      processName: window.process.name,
      title: window.title,
    }));
