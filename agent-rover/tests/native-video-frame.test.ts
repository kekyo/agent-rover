// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, it } from 'vitest';

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

describe('native video frame model', () => {
  it('keeps a fixed even frame while following or fixing capture bounds', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-video-frame-'));
    const harness = join(directory, 'video_frame_test.cpp');
    const executable = join(directory, 'video_frame_test');
    await writeFile(
      harness,
      String.raw`
#include "video_frame.h"

#include <iostream>
#include <string>
#include <vector>

static bool EqualRect(
    const agent_rover::WindowRect& value,
    int x,
    int y,
    int width,
    int height) {
  return value.x == x && value.y == y && value.width == width &&
         value.height == height;
}

int main() {
  const agent_rover::WindowRect initial = {10, 20, 3, 5};
  const agent_rover::WindowRect current = {40, 50, 8, 7};
  const agent_rover::VideoFrameSize size =
      agent_rover::CreateVideoFrameSize(initial);
  if (size.width != 4 || size.height != 6) {
    std::cerr << "frame size was not padded to even dimensions\n";
    return 1;
  }
  if (!EqualRect(
          agent_rover::SelectVideoCaptureBounds(
              agent_rover::VideoWindowTracking::FollowWindow,
              initial,
              current),
          40, 50, 8, 7)) {
    std::cerr << "follow mode did not select current bounds\n";
    return 1;
  }
  if (!EqualRect(
          agent_rover::SelectVideoCaptureBounds(
              agent_rover::VideoWindowTracking::InitialBounds,
              initial,
              current),
          10, 20, 3, 5)) {
    std::cerr << "fixed mode did not select initial bounds\n";
    return 1;
  }

  const agent_rover::WindowRect requested = {10, 20, 3, 2};
  const agent_rover::WindowRect visible = {11, 20, 2, 2};
  const std::vector<unsigned char> visible_bgra = {
      1, 2, 3, 255, 4, 5, 6, 255,
      7, 8, 9, 255, 10, 11, 12, 255};
  std::vector<unsigned char> output;
  std::string error;
  if (!agent_rover::ComposeVideoFrameBgra(
          requested, visible, visible_bgra, {4, 2}, &output, &error)) {
    std::cerr << error << "\n";
    return 1;
  }
  const std::vector<unsigned char> expected = {
      0, 0, 0, 255, 1, 2, 3, 255, 4, 5, 6, 255, 0, 0, 0, 255,
      0, 0, 0, 255, 7, 8, 9, 255, 10, 11, 12, 255, 0, 0, 0, 255};
  if (output != expected) {
    std::cerr << "visible pixels were not positioned over black padding\n";
    return 1;
  }

  const agent_rover::WindowRect larger = {10, 20, 6, 2};
  const std::vector<unsigned char> larger_bgra(6 * 2 * 4, 42);
  if (!agent_rover::ComposeVideoFrameBgra(
          larger, larger, larger_bgra, {4, 2}, &output, &error) ||
      output.size() != 4 * 2 * 4) {
    std::cerr << "larger frames were not cropped to the fixed frame\n";
    return 1;
  }
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
          join(windowsAgentDirectory, 'video_frame.cpp'),
          harness,
          '-o',
          executable,
        ],
        repositoryDirectory
      );
      await execFileChecked(executable, [], repositoryDirectory);
    } finally {
      await rm(directory, { force: true, recursive: true });
    }
  });
});
