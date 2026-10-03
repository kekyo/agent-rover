// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import {
  connectRemoteAgent,
  type RemoteApplicationLaunchOptions,
} from '../src/index';

import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

const executable = fileURLToPath(new URL('../dist/arctl.mjs', import.meta.url));

const cliEnvironment = (overrides: NodeJS.ProcessEnv): NodeJS.ProcessEnv => ({
  ...process.env,
  AGENT_ROVER_HOST: undefined,
  AGENT_ROVER_PORT: undefined,
  AGENT_ROVER_AUTH_TOKEN: undefined,
  ...overrides,
});

const runArctl = async (args: readonly string[], env: NodeJS.ProcessEnv) =>
  await new Promise<{ code: number; stdout: string; stderr: string }>(
    (resolve, reject) => {
      execFile(
        process.execPath,
        [executable, ...args],
        {
          env: cliEnvironment(env),
          timeout: 20000,
        },
        (error, stdout, stderr) => {
          if (error !== null && typeof error.code !== 'number') {
            reject(error);
          } else {
            resolve({ code: error?.code ?? 0, stdout, stderr });
          }
        }
      );
    }
  );

describe('arctl', () => {
  it.each([['--help'], ['windows', '--help'], ['--version']])(
    'runs %j without a connection',
    async (...args) => {
      const result = await runArctl(args, {
        AGENT_ROVER_HOST: 'unused.invalid',
      });
      expect(result.code).toBe(0);
      expect(result.stderr).toBe('');
      expect(result.stdout).toMatch(
        args.includes('--version') ? /^arctl \d+\.\d+\.\d+/u : /Usage: arctl/u
      );
    }
  );

  it('authenticates using explicit settings before and after the command', async () => {
    const fake = await startFakeTcpAgent({
      authToken: 'explicit-test-token',
      windows: [
        defaultFakeWindow,
        { ...defaultFakeWindow, id: '0x1002', visible: false },
      ],
    });
    try {
      const result = await runArctl(
        [
          '--host',
          fake.host,
          'windows',
          '--port',
          String(fake.port),
          '--token',
          'explicit-test-token',
          '--json',
        ],
        {
          AGENT_ROVER_HOST: 'unused.invalid',
          AGENT_ROVER_PORT: 'invalid',
          AGENT_ROVER_AUTH_TOKEN: 'wrong-test-token',
        }
      );
      expect(result.code).toBe(0);
      expect(result.stderr).toBe('');
      expect(JSON.parse(result.stdout)).toEqual({
        command: 'windows',
        result: [
          {
            id: defaultFakeWindow.id,
            pid: defaultFakeWindow.process.id,
            processName: defaultFakeWindow.process.name,
            title: defaultFakeWindow.title,
          },
        ],
      });
    } finally {
      await fake.close();
    }
  });

  it('uses environment settings and displays readable window results', async () => {
    const fake = await startFakeTcpAgent({
      authToken: 'environment-test-token',
    });
    try {
      const result = await runArctl(['windows'], {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
        AGENT_ROVER_AUTH_TOKEN: 'environment-test-token',
      });
      expect(result.code).toBe(0);
      expect(result.stdout).toContain(defaultFakeWindow.id);
      expect(result.stdout).toContain(defaultFakeWindow.process.name);
      expect(result.stdout).toContain(defaultFakeWindow.title);
      expect(result.stdout).toContain(String(defaultFakeWindow.process.id));
      expect(result.stderr).toBe('');
    } finally {
      await fake.close();
    }
  });

  it('succeeds with an empty window list on an unauthenticated agent', async () => {
    const fake = await startFakeTcpAgent({ windows: [] });
    try {
      const result = await runArctl(
        ['windows', '--host', fake.host, '--port', String(fake.port), '--json'],
        {}
      );
      expect(result.code).toBe(0);
      expect(JSON.parse(result.stdout)).toEqual({
        command: 'windows',
        result: [],
      });
    } finally {
      await fake.close();
    }
  });

  it('rejects invalid arguments before opening a connection', async () => {
    let connections = 0;
    const server = createServer((socket) => {
      connections += 1;
      socket.destroy();
    });
    await new Promise<void>((resolve) => {
      server.listen(0, '127.0.0.1', resolve);
    });
    const address = server.address();
    if (address === null || typeof address === 'string')
      throw new Error('Expected a TCP listener.');
    try {
      for (const args of [
        [],
        ['unknown'],
        ['windows', 'extra'],
        ['windows', '--unknown'],
        ['windows', '--host', ''],
        ['windows', '--port', '0'],
        ['windows', '--port', '65536'],
        ['windows', '--port', '1.5'],
        ['windows', '--timeout', '0'],
        ['windows', '--timeout', 'NaN'],
        ['windows', '--token'],
      ]) {
        const result = await runArctl(args, {
          AGENT_ROVER_HOST: '127.0.0.1',
          AGENT_ROVER_PORT: String(address.port),
        });
        expect(result.code, JSON.stringify(args)).toBe(2);
        expect(result.stdout).toBe('');
        expect(result.stderr).not.toBe('');
      }
      expect(connections).toBe(0);
    } finally {
      await new Promise<void>((resolve, reject) => {
        server.close((error) => {
          if (error) reject(error);
          else resolve();
        });
      });
    }
  });

  it('reports operation failures on stderr without leaking the token', async () => {
    const token = 'sensitive-test-token';
    const fake = await startFakeTcpAgent({
      authToken: token,
      beforeRequest: () => ({
        code: 'OPERATION_FAILED',
        message: `Cannot enumerate windows: ${token}`,
      }),
    });
    try {
      const result = await runArctl(['windows', '--json'], {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
        AGENT_ROVER_AUTH_TOKEN: token,
      });
      expect(result.code).toBe(1);
      expect(result.stdout).toBe('');
      expect(result.stderr).toContain('OPERATION_FAILED');
      expect(result.stderr).not.toContain(token);
    } finally {
      await fake.close();
    }
  });

  it('launches once with literal arguments, environment, and persistent output', async () => {
    const launches: RemoteApplicationLaunchOptions[] = [];
    const fake = await startFakeTcpAgent({
      launches,
      launchResult: { id: 4321, name: 'launched.exe' },
    });
    const arguments_ = [
      '日本語',
      'two words',
      'a"b',
      '',
      '--help',
      '--host',
      'child-host',
    ];
    try {
      const result = await runArctl(
        [
          'launch',
          '--host',
          fake.host,
          '--port',
          String(fake.port),
          '--json',
          '--cwd',
          'C:\\work space',
          '--env',
          'ARCTL_TEST=first',
          '--env',
          'ARCTL_TEST=second=value',
          '--env',
          'EMPTY=',
          '--stdout',
          'C:\\work space\\stdout.log',
          '--stderr',
          'C:\\work space\\stderr.log',
          '--',
          'C:\\work space\\app.exe',
          ...arguments_,
        ],
        {}
      );
      expect(result.code, result.stderr).toBe(0);
      expect(result.stderr).toBe('');
      const output = JSON.parse(result.stdout);
      expect(output.command).toBe('launch');
      expect(output.result).toEqual({ pid: 4321, name: 'launched.exe' });
      expect(launches).toEqual([
        {
          path: 'C:\\work space\\app.exe',
          arguments: arguments_,
          workingDirectory: 'C:\\work space',
          environment: { ARCTL_TEST: 'second=value', EMPTY: '' },
          stdoutPath: 'C:\\work space\\stdout.log',
          stderrPath: 'C:\\work space\\stderr.log',
        },
      ]);
      const observer = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
      });
      try {
        expect(await observer.processes.exists(output.result.pid)).toBe(true);
        expect(
          (await observer.files.readFile('C:/work space/stdout.log')).toString()
        ).toBe('managed stdout');
        expect(
          (await observer.files.readFile('C:/work space/stderr.log')).toString()
        ).toBe('managed stderr');
        expect(fake.managedProcessCount()).toBe(0);
      } finally {
        observer.release();
      }
    } finally {
      await fake.close();
    }
  });

  it('reports an unknown launch outcome after response loss without retrying', async () => {
    const launches: RemoteApplicationLaunchOptions[] = [];
    const fake = await startFakeTcpAgent({
      launches,
      disconnectAfterRequest: ['applications.launch'],
    });
    try {
      const result = await runArctl(['launch', '--', 'app.exe'], {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
      });
      expect(result.code).toBe(1);
      expect(result.stdout).toBe('');
      expect(result.stderr).toContain('outcome is unknown');
      expect(launches).toHaveLength(1);
      const observer = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
      });
      try {
        expect(await observer.processes.exists(4321)).toBe(true);
      } finally {
        observer.release();
      }
    } finally {
      await fake.close();
    }
  });

  it('validates launch arguments before connecting and shows its own help', async () => {
    for (const args of [
      ['launch'],
      ['launch', '--'],
      ['launch', 'app.exe'],
      ['launch', '--env', '=value', '--', 'app.exe'],
      ['launch', '--env', 'MISSING_VALUE', '--', 'app.exe'],
      ['launch', '--cwd', '', '--', 'app.exe'],
    ]) {
      const result = await runArctl(args, {
        AGENT_ROVER_HOST: 'unused.invalid',
      });
      expect(result.code, JSON.stringify(args)).toBe(2);
      expect(result.stdout).toBe('');
    }
    const help = await runArctl(['launch', '--help'], {});
    expect(help.code).toBe(0);
    expect(help.stdout).toContain('--env');
    expect(help.stdout).toContain('-- <command>');
  });

  it('exits with 130 when interrupted during the connection handshake', async () => {
    const server = createServer();
    await new Promise<void>((resolve) => {
      server.listen(0, '127.0.0.1', resolve);
    });
    const address = server.address();
    if (address === null || typeof address === 'string')
      throw new Error('Expected a TCP listener.');
    const connected = once(server, 'connection');
    const child = spawn(process.execPath, [executable, 'windows'], {
      env: cliEnvironment({
        AGENT_ROVER_HOST: '127.0.0.1',
        AGENT_ROVER_PORT: String(address.port),
      }),
      stdio: 'ignore',
    });
    const exited = once(child, 'exit');
    try {
      const [socket] = await connected;
      const closed = once(socket, 'close');
      socket.resume();
      child.kill('SIGINT');
      expect(await exited).toEqual([130, null]);
      await closed;
    } finally {
      if (child.exitCode === null && child.signalCode === null) {
        child.kill('SIGKILL');
        await exited;
      }
      await new Promise<void>((resolve) => {
        server.close(() => {
          resolve();
        });
      });
    }
  }, 10000);
});
