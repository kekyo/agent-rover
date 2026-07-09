// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, describe, expect, it } from 'vitest';

import { connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

const temporaryDirectories: string[] = [];

const createLocalDirectory = async (): Promise<string> => {
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-sync-'));
  temporaryDirectories.push(directory);
  return directory;
};

const writeLocalFile = async (
  root: string,
  relativePath: string,
  text: string
): Promise<void> => {
  const path = join(root, ...relativePath.split('/'));
  await mkdir(join(path, '..'), {
    recursive: true,
  });
  await writeFile(path, text, 'utf8');
};

describe('directory synchronization', () => {
  afterEach(async () => {
    for (const directory of temporaryDirectories.splice(0)) {
      await rm(directory, {
        force: true,
        recursive: true,
      });
    }
  });

  it('uploads missing files, replaces changed files, and skips unchanged files', async () => {
    const localPath = await createLocalDirectory();
    await writeLocalFile(localPath, 'same.txt', 'same');
    await writeLocalFile(localPath, 'changed.txt', 'new');
    await writeLocalFile(localPath, 'nested/missing.txt', 'missing');

    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.mkdir('C:/sync', {
        recursive: true,
      });
      await agent.files.writeFile('C:/sync/same.txt', Buffer.from('same'));
      await agent.files.writeFile('C:/sync/changed.txt', Buffer.from('old'));

      const result = await agent.files.syncDirectory({
        localPath,
        remotePath: 'C:/sync',
      });

      expect(result).toMatchObject({
        deletedDirectories: 0,
        deletedFiles: 0,
        skippedFiles: 1,
        uploadedFiles: 2,
      });
      await expect(agent.files.readFile('C:/sync/same.txt')).resolves.toEqual(
        Buffer.from('same')
      );
      await expect(
        agent.files.readFile('C:/sync/changed.txt')
      ).resolves.toEqual(Buffer.from('new'));
      await expect(
        agent.files.readFile('C:/sync/nested/missing.txt')
      ).resolves.toEqual(Buffer.from('missing'));
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('mirrors local contents while preserving excluded remote paths', async () => {
    const localPath = await createLocalDirectory();
    await writeLocalFile(localPath, 'keep.txt', 'keep');

    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile('C:/sync/keep.txt', Buffer.from('old'));
      await agent.files.writeFile('C:/sync/old.txt', Buffer.from('old'));
      await agent.files.writeFile(
        'C:/sync/old-dir/nested.txt',
        Buffer.from('old')
      );
      await agent.files.writeFile('C:/sync/cache/keep.log', Buffer.from('log'));

      const result = await agent.files.syncDirectory({
        exclude: ['cache/**'],
        localPath,
        remotePath: 'C:/sync',
      });

      expect(result.deletedFiles).toBe(2);
      await expect(agent.files.exists('C:/sync/old.txt')).resolves.toBe(false);
      await expect(agent.files.exists('C:/sync/old-dir')).resolves.toBe(false);
      await expect(agent.files.exists('C:/sync/cache/keep.log')).resolves.toBe(
        true
      );
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('keeps extraneous remote files in update mode', async () => {
    const localPath = await createLocalDirectory();
    await writeLocalFile(localPath, 'keep.txt', 'keep');

    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile('C:/sync/old.txt', Buffer.from('old'));

      await agent.files.syncDirectory({
        localPath,
        mode: 'update',
        remotePath: 'C:/sync',
      });

      await expect(agent.files.exists('C:/sync/old.txt')).resolves.toBe(true);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('kills related processes and retries locked file operations', async () => {
    const localPath = await createLocalDirectory();
    await writeLocalFile(localPath, 'app.dll', 'new');
    const killedProcessIds: number[] = [];
    const fakeAgent = await startFakeTcpAgent({
      initialProcesses: [
        {
          createdAt: '2026-06-25T00:00:00.000Z',
          exitCode: null,
          id: 4001,
          name: 'muon.exe',
          parentProcessId: null,
          path: 'C:/sync/muon.exe',
          running: true,
        },
      ],
      killedProcessIds,
      lockedRenameFailures: {
        'C:/sync/app.dll': 1,
      },
    });
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile('C:/sync/app.dll', Buffer.from('old'));

      await agent.files.syncDirectory({
        localPath,
        onLockedFile: 'killRelatedProcessesAndRetry',
        remotePath: 'C:/sync',
      });

      expect(killedProcessIds).toEqual([4001]);
      await expect(agent.files.readFile('C:/sync/app.dll')).resolves.toEqual(
        Buffer.from('new')
      );
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });

  it('downloads a remote directory and ignores a missing optional source', async () => {
    const localPath = await createLocalDirectory();
    const missingPath = await createLocalDirectory();
    const fakeAgent = await startFakeTcpAgent({});
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile(
        'C:/artifacts/logs/output.txt',
        Buffer.from('out')
      );
      await agent.files.writeFile(
        'C:/artifacts/config.json',
        Buffer.from('{"ok":true}')
      );

      const result = await agent.files.downloadDirectory({
        localPath,
        remotePath: 'C:/artifacts',
      });
      const missing = await agent.files.downloadDirectory({
        ignoreMissing: true,
        localPath: missingPath,
        remotePath: 'C:/does-not-exist',
      });

      expect(result.downloadedFiles).toBe(2);
      expect(missing.downloadedFiles).toBe(0);
      await expect(
        readFile(join(localPath, 'logs', 'output.txt'), 'utf8')
      ).resolves.toBe('out');
      await expect(
        readFile(join(localPath, 'config.json'), 'utf8')
      ).resolves.toBe('{"ok":true}');
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });
});
