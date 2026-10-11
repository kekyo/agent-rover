// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { readFile } from 'node:fs/promises';
import { isIP } from 'node:net';
import { homedir } from 'node:os';
import { join } from 'node:path';
import type { RemoteAgentError } from '../index';
import { createCredentialRedactor } from '../auth';

/** One configured address and its authentication credential. */
export interface ConnectionEndpoint {
  /** DNS name or unbracketed IP address. */
  readonly host: string;
  /** TCP port from 1 through 65535. */
  readonly port: number;
  /** Undefined for explicitly unauthenticated endpoints. */
  readonly authToken: string | undefined;
}

/** A snapshot of a selected section and its output filter. */
export interface ConnectionSection {
  /** Case-sensitive section name. */
  readonly name: string;
  /** Candidates in declaration order, before shuffling. */
  readonly endpoints: readonly ConnectionEndpoint[];
  /** Removes credentials loaded from the configuration from diagnostic text. */
  readonly redact: (text: string) => string;
}

const configurationError = (message: string): RemoteAgentError =>
  Object.assign(new Error(message), { code: 'INVALID_ARGUMENT' as const });

/**
 * Parses the small section/key=value dialect used by agent-rover.
 * @param text UTF-8 configuration text, optionally starting with a BOM.
 * @param source File name used only in diagnostics.
 * @returns Literal section names and their endpoint/credential pairs.
 * @remarks Values are never unquoted, expanded, or converted to other types.
 * Errors include line numbers without echoing configuration contents.
 */
export const parseConnectionConfiguration = (
  text: string,
  source: string
): ReadonlyMap<string, readonly ConnectionEndpoint[]> => {
  const sections = new Map<string, ConnectionEndpoint[]>();
  let endpoints: ConnectionEndpoint[] | undefined;
  let seen = new Set<string>();
  const lines = text.replace(/^\uFEFF/u, '').split(/\r?\n/u);
  for (const [index, rawLine] of lines.entries()) {
    const line = rawLine.trim();
    if (line === '' || line.startsWith('#') || line.startsWith(';')) continue;
    const fail = (reason: string): never => {
      throw configurationError(`${source}:${index + 1}: ${reason}`);
    };
    const header = /^\[([^\[\]]*)\]$/u.exec(line);
    if (header !== null) {
      const name = header[1]!.trim();
      if (name === '') fail('Section name must not be empty.');
      if (sections.has(name)) fail('Duplicate section.');
      endpoints = [];
      sections.set(name, endpoints);
      seen = new Set();
      continue;
    }
    if (endpoints === undefined)
      fail('Declare a section before its endpoints.');
    const separator = line.indexOf('=');
    if (separator < 0) fail('Expected host[:port]=token.');
    const address = line.slice(0, separator).trim();
    const token = line.slice(separator + 1).trim();
    const match = address.startsWith('[')
      ? /^\[([^\]]+)\](?::([0-9]+))?$/u.exec(address)
      : /^([^:]+)(?::([0-9]+))?$/u.exec(address);
    if (match === null) fail('Invalid endpoint address.');
    const host = match![1]!;
    if (
      (address.startsWith('[') && isIP(host) !== 6) ||
      /[\s\u0000-\u001f\u007f;,=#@/\\\[\]]/u.test(host)
    )
      fail('Invalid host.');
    const port = match![2] === undefined ? 39397 : Number(match![2]);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      fail('Port must be an integer from 1 through 65535.');
    }
    // Brackets make IPv6 and DNS endpoint identities unambiguous. Credentials
    // are deliberately excluded so conflicting definitions cannot gain weight.
    const identity = `[${host.toLowerCase()}]:${port}`;
    if (seen.has(identity)) fail('Duplicate endpoint in this section.');
    seen.add(identity);
    endpoints!.push({
      host,
      port,
      authToken: token === '' ? undefined : token,
    });
  }
  return sections;
};

/**
 * Reads and validates the first available configuration section.
 * @param paths Configuration files in priority order.
 * @param name Exact name of the section to select.
 * @returns Nonempty candidates and a filter for all credentials in the selected file.
 * @remarks Missing files and missing sections advance to the next file. Empty
 * sections, syntax errors, and other read errors fail immediately. The caller
 * retains this snapshot throughout all retry rounds.
 */
export const readConnectionSection = async (
  paths: readonly string[],
  name: string
): Promise<ConnectionSection> => {
  if (typeof name !== 'string' || name.trim() === '') {
    throw configurationError('section must be a nonempty string.');
  }
  for (const path of paths) {
    let text: string;
    try {
      text = await readFile(path, 'utf8');
    } catch (error) {
      if (
        typeof error === 'object' &&
        error !== null &&
        'code' in error &&
        error.code === 'ENOENT'
      )
        continue;
      throw configurationError(`Cannot read agent configuration: ${path}.`);
    }
    const sections = parseConnectionConfiguration(text, path);
    const endpoints = sections.get(name);
    if (endpoints === undefined) continue;
    if (endpoints.length === 0) {
      throw configurationError(`Selected section is empty in ${path}.`);
    }
    const redact = createCredentialRedactor(
      [...sections.values()].flatMap((entries) =>
        entries.map(({ authToken }) => authToken)
      )
    );
    return { name, endpoints, redact };
  }
  throw configurationError(
    `Selected section ${JSON.stringify(name)} was not found. Searched: ${paths.join(', ')}.`
  );
};

/**
 * Loads a section from .agent-rover in the working directory, then the user's home.
 * @param name Exact section name, normally default.
 * @returns A validated configuration snapshot.
 * @remarks Both directories are captured when this call begins. The first
 * matching section supplies all candidates; files are never merged.
 */
export const loadConnectionSection = async (
  name: string
): Promise<ConnectionSection> =>
  await readConnectionSection(
    [
      ...new Set([
        join(process.cwd(), '.agent-rover'),
        join(homedir(), '.agent-rover'),
      ]),
    ],
    name
  );
