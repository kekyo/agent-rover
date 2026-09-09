// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import type { RemoteAgentError, RemoteOperationErrorDetails } from '../index';

/** Shared monotonic deadline for one resource operation. */
export interface ResourceDeadline {
  /** Monotonic operation start in milliseconds. */
  readonly startedAt: number;
  /** Monotonic deadline in milliseconds. */
  readonly expiresAt: number;
}

/** Creates a bounded operation deadline; zero allows one immediate attempt. */
export const createResourceDeadline = (
  timeoutMs: number | undefined
): ResourceDeadline => {
  const timeout = timeoutMs ?? 10000;
  if (!Number.isFinite(timeout) || timeout < 0)
    throw Object.assign(
      new Error('timeoutMs must be a non-negative finite number.'),
      { code: 'INVALID_ARGUMENT' }
    );
  const startedAt = performance.now();
  return { startedAt, expiresAt: startedAt + timeout };
};

/** Extracts structured operation details without inspecting diagnostic text. */
export const operationDetails = (
  error: unknown
): RemoteOperationErrorDetails | undefined =>
  error instanceof Error ? (error as RemoteAgentError).details : undefined;

/**
 * Retries only failures accepted by the supplied policy, using one shared deadline.
 * @param deadline Deadline created by the outermost resource operation.
 * @param retryable Whether the failed attempt may be repeated.
 * @param operation One attempt at the operation.
 * @returns The first completed result; failures retain their OS details and retry outcome.
 */
export const retryResourceOperation = async <T>(
  deadline: ResourceDeadline,
  retryable: (error: unknown) => boolean,
  operation: () => Promise<T>
): Promise<T> => {
  let attempts = 0;
  while (true) {
    attempts += 1;
    try {
      return await operation();
    } catch (error) {
      const now = performance.now();
      const details = operationDetails(error);
      if (details !== undefined)
        Object.assign(error as Error, {
          details: {
            ...details,
            attempts,
            elapsedMs: now - deadline.startedAt,
            timedOut: now >= deadline.expiresAt,
          },
        });
      if (!retryable(error) || now >= deadline.expiresAt) throw error;
      const waitMs = Math.min(
        25 * 2 ** Math.min(attempts - 1, 4),
        deadline.expiresAt - now
      );
      await new Promise<void>((resolve) => {
        setTimeout(resolve, waitMs);
      });
    }
  }
};
