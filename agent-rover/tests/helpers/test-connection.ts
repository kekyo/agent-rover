// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import type { ConnectRemoteAgentOptions, RemoteAgent } from '../../src/index';
import { connectRemoteAgentInSection } from '../../src/driver/connection';

/** Endpoint of a test-owned agent, together with connection timing controls. */
export interface TestConnectionOptions extends ConnectRemoteAgentOptions {
  /** Host of the test server. */
  readonly host: string;
  /** Port assigned to the test server. */
  readonly port: number;
  /** Test server credential, when authentication is enabled. */
  readonly authToken?: string | undefined;
}

/**
 * Connects an operation test to its own server without reading user configuration.
 * @param options Test endpoint and retry controls. Tests default to one round.
 * @returns A session created by the same selector and transport as the SDK.
 * @remarks File loading and section selection are covered by section-connection tests.
 */
export const connectTestAgent = async (
  options: TestConnectionOptions
): Promise<RemoteAgent> => {
  const { host, port, authToken, ...settings } = options;
  return await connectRemoteAgentInSection(
    {
      name: 'test',
      endpoints: [{ host, port, authToken }],
      redact: (text) =>
        authToken ? text.replaceAll(authToken, '[redacted]') : text,
    },
    { retry: false, ...settings }
  );
};
