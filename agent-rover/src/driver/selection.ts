// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { delay } from 'async-primitives';
import type { ConnectRemoteAgentOptions, RemoteAgentErrorCode } from '../index';
import type { ConnectionEndpoint, ConnectionSection } from './configuration';
import { createCredentialRedactor } from '../auth';

/** Retry controls shared by section-based and explicit connections. */
export type ConnectionSelectionOptions = Pick<
  ConnectRemoteAgentOptions,
  'retry' | 'onRetry'
>;

const resolveRetrySettings = (options: ConnectionSelectionOptions) => {
  const retry = options.retry ?? true;
  const settings = typeof retry === 'object' ? retry : undefined;
  const maxRetries = retry === false ? 0 : (settings?.maxRetries ?? 360);
  const intervalMs = settings?.intervalMs ?? 10000;
  if (
    !Number.isSafeInteger(maxRetries) ||
    maxRetries < 0 ||
    !Number.isInteger(intervalMs) ||
    intervalMs < 0 ||
    intervalMs > 2147483647
  ) {
    throw Object.assign(
      new Error(
        'retry.maxRetries must be a non-negative safe integer and retry.intervalMs must be an integer from 0 through 2147483647.'
      ),
      { code: 'INVALID_ARGUMENT' }
    );
  }
  const onRetry =
    options.onRetry ??
    ((message: string): void => {
      // Console interception can hide output until a test fails; report waits live.
      process.stderr.write(`${message}\n`);
    });
  return { maxRetries, intervalMs, onRetry };
};

/**
 * Connects to one explicitly specified agent with the legacy busy-only retry policy.
 * @typeParam T Accepted session type.
 * @param endpoint Explicit host, port, and resolved credential.
 * @param options Retry limits and reporter.
 * @param connectOnce Attempts the endpoint and releases rejected connections.
 * @returns The accepted session.
 * @remarks Non-busy failures and exhausted retries preserve the original error code.
 */
export const selectExplicitConnection = async <T>(
  endpoint: ConnectionEndpoint,
  options: ConnectionSelectionOptions,
  connectOnce: (endpoint: ConnectionEndpoint) => Promise<T>
): Promise<T> => {
  const { maxRetries, intervalMs, onRetry } = resolveRetrySettings(options);
  const redact = createCredentialRedactor([endpoint.authToken]);
  for (let retries = 0; ; ++retries) {
    try {
      return await connectOnce(endpoint);
    } catch (error) {
      if (error instanceof Error) error.message = redact(error.message);
      if (
        retries >= maxRetries ||
        typeof error !== 'object' ||
        error === null ||
        !('code' in error) ||
        error.code !== 'AGENT_BUSY'
      )
        throw error;
      onRetry(
        redact(
          `[agent-rover] Agent at ${endpoint.host}:${endpoint.port} is busy (AGENT_BUSY). Retrying in ${intervalMs / 1000}s (${retries + 1}/${maxRetries}).`
        )
      );
      await delay(intervalMs);
    }
  }
};

/**
 * Selects the first available candidate, delaying only between complete rounds.
 * @typeParam T Accepted session type.
 * @param section Validated, nonempty candidates and their diagnostic filter.
 * @param options Retry limits and the optional retry reporter.
 * @param connectOnce Attempts one candidate and releases rejected connections.
 * @returns The first accepted session.
 * @remarks Each round shuffles a copy of the complete endpoint/token pairs.
 */
export const selectConnection = async <T>(
  section: ConnectionSection,
  options: ConnectionSelectionOptions,
  connectOnce: (endpoint: ConnectionEndpoint) => Promise<T>
): Promise<T> => {
  const { maxRetries, intervalMs, onRetry } = resolveRetrySettings(options);
  if (section.endpoints.length === 0) {
    throw Object.assign(new Error('A nonempty section is required.'), {
      code: 'INVALID_ARGUMENT',
    });
  }
  for (let round = 0; ; ++round) {
    const candidates = [...section.endpoints];
    for (let index = candidates.length - 1; index > 0; --index) {
      const other = Math.floor(Math.random() * (index + 1));
      [candidates[index], candidates[other]] = [
        candidates[other]!,
        candidates[index]!,
      ];
    }
    const failures: { code: RemoteAgentErrorCode; message: string }[] = [];
    for (const endpoint of candidates) {
      try {
        return await connectOnce(endpoint);
      } catch (error) {
        if (
          !(error instanceof Error) ||
          !('code' in error) ||
          ![
            'AGENT_BUSY',
            'CONNECTION_FAILED',
            'AUTHENTICATION_FAILED',
            'HANDSHAKE_FAILED',
            'DISCONNECTED',
            'PROTOCOL_ERROR',
          ].includes(String(error.code))
        )
          throw error;
        const host = endpoint.host.includes(':')
          ? `[${endpoint.host}]`
          : endpoint.host;
        failures.push({
          code: error.code as RemoteAgentErrorCode,
          message: `${host}:${endpoint.port} (${String(error.code)}): ${error.message}`,
        });
      }
    }
    const message = section.redact(
      `No available agent in section "${section.name}". ${failures.map(({ message }) => message).join('; ')}`
    );
    if (round >= maxRetries) {
      const firstCode = failures[0]!.code;
      throw Object.assign(new Error(message), {
        code: failures.every(({ code }) => code === firstCode)
          ? firstCode
          : 'CONNECTION_FAILED',
      });
    }
    onRetry(
      `${message} Retrying in ${intervalMs / 1000}s (${round + 1}/${maxRetries}).`
    );
    await delay(intervalMs);
  }
};
