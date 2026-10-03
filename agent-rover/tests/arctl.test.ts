// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import { writeFileSync } from 'node:fs';
import {
  mkdir,
  mkdtemp,
  readFile,
  rm,
  stat,
  writeFile,
} from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';
import { PNG } from 'pngjs';

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
  it.each([undefined, defaultFakeWindow.id])(
    'saves a PNG from the selected capture target (%s)',
    async (windowId) => {
      const directory = await mkdtemp(join(tmpdir(), 'arctl-shot-'));
      const path = join(directory, 'nested', 'capture.png');
      const png = new PNG({ width: 2, height: 1 });
      png.data.set([255, 0, 0, 255, 0, 255, 0, 255]);
      const requests: string[] = [];
      const fake = await startFakeTcpAgent({
        screenshotImage: PNG.sync.write(png),
        beforeRequest: (method) => {
          requests.push(method);
          return undefined;
        },
      });
      try {
        const result = await runArctl(
          [
            'screenshot',
            path,
            '--json',
            ...(windowId === undefined ? [] : ['--window', windowId]),
          ],
          { AGENT_ROVER_HOST: fake.host, AGENT_ROVER_PORT: String(fake.port) }
        );
        expect(result.code, result.stderr).toBe(0);
        expect(result.stderr).toBe('');
        expect(JSON.parse(result.stdout)).toMatchObject({
          command: 'screenshot',
          result: { path, clipped: false },
        });
        expect(PNG.sync.read(await readFile(path)).data).toEqual(png.data);
        expect(
          requests.filter((method) => method.endsWith('screenshot'))
        ).toEqual([
          windowId === undefined ? 'agent.screenshot' : 'window.screenshot',
        ]);
        expect(requests).not.toContain('window.activate');
        expect(requests).not.toContain('window.show');
      } finally {
        await fake.close();
        await rm(directory, { recursive: true, force: true });
      }
    }
  );

  it('does not capture with an invalid window or an unusable existing destination', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-shot-errors-'));
    const path = join(directory, 'existing');
    const requests: string[] = [];
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) => {
        requests.push(method);
        return undefined;
      },
    });
    const env = {
      AGENT_ROVER_HOST: fake.host,
      AGENT_ROVER_PORT: String(fake.port),
    };
    try {
      await writeFile(path, 'keep original');
      for (const args of [
        ['screenshot', path],
        ['screenshot', join(path, 'nested.png')],
        ['screenshot', join(directory, 'invalid.png'), '--window', '0xmissing'],
      ]) {
        const result = await runArctl(args, env);
        expect(result.code, result.stderr).toBe(1);
        expect(result.stdout).toBe('');
      }
      expect(await readFile(path, 'utf8')).toBe('keep original');
      expect(requests.some((method) => method.endsWith('screenshot'))).toBe(
        false
      );
    } finally {
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  });

  it('preserves a destination created after screenshot preflight', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-shot-race-'));
    const path = join(directory, 'capture.png');
    const fake = await startFakeTcpAgent({
      beforeRequest: (method) => {
        if (method === 'agent.screenshot')
          writeFileSync(path, 'competing output');
        return undefined;
      },
    });
    try {
      const result = await runArctl(['screenshot', path], {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
      });
      expect(result.code).toBe(1);
      expect(result.stdout).toBe('');
      expect(await readFile(path, 'utf8')).toBe('competing output');
    } finally {
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  });

  it('recursively copies directory contents including empty directories, preserving extra entries', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-tree-'));
    const fake = await startFakeTcpAgent({});
    const env = {
      AGENT_ROVER_HOST: fake.host,
      AGENT_ROVER_PORT: String(fake.port),
    };
    const observer = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    const source = join(directory, 'source');
    const target = join(directory, 'target');
    try {
      await mkdir(join(source, 'deep', '日本語 folder'), { recursive: true });
      await mkdir(join(source, 'empty'));
      await writeFile(
        join(source, 'deep', '日本語 folder', 'data.bin'),
        Buffer.from([0, 255, 1])
      );
      await writeFile(join(source, 'zero'), Buffer.alloc(0));
      await mkdir(target);
      await writeFile(join(target, 'local-extra'), 'local keeper');
      await observer.files.writeFile(
        'C:/tree/remote-extra',
        Buffer.from('remote keeper')
      );
      const put = await runArctl(
        ['put', '-r', source + '/', 'C:/tree/', '--json'],
        env
      );
      expect(put.code, put.stderr).toBe(0);
      expect(JSON.parse(put.stdout).result).toMatchObject({
        files: 2,
        directories: 4,
        bytes: 3,
      });
      expect((await observer.files.stat('C:/tree/empty')).type).toBe(
        'directory'
      );
      expect(
        await observer.files.readFile('C:/tree/deep/日本語 folder/data.bin')
      ).toEqual(Buffer.from([0, 255, 1]));
      expect(
        (await observer.files.readFile('C:/tree/remote-extra')).toString()
      ).toBe('remote keeper');
      const get = await runArctl(
        ['get', 'C:/tree/', target + '/', '-r', '--json'],
        env
      );
      expect(get.code, get.stderr).toBe(0);
      expect(JSON.parse(get.stdout).result).toMatchObject({
        files: 3,
        directories: 4,
        bytes: 16,
      });
      expect((await stat(join(target, 'empty'))).isDirectory()).toBe(true);
      expect(
        await readFile(join(target, 'deep', '日本語 folder', 'data.bin'))
      ).toEqual(Buffer.from([0, 255, 1]));
      expect(await readFile(join(target, 'local-extra'), 'utf8')).toBe(
        'local keeper'
      );
      expect(await readFile(join(target, 'remote-extra'), 'utf8')).toBe(
        'remote keeper'
      );
      for (const args of [
        ['put', '-r', join(source, 'zero'), 'C:/bad'],
        ['get', '-r', 'C:/tree/zero', join(target, 'bad')],
      ]) {
        expect((await runArctl(args, env)).code).toBe(1);
      }
    } finally {
      observer.release();
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  });

  it.each(['put', 'get'] as const)(
    'keeps completed copies after a later %s failure without deleting or killing',
    async (command) => {
      const directory = await mkdtemp(join(tmpdir(), 'arctl-partial-'));
      let enabled = false;
      const forbidden: string[] = [];
      const fake = await startFakeTcpAgent({
        beforeRequest: (method, params) => {
          if (method.includes('remove') || method.includes('kill'))
            forbidden.push(method);
          const path =
            typeof params === 'object' && params !== null && 'path' in params
              ? String(params.path).replaceAll('\\', '/')
              : '';
          if (
            enabled &&
            method === (command === 'put' ? 'file.write' : 'file.read') &&
            path.endsWith('/z-failure')
          )
            return {
              code: 'OPERATION_FAILED',
              message: 'Injected transfer failure',
            };
          return undefined;
        },
      });
      const observer = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
      });
      try {
        await writeFile(join(directory, 'a-first'), 'completed');
        await writeFile(join(directory, 'z-failure'), 'not copied');
        await observer.files.writeFile(
          'C:/tree/a-first',
          Buffer.from('completed')
        );
        await observer.files.writeFile(
          'C:/tree/z-failure',
          Buffer.from('not copied')
        );
        enabled = true;
        const args =
          command === 'put'
            ? ['put', '-r', directory, 'C:/target']
            : ['get', '-r', 'C:/tree', join(directory, 'target')];
        const result = await runArctl(args, {
          AGENT_ROVER_HOST: fake.host,
          AGENT_ROVER_PORT: String(fake.port),
        });
        expect(result.code).toBe(1);
        expect(result.stdout).toBe('');
        expect(result.stderr).toContain('Injected transfer failure');
        expect(
          command === 'put'
            ? (await observer.files.readFile('C:/target/a-first')).toString()
            : await readFile(join(directory, 'target', 'a-first'), 'utf8')
        ).toBe('completed');
        expect(forbidden).toEqual([]);
      } finally {
        observer.release();
        await fake.close();
        await rm(directory, { recursive: true, force: true });
      }
    }
  );

  it.each(['put', 'get'] as const)(
    'preserves both types of nested conflict during recursive %s',
    async (command) => {
      const directory = await mkdtemp(join(tmpdir(), 'arctl-tree-collision-'));
      const fake = await startFakeTcpAgent({});
      const observer = await connectRemoteAgent({
        host: fake.host,
        port: fake.port,
      });
      const env = {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
      };
      try {
        for (const sourceIsDirectory of [true, false]) {
          const name = sourceIsDirectory ? 'directory-source' : 'file-source';
          const local = join(directory, name);
          const remote = `C:/${name}`;
          await mkdir(local);
          if ((command === 'put') === sourceIsDirectory)
            await mkdir(join(local, 'item'));
          else await writeFile(join(local, 'item'), 'local keeper');
          if ((command === 'get') === sourceIsDirectory)
            await observer.files.mkdir(`${remote}/item`, { recursive: true });
          else
            await observer.files.writeFile(
              `${remote}/item`,
              Buffer.from('remote keeper')
            );
          const args =
            command === 'put'
              ? ['put', '-r', local, remote]
              : ['get', '-r', remote, local];
          const result = await runArctl(args, env);
          expect(result.code, result.stderr).toBe(1);
          expect((await stat(join(local, 'item'))).isDirectory()).toBe(
            (command === 'put') === sourceIsDirectory
          );
          expect((await observer.files.stat(`${remote}/item`)).type).toBe(
            (command === 'get') === sourceIsDirectory ? 'directory' : 'file'
          );
          if ((command === 'put') !== sourceIsDirectory)
            expect(await readFile(join(local, 'item'), 'utf8')).toBe(
              'local keeper'
            );
          if ((command === 'get') !== sourceIsDirectory)
            expect(
              (await observer.files.readFile(`${remote}/item`)).toString()
            ).toBe('remote keeper');
        }
      } finally {
        observer.release();
        await fake.close();
        await rm(directory, { recursive: true, force: true });
      }
    }
  );

  it.each([Buffer.alloc(0), Buffer.from([0, 255, 10, 128, 0, 1])])(
    'round-trips and overwrites a single file (%j)',
    async (data) => {
      const directory = await mkdtemp(join(tmpdir(), 'arctl-file-'));
      const fake = await startFakeTcpAgent({});
      const env = {
        AGENT_ROVER_HOST: fake.host,
        AGENT_ROVER_PORT: String(fake.port),
      };
      const source = join(directory, '日本語 source.bin');
      const target = join(directory, 'download', 'nested', 'result.bin');
      const remote = 'C:\\arctl\\deep\\data.bin';
      try {
        await writeFile(source, Buffer.from('old'));
        expect((await runArctl(['put', source, remote], env)).code).toBe(0);
        await writeFile(source, data);
        const uploaded = await runArctl(['put', source, remote, '--json'], env);
        expect(uploaded.code, uploaded.stderr).toBe(0);
        expect(JSON.parse(uploaded.stdout)).toEqual({
          command: 'put',
          result: {
            source,
            destination: remote,
            files: 1,
            directories: 0,
            bytes: data.length,
          },
        });
        const downloaded = await runArctl(
          ['get', remote, target, '--json'],
          env
        );
        expect(downloaded.code, downloaded.stderr).toBe(0);
        expect(JSON.parse(downloaded.stdout).result).toEqual({
          source: remote,
          destination: target,
          files: 1,
          directories: 0,
          bytes: data.length,
        });
        expect(await readFile(target)).toEqual(data);
        await writeFile(target, 'old local contents');
        expect((await runArctl(['get', remote, target], env)).code).toBe(0);
        expect(await readFile(target)).toEqual(data);
      } finally {
        await fake.close();
        await rm(directory, { recursive: true, force: true });
      }
    }
  );

  it('rejects file/directory collisions and preserves the destination in both directions', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'arctl-file-collision-'));
    const fake = await startFakeTcpAgent({});
    const env = {
      AGENT_ROVER_HOST: fake.host,
      AGENT_ROVER_PORT: String(fake.port),
    };
    const observer = await connectRemoteAgent({
      host: fake.host,
      port: fake.port,
    });
    try {
      const source = join(directory, 'source');
      const destination = join(directory, 'directory');
      await writeFile(source, 'source bytes');
      await mkdir(destination);
      await writeFile(join(destination, 'keep'), 'local keeper');
      await observer.files.writeFile('C:/file', Buffer.from('remote file'));
      await observer.files.writeFile(
        'C:/directory/keep',
        Buffer.from('remote keeper')
      );
      for (const args of [
        ['put', source, 'C:/directory'],
        ['get', 'C:/file', destination],
        ['put', destination, 'C:/file'],
        ['get', 'C:/directory', source],
      ]) {
        const result = await runArctl(args, env);
        expect(result.code, JSON.stringify(args)).toBe(1);
        expect(result.stdout).toBe('');
        expect(result.stderr).not.toBe('');
      }
      expect(await readFile(join(destination, 'keep'), 'utf8')).toBe(
        'local keeper'
      );
      expect(await readFile(source, 'utf8')).toBe('source bytes');
      expect(
        (await observer.files.readFile('C:/directory/keep')).toString()
      ).toBe('remote keeper');
      expect((await observer.files.readFile('C:/file')).toString()).toBe(
        'remote file'
      );
    } finally {
      observer.release();
      await fake.close();
      await rm(directory, { recursive: true, force: true });
    }
  });

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
