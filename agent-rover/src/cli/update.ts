// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Version-selected agent updates
// Copyright (c) Kouji Matsui. Under MIT.
import { randomUUID } from 'node:crypto';
import { createInterface } from 'node:readline/promises';
import type { AgentUpdateSession } from '../driver/connection.ts';
import { compareVersions, fetchAgentRelease } from '../update/release.ts';

/** Confirms that a release requires manual installation for the next update.
 * @param endpoint Selected host and port. @param version Requested release.
 * @param allowed Explicit automation flag. @param ask Interactive input, if available.
 * @param warn Shows the consequence before changing Windows. @returns Whether to proceed. */
export const confirmLegacyUpdate = async (
  endpoint: string,
  version: string,
  allowed: boolean,
  ask: (() => Promise<string>) | undefined,
  warn: (text: string) => void
): Promise<boolean> => {
  warn(
    'Switch ' +
      endpoint +
      ' to agent-rover ' +
      version +
      "? This release does not support 'arctl update'.\nThe next update requires a manual installation on " +
      endpoint +
      '.\n'
  );
  if (allowed) return true;
  if (!ask)
    throw new Error(
      'Noninteractive legacy downgrade requires --allow-legacy-downgrade.'
    );
  return (await ask()).trim().toLowerCase() === 'y';
};

/** Runs one update on the already selected Windows session.
 * @param session Authenticated session. @param version Requested release. @param allowLegacy Explicit consent.
 * @param signal Cancellation signal. @param warn Progress and warnings.
 * @returns A structured result and the observed outcome. */
export const installAgentRelease = async (
  session: AgentUpdateSession,
  version: string,
  allowLegacy: boolean,
  signal: AbortSignal,
  warn: (text: string) => void
) => {
  const { endpoint, capabilities } = session;
  const selected = endpoint.host + ':' + endpoint.port;
  if (
    !capabilities.features.includes('agent.update.v1') ||
    !capabilities.agentVersion ||
    !capabilities.architecture
  )
    throw new Error(
      selected + ' requires manual installation of an update-capable agent.'
    );
  const before = capabilities.agentVersion,
    direction = compareVersions(version, before);
  const base = {
    endpoint,
    previousVersion: before,
    requestedVersion: version,
    architecture: capabilities.architecture,
  };
  try {
    const sha256 = await session.imageHash();
    const release = await fetchAgentRelease(
      version,
      capabilities.architecture,
      signal
    );
    if (!direction && release.sha256 === sha256)
      return {
        ...base,
        status: 'unchanged',
        actualVersion: before,
        manualUpdateRequired: false,
      };
    warn(
      (direction < 0
        ? 'Downgrade'
        : direction > 0
          ? 'Upgrade'
          : 'Replace different build of') +
        ' ' +
        selected +
        ' from ' +
        before +
        ' to ' +
        version +
        '.\n'
    );
    if (release.updateProtocol === 0) {
      const ask = process.stdin.isTTY
        ? async () => {
            const input = createInterface({
              input: process.stdin,
              output: process.stderr,
            });
            try {
              return await input.question('Continue? [y/N] ', { signal });
            } finally {
              input.close();
            }
          }
        : undefined;
      if (
        !(await confirmLegacyUpdate(selected, version, allowLegacy, ask, warn))
      )
        return {
          ...base,
          status: 'cancelled',
          actualVersion: before,
          manualUpdateRequired: false,
        };
    }
    signal.throwIfAborted();
    const updateId = randomUUID();
    const outcome = await session.install(updateId, sha256, release);
    return {
      ...base,
      updateId,
      ...outcome,
      manualUpdateRequired:
        outcome.status === 'applied' && release.updateProtocol === 0,
    };
  } catch (error) {
    if (error instanceof Error)
      error.message = selected + ' -> ' + version + ': ' + error.message;
    throw error;
  }
};
