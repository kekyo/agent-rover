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

/** Environment variable read by the driver when no auth token option is set. */
export const authTokenEnvVarName = 'AGENT_ROVER_AUTH_TOKEN';

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

/** Resolves a driver auth token from explicit options or the environment. */
export const resolveAuthToken = (
  explicitToken: string | undefined
): string | undefined => {
  const token = explicitToken ?? process.env[authTokenEnvVarName];
  return token === undefined || token.length === 0 ? undefined : token;
};
