// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import {
  connectRemoteAgent,
  saveDiagnostics,
  withDiagnostics,
} from '../src/index';
import { defaultFakeWindow, startFakeTcpAgent } from './helpers/fake-tcp-agent';

describe.concurrent('diagnostics artifacts', () => {
  it('captures and saves remote diagnostics without sensitive protocol data', async () => {
    const authToken = 'secret-auth-token';
    const fakeAgent = await startFakeTcpAgent({
      authToken,
      eventLogs: [
        {
          id: 10,
          level: 'Information',
          message: 'diagnostic event',
          provider: 'agent-rover',
          timestamp: '2026-06-25T00:00:00.000Z',
        },
      ],
      windows: [
        {
          ...defaultFakeWindow,
          active: true,
          focused: true,
        },
      ],
    });
    const outputDirectory = await mkdtemp(join(tmpdir(), 'agent-rover-diag-'));

    const agent = await connectRemoteAgent({
      authToken,
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.keyboard.press('A');
      await agent.files.writeFile(
        'C:/agent-rover/payload.bin',
        Buffer.from('secret-binary-payload')
      );

      const capture = await agent.diagnostics.capture({
        eventLogs: {
          maxEntries: 1,
        },
        includeDescendants: true,
      });
      const artifacts = await saveDiagnostics(outputDirectory, {
        capture,
      });

      expect(capture.activeWindow).toMatchObject({
        id: defaultFakeWindow.id,
        title: defaultFakeWindow.title,
      });
      expect(capture.windows[0]?.children?.[0]).toMatchObject({
        className: 'Button',
        title: 'OK',
      });
      expect(capture.eventLogs).toHaveLength(1);
      expect(capture.inputOperations).toContainEqual({
        key: 'A',
        kind: 'keyboard.press',
        modifiers: [],
      });
      expect(capture.protocolOperations.map((entry) => entry.method)).toEqual(
        expect.arrayContaining([
          'agent.screenshot',
          'agent.windows',
          'file.write',
          'input.perform',
          'window.children',
        ])
      );

      const manifest = JSON.parse(
        await readFile(artifacts.manifestPath, 'utf8')
      ) as {
        readonly artifacts: readonly {
          readonly contentType: string;
          readonly kind: string;
          readonly path: string;
        }[];
      };
      expect(manifest.artifacts).toContainEqual({
        contentType: 'image/png',
        kind: 'screenScreenshot',
        path: 'screen.png',
      });
      await expect(stat(artifacts.screenshotPath)).resolves.toMatchObject({
        size: capture.screenshot.image.byteLength,
      });

      const protocolTrace = await readFile(
        join(outputDirectory, 'protocol-trace.json'),
        'utf8'
      );
      expect(protocolTrace).not.toContain(authToken);
      expect(protocolTrace).not.toContain('dataBase64');
      expect(protocolTrace).not.toContain('secret-binary-payload');
    } finally {
      agent.release();
      await fakeAgent.close();
      await rm(outputDirectory, {
        force: true,
        recursive: true,
      });
    }
  });

  it('saves diagnostics on failure and rethrows the original error', async () => {
    const fakeAgent = await startFakeTcpAgent({});
    const outputDirectory = await mkdtemp(join(tmpdir(), 'agent-rover-diag-'));
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const expectedError = new Error('assertion failed');
      await expect(
        withDiagnostics(agent, outputDirectory, async () => {
          throw expectedError;
        })
      ).rejects.toBe(expectedError);

      await expect(
        stat(join(outputDirectory, 'manifest.json'))
      ).resolves.toBeDefined();
    } finally {
      agent.release();
      await fakeAgent.close();
      await rm(outputDirectory, {
        force: true,
        recursive: true,
      });
    }
  });

  it('saves remote files, remote directories, and managed process output as attachments', async () => {
    const fakeAgent = await startFakeTcpAgent({
      launchedStderr: 'stderr text',
      launchedStdout: 'stdout text',
    });
    const outputDirectory = await mkdtemp(join(tmpdir(), 'agent-rover-diag-'));
    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      await agent.files.writeFile(
        'C:/artifacts/muon.json',
        Buffer.from('{"name":"muon"}')
      );
      await agent.files.writeFile(
        'C:/artifacts/profile/log.txt',
        Buffer.from('profile log')
      );
      const process = await agent.processes.launchManaged({
        captureStderr: true,
        captureStdout: true,
        path: 'C:/artifacts/muon.exe',
      });

      const result = await saveDiagnostics(outputDirectory, {
        agent,
        attachments: [
          {
            kind: 'remoteFile',
            name: 'muon-json',
            path: 'C:/artifacts/muon.json',
          },
          {
            kind: 'remoteDirectory',
            name: 'profile',
            path: 'C:/artifacts/profile',
          },
          {
            kind: 'managedProcess',
            name: 'muon-process',
            process,
          },
          {
            kind: 'remoteFile',
            name: 'optional-missing',
            optional: true,
            path: 'C:/artifacts/missing.txt',
          },
        ],
      });

      const manifest = JSON.parse(
        await readFile(result.manifestPath, 'utf8')
      ) as {
        readonly attachmentErrors: readonly { readonly name: string }[];
        readonly artifacts: readonly { readonly path: string }[];
      };
      expect(manifest.artifacts.map((artifact) => artifact.path)).toEqual(
        expect.arrayContaining([
          'attachments/muon-json/muon.json',
          'attachments/profile/log.txt',
          'attachments/muon-process/stdout.txt',
          'attachments/muon-process/stderr.txt',
        ])
      );
      expect(manifest.attachmentErrors.map((error) => error.name)).toEqual([
        'optional-missing',
      ]);
      await expect(
        readFile(
          join(outputDirectory, 'attachments', 'muon-json', 'muon.json'),
          'utf8'
        )
      ).resolves.toBe('{"name":"muon"}');
      await expect(
        readFile(
          join(outputDirectory, 'attachments', 'profile', 'log.txt'),
          'utf8'
        )
      ).resolves.toBe('profile log');
      await expect(
        readFile(
          join(outputDirectory, 'attachments', 'muon-process', 'stdout.txt'),
          'utf8'
        )
      ).resolves.toBe('stdout text');
      await expect(
        readFile(
          join(outputDirectory, 'attachments', 'muon-process', 'stderr.txt'),
          'utf8'
        )
      ).resolves.toBe('stderr text');
    } finally {
      agent.release();
      await fakeAgent.close();
      await rm(outputDirectory, {
        force: true,
        recursive: true,
      });
    }
  });
});
