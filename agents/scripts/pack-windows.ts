// agent-rover - Build ZIP and NSIS distributions from one native build
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { execFile } from 'node:child_process';
import {
  mkdir,
  mkdtemp,
  readFile,
  rename,
  rm,
  writeFile,
} from 'node:fs/promises';
import { join, resolve, win32 } from 'node:path';
import { promisify } from 'node:util';
import {
  buildCandidateVariables,
  runScriptOnceToText,
  type FunCityLogEntry,
} from 'funcity';
import { createZipArchive } from './zip.ts';

const exec = promisify(execFile);
const agents = resolve(import.meta.dirname, '../..');
const repo = resolve(agents, '..');
const screwUp = resolve(repo, 'node_modules/screw-up/dist/main.mjs');
const format = async (expression: string) =>
  (
    await exec(
      process.execPath,
      [screwUp, 'format', '--expression', expression],
      { cwd: agents }
    )
  ).stdout.trim();
const version = await format('{version}');
const windowsVersion = await format('{version}.0');
if (
  !/^\d+\.\d+\.\d+$/u.test(version) ||
  !/^\d+\.\d+\.\d+\.\d+$/u.test(windowsVersion) ||
  windowsVersion.split('.').some((part) => Number(part) > 65535)
)
  throw new Error(
    'The screw-up version cannot be represented by NSIS (four integers from 0 through 65535).'
  );

const documents = [
  'LICENSE',
  'README_pack.md',
  'images/README.md',
  'images/agent-rover-120.png',
].map((name) => ({
  name,
  path: resolve(repo, name),
  directory: win32.dirname(name),
  filename: win32.basename(name),
}));
const binaries = ['amd64', 'i686'].map((arch) => ({
  name: `agent-${arch}.exe`,
  path: resolve(agents, `dist/agent-${arch}.exe`),
}));
const entries = [
  'agent-rover-agent.exe',
  ...documents.map((document) => document.name),
  'uninstall.exe',
].map((name, index) => ({ name, index }));
const artifacts = resolve(repo, 'artifacts');
await mkdir(artifacts, { recursive: true });
const stage = await mkdtemp(join(artifacts, '.windows-'));
const basename = `agent-rover-agent-win32-${version}`;
try {
  const template = resolve(agents, 'installer/windows.nsi.fc');
  const logs: FunCityLogEntry[] = [];
  const rendered = await runScriptOnceToText(await readFile(template, 'utf8'), {
    sourceId: template,
    logs,
    variables: buildCandidateVariables({
      version,
      windowsVersion,
      output: join(stage, `${basename}-setup.exe`),
      icon: resolve(repo, 'images/agent-rover.ico'),
      amd64: binaries[0]!.path,
      i686: binaries[1]!.path,
      documents,
      entries,
      reversedEntries: [...entries].reverse(),
      winpath: (...parts: unknown[]) => win32.join(...parts.map(String)),
      // NSIS literals must not expand dollars from a checkout path.
      nsis: (value: unknown) =>
        String(value).replaceAll('$', '$$').replaceAll('"', '$\\"'),
    }),
  });
  if (rendered === undefined || logs.length)
    throw new Error(`NSIS template failed: ${JSON.stringify(logs)}`);
  const script = join(stage, 'windows.nsi');
  await writeFile(script, rendered);
  await exec(
    resolve(agents, 'scripts/windows-toolchain.sh'),
    ['makensis', '-WX', script],
    { cwd: agents, maxBuffer: 8 * 1024 * 1024 }
  );
  await createZipArchive(join(stage, `${basename}.zip`), [
    ...binaries,
    ...documents,
  ]);
  for (const suffix of ['.zip', '-setup.exe']) {
    const name = basename + suffix;
    await rename(join(stage, name), join(artifacts, name));
    console.log(`Created ${join(artifacts, name)}`);
  }
} finally {
  await rm(stage, { recursive: true, force: true });
}
