// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { expect, it } from 'vitest';
import { connectRemoteAgent, type RemoteAgent } from '../src/index';
import { waitForResult } from '../src/wait';
import { nativeTestPaths } from './helpers/native-paths';
import { connectWindowsBootstrap } from './helpers/windows-bootstrap';

const host = process.env.AGENT_ROVER_WIN11_HOST;
const enabled = Boolean(host && process.env.AGENT_ROVER_WIN11_TOKEN);
const exec = promisify(execFile);

for (const [architecture, limitMiB] of [
  ['amd64', 1],
  ['i686', 1],
  ['amd64', 65],
] as const) {
  it.skipIf(!enabled)(
    `applies --max-transfer-size ${limitMiB} to ${architecture} file transfers`,
    async () => {
      const { agentsDirectory } = nativeTestPaths(import.meta.url);
      await exec('make', ['-j4', architecture], {
        cwd: agentsDirectory,
        maxBuffer: 8 * 1024 * 1024,
      });
      const bootstrap = await connectWindowsBootstrap();
      const remote = `C:\\Windows\\Temp\\agent-rover-transfer-${process.pid}-${Date.now()}`;
      const token = randomBytes(24).toString('base64url');
      const port = 39417;
      let agent: RemoteAgent | undefined;
      let deployment: { managedProcessId: number } | undefined;
      try {
        await bootstrap.request('file.mkdir', {
          path: remote,
          recursive: true,
        });
        await bootstrap.upload(
          `${remote}\\agent.exe`,
          await readFile(
            join(agentsDirectory, `dist/agent-${architecture}.exe`)
          )
        );
        deployment = (await bootstrap.request('process.launchManaged', {
          path: `${remote}\\agent.exe`,
          arguments: [
            '--host',
            '0.0.0.0',
            '--port',
            String(port),
            '--unsafe-token',
            token,
            '--max-transfer-size',
            String(limitMiB),
          ],
          createNoWindow: true,
          killTreeOnRelease: true,
          environment: { APPDATA: remote },
        })) as { managedProcessId: number };
        agent = await waitForResult(
          async () => {
            try {
              return await connectRemoteAgent({
                host: host!,
                port,
                authToken: token,
                timeoutMs: 120000,
              });
            } catch (error) {
              throw new Error('Transfer test agent is starting.', {
                cause: error,
              });
            }
          },
          { timeoutMs: 30000 }
        );
        const data = Buffer.alloc(limitMiB * 1024 * 1024, 0xa5);
        const path = `${remote}\\boundary.bin`;
        await agent.files.writeFile(path, data);
        expect((await agent.files.readFile(path)).equals(data)).toBe(true);
        // Consuming the first upload must release the retained-byte budget.
        await agent.files.writeFile(`${remote}\\next.bin`, Buffer.from('next'));
        if (limitMiB === 1) {
          const tooLarge = Buffer.alloc(data.length + 1, 0x5a);
          const existing = `${remote}\\too-large.bin`;
          await bootstrap.upload(existing, tooLarge);
          await expect(agent.files.readFile(existing)).rejects.toThrow(
            'File size is unsupported.'
          );
          expect((await agent.capabilities()).platform).toBe('windows');
          const rejected = `${remote}\\rejected.bin`;
          await expect(
            agent.files.writeFile(rejected, tooLarge)
          ).rejects.toThrow();
          expect(
            await bootstrap.request('file.exists', { path: rejected })
          ).toEqual({ exists: false });
        }
      } finally {
        agent?.release();
        try {
          if (deployment)
            await bootstrap.releaseManaged(deployment.managedProcessId);
          await waitForResult(
            async () => {
              await bootstrap.request('file.remove', {
                path: remote,
                recursive: true,
                ignoreMissing: true,
              });
            },
            { timeoutMs: 15000 }
          );
        } finally {
          await bootstrap.close();
        }
      }
    },
    180000
  );
}
