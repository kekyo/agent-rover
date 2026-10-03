// Runs the compatibility probes on the explicitly configured Windows machine.
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { connectRemoteAgent } from '../../../agent-rover/dist/index.mjs';

const host = process.env.AGENT_ROVER_WIN11_HOST;
const authToken = process.env.AGENT_ROVER_WIN11_TOKEN;
if (!host || !authToken)
  throw new Error('Windows host and token must be configured.');
const output = resolve(import.meta.dirname, '../../.build/async-foundation');
const agent = await connectRemoteAgent({
  host,
  port: 39397,
  authToken,
  timeoutMs: 30000,
});
let directory;
const results = [];
try {
  directory = await agent.files.mkdtemp('agent-rover-cardio120-');
  for (const architecture of ['amd64', 'i686']) {
    for (const probe of ['core', 'handles', 'runtime']) {
      const name = `${probe}-${architecture}.exe`;
      const remote = `${directory}\\${name}`;
      await agent.files.writeFile(
        remote,
        await readFile(resolve(output, name))
      );
      const process = await agent.processes.launchManaged({
        path: remote,
        arguments: probe === 'runtime' ? ['--native-modal'] : [],
        captureStdout: true,
        captureStderr: true,
        createNoWindow: true,
        killTreeOnRelease: true,
      });
      try {
        const snapshot = await process.waitForExit({ timeoutMs: 60000 });
        const stdout = await process.stdoutText();
        const stderr = await process.stderrText();
        assert.equal(snapshot.root.exitCode, 0, `${name}: ${stderr}`);
        if (probe === 'runtime') {
          assert.match(stdout, /nested message loop passed/u);
          assert.match(stdout, /native popup menu and sizing loop passed/u);
        }
        results.push({
          name,
          exitCode: snapshot.root.exitCode,
          stdout: stdout.trim(),
          stderr: stderr.trim(),
        });
        console.log(results.at(-1));
      } finally {
        await process.releaseAsync();
      }
    }
  }
  await writeFile(
    resolve(output, 'windows11-results.json'),
    `${JSON.stringify({ date: new Date().toISOString(), results }, null, 2)}\n`
  );
} finally {
  try {
    if (directory) await agent.files.remove(directory, { recursive: true });
  } finally {
    agent.release();
  }
}
