#!/usr/bin/env node
// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import type { RemoteAgent } from './index';
import { connectRemoteAgent } from './driver/connection';
import { version } from './generated/packageMetadata';
import { parseArctlArguments } from './cli/arguments';
import { launchArctlApplication, listArctlWindows } from './cli/commands';
import { transferArctlFiles } from './cli/files';
import { captureArctlScreenshot } from './cli/capture';

const main = async (): Promise<number> => {
  const args = process.argv.slice(2);
  let agent: RemoteAgent | undefined;
  let executing = false;
  let interrupted = false;
  const interruption = new AbortController();
  const secrets = [process.env.AGENT_ROVER_AUTH_TOKEN];
  for (let index = 0; index < args.length; ++index) {
    const arg = args[index];
    if (arg === '--') break;
    if (arg === '--token') secrets.push(args[index + 1]);
    else if (arg?.startsWith('--token='))
      secrets.push(arg.slice('--token='.length));
  }
  const redact = (text: string): string => {
    for (const secret of secrets) {
      if (secret) text = text.replaceAll(secret, '[redacted]');
    }
    return text;
  };
  const onInterrupt = (): void => {
    interrupted = true;
    interruption.abort();
    if (agent === undefined) {
      // No operation or temporary transfer exists during the handshake. Ending
      // the CLI closes its pending socket even before the SDK returns a handle.
      process.stderr.write('arctl: Interrupted.\n', () => {
        process.exit(130);
      });
    } else {
      agent.release();
    }
  };
  process.on('SIGINT', onInterrupt);
  try {
    const parsed = parseArctlArguments(args, process.env, version);
    if (parsed.action === 'display') {
      process.stdout.write(parsed.text);
      return 0;
    }
    executing = true;
    agent = await connectRemoteAgent(parsed.connection);
    if (interrupted) return 130;
    if (parsed.command === 'screenshot') {
      const result = await captureArctlScreenshot(
        agent,
        parsed.output,
        parsed.window,
        interruption.signal
      );
      if (interrupted) return 130;
      process.stdout.write(
        parsed.json
          ? `${JSON.stringify({ command: parsed.command, result })}\n`
          : `Saved PNG: ${result.path}\n`
      );
      return 0;
    }
    if (parsed.command === 'put' || parsed.command === 'get') {
      const result = await transferArctlFiles(
        agent,
        parsed.command,
        parsed.source,
        parsed.destination,
        parsed.recursive,
        interruption.signal
      );
      if (interrupted) return 130;
      process.stdout.write(
        parsed.json
          ? `${JSON.stringify({ command: parsed.command, result })}\n`
          : `${result.source} -> ${result.destination}\n${result.files} files, ${result.directories} directories, ${result.bytes} bytes\n`
      );
      return 0;
    }
    if (parsed.command === 'launch') {
      const result = await launchArctlApplication(agent, parsed.options);
      if (interrupted) return 130;
      process.stdout.write(
        parsed.json
          ? `${JSON.stringify({ command: parsed.command, result })}\n`
          : `Started ${result.pid}\t${result.name}\n`
      );
      return 0;
    }
    const result = await listArctlWindows(agent);
    if (interrupted) return 130;
    process.stdout.write(
      parsed.json
        ? `${JSON.stringify({ command: parsed.command, result })}\n`
        : [
            'ID\tPID\tPROCESS\tTITLE',
            ...result.map(
              (window) =>
                `${window.id}\t${window.pid}\t${window.processName}\t${window.title.replace(/[\r\n\t]/gu, ' ')}`
            ),
          ].join('\n') + '\n'
    );
    return 0;
  } catch (error) {
    if (interrupted) {
      process.stderr.write('arctl: Interrupted.\n');
      return 130;
    }
    const code =
      typeof error === 'object' && error !== null && 'code' in error
        ? String(error.code)
        : undefined;
    const message = error instanceof Error ? error.message : String(error);
    const uncertain =
      executing && (code === 'DISCONNECTED' || code === 'TIMEOUT');
    process.stderr.write(
      redact(
        `arctl: ${code === undefined ? '' : `${code}: `}${message}${uncertain ? ' Operation outcome is unknown; the request was not retried.' : ''}\n`
      )
    );
    return executing ? 1 : 2;
  } finally {
    agent?.release();
    process.off('SIGINT', onInterrupt);
  }
};

process.exitCode = await main();
