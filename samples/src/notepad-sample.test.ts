// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { RemoteAgent } from 'agent-rover';
import { waitForResult } from 'agent-rover/testing';
import { describe, expect, it } from 'vitest';

type WaitForResult = typeof waitForResult;

interface SampleConnection {
  readonly authToken: string;
  readonly host: string;
  readonly port: number;
}

const requiredConnectionEnvironment = [
  'AGENT_ROVER_SAMPLE_AUTH_TOKEN',
  'AGENT_ROVER_SAMPLE_HOST',
  'AGENT_ROVER_SAMPLE_PORT',
] as const;

const remoteDirectory = String.raw`C:\agent-rover\sample`;
const remoteFilePath = String.raw`C:\agent-rover\sample\notepad-sample.txt`;
const initialText = [
  'agent-rover Notepad sample',
  'This file was uploaded before Notepad started.',
].join('\r\n');
const appendedText = 'This line was appended by agent-rover keyboard input.';
const expectedText = `${initialText}\r\n${appendedText}`;

const pad2 = (value: number): string => value.toString().padStart(2, '0');

const formatTimestamp = (date: Date): string =>
  `${String(date.getFullYear())}${pad2(date.getMonth() + 1)}${pad2(
    date.getDate()
  )}_${pad2(date.getHours())}${pad2(date.getMinutes())}${pad2(
    date.getSeconds()
  )}`;

const sampleRootDirectory = (): string =>
  resolve(dirname(fileURLToPath(import.meta.url)), '..');

const hasConnectionEnvironment = (): boolean =>
  requiredConnectionEnvironment.every((name) => {
    const value = process.env[name];
    return value !== undefined && value.length >= 1;
  });

const parseConnection = (): SampleConnection => {
  const authToken = process.env.AGENT_ROVER_SAMPLE_AUTH_TOKEN;
  const host = process.env.AGENT_ROVER_SAMPLE_HOST;
  const portText = process.env.AGENT_ROVER_SAMPLE_PORT;

  if (
    authToken === undefined ||
    authToken.length < 1 ||
    host === undefined ||
    host.length < 1 ||
    portText === undefined ||
    portText.length < 1
  ) {
    throw new Error(
      `Set ${requiredConnectionEnvironment.join(', ')} to run this sample.`
    );
  }

  const port = Number.parseInt(portText, 10);
  if (!Number.isSafeInteger(port) || port < 1) {
    throw new Error('AGENT_ROVER_SAMPLE_PORT must be a positive integer.');
  }

  return {
    authToken,
    host,
    port,
  };
};

const waitForRemoteFile = async (
  agent: RemoteAgent,
  path: string,
  expected: string,
  waitForResult: WaitForResult
): Promise<Buffer> => {
  return await waitForResult(
    async () => {
      const receivedFile = await agent.files.readFile(path);
      expect(receivedFile.toString('utf8')).toBe(expected);
      return receivedFile;
    },
    {
      intervalMs: 250,
      message: 'Timed out waiting for Notepad to save the file.',
      timeoutMs: 5000,
    }
  );
};

const remoteAgentIt = hasConnectionEnvironment() ? it : it.skip;

describe('notepad sample', () => {
  remoteAgentIt('edits a remote Notepad file through agent-rover', async () => {
    const connection = parseConnection();
    const [{ connectRemoteAgent }, { expectCapture, waitForResult }] =
      await Promise.all([import('agent-rover'), import('agent-rover/testing')]);

    const outputDirectory = join(
      sampleRootDirectory(),
      'test-results',
      formatTimestamp(new Date())
    );
    await mkdir(outputDirectory, {
      recursive: true,
    });

    const agent = await connectRemoteAgent({
      host: connection.host,
      port: connection.port,
      authToken: connection.authToken,
      timeoutMs: 30000,
    });

    try {
      await agent.files.mkdir(remoteDirectory, {
        recursive: true,
      });
      await agent.files.writeFile(remoteFilePath, Buffer.from(initialText));
      const launchedProcess = await agent.applications.launch({
        arguments: [remoteFilePath],
        path: 'notepad.exe',
        workingDirectory: remoteDirectory,
      });
      const notepadWindow = await agent.waitForWindow(
        {
          processId: launchedProcess.id,
          visible: true,
        },
        {
          intervalMs: 500,
          message: 'Timed out waiting for Notepad window.',
          timeoutMs: 15000,
        }
      );

      await notepadWindow.activate();
      await agent.keyboard.press('End', {
        modifiers: ['Control'],
      });
      await agent.keyboard.press('Enter');
      await agent.keyboard.pasteText(appendedText);

      const screenshot = await notepadWindow.screenshot();
      await writeFile(join(outputDirectory, 'capture.png'), screenshot.image);
      if (process.env.AGENT_ROVER_SAMPLE_EXPECTED_CAPTURE !== undefined) {
        await expectCapture(screenshot, 'notepad-window').toLookSimilar(
          await readFile(process.env.AGENT_ROVER_SAMPLE_EXPECTED_CAPTURE),
          {
            maxDiffRatio: 0.01,
            outputResultPath: outputDirectory,
            threshold: 0.1,
            variant: 'sample',
          }
        );
      }

      await agent.keyboard.press('s', {
        modifiers: ['Control'],
      });

      const receivedFile = await waitForRemoteFile(
        agent,
        remoteFilePath,
        expectedText,
        waitForResult
      );
      await writeFile(join(outputDirectory, 'received.txt'), receivedFile);

      process.stdout.write(`Artifacts: ${outputDirectory}\n`);
    } finally {
      agent.release();
    }
  });
});
