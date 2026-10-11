// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { createConfigurationFixture } from './helpers/configuration-fixture';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

const exec = promisify(execFile);

it.each(
  ['module', 'commonjs'].flatMap((format) =>
    ['home', 'working'].map((source) => ({ format, source }))
  )
)(
  'loads the $source default section from the packaged $format entry',
  async ({ format, source }) => {
    const fake = await startFakeTcpAgent({ authToken: 'package-token' });
    const configuration = `[default]\n${fake.host}:${fake.port}=package-token\n`;
    const fixture = await createConfigurationFixture(
      source === 'home' ? configuration : ''
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
      if (source === 'working') {
        await writeFile(
          join(fixture.workingDirectory, '.agent-rover'),
          configuration
        );
      }
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
        {
          cwd: fixture.workingDirectory,
          env: fixture.environment,
          timeout: 10000,
        }
      );
      expect(result.stdout).toBe('windows\n');
      expect(result.stderr).toBe('');
    } finally {
      await fake.close();
      await fixture.close();
    }
  }
);
