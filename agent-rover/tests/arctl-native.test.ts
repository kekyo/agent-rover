// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile, spawn, type ChildProcess } from 'node:child_process';
import { once } from 'node:events';
import {
  mkdir,
  mkdtemp,
  readFile,
  rm,
  stat,
  writeFile,
} from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { PNG } from 'pngjs';
import { connectRemoteAgent, type RemoteAgent } from '../src/index';
import { waitForResult } from '../src/wait';
import { nativeTestPaths } from './helpers/native-paths';

const { repositoryDirectory: root, agentsDirectory } = nativeTestPaths(
  import.meta.url
);
const exec = promisify(execFile);
const environment = { ...process.env, WINEDEBUG: '-all' };
const port = 39751;
let server: ChildProcess | undefined;
let observer: RemoteAgent | undefined;
let directory: string;
let remoteDirectory: string;
let remoteFixture: string;
const fixture = join(agentsDirectory, '.build/arctl/launch-amd64.exe');
const control = join(agentsDirectory, '.build/gui/control-amd64.exe');

const arctl = async (args: readonly string[]) =>
  await exec(
    process.execPath,
    [join(root, 'agent-rover/dist/arctl.mjs'), ...args],
    {
      env: {
        ...environment,
        AGENT_ROVER_HOST: '127.0.0.1',
        AGENT_ROVER_PORT: String(port),
        AGENT_ROVER_AUTH_TOKEN: undefined,
      },
      timeout: 30000,
    }
  );

it('round-trips binary and empty files through the Windows file API', async () => {
  const source = join(directory, '日本語 data.bin');
  const destination = join(directory, 'get', 'deep', '日本語 data.bin');
  const remote = `${remoteDirectory}\\put\\nested\\data.bin`;
  for (const data of [Buffer.from([0, 255, 128, 10, 0]), Buffer.alloc(0)]) {
    await writeFile(source, data);
    const uploaded = await arctl(['put', source, remote, '--json']);
    expect(JSON.parse(uploaded.stdout).result.bytes).toBe(data.length);
    await arctl(['get', remote, destination]);
    expect(await readFile(destination)).toEqual(data);
  }
});

it('copies real directory trees and preserves extra files and conflicting entries', async () => {
  const source = join(directory, 'tree');
  const destination = join(directory, 'download-tree');
  const remote = `${remoteDirectory}\\remote-tree`;
  await mkdir(join(source, '日本語 folder', 'deep'), { recursive: true });
  await mkdir(join(source, 'empty'));
  await writeFile(join(source, '日本語 folder', 'deep', 'data'), 'contents');
  await observer!.files.writeFile(
    `${remote}\\keeper`,
    Buffer.from('keep remote')
  );
  await arctl(['put', '-r', source, remote]);
  await mkdir(destination);
  await writeFile(join(destination, 'local-keeper'), 'keep local');
  await arctl(['get', '-r', remote, destination]);
  expect(
    await readFile(join(destination, '日本語 folder', 'deep', 'data'), 'utf8')
  ).toBe('contents');
  expect((await stat(join(destination, 'empty'))).isDirectory()).toBe(true);
  expect(await readFile(join(destination, 'keeper'), 'utf8')).toBe(
    'keep remote'
  );
  expect(await readFile(join(destination, 'local-keeper'), 'utf8')).toBe(
    'keep local'
  );
  await observer!.files.writeFile(
    `${remote}\\empty\\keeper`,
    Buffer.from('still here')
  );
  await writeFile(join(source, 'keeper'), 'replacement');
  await arctl(['put', '-r', source, remote]);
  expect(
    (await observer!.files.readFile(`${remote}\\empty\\keeper`)).toString()
  ).toBe('still here');
  expect((await observer!.files.readFile(`${remote}\\keeper`)).toString()).toBe(
    'replacement'
  );
  await writeFile(join(source, 'conflict'), 'preserve local');
  await observer!.files.mkdir(`${remote}\\conflict`);
  await expect(arctl(['put', '-r', source, remote])).rejects.toMatchObject({
    code: 1,
  });
  await expect(arctl(['get', '-r', remote, source])).rejects.toMatchObject({
    code: 1,
  });
  expect(await readFile(join(source, 'conflict'), 'utf8')).toBe(
    'preserve local'
  );
  expect((await observer!.files.stat(`${remote}\\conflict`)).type).toBe(
    'directory'
  );
});

it('captures visible desktop and window pixels without changing the active window', async () => {
  const remoteDisplay = (
    await exec(
      'winepath',
      ['-w', join(agentsDirectory, '.build/arctl/display-amd64.exe')],
      { env: environment }
    )
  ).stdout.trim();
  const title = `arctl-capture-${process.pid}`;
  const process_ = await observer!.applications.launch({
    path: remoteDisplay,
    arguments: [title],
    createNoWindow: true,
  });
  let foregroundPid: number | undefined;
  try {
    await exec('wine', [fixture, 'wait-ready', `${title}-ready`], {
      env: environment,
      timeout: 40000,
    });
    const window = await observer!.waitForWindow({ title, visible: true });
    const expectedPixel = [36, 180, 90, 255];
    // WM_PAINT completes before the desktop compositor finishes showing a
    // new window. Observe the rendered content without activating the window.
    await waitForResult(async () => {
      const reference = PNG.sync.read((await window.screenshot()).image);
      const offset =
        (Math.floor(reference.height / 2) * reference.width +
          Math.floor(reference.width / 2)) *
        4;
      expect([...reference.data.subarray(offset, offset + 4)]).toEqual(
        expectedPixel
      );
    });
    // Own the foreground reference so an unrelated window's lifetime cannot
    // decide which window is expected to remain active after capture.
    const foregroundProcess = await observer!.applications.launch({
      path: remoteDisplay,
      arguments: [`${title}-foreground`],
      createNoWindow: true,
    });
    foregroundPid = foregroundProcess.id;
    const foreground = await observer!.waitForWindow({
      processId: foregroundPid,
      visible: true,
    });
    await foreground.setBounds({
      ...foreground.bounds,
      x: window.bounds.x + window.bounds.width + 20,
    });
    await foreground.activate();
    await waitForResult(async () => {
      expect(
        (await observer!.windows())
          .filter((entry) => entry.active)
          .map((entry) => entry.id)
      ).toEqual([foreground.id]);
    });
    for (const wholeScreen of [true, false]) {
      const path = join(directory, wholeScreen ? 'desktop.png' : 'window.png');
      const captured = await arctl([
        'screenshot',
        path,
        '--json',
        ...(wholeScreen ? [] : ['--window', window.id]),
      ]);
      const result = JSON.parse(captured.stdout).result;
      expect(result.bounds).toEqual(
        wholeScreen ? await observer!.bounds() : window.frameBounds
      );
      const png = PNG.sync.read(await readFile(path));
      expect(png.width).toBe(result.visibleBounds.width);
      expect(png.height).toBe(result.visibleBounds.height);
      const x = Math.floor(
        window.frameBounds.x +
          window.frameBounds.width / 2 -
          result.visibleBounds.x
      );
      const y = Math.floor(
        window.frameBounds.y +
          window.frameBounds.height / 2 -
          result.visibleBounds.y
      );
      expect([
        ...png.data.subarray(
          (y * png.width + x) * 4,
          (y * png.width + x) * 4 + 4
        ),
      ]).toEqual(expectedPixel);
      await expect(arctl(['screenshot', path])).rejects.toMatchObject({
        code: 1,
      });
    }
    expect(
      (await observer!.windows())
        .filter((window) => window.active)
        .map((window) => window.id)
    ).toEqual([foreground.id]);
  } finally {
    if (foregroundPid !== undefined)
      await observer!.processes.kill(foregroundPid);
    await observer!.processes.kill(process_.id);
  }
}, 60000);

beforeAll(async () => {
  await exec('make', ['-j4', 'amd64'], { cwd: agentsDirectory });
  await exec('make', ['-C', 'tests/arctl'], { cwd: agentsDirectory });
  await exec(
    'make',
    ['-C', 'tests/gui', '../../.build/gui/control-amd64.exe'],
    { cwd: agentsDirectory }
  );
  directory = await mkdtemp(join(tmpdir(), 'agent-rover-arctl-native-'));
  remoteDirectory = (
    await exec('winepath', ['-w', directory], { env: environment })
  ).stdout.trim();
  remoteFixture = (
    await exec('winepath', ['-w', fixture], { env: environment })
  ).stdout.trim();
  server = spawn(
    'wine',
    [
      join(agentsDirectory, 'dist/agent-amd64.exe'),
      '--host',
      '127.0.0.1',
      '--port',
      String(port),
      '--no-auth',
    ],
    { env: environment, stdio: 'ignore' }
  );
  observer = await waitForResult(
    async () => {
      try {
        return await connectRemoteAgent({
          host: '127.0.0.1',
          port,
          timeoutMs: 10000,
        });
      } catch (error) {
        throw new Error('Native CLI agent is starting.', { cause: error });
      }
    },
    { timeoutMs: 30000 }
  );
}, 120000);

afterAll(async () => {
  observer?.release();
  try {
    await exec('wine', [control, 'exit', String(port)], {
      env: environment,
      timeout: 30000,
    });
  } finally {
    if (server && server.exitCode === null && server.signalCode === null) {
      const exited = once(server, 'exit');
      server.kill('SIGKILL');
      await exited;
    }
    if (directory) await rm(directory, { force: true, recursive: true });
  }
}, 60000);

it('leaves a real application running with literal arguments, cwd, environment, and logs', async () => {
  const event = `Local\\AgentRoverArctlLaunch-${process.pid}`;
  const args = [
    '日本語',
    'two words',
    'a"b',
    '',
    'C:\\path with spaces\\',
    '--help',
    '--host',
    'child-host',
  ];
  let pid: number | undefined;
  try {
    const launched = await arctl([
      'launch',
      '--json',
      '--cwd',
      remoteDirectory,
      '--env',
      'ARCTL_TEST=first',
      '--env',
      'ARCTL_TEST=日本語 value=last',
      '--env',
      'ARCTL_EMPTY=',
      '--stdout',
      `${remoteDirectory}\\stdout.log`,
      '--stderr',
      `${remoteDirectory}\\stderr.log`,
      '--',
      remoteFixture,
      'run',
      event,
      ...args,
    ]);
    pid = JSON.parse(launched.stdout).result.pid;
    await exec('wine', [fixture, 'wait-ready', event], {
      env: environment,
      timeout: 40000,
    });
    expect(await observer!.processes.exists(pid!)).toBe(true);
    const stdout = (
      await readFile(join(directory, 'stdout.log'), 'utf8')
    ).replaceAll('\r\n', '\n');
    expect(stdout.split('\n')).toEqual([
      `cwd=${remoteDirectory}`,
      'env=日本語 value=last',
      'empty=',
      ...args.map((arg) => `arg=${arg}`),
      '',
    ]);
    expect((await readFile(join(directory, 'stderr.log'), 'utf8')).trim()).toBe(
      'launch-error-output'
    );
    expect(launched.stderr).toBe('');
  } finally {
    if (pid !== undefined) await observer!.processes.kill(pid);
  }
}, 60000);
