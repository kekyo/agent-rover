// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHmac } from 'node:crypto';

import { authChallengePrefix } from './protocol_version';

/** Auth challenge byte length used by the TCP frame handshake. */
export const authChallengeBytes = 32;

/** Auth challenge response byte length used by the TCP frame handshake. */
export const authResponseBytes = 32;

/** Environment variable used by explicit connections when authToken is omitted. */
export const authTokenEnvVarName = 'AGENT_ROVER_AUTH_TOKEN';

/**
 * Resolves an explicit connection's credential without reading configuration.
 * @param explicitToken Token option, or undefined to use the environment.
 * @returns The credential, or undefined for unauthenticated access.
 * @remarks An explicit empty string disables the environment fallback.
 */
export const resolveAuthToken = (
  explicitToken: string | undefined
): string | undefined => {
  const token = explicitToken ?? process.env[authTokenEnvVarName];
  return token === undefined || token.length === 0 ? undefined : token;
};

/**
 * Creates a filter that removes credentials from diagnostic and result text.
 * @param tokens Credentials to hide; empty and undefined values are ignored.
 * @returns A reusable text filter.
 */
export const createCredentialRedactor = (
  tokens: readonly (string | undefined)[]
): ((text: string) => string) => {
  // Match longer credentials first, so a shared prefix cannot expose a suffix.
  const secrets = [
    ...new Set(
      tokens.filter(
        (token): token is string => token !== undefined && token !== ''
      )
    ),
  ].sort((a, b) => b.length - a.length);
  return (text) => {
    for (const secret of secrets) text = text.replaceAll(secret, '[redacted]');
    return text;
  };
};

/** Creates the raw HMAC-SHA-256 response for one auth challenge. */
export const createAuthChallengeResponse = (
  token: string,
  challenge: Buffer
): Buffer => {
  if (challenge.byteLength !== authChallengeBytes) {
    throw new Error('Auth challenge must be 32 bytes.');
  }
  const hmac = createHmac('sha256', Buffer.from(token, 'utf8'));
  hmac.update(Buffer.concat([authChallengePrefix, challenge]));
  return hmac.digest();
};
