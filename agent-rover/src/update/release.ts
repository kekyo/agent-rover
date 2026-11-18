// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// agent-rover - Select a Windows executable from a GitHub release
// Copyright (c) Kouji Matsui. Under MIT.
import { createHash } from 'node:crypto';
import { unzipSync } from 'fflate';
import legacy from './legacy-releases.json';

/** CPU of the running native agent. */
export type AgentArchitecture = 'amd64' | 'i686';
const repository = 'kekyo/agent-rover';
const maxArchive = 128 * 1024 * 1024;
const maxImage = 64 * 1024 * 1024;
const hash = (bytes: Uint8Array) =>
  createHash('sha256').update(bytes).digest('hex');
const record = (v: unknown): v is Record<string, unknown> =>
  typeof v === 'object' && v !== null && !Array.isArray(v);

/** Validates an exact release tag and compares its three numeric components.
 * @param left First release. @param right Second release. @returns -1, 0 or 1. */
export const compareVersions = (left: string, right: string): number => {
  const parts = (v: string) => {
    if (v.length > 60 || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/u.test(v))
      throw new Error('Expected an exact release version such as 0.10.0.');
    return v.split('.').map((part) => BigInt(part));
  };
  const a = parts(left),
    b = parts(right);
  for (const [index, component] of a.entries()) {
    if (component !== b[index]) return component < b[index]! ? -1 : 1;
  }
  return 0;
};

const extractEntry = (zip: Buffer, names: string[], limit: number): Buffer => {
  let matches = 0;
  const entries = unzipSync(zip, {
    filter: (entry) => {
      if (!names.includes(entry.name)) return false;
      if (
        ++matches !== 1 ||
        entry.originalSize <= 0 ||
        entry.originalSize > limit ||
        entry.size > zip.length ||
        (entry.compression !== 0 && entry.compression !== 8)
      )
        throw new Error(
          'Duplicate, oversized or unsupported release entry: ' + entry.name
        );
      return true;
    },
  });
  if (matches !== 1) throw new Error('Missing release entry: ' + names[0]);
  return Buffer.from(Object.values(entries)[0]!);
};

/** Extracts one bounded PE image from the archive, without writing any archive paths.
 * @param zip Entire release ZIP. @param architecture Running agent CPU. @returns Validated PE image. */
export const extractExecutable = (
  zip: Buffer,
  architecture: AgentArchitecture
): Buffer => {
  const name = 'agent-' + architecture + '.exe';
  const data = extractEntry(zip, [name, 'dist/' + name], maxImage);
  const pe = data.length >= 64 ? data.readUInt32LE(60) : -1;
  if (
    data.toString('latin1', 0, 2) !== 'MZ' ||
    pe < 64 ||
    pe > data.length - 26 ||
    data.readUInt32LE(pe) !== 0x4550 ||
    data.readUInt16LE(pe + 4) !== (architecture === 'amd64' ? 0x8664 : 0x14c) ||
    (data.readUInt16LE(pe + 22) & 0x2002) !== 2 ||
    data.readUInt16LE(pe + 24) !== (architecture === 'amd64' ? 0x20b : 0x10b)
  )
    throw new Error('Expected a Windows ' + architecture + ' executable.');
  return data;
};

/** Selects the verified executable and its release metadata.
 * @param version Expected tag. @param architecture Running CPU. @param zip Release ZIP.
 * @param digest GitHub asset digest. @param historicalManifest Previously recorded legacy release, if present.
 * @returns Verified bytes and the information needed by the updater. */
export const checkRelease = (
  version: string,
  architecture: AgentArchitecture,
  zip: Buffer,
  digest: string,
  historicalManifest: unknown
) => {
  if (zip.length > maxArchive || digest !== 'sha256:' + hash(zip))
    throw new Error('Release ZIP SHA-256 mismatch.');
  const manifest: unknown =
    historicalManifest ??
    JSON.parse(extractEntry(zip, ['agent-update.json'], 8192).toString('utf8'));
  if (
    !record(manifest) ||
    manifest.repository !== repository ||
    manifest.version !== version ||
    ('digest' in manifest && manifest.digest !== digest) ||
    !record(manifest.binaries) ||
    typeof manifest.gitCommit !== 'string' ||
    !/^[a-f0-9]{40}$/u.test(manifest.gitCommit) ||
    (manifest.updateProtocol !== 0 && manifest.updateProtocol !== 1)
  )
    throw new Error('Release metadata does not match the requested agent.');
  const binary = manifest.binaries[architecture];
  const executable = extractExecutable(zip, architecture);
  if (
    !record(binary) ||
    binary.bytes !== executable.length ||
    binary.sha256 !== hash(executable)
  )
    throw new Error('Release executable size or SHA-256 mismatch.');
  return {
    version,
    architecture,
    executable,
    sha256: binary.sha256 as string,
    gitCommit: manifest.gitCommit,
    updateProtocol: manifest.updateProtocol,
  };
};

/** A release checked against its GitHub asset digest. */
export type VerifiedAgentRelease = ReturnType<typeof checkRelease>;

const download = async (
  url: URL,
  limit: number,
  signal: AbortSignal
): Promise<Buffer> => {
  for (let redirects = 0; redirects <= 8; redirects++) {
    if (url.protocol !== 'https:')
      throw new Error('Release downloads require HTTPS.');
    const response = await fetch(url, {
      redirect: 'manual',
      signal,
      headers: {
        'User-Agent': 'agent-rover-arctl',
        Accept: 'application/json, application/octet-stream',
      },
    });
    if (
      response.status >= 300 &&
      response.status < 400 &&
      response.headers.has('location')
    ) {
      await response.body?.cancel();
      url = new URL(response.headers.get('location')!, url);
      continue;
    }
    if (!response.ok || !response.body) {
      await response.body?.cancel();
      throw new Error(
        'GitHub release download failed (HTTP ' + response.status + ').'
      );
    }
    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let length = 0;
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        length += value.length;
        if (length > limit)
          throw new Error('Release download exceeds the expected size.');
        chunks.push(value);
      }
    } finally {
      await reader.cancel();
      reader.releaseLock();
    }
    return Buffer.concat(chunks, length);
  }
  throw new Error('Too many GitHub release redirects.');
};

/** Downloads the exact official release and checks its ZIP and executable.
 * @param version Exact tag. @param architecture Selected CPU. @param signal Cancels network reads.
 * @returns Verified executable and release metadata. */
export const fetchAgentRelease = async (
  version: string,
  architecture: AgentArchitecture,
  signal: AbortSignal
): Promise<VerifiedAgentRelease> => {
  compareVersions(version, version);
  const release: unknown = JSON.parse(
    (
      await download(
        new URL(
          'https://api.github.com/repos/' +
            repository +
            '/releases/tags/' +
            version
        ),
        1024 * 1024,
        signal
      )
    ).toString('utf8')
  );
  const name = 'agent-rover-agent-win32-' + version + '.zip';
  if (
    !record(release) ||
    release.tag_name !== version ||
    release.draft === true ||
    !Array.isArray(release.assets)
  )
    throw new Error('GitHub did not return the requested release.');
  const candidates: unknown[] = release.assets.filter(
    (a: unknown) => record(a) && a.name === name
  );
  const asset = candidates[0];
  const url =
    'https://github.com/' +
    repository +
    '/releases/download/' +
    version +
    '/' +
    name;
  if (
    candidates.length !== 1 ||
    !record(asset) ||
    asset.browser_download_url !== url ||
    typeof asset.size !== 'number' ||
    !Number.isSafeInteger(asset.size) ||
    asset.size < 1 ||
    asset.size > maxArchive ||
    typeof asset.digest !== 'string' ||
    !/^sha256:[0-9a-f]{64}$/u.test(asset.digest)
  )
    throw new Error('Missing or invalid GitHub release asset ' + name + '.');
  const zip = await download(new URL(url), asset.size, signal);
  if (zip.length !== asset.size) throw new Error('Release ZIP size mismatch.');
  return checkRelease(
    version,
    architecture,
    zip,
    asset.digest,
    Object.hasOwn(legacy, version)
      ? legacy[version as keyof typeof legacy]
      : undefined
  );
};
