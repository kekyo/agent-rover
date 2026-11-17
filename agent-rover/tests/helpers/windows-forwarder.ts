// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { randomBytes } from 'node:crypto';
import { createServer, isIP, type Socket } from 'node:net';
import type { WindowsBootstrap } from './windows-bootstrap';

/** One test connection forwarded through an outbound Windows PowerShell connection. */
export interface WindowsForwarder {
  /** Loopback host for the SDK. */
  readonly host: string;
  /** Ephemeral loopback port for the SDK. */
  readonly port: number;
  /** Closes sockets and releases the remote helper. */
  readonly close: () => Promise<void>;
}

/**
 * Creates a single authenticated reverse connection without changing Windows firewall rules.
 * @param bootstrap Existing deployment session.
 * @param reverseHost Test-host IP reachable from Windows.
 * @param targetPort Agent port on Windows loopback.
 * @returns A local endpoint and cleanup operation.
 * @remarks Requires Windows PowerShell with .NET 4.5 or newer; only used by Windows 11 tests.
 */
export const createWindowsForwarder = async (
  bootstrap: WindowsBootstrap,
  reverseHost: string,
  targetPort: number
): Promise<WindowsForwarder> => {
  if (
    !isIP(reverseHost) ||
    !Number.isInteger(targetPort) ||
    targetPort < 1 ||
    targetPort > 65535
  )
    throw new Error('Invalid Windows forwarder endpoint.');
  const nonce = randomBytes(32).toString('hex');
  const sockets = new Set<Socket>();
  let accept: (socket: Socket) => void = () => {};
  const ready = new Promise<Socket>((resolve) => {
    accept = resolve;
  });
  let connected = false;
  const reverse = createServer((socket) => {
    sockets.add(socket);
    socket.on('error', () => socket.destroy());
    socket.on('close', () => sockets.delete(socket));
    socket.setTimeout(10000, () => socket.destroy());
    let header = Buffer.alloc(0);
    const receive = (data: Buffer): void => {
      header = Buffer.concat([header, data]);
      if (header.length < nonce.length) return;
      if (
        connected ||
        header.subarray(0, nonce.length).toString('ascii') !== nonce
      ) {
        socket.destroy();
        return;
      }
      connected = true;
      socket.pause();
      socket.off('data', receive);
      socket.setTimeout(0);
      if (header.length > nonce.length)
        socket.unshift(header.subarray(nonce.length));
      accept(socket);
    };
    socket.on('data', receive);
  });
  const local = createServer();
  let deployment: { managedProcessId: number } | undefined;
  const close = async (): Promise<void> => {
    for (const socket of sockets) socket.destroy();
    for (const server of [local, reverse]) {
      if (server.listening)
        await new Promise<void>((resolve, reject) => {
          server.close((error) => (error ? reject(error) : resolve()));
        });
    }
    if (deployment !== undefined)
      await bootstrap.releaseManaged(deployment.managedProcessId);
  };
  try {
    await new Promise<void>((resolve, reject) => {
      reverse.once('error', reject);
      reverse.listen(0, reverseHost, resolve);
    });
    const address = reverse.address();
    if (address === null || typeof address === 'string')
      throw new Error('Missing reverse endpoint.');
    // CopyToAsync uses asynchronous stream I/O in both directions. Only this
    // helper waits; it cannot block the agent's dispatcher or GUI.
    const script = `
$ErrorActionPreference = 'Stop'
$remote = New-Object System.Net.Sockets.TcpClient
$target = New-Object System.Net.Sockets.TcpClient
try {
  $target.Connect('127.0.0.1', ${targetPort})
  $remote.Connect('${reverseHost}', ${address.port})
  $a = $remote.GetStream()
  $b = $target.GetStream()
  $nonce = [Text.Encoding]::ASCII.GetBytes('${nonce}')
  $a.Write($nonce, 0, $nonce.Length)
  $outbound = $b.CopyToAsync($a)
  $inbound = $a.CopyToAsync($b)
  [Threading.Tasks.Task]::WhenAny([Threading.Tasks.Task[]]@($outbound, $inbound)).GetAwaiter().GetResult() | Out-Null
} finally { $remote.Close(); $target.Close() }
`;
    deployment = (await bootstrap.request('process.launchManaged', {
      path: 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe',
      arguments: [
        '-NoProfile',
        '-NonInteractive',
        '-EncodedCommand',
        Buffer.from(script, 'utf16le').toString('base64'),
      ],
      createNoWindow: true,
      killTreeOnRelease: true,
    })) as { managedProcessId: number };
    let timer: ReturnType<typeof setTimeout> | undefined;
    let remote: Socket;
    try {
      remote = await Promise.race([
        ready,
        new Promise<never>((_, reject) => {
          timer = setTimeout(
            () => reject(new Error('Windows reverse connection timed out.')),
            30000
          );
        }),
      ]);
    } finally {
      clearTimeout(timer);
    }
    local.once('connection', (socket) => {
      sockets.add(socket);
      socket.on('error', () => socket.destroy());
      socket.on('close', () => {
        sockets.delete(socket);
        remote.destroy();
      });
      remote.on('close', () => socket.destroy());
      remote.pipe(socket);
      socket.pipe(remote);
      local.close();
    });
    await new Promise<void>((resolve, reject) => {
      local.once('error', reject);
      local.listen(0, '127.0.0.1', resolve);
    });
    const endpoint = local.address();
    if (endpoint === null || typeof endpoint === 'string')
      throw new Error('Missing local endpoint.');
    return { host: '127.0.0.1', port: endpoint.port, close };
  } catch (error) {
    await close();
    throw error;
  }
};
