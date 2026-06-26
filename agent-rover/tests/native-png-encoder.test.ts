// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { PNG } from 'pngjs';
import { describe, expect, it } from 'vitest';

import { nativeTestPaths } from './helpers/native-paths';

const { repositoryDirectory, windowsAgentDirectory } = nativeTestPaths(
  import.meta.url
);

const execFileChecked = async (
  file: string,
  args: readonly string[],
  cwd: string
): Promise<void> => {
  await new Promise<void>((resolve, reject) => {
    execFile(file, args, { cwd }, (error, stdout, stderr) => {
      if (error === null) {
        resolve();
      } else {
        reject(
          new Error(`${file} failed\nstdout:\n${stdout}\nstderr:\n${stderr}`)
        );
      }
    });
  });
};

describe('native png encoder', () => {
  it('writes a PNG that pngjs can decode', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-png-'));
    const harness = join(directory, 'png_encoder_test.cpp');
    const executable = join(directory, 'png_encoder_test');
    const output = join(directory, 'out.png');
    await writeFile(
      harness,
      String.raw`
#include "png_encoder.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }
  std::vector<unsigned char> rgba = {
    255, 0, 0, 255,
    0, 128, 255, 255
  };
  std::vector<unsigned char> png;
  std::string error;
  if (!agent_rover::EncodePngRgba(2, 1, rgba, &png, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  FILE* file = std::fopen(argv[1], "wb");
  if (file == nullptr) {
    return 1;
  }
  std::fwrite(png.data(), 1, png.size(), file);
  std::fclose(file);
  return 0;
}
`,
      'utf8'
    );

    try {
      await execFileChecked(
        'g++',
        [
          '-std=c++20',
          '-I',
          windowsAgentDirectory,
          join(windowsAgentDirectory, 'png_encoder.cpp'),
          harness,
          '-o',
          executable,
        ],
        repositoryDirectory
      );
      await execFileChecked(executable, [output], repositoryDirectory);

      const png = PNG.sync.read(await readFile(output));
      expect({
        height: png.height,
        width: png.width,
      }).toEqual({
        height: 1,
        width: 2,
      });
      expect([...png.data.subarray(0, 8)]).toEqual([
        255, 0, 0, 255, 0, 128, 255, 255,
      ]);
    } finally {
      await rm(directory, {
        force: true,
        recursive: true,
      });
    }
  });
});
