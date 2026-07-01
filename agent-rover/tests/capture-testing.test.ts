// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { PNG } from 'pngjs';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  createCaptureExpect,
  expectCapture,
  type CaptureExpect,
  type CaptureExpectation,
  type CaptureExpectedImage,
  type CaptureLookSimilarResult,
  type CapturePixelRegion,
  type CaptureVisualError,
  type CaptureVisualResult,
} from '../src/testing';
import type { AsyncReleaseable, ScreenRect } from '../src/index';

type PixelColor = readonly [number, number, number, number];

interface MockTesseractWord {
  readonly bbox: {
    readonly x0: number;
    readonly x1: number;
    readonly y0: number;
    readonly y1: number;
  };
  readonly confidence?: number;
  readonly text: string;
}

interface MockTesseractResult {
  readonly blocks?: readonly {
    readonly paragraphs: readonly {
      readonly lines: readonly {
        readonly words: readonly MockTesseractWord[];
      }[];
    }[];
  }[];
  readonly confidence: number;
  readonly text: string;
  readonly words?: readonly MockTesseractWord[];
}

interface MockTesseractWorker {
  readonly recognize: ReturnType<typeof vi.fn>;
  readonly setParameters: ReturnType<typeof vi.fn>;
  readonly terminate: ReturnType<typeof vi.fn>;
}

interface TestCapture {
  readonly bounds: ScreenRect;
  readonly clipped: boolean;
  readonly image: Buffer;
  readonly visibleBounds: ScreenRect;
}

const tesseractMock = vi.hoisted(() => {
  const state: {
    readonly createWorker: ReturnType<typeof vi.fn>;
    readonly workers: MockTesseractWorker[];
    results: MockTesseractResult[];
  } = {
    createWorker: vi.fn(),
    results: [],
    workers: [],
  };
  const reset = (): void => {
    state.results = [];
    state.workers.splice(0);
    state.createWorker.mockReset();
    state.createWorker.mockImplementation(async () => {
      const worker: MockTesseractWorker = {
        recognize: vi.fn(async () => {
          const result = state.results.shift() ?? {
            confidence: 91,
            text: 'Submit\n',
          };
          return {
            data: result,
          };
        }),
        setParameters: vi.fn(async () => undefined),
        terminate: vi.fn(async () => undefined),
      };
      state.workers.push(worker);
      return worker;
    });
  };
  reset();
  return {
    reset,
    state,
  };
});

vi.mock('tesseract.js', () => ({
  createWorker: tesseractMock.state.createWorker,
}));

const originalOutputResultPath =
  process.env.AGENT_ROVER_VISUAL_OUTPUT_RESULT_PATH;
const originalVariant = process.env.AGENT_ROVER_VISUAL_VARIANT;
const tempRoots: string[] = [];

const restoreEnv = (name: string, value: string | undefined): void => {
  if (value === undefined) {
    delete process.env[name];
    return;
  }
  process.env[name] = value;
};

const createTempRoot = async (): Promise<string> => {
  const tempRoot = await mkdtemp(
    join(tmpdir(), 'agent-rover-capture-testing-')
  );
  tempRoots.push(tempRoot);
  return tempRoot;
};

const expectType = <Expected>(_value: Expected): void => {
  // Type-only assertion helper.
};

const createPngBuffer = (
  width: number,
  height: number,
  pixelAt: (x: number, y: number) => PixelColor
): Buffer => {
  const png = new PNG({
    height,
    width,
  });
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const [red, green, blue, alpha] = pixelAt(x, y);
      const index = (y * width + x) * 4;
      png.data[index] = red;
      png.data[index + 1] = green;
      png.data[index + 2] = blue;
      png.data[index + 3] = alpha;
    }
  }
  return PNG.sync.write(png);
};

const solidPng = (width: number, height: number, color: PixelColor): Buffer =>
  createPngBuffer(width, height, () => color);

const createCapture = (
  image: Buffer,
  width: number,
  height: number
): TestCapture => ({
  bounds: {
    height,
    width,
    x: 10,
    y: 20,
  },
  clipped: false,
  image,
  visibleBounds: {
    height,
    width,
    x: 10,
    y: 20,
  },
});

const expectVisualError = async <Result extends CaptureVisualResult>(
  operation: () => Promise<Result>
): Promise<CaptureVisualError<Result>> => {
  try {
    await operation();
  } catch (error) {
    expect(error).toHaveProperty('result');
    return error as CaptureVisualError<Result>;
  }
  throw new Error('Expected visual assertion to fail.');
};

afterEach(async () => {
  tesseractMock.reset();
  restoreEnv('AGENT_ROVER_VISUAL_OUTPUT_RESULT_PATH', originalOutputResultPath);
  restoreEnv('AGENT_ROVER_VISUAL_VARIANT', originalVariant);

  await Promise.all(
    tempRoots.splice(0).map(async (tempRoot) => {
      await rm(tempRoot, {
        force: true,
        recursive: true,
      });
    })
  );
});

describe('capture visual testing foundation', () => {
  it('accepts expected images from buffers, path strings, and file URLs', async () => {
    const root = await createTempRoot();
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);
    const expectedPath = join(root, 'expected.png');
    await writeFile(expectedPath, expectedImage);
    const captureExpect = createCaptureExpect({
      outputResultPath: join(root, 'artifacts'),
      variant: 'unit',
    });

    await expect(
      captureExpect
        .expectCapture(createCapture(expectedImage, 4, 4), 'buffer')
        .toLookSimilar(expectedImage)
    ).resolves.toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      pass: true,
    });
    await expect(
      captureExpect
        .expectCapture(createCapture(expectedImage, 4, 4), 'path')
        .toLookSimilar(expectedPath)
    ).resolves.toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      pass: true,
    });
    await expect(
      captureExpect
        .expectCapture(createCapture(expectedImage, 4, 4), 'file-url')
        .toLookSimilar(new URL(`file://${expectedPath}`))
    ).resolves.toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      pass: true,
    });
  });

  it('writes artifacts to the environment output result path when specified', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    process.env.AGENT_ROVER_VISUAL_OUTPUT_RESULT_PATH = outputResultPath;
    process.env.AGENT_ROVER_VISUAL_VARIANT = 'unit env';
    const captureExpect = createCaptureExpect();
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);

    const result = await captureExpect
      .expectCapture(createCapture(expectedImage, 4, 4), 'env root')
      .toLookSimilar(expectedImage);

    expect(result.outputResultPath).toBe(
      join(outputResultPath, 'unit_env', 'env_root-000000')
    );
    await expect(readFile(result.actualImagePath!)).resolves.toEqual(
      expectedImage
    );
    await expect(
      JSON.parse(await readFile(result.metadataJsonPath!, 'utf8'))
    ).toMatchObject({
      expectedImageSource: 'buffer',
      matcher: 'toLookSimilar',
      name: 'env root',
      variant: 'unit env',
    });
  });

  it('does not write artifacts when only a variant is specified', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    const captureExpect = createCaptureExpect({
      variant: 'unit',
    });
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);

    const result = await captureExpect
      .expectCapture(createCapture(expectedImage, 4, 4), 'default-root')
      .toLookSimilar(expectedImage);

    expect(result.actualImagePath).toBeUndefined();
    expect(result.outputResultPath).toBeUndefined();
    expect(result.metadataJsonPath).toBeUndefined();
    await expect(readdir(outputResultPath)).rejects.toMatchObject({
      code: 'ENOENT',
    });
  });

  it('exposes failure results without artifacts when output is disabled', async () => {
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);
    const actualImage = solidPng(4, 4, [255, 255, 255, 255]);

    const error = await expectVisualError(() =>
      expectCapture(
        createCapture(actualImage, 4, 4),
        'different'
      ).toLookSimilar(expectedImage, {
        maxDiffRatio: 0,
        threshold: 0,
      })
    );

    expect(error.result).toMatchObject({
      diffPixels: 16,
      diffRatio: 1,
      pass: false,
      totalPixels: 16,
    });
    expect(error.result.actualImagePath).toBeUndefined();
    expect(error.result.diffImagePath).toBeUndefined();
    expect(error.result.expectedImagePath).toBeUndefined();
  });

  it('writes expected and diff artifacts when pixel comparison fails', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    const captureExpect = createCaptureExpect({
      outputResultPath,
      variant: 'unit',
    });
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);
    const actualImage = solidPng(4, 4, [255, 255, 255, 255]);

    const error = await expectVisualError(() =>
      captureExpect
        .expectCapture(createCapture(actualImage, 4, 4), 'different')
        .toLookSimilar(expectedImage, {
          maxDiffRatio: 0,
          threshold: 0,
        })
    );

    expect(error.result).toMatchObject({
      diffPixels: 16,
      diffRatio: 1,
      pass: false,
      totalPixels: 16,
    });
    await expect(readFile(error.result.actualImagePath!)).resolves.toEqual(
      actualImage
    );
    await expect(readFile(error.result.expectedImagePath!)).resolves.toEqual(
      expectedImage
    );
    const diffPng = PNG.sync.read(await readFile(error.result.diffImagePath!));
    expect(diffPng.width).toBe(4);
    expect(diffPng.height).toBe(4);
    await expect(
      JSON.parse(await readFile(error.result.metadataJsonPath!, 'utf8'))
    ).toMatchObject({
      matcher: 'toLookSimilar',
      name: 'different',
      variant: 'unit',
    });
  });

  it('compares captures with regions and masks', async () => {
    const expectedImage = solidPng(4, 4, [0, 0, 0, 255]);
    const outsideRegionDifferent = createPngBuffer(4, 4, (x, y) =>
      x === 3 && y === 3 ? [255, 255, 255, 255] : [0, 0, 0, 255]
    );

    const regionResult = await expectCapture(
      createCapture(outsideRegionDifferent, 4, 4),
      'region'
    ).toLookSimilar(expectedImage, {
      maxDiffRatio: 0,
      region: {
        height: 2,
        width: 2,
        x: 0,
        y: 0,
      },
    });
    expect(regionResult).toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      totalPixels: 4,
    });

    const maskedResult = await expectCapture(
      createCapture(outsideRegionDifferent, 4, 4),
      'masked'
    ).toLookSimilar(expectedImage, {
      masks: [
        {
          height: 1,
          width: 1,
          x: 3,
          y: 3,
        },
      ],
      maxDiffRatio: 0,
    });
    expect(maskedResult).toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      totalPixels: 16,
    });
  });

  it('rejects invalid regions and masks with TypeError', async () => {
    const image = solidPng(4, 4, [0, 0, 0, 255]);

    await expect(
      expectCapture(createCapture(image, 4, 4), 'invalid-region').toLookSimilar(
        image,
        {
          region: {
            height: 1,
            width: 2,
            x: 3,
            y: 0,
          },
        }
      )
    ).rejects.toThrow(TypeError);

    await expect(
      expectCapture(createCapture(image, 4, 4), 'invalid-mask').toLookSimilar(
        image,
        {
          masks: [
            {
              height: 1,
              width: 1,
              x: 4,
              y: 0,
            },
          ],
        }
      )
    ).rejects.toThrow(TypeError);
  });

  it('compares captures by structural similarity', async () => {
    const expectedImage = solidPng(16, 16, [0, 0, 0, 255]);

    await expect(
      expectCapture(
        createCapture(expectedImage, 16, 16),
        'similar'
      ).toHaveSimilarity(expectedImage)
    ).resolves.toMatchObject({
      diffPixels: 0,
      diffRatio: 0,
      pass: true,
      similarity: 1,
      totalPixels: 256,
    });

    const outsideRegionDifferent = createPngBuffer(16, 16, (x, y) =>
      x === 15 && y === 15 ? [255, 255, 255, 255] : [0, 0, 0, 255]
    );
    await expect(
      expectCapture(
        createCapture(outsideRegionDifferent, 16, 16),
        'similar-region'
      ).toHaveSimilarity(expectedImage, {
        minSimilarity: 1,
        region: {
          height: 12,
          width: 12,
          x: 0,
          y: 0,
        },
      })
    ).resolves.toMatchObject({
      diffPixels: 0,
      pass: true,
      similarity: 1,
      totalPixels: 144,
    });

    await expect(
      expectCapture(
        createCapture(outsideRegionDifferent, 16, 16),
        'similar-masked'
      ).toHaveSimilarity(expectedImage, {
        masks: [
          {
            height: 1,
            width: 1,
            x: 15,
            y: 15,
          },
        ],
        minSimilarity: 1,
      })
    ).resolves.toMatchObject({
      diffPixels: 0,
      pass: true,
      similarity: 1,
      totalPixels: 256,
    });
  });

  it('writes structural similarity failure artifacts', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    const captureExpect = createCaptureExpect({
      outputResultPath,
      variant: 'unit',
    });
    const expectedImage = solidPng(16, 16, [0, 0, 0, 255]);
    const actualImage = solidPng(16, 16, [255, 255, 255, 255]);

    const error = await expectVisualError(() =>
      captureExpect
        .expectCapture(createCapture(actualImage, 16, 16), 'ssim-different')
        .toHaveSimilarity(expectedImage, {
          minSimilarity: 0.99,
        })
    );

    expect(error.result).toMatchObject({
      diffPixels: 256,
      diffRatio: 1,
      pass: false,
      totalPixels: 256,
    });
    expect(error.result.similarity).toBeLessThan(0.99);
    await expect(readFile(error.result.expectedImagePath!)).resolves.toEqual(
      expectedImage
    );
    const diffPng = PNG.sync.read(await readFile(error.result.diffImagePath!));
    expect(diffPng.width).toBe(16);
    expect(diffPng.height).toBe(16);
    await expect(
      JSON.parse(await readFile(error.result.metadataJsonPath!, 'utf8'))
    ).toMatchObject({
      matcher: 'toHaveSimilarity',
      name: 'ssim-different',
      variant: 'unit',
    });
  });

  it('reads OCR text with per-read workers and page segmentation parameters', async () => {
    tesseractMock.state.results = [
      {
        confidence: 10,
        text: 'Noise\n',
      },
      {
        confidence: 88,
        text: 'Submit\n',
      },
    ];
    const capture = createCapture(solidPng(8, 4, [255, 255, 255, 255]), 8, 4);

    const ocrText = await expectCapture(capture, 'ocr-submit').readText({
      pageSegmentationModes: ['singleWord', 'singleLine'],
      parameters: {
        preserve_interword_spaces: '1',
      },
    });

    expect(ocrText).toMatchObject({
      confidence: 88,
      normalizedText: 'Submit',
      pageSegmentationMode: 'singleLine',
      text: 'Submit\n',
    });
    expect(ocrText.attempts).toHaveLength(2);
    expect(tesseractMock.state.createWorker).toHaveBeenCalledTimes(1);
    expect(tesseractMock.state.createWorker).toHaveBeenCalledWith(
      'eng',
      undefined,
      expect.objectContaining({
        cacheMethod: 'none',
        gzip: true,
        langPath: expect.stringContaining('@tesseract.js-data/eng'),
      })
    );
    expect(tesseractMock.state.workers[0]!.setParameters).toHaveBeenCalledWith({
      preserve_interword_spaces: '1',
      tessedit_pageseg_mode: '8',
    });
    expect(tesseractMock.state.workers[0]!.setParameters).toHaveBeenCalledWith({
      preserve_interword_spaces: '1',
      tessedit_pageseg_mode: '7',
    });
    expect(tesseractMock.state.workers[0]!.recognize).toHaveBeenCalledTimes(2);
    expect(tesseractMock.state.workers[0]!.recognize).toHaveBeenCalledWith(
      expect.any(Buffer),
      {},
      { blocks: true, text: true }
    );
    expect(tesseractMock.state.workers[0]!.terminate).toHaveBeenCalledTimes(1);
  });

  it('writes OCR artifacts and applies preprocessing options', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    const sourceImage = createPngBuffer(2, 2, (x, y) => {
      if (x === 1 && y === 0) {
        return [0, 0, 0, 255];
      }
      if (x === 1 && y === 1) {
        return [255, 255, 255, 255];
      }
      return [127, 127, 127, 255];
    });
    const capture = createCapture(sourceImage, 2, 2);
    const captureExpect = createCaptureExpect({
      outputResultPath,
      variant: 'unit',
    });

    const ocrText = await captureExpect
      .expectCapture(capture, 'ocr-preprocess')
      .readText({
        pageSegmentationModes: ['singleBlock'],
        preprocess: {
          grayscale: true,
          invert: true,
          scale: 2,
          threshold: 128,
        },
        region: {
          height: 2,
          width: 1,
          x: 1,
          y: 0,
        },
      });

    expect(ocrText.outputResultPath).toBe(
      join(outputResultPath, 'unit', 'ocr-preprocess-000000')
    );
    const ocrInput = PNG.sync.read(await readFile(ocrText.ocrInputImagePath!));
    expect(ocrInput.width).toBe(2);
    expect(ocrInput.height).toBe(4);
    for (let y = 0; y < 2; y += 1) {
      for (let x = 0; x < 2; x += 1) {
        const index = (y * ocrInput.width + x) * 4;
        expect([...ocrInput.data.subarray(index, index + 4)]).toEqual([
          255, 255, 255, 255,
        ]);
      }
    }
    for (let y = 2; y < 4; y += 1) {
      for (let x = 0; x < 2; x += 1) {
        const index = (y * ocrInput.width + x) * 4;
        expect([...ocrInput.data.subarray(index, index + 4)]).toEqual([
          0, 0, 0, 255,
        ]);
      }
    }
    await expect(
      JSON.parse(await readFile(ocrText.metadataJsonPath!, 'utf8'))
    ).toMatchObject({
      matcher: 'readText',
      preprocess: {
        grayscale: true,
        invert: true,
        scale: 2,
        threshold: 128,
      },
      region: {
        height: 2,
        width: 1,
        x: 1,
        y: 0,
      },
    });
  });

  it('releases shared OCR workers explicitly and asynchronously', async () => {
    const capture = createCapture(solidPng(8, 4, [255, 255, 255, 255]), 8, 4);
    const captureExpect = createCaptureExpect({
      ocr: {
        workerMode: 'shared',
      },
    });

    await captureExpect
      .expectCapture(capture, 'shared-a')
      .readText({ pageSegmentationModes: ['singleBlock'] });
    await captureExpect
      .expectCapture(capture, 'shared-b')
      .readText({ pageSegmentationModes: ['singleBlock'] });
    await captureExpect[Symbol.asyncDispose]();
    await captureExpect.releaseAsync();

    expect(tesseractMock.state.createWorker).toHaveBeenCalledTimes(1);
    expect(tesseractMock.state.workers[0]!.recognize).toHaveBeenCalledTimes(2);
    expect(tesseractMock.state.workers[0]!.terminate).toHaveBeenCalledTimes(1);
  });

  it('asserts OCR text with strings, regular expressions, and confidence', async () => {
    tesseractMock.state.results = [
      {
        confidence: 77,
        text: 'Ready\nNow',
      },
    ];
    const capture = createCapture(solidPng(8, 4, [255, 255, 255, 255]), 8, 4);

    await expect(
      expectCapture(capture, 'ocr-ready').toContainText('ready now', {
        pageSegmentationModes: ['singleBlock'],
      })
    ).resolves.toMatchObject({
      confidence: 77,
      expectedText: 'ready now',
      normalizedText: 'Ready Now',
      pass: true,
    });

    tesseractMock.state.results = [
      {
        confidence: 77,
        text: 'Ready\nNow',
      },
    ];
    await expect(
      expectCapture(capture, 'ocr-regexp').toContainText(/^ready now$/i, {
        pageSegmentationModes: ['singleBlock'],
      })
    ).resolves.toMatchObject({
      expectedText: '/^ready now$/i',
      pass: true,
    });

    tesseractMock.state.results = [
      {
        confidence: 49,
        text: 'Submit',
      },
    ];
    const error = await expectVisualError(() =>
      expectCapture(capture, 'ocr-confidence').toContainText('submit', {
        minConfidence: 50,
        pageSegmentationModes: ['singleBlock'],
      })
    );
    expect(error.result).toMatchObject({
      confidence: 49,
      expectedText: 'submit',
      normalizedText: 'Submit',
      pass: false,
    });
  });

  it('reuses read OCR text for multiple assertions without recognizing again', async () => {
    tesseractMock.state.results = [
      {
        confidence: 94,
        text: 'Submit Cancel',
      },
    ];
    const capture = createCapture(solidPng(8, 4, [255, 255, 255, 255]), 8, 4);

    const ocrText = await expectCapture(capture, 'ocr-dialog').readText({
      pageSegmentationModes: ['singleBlock'],
    });

    await expect(ocrText.toContainText('submit')).resolves.toMatchObject({
      pass: true,
    });
    await expect(ocrText.toContainText(/cancel/i)).resolves.toMatchObject({
      pass: true,
    });
    expect(tesseractMock.state.workers[0]!.recognize).toHaveBeenCalledTimes(1);
  });

  it('finds OCR text locations across words with region and scale transforms', async () => {
    tesseractMock.state.results = [
      {
        blocks: [
          {
            paragraphs: [
              {
                lines: [
                  {
                    words: [
                      {
                        bbox: { x0: 10, x1: 30, y0: 4, y1: 20 },
                        confidence: 90,
                        text: 'Muon',
                      },
                      {
                        bbox: { x0: 34, x1: 60, y0: 4, y1: 20 },
                        confidence: 91,
                        text: 'Probe',
                      },
                      {
                        bbox: { x0: 64, x1: 98, y0: 4, y1: 20 },
                        confidence: 92,
                        text: 'Select',
                      },
                    ],
                  },
                ],
              },
            ],
          },
        ],
        confidence: 94,
        text: 'Muon Probe Select',
      },
    ];
    const capture = createCapture(
      solidPng(80, 40, [255, 255, 255, 255]),
      80,
      40
    );

    const ocrText = await expectCapture(capture, 'ocr-location').readText({
      pageSegmentationModes: ['singleBlock'],
      preprocess: {
        scale: 2,
      },
      region: {
        height: 20,
        width: 50,
        x: 5,
        y: 2,
      },
    });

    const match = await ocrText.findText('Probe Select');

    expect(match).toMatchObject({
      bounds: {
        height: 8,
        width: 32,
        x: 22,
        y: 4,
      },
      confidence: 94,
      pageSegmentationMode: 'singleBlock',
      screenBounds: {
        height: 8,
        width: 32,
        x: 32,
        y: 24,
      },
      text: 'Probe Select',
    });
  });

  it('writes OCR failure diagnostics and artifacts', async () => {
    const root = await createTempRoot();
    const outputResultPath = join(root, 'artifacts');
    tesseractMock.state.results = [
      {
        confidence: 77,
        text: 'Ready',
      },
    ];
    const capture = createCapture(solidPng(8, 4, [255, 255, 255, 255]), 8, 4);
    const captureExpect = createCaptureExpect({
      outputResultPath,
      variant: 'unit',
    });

    const error = await expectVisualError(() =>
      captureExpect
        .expectCapture(capture, 'ocr-failure')
        .toContainText('Submit', {
          pageSegmentationModes: ['singleBlock'],
        })
    );

    expect(error.message).toContain('Recognized text');
    expect(error.result).toMatchObject({
      expectedText: 'Submit',
      normalizedText: 'Ready',
      pass: false,
    });
    await expect(readFile(error.result.actualImagePath!)).resolves.toEqual(
      capture.image
    );
    await expect(readFile(error.result.ocrInputImagePath!)).resolves.toEqual(
      capture.image
    );
    await expect(
      JSON.parse(await readFile(error.result.metadataJsonPath!, 'utf8'))
    ).toMatchObject({
      expectedText: {
        type: 'string',
        value: 'Submit',
      },
      matcher: 'toContainText',
      name: 'ocr-failure',
      variant: 'unit',
    });
  });

  it('rejects capture PNGs that do not match visible bounds', async () => {
    const root = await createTempRoot();
    const captureExpect = createCaptureExpect({
      outputResultPath: join(root, 'artifacts'),
      variant: 'unit',
    });
    const capture = createCapture(solidPng(2, 2, [0, 0, 0, 255]), 3, 2);

    await expect(
      captureExpect
        .expectCapture(capture, 'size-mismatch')
        .toLookSimilar(solidPng(2, 2, [0, 0, 0, 255]))
    ).rejects.toThrow('does not match visible bounds');
  });
});

describe('capture visual testing foundation types', () => {
  it('exposes typed capture expectation APIs', async () => {
    if (false) {
      const region: CapturePixelRegion = {
        height: 1,
        width: 1,
        x: 0,
        y: 0,
      };
      const capture = undefined as unknown as TestCapture;
      const expectedBuffer: CaptureExpectedImage = Buffer.alloc(0);
      const expectedImagePath: CaptureExpectedImage = 'tests/images/black.png';
      const expectedUrl: CaptureExpectedImage = new URL(
        'file:///tmp/black.png'
      );
      const expectation: CaptureExpectation = expectCapture(capture, 'typed');
      const captureExpect: CaptureExpect = createCaptureExpect({
        outputResultPath: 'test-results',
        variant: 'unit',
      });
      const releasable: AsyncReleaseable = captureExpect;
      const lookResult: CaptureLookSimilarResult =
        await expectation.toLookSimilar(expectedBuffer, {
          masks: [region],
          maxDiffPixels: 1,
          maxDiffRatio: 0.01,
          region,
          threshold: 0.1,
        });

      expect(lookResult.totalPixels).toBe(region.width * region.height);
      expectType<CaptureExpectedImage>(expectedImagePath);
      expectType<CaptureExpectedImage>(expectedUrl);
      await releasable.releaseAsync();
      // @ts-expect-error expected image must be supplied before options.
      await expectation.toLookSimilar({
        maxDiffPixels: 1,
      });
      await expectation.toLookSimilar(expectedBuffer, {
        // @ts-expect-error unknown options must not be accepted.
        unknownOption: true,
      });
    }

    expect(true).toBe(true);
  });
});
