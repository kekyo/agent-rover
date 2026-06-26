import { execFile } from 'node:child_process';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);
const minimumZipDate = new Date(Date.UTC(1980, 0, 1, 0, 0, 0));

const crc32Table = new Uint32Array(256);

for (let value = 0; value < crc32Table.length; value += 1) {
  let crc = value;

  for (let bit = 0; bit < 8; bit += 1) {
    crc = (crc & 1) === 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
  }

  crc32Table[value] = crc >>> 0;
}

const crc32 = (buffer) => {
  let crc = 0xffffffff;

  for (const byte of buffer) {
    crc = crc32Table[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }

  return (crc ^ 0xffffffff) >>> 0;
};

const toDosDateTime = (date) => {
  const normalizedDate = date < minimumZipDate ? minimumZipDate : date;
  const year = normalizedDate.getUTCFullYear();
  const month = normalizedDate.getUTCMonth() + 1;
  const day = normalizedDate.getUTCDate();
  const hours = normalizedDate.getUTCHours();
  const minutes = normalizedDate.getUTCMinutes();
  const seconds = Math.floor(normalizedDate.getUTCSeconds() / 2);

  return {
    date: ((year - 1980) << 9) | (month << 5) | day,
    time: (hours << 11) | (minutes << 5) | seconds,
  };
};

const writeUInt16 = (buffer, value, offset) => {
  buffer.writeUInt16LE(value, offset);
};

const writeUInt32 = (buffer, value, offset) => {
  buffer.writeUInt32LE(value >>> 0, offset);
};

const createLocalFileHeader = (entry) => {
  const header = Buffer.alloc(30);

  writeUInt32(header, 0x04034b50, 0);
  writeUInt16(header, 10, 4);
  writeUInt16(header, 0, 6);
  writeUInt16(header, 0, 8);
  writeUInt16(header, entry.modified.time, 10);
  writeUInt16(header, entry.modified.date, 12);
  writeUInt32(header, entry.crc, 14);
  writeUInt32(header, entry.content.length, 18);
  writeUInt32(header, entry.content.length, 22);
  writeUInt16(header, entry.fileName.length, 26);
  writeUInt16(header, 0, 28);

  return Buffer.concat([header, entry.fileName]);
};

const createCentralDirectoryHeader = (entry) => {
  const header = Buffer.alloc(46);

  writeUInt32(header, 0x02014b50, 0);
  writeUInt16(header, 20, 4);
  writeUInt16(header, 10, 6);
  writeUInt16(header, 0, 8);
  writeUInt16(header, 0, 10);
  writeUInt16(header, entry.modified.time, 12);
  writeUInt16(header, entry.modified.date, 14);
  writeUInt32(header, entry.crc, 16);
  writeUInt32(header, entry.content.length, 20);
  writeUInt32(header, entry.content.length, 24);
  writeUInt16(header, entry.fileName.length, 28);
  writeUInt16(header, 0, 30);
  writeUInt16(header, 0, 32);
  writeUInt16(header, 0, 34);
  writeUInt16(header, 0, 36);
  writeUInt32(header, 0, 38);
  writeUInt32(header, entry.localHeaderOffset, 42);

  return Buffer.concat([header, entry.fileName]);
};

const createEndOfCentralDirectory = ({
  entryCount,
  centralDirectorySize,
  centralDirectoryOffset,
}) => {
  const header = Buffer.alloc(22);

  writeUInt32(header, 0x06054b50, 0);
  writeUInt16(header, 0, 4);
  writeUInt16(header, 0, 6);
  writeUInt16(header, entryCount, 8);
  writeUInt16(header, entryCount, 10);
  writeUInt32(header, centralDirectorySize, 12);
  writeUInt32(header, centralDirectoryOffset, 16);
  writeUInt16(header, 0, 20);

  return header;
};

const createZipArchive = async (outputPath, inputs) => {
  const localParts = [];
  const centralParts = [];
  const entries = [];
  let offset = 0;

  for (const input of inputs) {
    const content = await readFile(input.path);
    const fileName = Buffer.from(input.name, 'utf8');
    const entry = {
      content,
      crc: crc32(content),
      fileName,
      localHeaderOffset: offset,
      modified: toDosDateTime(minimumZipDate),
    };

    const localHeader = createLocalFileHeader(entry);
    localParts.push(localHeader, content);
    offset += localHeader.length + content.length;
    entries.push(entry);
  }

  const centralDirectoryOffset = offset;

  for (const entry of entries) {
    const centralHeader = createCentralDirectoryHeader(entry);
    centralParts.push(centralHeader);
    offset += centralHeader.length;
  }

  const centralDirectorySize = offset - centralDirectoryOffset;
  const endOfCentralDirectory = createEndOfCentralDirectory({
    centralDirectoryOffset,
    centralDirectorySize,
    entryCount: entries.length,
  });

  await mkdir(dirname(outputPath), { recursive: true });
  await writeFile(
    outputPath,
    Buffer.concat([...localParts, ...centralParts, endOfCentralDirectory])
  );
};

export const getScrewUpVersion = async (agentsRoot) => {
  const screwUpPath = resolve(
    agentsRoot,
    '..',
    'node_modules',
    'screw-up',
    'dist',
    'main.mjs'
  );
  const result = await execFileAsync(process.execPath, [
    screwUpPath,
    'dump',
    agentsRoot,
  ]);
  const metadata = JSON.parse(result.stdout);

  if (typeof metadata.version !== 'string' || metadata.version.length === 0) {
    throw new Error('screw-up did not return a package version.');
  }

  return metadata.version;
};

export const findWindowsAgentBinaries = async (agentsRoot) => {
  const dist = join(agentsRoot, 'dist');
  const names = await readdir(dist);

  return names
    .filter((name) => /^agent-(?!rover-agent-).+\.exe$/u.test(name))
    .sort()
    .map((name) => ({
      name,
      path: join(dist, name),
    }));
};

export const createWindowsAgentArchive = async ({
  repoRoot,
  agentsRoot,
  version,
}) => {
  const binaries = await findWindowsAgentBinaries(agentsRoot);

  if (binaries.length === 0) {
    throw new Error('No Windows agent binaries were found in agents/dist.');
  }

  const archivePath = join(
    repoRoot,
    'artifacts',
    `agent-rover-windows-${version}.zip`
  );
  const inputs = [
    ...binaries,
    {
      name: 'LICENSE',
      path: join(repoRoot, 'LICENSE'),
    },
    {
      name: 'README_pack.md',
      path: join(repoRoot, 'README_pack.md'),
    },
  ].sort((left, right) => left.name.localeCompare(right.name));

  await createZipArchive(archivePath, inputs);

  return archivePath;
};

const main = async () => {
  const agentsRoot = resolve(import.meta.dirname, '..');
  const repoRoot = resolve(agentsRoot, '..');
  const version = await getScrewUpVersion(agentsRoot);
  const archivePath = await createWindowsAgentArchive({
    agentsRoot,
    repoRoot,
    version,
  });

  console.log(`Created ${archivePath}`);
};

if (process.argv[1] === import.meta.filename) {
  await main();
}
