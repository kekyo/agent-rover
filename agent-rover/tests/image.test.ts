// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { PNG } from 'pngjs';
import { describe, expect, it } from 'vitest';

import { compareImages, connectRemoteAgent } from '../src/index';
import { startFakeTcpAgent } from './helpers/fake-tcp-agent';

const solidPng = (
  width: number,
  height: number,
  rgba: readonly [number, number, number, number]
): Buffer => {
  const png = new PNG({
    height,
    width,
  });
  for (let offset = 0; offset < png.data.length; offset += 4) {
    png.data[offset] = rgba[0];
    png.data[offset + 1] = rgba[1];
    png.data[offset + 2] = rgba[2];
    png.data[offset + 3] = rgba[3];
  }
  return PNG.sync.write(png);
};

describe.concurrent('window screenshots', () => {
  it('captures a tcp agent window screenshot as PNG data', async () => {
    const fakeAgent = await startFakeTcpAgent({
      screenshotImage: solidPng(640, 480, [0, 0, 0, 255]),
    });

    const agent = await connectRemoteAgent({
      host: fakeAgent.host,
      port: fakeAgent.port,
    });
    try {
      const windows = await agent.windows();
      const firstWindow = windows[0];
      if (firstWindow === undefined) {
        throw new Error('Expected a mock top-level window.');
      }

      const screenshot = await firstWindow.screenshot();
      const decoded = PNG.sync.read(screenshot.image);

      expect(decoded.width).toBe(640);
      expect(decoded.height).toBe(480);
      expect(screenshot.bounds).toEqual(firstWindow.bounds);
      expect(screenshot.clipped).toBe(false);
    } finally {
      agent.release();
      await fakeAgent.close();
    }
  });
});

describe.concurrent('image assertions', () => {
  it('passes identical PNG images and reports different pixels', () => {
    const black = solidPng(4, 4, [0, 0, 0, 255]);
    const white = solidPng(4, 4, [255, 255, 255, 255]);

    expect(compareImages(black, black)).toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      pass: true,
      totalPixels: 16,
    });
    expect(
      compareImages(black, white, {
        maxDiffPixels: 0,
      })
    ).toMatchObject({
      diffPixels: 16,
      diffRatio: 1,
      pass: false,
      totalPixels: 16,
    });
  });
});
