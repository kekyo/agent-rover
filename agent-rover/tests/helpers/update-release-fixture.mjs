// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

// Provide local, unreleased build archives for the CLI round-trip test.
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';

const archives = JSON.parse(process.env.AGENT_ROVER_TEST_ARCHIVES);
const network = globalThis.fetch;
globalThis.fetch = async (input, options) => {
  const url = new URL(input);
  for (const [version, path] of Object.entries(archives)) {
    const name = 'agent-rover-agent-win32-' + version + '.zip';
    const download =
      'https://github.com/kekyo/agent-rover/releases/download/' +
      version +
      '/' +
      name;
    if (url.href === download) return new Response(await readFile(path));
    if (
      url.href ===
      'https://api.github.com/repos/kekyo/agent-rover/releases/tags/' + version
    ) {
      const bytes = await readFile(path);
      return Response.json({
        tag_name: version,
        assets: [
          {
            name,
            size: bytes.length,
            digest:
              'sha256:' + createHash('sha256').update(bytes).digest('hex'),
            browser_download_url: download,
          },
        ],
      });
    }
  }
  return await network(input, options);
};
