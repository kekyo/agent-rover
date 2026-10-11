// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { createConfigurationFixture } from './helpers/configuration-fixture';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

const exec = promisify(execFile);

it.each(['module', 'commonjs'])(
  'loads the default section from the packaged %s entry',
  async (format) => {
    const fake = await startFakeTcpAgent({ authToken: 'package-token' });
    const fixture = await createConfigurationFixture(
      `[default]\n${fake.host}:${fake.port}=package-token\n`
    );
    const path = fileURLToPath(
      new URL(
        `../dist/index.${format === 'module' ? 'mjs' : 'cjs'}`,
        import.meta.url
      )
    );
    const load =
      format === 'module'
        ? `import { connectRemoteAgent } from ${JSON.stringify(path)};`
        : `import { createRequire } from 'node:module';
       const { connectRemoteAgent } = createRequire(import.meta.url)(${JSON.stringify(path)});`;
    try {
      const result = await exec(
        process.execPath,
        [
          '--input-type=module',
          '-e',
          `${load}
      const agent = await connectRemoteAgent();
      try { console.log((await agent.capabilities()).platform); }
      finally { agent.release(); }
    `,
        ],
        { env: fixture.environment, timeout: 10000 }
      );
      expect(result.stdout).toBe('windows\n');
      expect(result.stderr).toBe('');
    } finally {
      await fake.close();
      await fixture.close();
    }
  }
);
