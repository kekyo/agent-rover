// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import pixelmatch from 'pixelmatch';
import { PNG } from 'pngjs';
import { ssim } from 'ssim.js';
import englishLanguageData from '@tesseract.js-data/eng';

import type { Releaseable, ScreenRect } from './index';

/////////////////////////////////////////////////////////////////////////////////////////

/** Pixel region inside a captured PNG image. */
export interface CapturePixelRegion {
  /** Left pixel offset from the capture image origin. */
  readonly x: number;

  /** Top pixel offset from the capture image origin. */
  readonly y: number;

  /** Region width in pixels. */
  readonly width: number;

  /** Region height in pixels. */
  readonly height: number;
}

/**
 * PNG capture accepted by capture visual assertions.
 * @remarks This shape is compatible with AppWindowScreenshot and RemoteScreenshot.
 */
export interface CaptureImage {
  /** PNG image buffer. */
  readonly image: Buffer;

  /** Requested or source bounds for the captured image. */
  readonly bounds: ScreenRect;

  /** Visible bounds represented by the PNG image. */
  readonly visibleBounds: ScreenRect;

  /** Whether capture bounds were clipped by the platform. */
  readonly clipped: boolean;
}

/**
 * Expected PNG image source for capture visual assertions.
 * @remarks Strings are interpreted as filesystem paths. URL values must use the file protocol.
 */
export type CaptureExpectedImage = Buffer | string | URL;

/** Tesseract page segmentation mode used by capture OCR assertions. */
export type CaptureOcrPageSegmentationMode =
  | 'osdOnly'
  | 'autoOsd'
  | 'autoOnly'
  | 'auto'
  | 'singleColumn'
  | 'singleBlockVerticalText'
  | 'singleBlock'
  | 'singleLine'
  | 'singleWord'
  | 'circleWord'
  | 'singleChar'
  | 'sparseText'
  | 'sparseTextOsd'
  | 'rawLine';

/** Worker lifecycle mode used by capture OCR assertions. */
export type CaptureOcrWorkerMode = 'perRead' | 'shared';

/** Cache mode passed to Tesseract.js language data loading. */
export type CaptureOcrCacheMethod = 'write' | 'readOnly' | 'refresh' | 'none';

/** Defaults shared by capture OCR assertions. */
export interface CaptureOcrDefaults {
  /**
   * Tesseract.js worker lifecycle mode.
   * @remarks The default is perRead, which creates and terminates a worker for each OCR read.
   */
  readonly workerMode?: CaptureOcrWorkerMode;

  /**
   * OCR language code or language code list.
   * @remarks The default is eng. Multiple languages are joined with + for Tesseract.js.
   */
  readonly languages?: string | readonly string[];

  /**
   * Tesseract.js core path.
   * @remarks Omit to use Tesseract.js defaults.
   */
  readonly corePath?: string;

  /**
   * Tesseract.js worker script path.
   * @remarks Omit to use Tesseract.js defaults.
   */
  readonly workerPath?: string;

  /**
   * Language data path passed to Tesseract.js.
   * @remarks Omit to use bundled English traineddata when languages is eng.
   */
  readonly langPath?: string;

  /**
   * Language data cache path passed to Tesseract.js.
   * @remarks Omit to let the selected cacheMethod decide whether Tesseract.js writes a cache file.
   */
  readonly cachePath?: string;

  /**
   * Language data cache behavior passed to Tesseract.js.
   * @remarks The default is none when bundled English traineddata is used.
   */
  readonly cacheMethod?: CaptureOcrCacheMethod;

  /**
   * Whether language data is gzipped.
   * @remarks The default follows the selected language data source.
   */
  readonly gzip?: boolean;
}

/** Output settings shared by capture assertions. */
export interface CaptureResultOutputOptions {
  /**
   * Output directory used as the base path for actual, expected, diff, and metadata files.
   * @remarks Output files are written only when outputResultPath or AGENT_ROVER_VISUAL_OUTPUT_RESULT_PATH is specified.
   */
  readonly outputResultPath?: string;

  /**
   * Artifact variant, usually a platform or backend name.
   * @remarks This value does not enable artifact output by itself.
   */
  readonly variant?: string;
}

/** Defaults shared by capture visual and OCR assertions. */
export interface CaptureVisualDefaults extends CaptureResultOutputOptions {
  /**
   * Defaults for OCR assertions.
   * @remarks OCR workers are created lazily only when an OCR assertion is used.
   */
  readonly ocr?: CaptureOcrDefaults;
}

/** Options shared by visual comparison assertions. */
export interface CaptureVisualComparisonOptions extends CaptureResultOutputOptions {
  /** Region to compare. Omit to compare the full capture image. */
  readonly region?: CapturePixelRegion;

  /** Regions to ignore during comparison. Coordinates use the full capture image. */
  readonly masks?: readonly CapturePixelRegion[];
}

/** Options for pixel-difference based capture comparison. */
export interface CaptureLookSimilarOptions extends CaptureVisualComparisonOptions {
  /** Pixelmatch color threshold from 0 to 1. */
  readonly threshold?: number;

  /** Maximum allowed mismatched pixel count. */
  readonly maxDiffPixels?: number;

  /** Maximum allowed mismatched pixel ratio from 0 to 1. */
  readonly maxDiffRatio?: number;
}

/** Options for structural-similarity (SSIM) based capture comparison. */
export interface CaptureSimilarityOptions extends CaptureVisualComparisonOptions {
  /** Minimum allowed MSSIM score from 0 to 1. */
  readonly minSimilarity?: number;
}

/** Image preprocessing options applied before OCR recognition. */
export interface CaptureOcrPreprocessOptions {
  /**
   * Nearest-neighbor scale factor applied after cropping.
   * @remarks The default is 1.
   */
  readonly scale?: number;

  /**
   * Converts RGB pixels to Rec. 709 luminance before OCR.
   * @remarks Thresholding also uses luminance even when this is false.
   */
  readonly grayscale?: boolean;

  /**
   * Converts luminance values to black or white using this threshold from 0 to 255.
   * @remarks Omit to keep continuous color or grayscale values.
   */
  readonly threshold?: number;

  /**
   * Inverts RGB values after grayscale or threshold processing.
   * @remarks Alpha values are preserved.
   */
  readonly invert?: boolean;
}

/** Options for reading text from a capture with OCR. */
export interface CaptureOcrOptions extends CaptureResultOutputOptions {
  /** Region to pass to OCR. Omit to read the full capture image. */
  readonly region?: CapturePixelRegion;

  /**
   * Tesseract page segmentation modes to try.
   * @remarks The default tries singleBlock, sparseText, singleLine, and singleWord.
   */
  readonly pageSegmentationModes?: readonly CaptureOcrPageSegmentationMode[];

  /** Image preprocessing applied before OCR recognition. */
  readonly preprocess?: CaptureOcrPreprocessOptions;

  /**
   * Additional Tesseract worker parameters applied before each recognition attempt.
   * @remarks tessedit_pageseg_mode is overwritten for each selected pageSegmentationMode.
   */
  readonly parameters?: Readonly<Record<string, string>>;
}

/** Options applied when matching OCR text against an expected value. */
export interface CaptureOcrTextAssertionOptions {
  /**
   * Whether string expectations are case-sensitive.
   * @remarks The default is false. Regular expressions use their own flags.
   */
  readonly caseSensitive?: boolean;

  /**
   * Whether OCR text whitespace is collapsed before matching.
   * @remarks The default is true.
   */
  readonly normalizeWhitespace?: boolean;

  /**
   * Minimum confidence required for a matching OCR attempt.
   * @remarks Omit to accept any confidence value returned by Tesseract.js.
   */
  readonly minConfidence?: number;
}

/** Options for asserting OCR text directly from a capture. */
export interface CaptureOcrAssertionOptions
  extends CaptureOcrOptions, CaptureOcrTextAssertionOptions {}

/** Common result fields returned by capture visual assertions. */
export interface CaptureVisualResult {
  /** Whether the assertion passed. */
  readonly pass: boolean;

  /** Output directory for this assertion call, present when result output is enabled. */
  readonly outputResultPath?: string;

  /** Saved actual PNG path, present when result output is enabled. */
  readonly actualImagePath?: string;

  /** Saved metadata JSON path, present when result output is enabled. */
  readonly metadataJsonPath?: string;

  /** Saved expected PNG path, present when failure output was generated. */
  readonly expectedImagePath?: string;

  /** Saved diff PNG path, present when a diff image was generated. */
  readonly diffImagePath?: string;
}

/** Result returned by pixel-difference based capture comparison. */
export interface CaptureLookSimilarResult extends CaptureVisualResult {
  /** Number of mismatched pixels after comparison processing. */
  readonly diffPixels: number;

  /** Mismatched pixel ratio after comparison processing. */
  readonly diffRatio: number;

  /** Number of pixels included in the comparison region. */
  readonly totalPixels: number;
}

/** Result returned by structural-similarity based capture comparison. */
export interface CaptureSimilarityResult extends CaptureVisualResult {
  /** MSSIM score returned by ssim.js. */
  readonly similarity: number;

  /** Diagnostic mismatched pixel count generated by pixelmatch. */
  readonly diffPixels: number;

  /** Diagnostic mismatched pixel ratio generated by pixelmatch. */
  readonly diffRatio: number;

  /** Number of pixels included in the comparison region. */
  readonly totalPixels: number;
}

/** One OCR word returned by capture OCR assertions. */
export interface CaptureOcrWord {
  /** Raw word text returned by Tesseract.js. */
  readonly text: string;

  /** Whitespace-normalized word text. */
  readonly normalizedText: string;

  /** Word confidence score returned by Tesseract.js. */
  readonly confidence: number;

  /** Word bounds relative to the capture image. */
  readonly bounds: CapturePixelRegion;

  /** Word bounds relative to the root screen. */
  readonly screenBounds: ScreenRect;
}

/** OCR text location match returned by findText(). */
export interface CaptureOcrTextMatch {
  /** Text covered by the matched OCR word span. */
  readonly text: string;

  /** Whitespace-normalized matched text. */
  readonly normalizedText: string;

  /** Confidence score from the selected OCR attempt. */
  readonly confidence: number;

  /** Page segmentation mode from the selected OCR attempt. */
  readonly pageSegmentationMode: CaptureOcrPageSegmentationMode;

  /** Matched word-span bounds relative to the capture image. */
  readonly bounds: CapturePixelRegion;

  /** Matched word-span bounds relative to the root screen. */
  readonly screenBounds: ScreenRect;

  /** OCR words included in the matched span. */
  readonly words: readonly CaptureOcrWord[];
}

/** One OCR recognition attempt for a capture. */
export interface CaptureOcrAttempt {
  /** Page segmentation mode used for this attempt. */
  readonly pageSegmentationMode: CaptureOcrPageSegmentationMode;

  /** Raw text returned by Tesseract.js. */
  readonly text: string;

  /** Whitespace-normalized text returned by Tesseract.js. */
  readonly normalizedText: string;

  /** Confidence score returned by Tesseract.js. */
  readonly confidence: number;

  /** OCR words with capture-relative and screen-relative bounds. */
  readonly words: readonly CaptureOcrWord[];
}

/** Result returned by OCR text assertions. */
export interface CaptureOcrResult extends CaptureVisualResult {
  /** Raw text selected from the best OCR attempt. */
  readonly text: string;

  /** Whitespace-normalized text selected from the best OCR attempt. */
  readonly normalizedText: string;

  /** Confidence score from the selected OCR attempt. */
  readonly confidence: number;

  /** Page segmentation mode from the selected OCR attempt. */
  readonly pageSegmentationMode: CaptureOcrPageSegmentationMode;

  /** All OCR recognition attempts produced for this assertion. */
  readonly attempts: readonly CaptureOcrAttempt[];

  /** Expected OCR text matcher as a diagnostic string. */
  readonly expectedText: string;

  /** Saved OCR input PNG path, present when result output is enabled. */
  readonly ocrInputImagePath?: string;
}

/** OCR text read from a capture and reusable for multiple assertions. */
export interface CaptureOcrText {
  /** Raw text selected from the best OCR attempt. */
  readonly text: string;

  /** Whitespace-normalized text selected from the best OCR attempt. */
  readonly normalizedText: string;

  /** Confidence score from the selected OCR attempt. */
  readonly confidence: number;

  /** Page segmentation mode from the selected OCR attempt. */
  readonly pageSegmentationMode: CaptureOcrPageSegmentationMode;

  /** All OCR recognition attempts produced for this read. */
  readonly attempts: readonly CaptureOcrAttempt[];

  /** Output directory for this OCR read, present when result output is enabled. */
  readonly outputResultPath?: string;

  /** Saved actual PNG path, present when result output is enabled. */
  readonly actualImagePath?: string;

  /** Saved OCR input PNG path, present when result output is enabled. */
  readonly ocrInputImagePath?: string;

  /** Saved metadata JSON path, present when result output is enabled. */
  readonly metadataJsonPath?: string;

  /**
   * Asserts that this OCR text contains the expected string or matches the expected regular expression.
   * @param expected Expected string or regular expression.
   * @param options Text matching options.
   * @returns OCR assertion result when the assertion passes.
   */
  readonly toContainText: (
    expected: string | RegExp,
    options?: CaptureOcrTextAssertionOptions
  ) => Promise<CaptureOcrResult>;

  /**
   * Finds the first OCR word span that contains the expected string or matches the expected regular expression.
   * @param expected Expected string or regular expression.
   * @param options Text matching options.
   * @returns Matched text location, or undefined when no OCR word span matches.
   */
  readonly findText: (
    expected: string | RegExp,
    options?: CaptureOcrTextAssertionOptions
  ) => Promise<CaptureOcrTextMatch | undefined>;
}

/** Error thrown by capture visual assertions. */
export interface CaptureVisualError<
  Result extends CaptureVisualResult = CaptureVisualResult,
> extends Error {
  /** Assertion result and artifact paths available at the failure point. */
  readonly result: Result;
}

/** Assertion object for a single capture image. */
export interface CaptureExpectation {
  /**
   * Compares the capture against an expected PNG image with pixel-level tolerance.
   * @param expectedImage Expected PNG image as a buffer, path string, or file URL.
   * @param options Comparison options.
   * @returns Comparison result when the assertion passes.
   */
  readonly toLookSimilar: (
    expectedImage: CaptureExpectedImage,
    options?: CaptureLookSimilarOptions
  ) => Promise<CaptureLookSimilarResult>;

  /**
   * Compares the capture against an expected PNG image with structural similarity (SSIM).
   * @param expectedImage Expected PNG image as a buffer, path string, or file URL.
   * @param options Comparison options.
   * @returns Comparison result when the assertion passes.
   */
  readonly toHaveSimilarity: (
    expectedImage: CaptureExpectedImage,
    options?: CaptureSimilarityOptions
  ) => Promise<CaptureSimilarityResult>;

  /**
   * Reads text from the capture with OCR and asserts that it contains expected text.
   * @param expected Expected string or regular expression.
   * @param options OCR and text matching options.
   * @returns OCR assertion result when the assertion passes.
   */
  readonly toContainText: (
    expected: string | RegExp,
    options?: CaptureOcrAssertionOptions
  ) => Promise<CaptureOcrResult>;

  /**
   * Reads text from the capture with OCR for reusable text assertions.
   * @param options OCR options.
   * @returns OCR text object that can run multiple assertions without recognizing again.
   */
  readonly readText: (options?: CaptureOcrOptions) => Promise<CaptureOcrText>;
}

/** Factory for capture visual assertions. */
export interface CaptureExpect extends Releaseable {
  /**
   * Releases resources owned by this expectation helper.
   * @remarks Shared OCR workers are terminated when OCR support has created one.
   */
  readonly release: () => Promise<void>;

  /**
   * Creates an assertion object for a captured image.
   * @param capture Capture to assert.
   * @param name Artifact name.
   * @returns Assertion object for the capture.
   */
  readonly expectCapture: (
    capture: CaptureImage,
    name: string
  ) => CaptureExpectation;

  /**
   * Asynchronously releases resources owned by this expectation helper.
   * @remarks OCR shared workers are released by later OCR support.
   */
  readonly [Symbol.asyncDispose]: () => Promise<void>;
}

interface ResolvedDefaults {
  readonly outputResultPath: string;
  readonly variant: string;
}

interface EnabledComparisonContext {
  readonly actualImagePath: string;
  readonly outputResultPath: string;
  readonly artifactsEnabled: true;
  readonly expectedImagePath: string;
  readonly diffImagePath: string;
  readonly metadataJsonPath: string;
  readonly ocrInputImagePath: string;
  readonly resolved: ResolvedDefaults;
}

interface DisabledComparisonContext {
  readonly artifactsEnabled: false;
}

type ComparisonContext = EnabledComparisonContext | DisabledComparisonContext;

interface DecodedPng {
  readonly data: Buffer;
  readonly height: number;
  readonly width: number;
}

interface LoadedExpectedImage {
  readonly data: Buffer;
  readonly source: 'buffer' | 'path' | 'file-url';
  readonly sourcePath: string | undefined;
}

interface ResolvedLookSimilarOptions {
  readonly maxDiffPixels: number | undefined;
  readonly maxDiffRatio: number;
  readonly threshold: number;
}

interface ResolvedSimilarityOptions {
  readonly minSimilarity: number;
}

interface PreparedComparison {
  readonly actualData: Uint8Array;
  readonly expectedData: Uint8Array;
  readonly height: number;
  readonly totalPixels: number;
  readonly width: number;
}

interface PixelComparison {
  readonly diffPixels: number;
  readonly diffPng: PNG;
  readonly diffRatio: number;
  readonly pass: boolean;
  readonly totalPixels: number;
}

interface ResolvedOcrOptions {
  readonly pageSegmentationModes: readonly CaptureOcrPageSegmentationMode[];
  readonly parameters: Readonly<Record<string, string>>;
  readonly preprocess: Required<CaptureOcrPreprocessOptions>;
  readonly region: CapturePixelRegion;
}

interface PreparedOcrImage {
  readonly image: Buffer;
  readonly preprocess: Required<CaptureOcrPreprocessOptions>;
  readonly region: CapturePixelRegion;
}

interface LoadedTesseractModule {
  readonly createWorker: (
    languages?: string | readonly string[],
    oem?: number,
    options?: Partial<TesseractWorkerOptions>
  ) => Promise<TesseractWorker>;
}

interface TesseractWorkerOptions {
  cacheMethod: string;
  cachePath: string;
  corePath: string;
  gzip: boolean;
  langPath: string;
  logger: (message: unknown) => void;
  workerPath: string;
}

interface TesseractWorker {
  readonly recognize: (
    image: Buffer,
    options?: Record<string, never>,
    output?: TesseractRecognizeOutput
  ) => Promise<{
    readonly data: {
      readonly blocks?: readonly TesseractBlock[] | null;
      readonly confidence: number;
      readonly text: string;
      readonly words?: readonly TesseractWord[];
    };
  }>;
  readonly setParameters: (
    parameters: Readonly<Record<string, string>>
  ) => Promise<unknown>;
  readonly terminate: () => Promise<unknown>;
}

interface TesseractRecognizeOutput {
  readonly blocks: boolean;
  readonly text: boolean;
}

interface TesseractBlock {
  readonly paragraphs?: readonly TesseractParagraph[];
}

interface TesseractParagraph {
  readonly lines?: readonly TesseractLine[];
}

interface TesseractLine {
  readonly words?: readonly TesseractWord[];
}

interface TesseractWord {
  readonly bbox?: {
    readonly x0?: number;
    readonly x1?: number;
    readonly y0?: number;
    readonly y1?: number;
  };
  readonly confidence?: number;
  readonly text?: string;
}

interface OcrWorkerController {
  readonly recognize: (
    preparedImage: PreparedOcrImage,
    options: ResolvedOcrOptions
  ) => Promise<readonly CaptureOcrAttempt[]>;
  readonly release: () => Promise<void>;
}

interface OcrTextData {
  readonly actualImagePath: string | undefined;
  readonly attempts: readonly CaptureOcrAttempt[];
  readonly confidence: number;
  readonly metadataJsonPath: string | undefined;
  readonly normalizedText: string;
  readonly ocrInputImagePath: string | undefined;
  readonly outputResultPath: string | undefined;
  readonly pageSegmentationMode: CaptureOcrPageSegmentationMode;
  readonly text: string;
}

const padNumber = (value: number, width: number): string =>
  value.toString().padStart(width, '0');

const hashText = (value: string): string =>
  createHash('sha256').update(value).digest('hex').slice(0, 10);

const defaultOcrPageSegmentationModes = [
  'singleBlock',
  'sparseText',
  'singleLine',
  'singleWord',
] as const satisfies readonly CaptureOcrPageSegmentationMode[];

const pageSegmentationModeValues: Record<
  CaptureOcrPageSegmentationMode,
  string
> = {
  auto: '3',
  autoOnly: '2',
  autoOsd: '1',
  circleWord: '9',
  osdOnly: '0',
  rawLine: '13',
  singleBlock: '6',
  singleBlockVerticalText: '5',
  singleChar: '10',
  singleColumn: '4',
  singleLine: '7',
  singleWord: '8',
  sparseText: '11',
  sparseTextOsd: '12',
};

let tesseractModulePromise: Promise<LoadedTesseractModule> | undefined;

const importTesseractModule = async (): Promise<LoadedTesseractModule> => {
  const loaded = (await import('tesseract.js')) as unknown as
    | LoadedTesseractModule
    | { readonly default: LoadedTesseractModule };
  return 'default' in loaded ? loaded.default : loaded;
};

const loadTesseractModule = async (): Promise<LoadedTesseractModule> => {
  tesseractModulePromise ??= importTesseractModule();
  return await tesseractModulePromise;
};

const normalizePathSegment = (value: string, fallback: string): string => {
  const trimmed = value.trim();
  const normalized = trimmed
    .replace(/[\x00-\x1f<>:"/\\|?*]+/g, '_')
    .replace(/\s+/g, '_')
    .replace(/_+/g, '_')
    .replace(/^_+|_+$/g, '');
  if (normalized.length === 0) {
    return fallback;
  }
  if (normalized.length <= 140) {
    return normalized;
  }
  return `${normalized.slice(0, 129)}-${hashText(normalized)}`;
};

const serializeBounds = (
  bounds: ScreenRect
): Record<keyof ScreenRect, number> => ({
  height: bounds.height,
  width: bounds.width,
  x: bounds.x,
  y: bounds.y,
});

const resolveDefaults = (
  defaults: CaptureVisualDefaults,
  options: CaptureResultOutputOptions | undefined
): ResolvedDefaults | undefined => {
  const outputResultPath =
    options?.outputResultPath ??
    defaults.outputResultPath ??
    process.env.AGENT_ROVER_VISUAL_OUTPUT_RESULT_PATH;
  if (outputResultPath === undefined) {
    return undefined;
  }

  return {
    outputResultPath: resolve(outputResultPath),
    variant:
      options?.variant ??
      defaults.variant ??
      process.env.AGENT_ROVER_VISUAL_VARIANT ??
      'default',
  };
};

const loadExpectedImage = async (
  expectedImage: CaptureExpectedImage
): Promise<LoadedExpectedImage> => {
  if (Buffer.isBuffer(expectedImage)) {
    return {
      data: expectedImage,
      source: 'buffer',
      sourcePath: undefined,
    };
  }

  if (typeof expectedImage === 'string') {
    const sourcePath = resolve(expectedImage);
    return {
      data: await readFile(sourcePath),
      source: 'path',
      sourcePath,
    };
  }

  if (expectedImage.protocol !== 'file:') {
    throw new TypeError('expectedImage URL must use the file: protocol.');
  }

  const sourcePath = fileURLToPath(expectedImage);
  return {
    data: await readFile(sourcePath),
    source: 'file-url',
    sourcePath,
  };
};

const decodePng = (image: Buffer): DecodedPng => {
  const png = PNG.sync.read(image);
  return {
    data: png.data,
    height: png.height,
    width: png.width,
  };
};

const validateCaptureImage = (capture: CaptureImage, png: DecodedPng): void => {
  if (
    png.width !== capture.visibleBounds.width ||
    png.height !== capture.visibleBounds.height
  ) {
    throw new Error(
      `Capture PNG size ${png.width}x${png.height} does not match visible bounds ${capture.visibleBounds.width}x${capture.visibleBounds.height}.`
    );
  }
};

const validateFiniteInteger = (value: number, label: string): void => {
  if (!Number.isInteger(value)) {
    throw new TypeError(`${label} must be an integer.`);
  }
};

const validateRegion = (
  region: CapturePixelRegion,
  imageWidth: number,
  imageHeight: number,
  label: string
): void => {
  validateFiniteInteger(region.x, `${label}.x`);
  validateFiniteInteger(region.y, `${label}.y`);
  validateFiniteInteger(region.width, `${label}.width`);
  validateFiniteInteger(region.height, `${label}.height`);
  if (region.x < 0 || region.y < 0) {
    throw new TypeError(`${label} origin must be inside the capture image.`);
  }
  if (region.width <= 0 || region.height <= 0) {
    throw new TypeError(`${label} size must be positive.`);
  }
  if (
    region.x + region.width > imageWidth ||
    region.y + region.height > imageHeight
  ) {
    throw new TypeError(`${label} must be inside the capture image.`);
  }
};

const validateRatio = (value: number, label: string): void => {
  if (!Number.isFinite(value) || value < 0 || value > 1) {
    throw new TypeError(`${label} must be a number from 0 to 1.`);
  }
};

const validateNonNegativeInteger = (value: number, label: string): void => {
  if (!Number.isInteger(value) || value < 0) {
    throw new TypeError(`${label} must be a non-negative integer.`);
  }
};

const getComparisonRegion = (
  options: CaptureVisualComparisonOptions | undefined,
  imageWidth: number,
  imageHeight: number
): CapturePixelRegion => {
  const region =
    options?.region ??
    ({ height: imageHeight, width: imageWidth, x: 0, y: 0 } as const);
  validateRegion(region, imageWidth, imageHeight, 'region');
  return region;
};

const validateMasks = (
  masks: readonly CapturePixelRegion[] | undefined,
  imageWidth: number,
  imageHeight: number
): void => {
  if (masks === undefined) {
    return;
  }
  for (const [index, mask] of masks.entries()) {
    validateRegion(mask, imageWidth, imageHeight, `masks[${index}]`);
  }
};

const copyRegionData = (
  source: DecodedPng,
  region: CapturePixelRegion
): Uint8Array => {
  const data = new Uint8Array(region.width * region.height * 4);
  for (let y = 0; y < region.height; y += 1) {
    const sourceStart = ((region.y + y) * source.width + region.x) * 4;
    const sourceEnd = sourceStart + region.width * 4;
    const targetStart = y * region.width * 4;
    data.set(source.data.subarray(sourceStart, sourceEnd), targetStart);
  }
  return data;
};

const applyMasks = (
  actualData: Uint8Array,
  expectedData: Uint8Array,
  region: CapturePixelRegion,
  masks: readonly CapturePixelRegion[] | undefined
): void => {
  if (masks === undefined) {
    return;
  }
  for (const mask of masks) {
    const left = Math.max(region.x, mask.x);
    const top = Math.max(region.y, mask.y);
    const right = Math.min(region.x + region.width, mask.x + mask.width);
    const bottom = Math.min(region.y + region.height, mask.y + mask.height);
    if (left >= right || top >= bottom) {
      continue;
    }
    for (let y = top; y < bottom; y += 1) {
      for (let x = left; x < right; x += 1) {
        const index = ((y - region.y) * region.width + (x - region.x)) * 4;
        actualData[index] = 0;
        actualData[index + 1] = 0;
        actualData[index + 2] = 0;
        actualData[index + 3] = 0;
        expectedData[index] = 0;
        expectedData[index + 1] = 0;
        expectedData[index + 2] = 0;
        expectedData[index + 3] = 0;
      }
    }
  }
};

const prepareComparison = (
  actualPng: DecodedPng,
  expectedPng: DecodedPng,
  options: CaptureVisualComparisonOptions | undefined
): PreparedComparison => {
  if (
    actualPng.width !== expectedPng.width ||
    actualPng.height !== expectedPng.height
  ) {
    throw new Error(
      `Expected image size ${expectedPng.width}x${expectedPng.height} does not match actual capture size ${actualPng.width}x${actualPng.height}.`
    );
  }

  const region = getComparisonRegion(
    options,
    actualPng.width,
    actualPng.height
  );
  validateMasks(options?.masks, actualPng.width, actualPng.height);

  const actualData = copyRegionData(actualPng, region);
  const expectedData = copyRegionData(expectedPng, region);
  applyMasks(actualData, expectedData, region, options?.masks);

  return {
    actualData,
    expectedData,
    height: region.height,
    totalPixels: region.width * region.height,
    width: region.width,
  };
};

const createDiffPng = (
  comparison: PreparedComparison,
  threshold: number
): {
  readonly diffPixels: number;
  readonly diffPng: PNG;
} => {
  const diffPng = new PNG({
    height: comparison.height,
    width: comparison.width,
  });
  const diffPixels = pixelmatch(
    comparison.expectedData,
    comparison.actualData,
    diffPng.data,
    comparison.width,
    comparison.height,
    { threshold }
  );
  return {
    diffPixels,
    diffPng,
  };
};

const createContext = async (
  defaults: CaptureVisualDefaults,
  options: CaptureResultOutputOptions | undefined,
  name: string,
  counter: number
): Promise<ComparisonContext> => {
  const resolved = resolveDefaults(defaults, options);
  if (resolved === undefined) {
    return {
      artifactsEnabled: false,
    };
  }

  const safeVariant = normalizePathSegment(resolved.variant, 'default');
  const safeName = normalizePathSegment(name, 'capture');
  const outputResultPath = join(
    resolved.outputResultPath,
    safeVariant,
    `${safeName}-${padNumber(counter, 6)}`
  );
  await mkdir(outputResultPath, { recursive: true });
  return {
    actualImagePath: join(outputResultPath, 'actual.png'),
    outputResultPath,
    artifactsEnabled: true,
    diffImagePath: join(outputResultPath, 'diff.png'),
    expectedImagePath: join(outputResultPath, 'expected.png'),
    metadataJsonPath: join(outputResultPath, 'metadata.json'),
    ocrInputImagePath: join(outputResultPath, 'ocr-input.png'),
    resolved,
  };
};

const writeMetadata = async (
  context: EnabledComparisonContext,
  capture: CaptureImage,
  name: string,
  matcher: string,
  options: CaptureVisualComparisonOptions | undefined,
  expectedImage: LoadedExpectedImage
): Promise<void> => {
  await writeFile(
    context.metadataJsonPath,
    `${JSON.stringify(
      {
        bounds: serializeBounds(capture.bounds),
        clipped: capture.clipped,
        expectedImageSource: expectedImage.source,
        expectedImageSourcePath: expectedImage.sourcePath,
        imageBytes: capture.image.length,
        matcher,
        masks: options?.masks,
        name,
        region: options?.region,
        variant: context.resolved.variant,
        visibleBounds: serializeBounds(capture.visibleBounds),
      },
      undefined,
      2
    )}\n`
  );
};

const createVisualError = <Result extends CaptureVisualResult>(
  message: string,
  result: Result
): CaptureVisualError<Result> =>
  Object.assign(new Error(message), {
    result,
  });

const saveActualAndMetadata = async (
  context: ComparisonContext,
  capture: CaptureImage,
  name: string,
  matcher: string,
  options: CaptureVisualComparisonOptions | undefined,
  expectedImage: LoadedExpectedImage
): Promise<void> => {
  if (!context.artifactsEnabled) {
    return;
  }

  await writeFile(context.actualImagePath, capture.image);
  await writeMetadata(context, capture, name, matcher, options, expectedImage);
};

const buildBaseResult = (context: ComparisonContext): CaptureVisualResult => {
  if (!context.artifactsEnabled) {
    return {
      pass: true,
    };
  }

  return {
    actualImagePath: context.actualImagePath,
    outputResultPath: context.outputResultPath,
    metadataJsonPath: context.metadataJsonPath,
    pass: true,
  };
};

const writeFailureArtifacts = async (
  context: ComparisonContext,
  expectedImage: Buffer,
  diffPng: PNG
): Promise<
  Pick<CaptureVisualResult, 'diffImagePath' | 'expectedImagePath'>
> => {
  if (!context.artifactsEnabled) {
    return {};
  }

  await writeFile(context.expectedImagePath, expectedImage);
  await writeFile(context.diffImagePath, PNG.sync.write(diffPng));
  return {
    diffImagePath: context.diffImagePath,
    expectedImagePath: context.expectedImagePath,
  };
};

const isLookSimilarPass = (
  diffPixels: number,
  diffRatio: number,
  options: ResolvedLookSimilarOptions
): boolean => {
  const ratioPass = diffRatio <= options.maxDiffRatio;
  const pixelsPass =
    options.maxDiffPixels === undefined || diffPixels <= options.maxDiffPixels;
  return ratioPass && pixelsPass;
};

const resolveLookSimilarOptions = (
  options: CaptureLookSimilarOptions | undefined
): ResolvedLookSimilarOptions => {
  const resolved = {
    maxDiffPixels: options?.maxDiffPixels,
    maxDiffRatio: options?.maxDiffRatio ?? 0.01,
    threshold: options?.threshold ?? 0.1,
  };
  validateRatio(resolved.threshold, 'threshold');
  validateRatio(resolved.maxDiffRatio, 'maxDiffRatio');
  if (resolved.maxDiffPixels !== undefined) {
    validateNonNegativeInteger(resolved.maxDiffPixels, 'maxDiffPixels');
  }
  return resolved;
};

const resolveSimilarityOptions = (
  options: CaptureSimilarityOptions | undefined
): ResolvedSimilarityOptions => {
  const resolved = {
    minSimilarity: options?.minSimilarity ?? 0.985,
  };
  validateRatio(resolved.minSimilarity, 'minSimilarity');
  return resolved;
};

const compareDecodedPngs = (
  actualPng: DecodedPng,
  expectedPng: DecodedPng,
  options: CaptureLookSimilarOptions | undefined
): PixelComparison => {
  const comparisonOptions = resolveLookSimilarOptions(options);
  const comparison = prepareComparison(actualPng, expectedPng, options);
  const { diffPixels, diffPng } = createDiffPng(
    comparison,
    comparisonOptions.threshold
  );
  const diffRatio =
    comparison.totalPixels === 0 ? 0 : diffPixels / comparison.totalPixels;
  return {
    diffPixels,
    diffPng,
    diffRatio,
    pass: isLookSimilarPass(diffPixels, diffRatio, comparisonOptions),
    totalPixels: comparison.totalPixels,
  };
};

/**
 * Compares two PNG images with the same pixel comparison engine used by capture assertions.
 * @param actualImage Actual PNG image buffer.
 * @param expectedImage Expected PNG image buffer.
 * @param options Pixel comparison options.
 * @returns Pixel comparison result without throwing on mismatch.
 */
export const comparePngImages = (
  actualImage: Buffer,
  expectedImage: Buffer,
  options?: CaptureLookSimilarOptions
): CaptureLookSimilarResult => {
  const comparison = compareDecodedPngs(
    decodePng(actualImage),
    decodePng(expectedImage),
    options
  );
  return {
    diffPixels: comparison.diffPixels,
    diffRatio: comparison.diffRatio,
    pass: comparison.pass,
    totalPixels: comparison.totalPixels,
  };
};

const validateOcrPageSegmentationMode = (
  value: CaptureOcrPageSegmentationMode,
  label: string
): void => {
  if (!(value in pageSegmentationModeValues)) {
    throw new TypeError(
      `${label} must be a supported OCR page segmentation mode.`
    );
  }
};

const validateThreshold = (value: number, label: string): void => {
  if (!Number.isInteger(value) || value < 0 || value > 255) {
    throw new TypeError(`${label} must be an integer from 0 to 255.`);
  }
};

const validatePositiveInteger = (value: number, label: string): void => {
  if (!Number.isInteger(value) || value <= 0) {
    throw new TypeError(`${label} must be a positive integer.`);
  }
};

const normalizeWhitespace = (value: string): string =>
  value.replace(/\s+/g, ' ').trim();

const screenBoundsFromCaptureBounds = (
  visibleBounds: ScreenRect,
  bounds: CapturePixelRegion
): ScreenRect => ({
  height: bounds.height,
  width: bounds.width,
  x: visibleBounds.x + bounds.x,
  y: visibleBounds.y + bounds.y,
});

const resolveOcrPreprocessOptions = (
  options: CaptureOcrPreprocessOptions | undefined
): Required<CaptureOcrPreprocessOptions> => {
  const resolved = {
    grayscale: options?.grayscale ?? false,
    invert: options?.invert ?? false,
    scale: options?.scale ?? 1,
    threshold: options?.threshold ?? -1,
  };
  validatePositiveInteger(resolved.scale, 'preprocess.scale');
  if (resolved.threshold !== -1) {
    validateThreshold(resolved.threshold, 'preprocess.threshold');
  }
  return resolved;
};

const resolveOcrOptions = (
  options: CaptureOcrOptions | undefined,
  imageWidth: number,
  imageHeight: number
): ResolvedOcrOptions => {
  const pageSegmentationModes =
    options?.pageSegmentationModes ?? defaultOcrPageSegmentationModes;
  if (pageSegmentationModes.length === 0) {
    throw new TypeError('pageSegmentationModes must not be empty.');
  }
  for (const [index, mode] of pageSegmentationModes.entries()) {
    validateOcrPageSegmentationMode(mode, `pageSegmentationModes[${index}]`);
  }
  const region =
    options?.region ??
    ({ height: imageHeight, width: imageWidth, x: 0, y: 0 } as const);
  validateRegion(region, imageWidth, imageHeight, 'region');
  return {
    pageSegmentationModes,
    parameters: options?.parameters ?? {},
    preprocess: resolveOcrPreprocessOptions(options?.preprocess),
    region,
  };
};

const hasOcrPreprocessing = (
  options: Required<CaptureOcrPreprocessOptions>,
  region: CapturePixelRegion,
  imageWidth: number,
  imageHeight: number
): boolean =>
  options.grayscale ||
  options.invert ||
  options.scale !== 1 ||
  options.threshold !== -1 ||
  region.x !== 0 ||
  region.y !== 0 ||
  region.width !== imageWidth ||
  region.height !== imageHeight;

const getLuminance = (red: number, green: number, blue: number): number =>
  Math.round(red * 0.2126 + green * 0.7152 + blue * 0.0722);

const prepareOcrImage = (
  originalImage: Buffer,
  png: DecodedPng,
  options: ResolvedOcrOptions
): PreparedOcrImage => {
  if (
    !hasOcrPreprocessing(
      options.preprocess,
      options.region,
      png.width,
      png.height
    )
  ) {
    return {
      image: originalImage,
      preprocess: options.preprocess,
      region: options.region,
    };
  }

  const output = new PNG({
    height: options.region.height * options.preprocess.scale,
    width: options.region.width * options.preprocess.scale,
  });
  for (let y = 0; y < output.height; y += 1) {
    for (let x = 0; x < output.width; x += 1) {
      const sourceX =
        options.region.x + Math.floor(x / options.preprocess.scale);
      const sourceY =
        options.region.y + Math.floor(y / options.preprocess.scale);
      const sourceIndex = (sourceY * png.width + sourceX) * 4;
      const targetIndex = (y * output.width + x) * 4;
      const red = png.data[sourceIndex]!;
      const green = png.data[sourceIndex + 1]!;
      const blue = png.data[sourceIndex + 2]!;
      const alpha = png.data[sourceIndex + 3]!;
      const luminance = getLuminance(red, green, blue);
      const thresholded =
        options.preprocess.threshold === -1
          ? luminance
          : luminance >= options.preprocess.threshold
            ? 255
            : 0;
      const value =
        options.preprocess.grayscale || options.preprocess.threshold !== -1
          ? thresholded
          : undefined;
      output.data[targetIndex] = options.preprocess.invert
        ? 255 - (value ?? red)
        : (value ?? red);
      output.data[targetIndex + 1] = options.preprocess.invert
        ? 255 - (value ?? green)
        : (value ?? green);
      output.data[targetIndex + 2] = options.preprocess.invert
        ? 255 - (value ?? blue)
        : (value ?? blue);
      output.data[targetIndex + 3] = alpha;
    }
  }

  return {
    image: PNG.sync.write(output),
    preprocess: options.preprocess,
    region: options.region,
  };
};

const isEnglishOnlyLanguage = (
  languages: string | readonly string[] | undefined
): boolean => {
  if (languages === undefined) {
    return true;
  }
  if (typeof languages === 'string') {
    return languages === 'eng';
  }
  return languages.length === 1 && languages[0] === 'eng';
};

const resolveOcrWorkerOptions = (
  defaults: CaptureOcrDefaults | undefined
): {
  readonly languages: string | readonly string[];
  readonly options: Partial<TesseractWorkerOptions>;
} => {
  const languages =
    defaults?.languages === undefined
      ? englishLanguageData.code
      : typeof defaults.languages === 'string'
        ? defaults.languages
        : [...defaults.languages];
  const useBundledEnglishData =
    defaults?.langPath === undefined && isEnglishOnlyLanguage(languages);
  const workerOptions: Partial<TesseractWorkerOptions> = {
    logger: () => undefined,
  };
  if (defaults?.cachePath !== undefined) {
    workerOptions.cachePath = defaults.cachePath;
  }
  if (defaults?.corePath !== undefined) {
    workerOptions.corePath = defaults.corePath;
  }
  if (defaults?.workerPath !== undefined) {
    workerOptions.workerPath = defaults.workerPath;
  }
  if (defaults?.langPath !== undefined) {
    workerOptions.langPath = defaults.langPath;
  } else if (useBundledEnglishData) {
    workerOptions.langPath = englishLanguageData.langPath;
  }
  if (defaults?.gzip !== undefined) {
    workerOptions.gzip = defaults.gzip;
  } else if (useBundledEnglishData) {
    workerOptions.gzip = englishLanguageData.gzip;
  }
  if (defaults?.cacheMethod !== undefined) {
    workerOptions.cacheMethod = defaults.cacheMethod;
  } else if (useBundledEnglishData) {
    workerOptions.cacheMethod = 'none';
  }
  return {
    languages,
    options: workerOptions,
  };
};

const createOcrWorker = async (
  defaults: CaptureOcrDefaults | undefined
): Promise<TesseractWorker> => {
  const tesseract = await loadTesseractModule();
  const resolved = resolveOcrWorkerOptions(defaults);
  return await tesseract.createWorker(
    resolved.languages,
    undefined,
    resolved.options
  );
};

const finiteNumber = (value: unknown): number | undefined =>
  typeof value === 'number' && Number.isFinite(value) ? value : undefined;

const convertTesseractWord = (
  word: TesseractWord,
  preparedImage: PreparedOcrImage,
  fallbackConfidence: number
): CaptureOcrWord | undefined => {
  const text = typeof word.text === 'string' ? word.text : undefined;
  const x0 = finiteNumber(word.bbox?.x0);
  const x1 = finiteNumber(word.bbox?.x1);
  const y0 = finiteNumber(word.bbox?.y0);
  const y1 = finiteNumber(word.bbox?.y1);
  if (
    text === undefined ||
    text.length === 0 ||
    x0 === undefined ||
    x1 === undefined ||
    y0 === undefined ||
    y1 === undefined ||
    x1 <= x0 ||
    y1 <= y0
  ) {
    return undefined;
  }

  const scale = preparedImage.preprocess.scale;
  const left = Math.floor(preparedImage.region.x + x0 / scale);
  const top = Math.floor(preparedImage.region.y + y0 / scale);
  const right = Math.ceil(preparedImage.region.x + x1 / scale);
  const bottom = Math.ceil(preparedImage.region.y + y1 / scale);
  const bounds = {
    height: Math.max(1, bottom - top),
    width: Math.max(1, right - left),
    x: left,
    y: top,
  };
  return {
    bounds,
    confidence: Number.isFinite(word.confidence)
      ? (word.confidence as number)
      : fallbackConfidence,
    normalizedText: normalizeWhitespace(text),
    screenBounds: screenBoundsFromCaptureBounds(
      { height: 0, width: 0, x: 0, y: 0 },
      bounds
    ),
    text,
  };
};

const collectTesseractWordsFromBlocks = (
  blocks: readonly TesseractBlock[] | null | undefined
): readonly TesseractWord[] => {
  if (blocks === null || blocks === undefined) {
    return [];
  }

  const words: TesseractWord[] = [];
  for (const block of blocks) {
    for (const paragraph of block.paragraphs ?? []) {
      for (const line of paragraph.lines ?? []) {
        words.push(...(line.words ?? []));
      }
    }
  }
  return words;
};

const collectTesseractWords = (data: {
  readonly blocks?: readonly TesseractBlock[] | null;
  readonly words?: readonly TesseractWord[];
}): readonly TesseractWord[] =>
  data.words !== undefined && data.words.length > 0
    ? data.words
    : collectTesseractWordsFromBlocks(data.blocks);

const addScreenBoundsToWords = (
  words: readonly CaptureOcrWord[],
  visibleBounds: ScreenRect
): readonly CaptureOcrWord[] =>
  words.map((word) => ({
    ...word,
    screenBounds: screenBoundsFromCaptureBounds(visibleBounds, word.bounds),
  }));

const addScreenBoundsToAttempts = (
  attempts: readonly CaptureOcrAttempt[],
  visibleBounds: ScreenRect
): readonly CaptureOcrAttempt[] =>
  attempts.map((attempt) => ({
    ...attempt,
    words: addScreenBoundsToWords(attempt.words, visibleBounds),
  }));

const recognizeWithWorker = async (
  worker: TesseractWorker,
  preparedImage: PreparedOcrImage,
  options: ResolvedOcrOptions
): Promise<readonly CaptureOcrAttempt[]> => {
  const attempts: CaptureOcrAttempt[] = [];
  for (const pageSegmentationMode of options.pageSegmentationModes) {
    await worker.setParameters({
      ...options.parameters,
      tessedit_pageseg_mode: pageSegmentationModeValues[pageSegmentationMode],
    });
    const recognized = await worker.recognize(
      preparedImage.image,
      {},
      { blocks: true, text: true }
    );
    const confidence = Number.isFinite(recognized.data.confidence)
      ? recognized.data.confidence
      : 0;
    attempts.push({
      confidence,
      normalizedText: normalizeWhitespace(recognized.data.text),
      pageSegmentationMode,
      text: recognized.data.text,
      words: collectTesseractWords(recognized.data)
        .map((word) => convertTesseractWord(word, preparedImage, confidence))
        .filter((word): word is CaptureOcrWord => word !== undefined),
    });
  }
  return attempts;
};

const createOcrWorkerController = (
  defaults: CaptureOcrDefaults | undefined
): OcrWorkerController => {
  if (defaults?.workerMode !== 'shared') {
    return {
      recognize: async (
        preparedImage: PreparedOcrImage,
        options: ResolvedOcrOptions
      ): Promise<readonly CaptureOcrAttempt[]> => {
        const worker = await createOcrWorker(defaults);
        try {
          return await recognizeWithWorker(worker, preparedImage, options);
        } finally {
          await worker.terminate();
        }
      },
      release: async (): Promise<void> => undefined,
    };
  }

  let workerPromise: Promise<TesseractWorker> | undefined;
  let queue: Promise<void> = Promise.resolve();
  const getWorker = (): Promise<TesseractWorker> => {
    workerPromise ??= createOcrWorker(defaults);
    return workerPromise;
  };
  const enqueue = async <T>(operation: () => Promise<T>): Promise<T> => {
    const previous = queue;
    let resolveNext: () => void = () => undefined;
    queue = new Promise<void>((resolve) => {
      resolveNext = resolve;
    });
    await previous;
    try {
      return await operation();
    } finally {
      resolveNext();
    }
  };

  return {
    recognize: async (
      preparedImage: PreparedOcrImage,
      options: ResolvedOcrOptions
    ): Promise<readonly CaptureOcrAttempt[]> =>
      await enqueue(async () =>
        recognizeWithWorker(await getWorker(), preparedImage, options)
      ),
    release: async (): Promise<void> => {
      await queue;
      if (workerPromise === undefined) {
        return;
      }
      const worker = await workerPromise;
      workerPromise = undefined;
      await worker.terminate();
    },
  };
};

const selectBestOcrAttempt = (
  attempts: readonly CaptureOcrAttempt[]
): CaptureOcrAttempt => {
  const [firstAttempt] = attempts;
  if (firstAttempt === undefined) {
    throw new Error('OCR did not return any recognition attempts.');
  }
  return attempts.reduce((best, current) =>
    current.confidence > best.confidence ? current : best
  );
};

const createOcrTextData = (
  context: ComparisonContext,
  attempts: readonly CaptureOcrAttempt[]
): OcrTextData => {
  const bestAttempt = selectBestOcrAttempt(attempts);
  if (!context.artifactsEnabled) {
    return {
      actualImagePath: undefined,
      attempts,
      confidence: bestAttempt.confidence,
      metadataJsonPath: undefined,
      normalizedText: bestAttempt.normalizedText,
      ocrInputImagePath: undefined,
      outputResultPath: undefined,
      pageSegmentationMode: bestAttempt.pageSegmentationMode,
      text: bestAttempt.text,
    };
  }
  return {
    actualImagePath: context.actualImagePath,
    attempts,
    confidence: bestAttempt.confidence,
    metadataJsonPath: context.metadataJsonPath,
    normalizedText: bestAttempt.normalizedText,
    ocrInputImagePath: context.ocrInputImagePath,
    outputResultPath: context.outputResultPath,
    pageSegmentationMode: bestAttempt.pageSegmentationMode,
    text: bestAttempt.text,
  };
};

const validateConfidence = (value: number, label: string): void => {
  if (!Number.isFinite(value) || value < 0 || value > 100) {
    throw new TypeError(`${label} must be a number from 0 to 100.`);
  }
};

const formatExpectedText = (expected: string | RegExp): string =>
  typeof expected === 'string' ? expected : expected.toString();

const serializeExpectedText = (
  expected: string | RegExp | undefined
):
  | {
      readonly type: 'string';
      readonly value: string;
    }
  | {
      readonly flags: string;
      readonly source: string;
      readonly type: 'regexp';
    }
  | undefined => {
  if (expected === undefined) {
    return undefined;
  }
  if (typeof expected === 'string') {
    return {
      type: 'string',
      value: expected,
    };
  }
  return {
    flags: expected.flags,
    source: expected.source,
    type: 'regexp',
  };
};

const writeOcrMetadata = async (
  context: EnabledComparisonContext,
  capture: CaptureImage,
  name: string,
  matcher: string,
  options: CaptureOcrOptions | undefined,
  preparedImage: PreparedOcrImage,
  attempts: readonly CaptureOcrAttempt[],
  expected: string | RegExp | undefined
): Promise<void> => {
  await writeFile(
    context.metadataJsonPath,
    `${JSON.stringify(
      {
        attempts,
        bounds: serializeBounds(capture.bounds),
        clipped: capture.clipped,
        expectedText: serializeExpectedText(expected),
        imageBytes: capture.image.length,
        matcher,
        name,
        ocrInputBytes: preparedImage.image.length,
        pageSegmentationModes: options?.pageSegmentationModes,
        parameters: options?.parameters,
        preprocess: preparedImage.preprocess,
        region: preparedImage.region,
        variant: context.resolved.variant,
        visibleBounds: serializeBounds(capture.visibleBounds),
      },
      undefined,
      2
    )}\n`
  );
};

const saveOcrArtifactsAndMetadata = async (
  context: ComparisonContext,
  capture: CaptureImage,
  name: string,
  matcher: string,
  options: CaptureOcrOptions | undefined,
  preparedImage: PreparedOcrImage,
  attempts: readonly CaptureOcrAttempt[],
  expected: string | RegExp | undefined
): Promise<void> => {
  if (!context.artifactsEnabled) {
    return;
  }

  await writeFile(context.actualImagePath, capture.image);
  await writeFile(context.ocrInputImagePath, preparedImage.image);
  await writeOcrMetadata(
    context,
    capture,
    name,
    matcher,
    options,
    preparedImage,
    attempts,
    expected
  );
};

const matchOcrAttempt = (
  attempt: CaptureOcrAttempt,
  expected: string | RegExp,
  options: CaptureOcrTextAssertionOptions | undefined
): boolean => {
  const normalize = options?.normalizeWhitespace ?? true;
  const candidate = normalize ? attempt.normalizedText : attempt.text;
  const minConfidence = options?.minConfidence;
  if (minConfidence !== undefined) {
    validateConfidence(minConfidence, 'minConfidence');
    if (attempt.confidence < minConfidence) {
      return false;
    }
  }

  if (typeof expected === 'string') {
    if (expected.length === 0) {
      throw new TypeError('expected text must not be empty.');
    }
    const expectedText = normalize ? normalizeWhitespace(expected) : expected;
    if (expectedText.length === 0) {
      throw new TypeError(
        'expected text must not be empty after normalization.'
      );
    }
    if (options?.caseSensitive ?? false) {
      return candidate.includes(expectedText);
    }
    return candidate
      .toLocaleLowerCase()
      .includes(expectedText.toLocaleLowerCase());
  }

  expected.lastIndex = 0;
  const matched = expected.test(candidate);
  expected.lastIndex = 0;
  return matched;
};

const createOcrResult = (
  data: OcrTextData,
  expected: string | RegExp,
  pass: boolean,
  selectedAttempt: CaptureOcrAttempt
): CaptureOcrResult => {
  const artifactPaths =
    data.outputResultPath === undefined
      ? {}
      : {
          actualImagePath: data.actualImagePath!,
          metadataJsonPath: data.metadataJsonPath!,
          ocrInputImagePath: data.ocrInputImagePath!,
          outputResultPath: data.outputResultPath,
        };
  return {
    ...artifactPaths,
    attempts: data.attempts,
    confidence: selectedAttempt.confidence,
    expectedText: formatExpectedText(expected),
    normalizedText: selectedAttempt.normalizedText,
    pageSegmentationMode: selectedAttempt.pageSegmentationMode,
    pass,
    text: selectedAttempt.text,
  };
};

const assertOcrText = async (
  data: OcrTextData,
  expected: string | RegExp,
  options: CaptureOcrTextAssertionOptions | undefined
): Promise<CaptureOcrResult> => {
  const matchedAttempt = data.attempts.find((attempt) =>
    matchOcrAttempt(attempt, expected, options)
  );
  if (matchedAttempt !== undefined) {
    return createOcrResult(data, expected, true, matchedAttempt);
  }

  const bestAttempt = selectBestOcrAttempt(data.attempts);
  const failedResult = createOcrResult(data, expected, false, bestAttempt);
  throw createVisualError(
    `Capture OCR text did not contain ${formatExpectedText(
      expected
    )}. Recognized text: ${JSON.stringify(bestAttempt.normalizedText)}.`,
    failedResult
  );
};

const candidateMatchesExpectedText = (
  candidate: string,
  expected: string | RegExp,
  options: CaptureOcrTextAssertionOptions | undefined
): boolean => {
  if (typeof expected === 'string') {
    if (expected.length === 0) {
      throw new TypeError('expected text must not be empty.');
    }
    const expectedText =
      (options?.normalizeWhitespace ?? true)
        ? normalizeWhitespace(expected)
        : expected;
    if (expectedText.length === 0) {
      throw new TypeError(
        'expected text must not be empty after normalization.'
      );
    }
    if (options?.caseSensitive ?? false) {
      return candidate.includes(expectedText);
    }
    return candidate
      .toLocaleLowerCase()
      .includes(expectedText.toLocaleLowerCase());
  }

  expected.lastIndex = 0;
  const matched = expected.test(candidate);
  expected.lastIndex = 0;
  return matched;
};

const unionPixelRegions = (
  regions: readonly CapturePixelRegion[]
): CapturePixelRegion => {
  const left = Math.min(...regions.map((region) => region.x));
  const top = Math.min(...regions.map((region) => region.y));
  const right = Math.max(...regions.map((region) => region.x + region.width));
  const bottom = Math.max(...regions.map((region) => region.y + region.height));
  return {
    height: bottom - top,
    width: right - left,
    x: left,
    y: top,
  };
};

const createOcrTextMatch = (
  attempt: CaptureOcrAttempt,
  words: readonly CaptureOcrWord[]
): CaptureOcrTextMatch => {
  const bounds = unionPixelRegions(words.map((word) => word.bounds));
  const screenBounds = unionPixelRegions(
    words.map((word) => word.screenBounds)
  );
  const text = words.map((word) => word.text).join(' ');
  return {
    bounds,
    confidence: attempt.confidence,
    normalizedText: normalizeWhitespace(text),
    pageSegmentationMode: attempt.pageSegmentationMode,
    screenBounds,
    text,
    words,
  };
};

const findOcrTextMatchInAttempt = (
  attempt: CaptureOcrAttempt,
  expected: string | RegExp,
  options: CaptureOcrTextAssertionOptions | undefined
): CaptureOcrTextMatch | undefined => {
  const minConfidence = options?.minConfidence;
  if (minConfidence !== undefined) {
    validateConfidence(minConfidence, 'minConfidence');
    if (attempt.confidence < minConfidence) {
      return undefined;
    }
  }

  for (let length = 1; length <= attempt.words.length; length += 1) {
    for (let start = 0; start + length <= attempt.words.length; start += 1) {
      const words = attempt.words.slice(start, start + length);
      const rawText = words.map((word) => word.text).join(' ');
      const candidate =
        (options?.normalizeWhitespace ?? true)
          ? normalizeWhitespace(rawText)
          : rawText;
      if (candidateMatchesExpectedText(candidate, expected, options)) {
        return createOcrTextMatch(attempt, words);
      }
    }
  }
  return undefined;
};

const findOcrTextMatch = (
  data: OcrTextData,
  expected: string | RegExp,
  options: CaptureOcrTextAssertionOptions | undefined
): CaptureOcrTextMatch | undefined => {
  for (const attempt of data.attempts) {
    const match = findOcrTextMatchInAttempt(attempt, expected, options);
    if (match !== undefined) {
      return match;
    }
  }
  return undefined;
};

const createOcrText = (data: OcrTextData): CaptureOcrText => {
  const artifactPaths =
    data.outputResultPath === undefined
      ? {}
      : {
          actualImagePath: data.actualImagePath!,
          metadataJsonPath: data.metadataJsonPath!,
          ocrInputImagePath: data.ocrInputImagePath!,
          outputResultPath: data.outputResultPath,
        };
  return {
    ...artifactPaths,
    attempts: data.attempts,
    confidence: data.confidence,
    normalizedText: data.normalizedText,
    pageSegmentationMode: data.pageSegmentationMode,
    text: data.text,
    findText: async (
      expected: string | RegExp,
      options?: CaptureOcrTextAssertionOptions
    ): Promise<CaptureOcrTextMatch | undefined> =>
      findOcrTextMatch(data, expected, options),
    toContainText: async (
      expected: string | RegExp,
      options?: CaptureOcrTextAssertionOptions
    ): Promise<CaptureOcrResult> =>
      await assertOcrText(data, expected, options),
  };
};

const readCaptureText = async (
  defaults: CaptureVisualDefaults,
  nextCounter: () => number,
  workerController: OcrWorkerController,
  capture: CaptureImage,
  name: string,
  matcher: string,
  options: CaptureOcrOptions | undefined,
  expected: string | RegExp | undefined
): Promise<CaptureOcrText> => {
  const context = await createContext(defaults, options, name, nextCounter());
  const actualPng = decodePng(capture.image);
  validateCaptureImage(capture, actualPng);
  const ocrOptions = resolveOcrOptions(
    options,
    actualPng.width,
    actualPng.height
  );
  const preparedImage = prepareOcrImage(capture.image, actualPng, ocrOptions);
  const attempts = addScreenBoundsToAttempts(
    await workerController.recognize(preparedImage, ocrOptions),
    capture.visibleBounds
  );
  await saveOcrArtifactsAndMetadata(
    context,
    capture,
    name,
    matcher,
    options,
    preparedImage,
    attempts,
    expected
  );
  return createOcrText(createOcrTextData(context, attempts));
};

const createLookSimilarAssertion = (
  defaults: CaptureVisualDefaults,
  nextCounter: () => number,
  capture: CaptureImage,
  name: string
): ((
  expectedImage: CaptureExpectedImage,
  options?: CaptureLookSimilarOptions
) => Promise<CaptureLookSimilarResult>) => {
  const toLookSimilar = async (
    expectedImage: CaptureExpectedImage,
    options?: CaptureLookSimilarOptions
  ): Promise<CaptureLookSimilarResult> => {
    const context = await createContext(defaults, options, name, nextCounter());
    const actualPng = decodePng(capture.image);
    validateCaptureImage(capture, actualPng);
    const loadedExpectedImage = await loadExpectedImage(expectedImage);
    const expectedPng = decodePng(loadedExpectedImage.data);
    await saveActualAndMetadata(
      context,
      capture,
      name,
      'toLookSimilar',
      options,
      loadedExpectedImage
    );

    const comparisonOptions = resolveLookSimilarOptions(options);
    const comparison = prepareComparison(actualPng, expectedPng, options);
    const { diffPixels, diffPng } = createDiffPng(
      comparison,
      comparisonOptions.threshold
    );
    const diffRatio =
      comparison.totalPixels === 0 ? 0 : diffPixels / comparison.totalPixels;
    const result: CaptureLookSimilarResult = {
      ...buildBaseResult(context),
      diffPixels,
      diffRatio,
      totalPixels: comparison.totalPixels,
    };

    if (isLookSimilarPass(diffPixels, diffRatio, comparisonOptions)) {
      return result;
    }

    const failureArtifacts = await writeFailureArtifacts(
      context,
      loadedExpectedImage.data,
      diffPng
    );
    const failedResult: CaptureLookSimilarResult = {
      ...result,
      ...failureArtifacts,
      pass: false,
    };
    throw createVisualError(
      `Capture image differs: ${diffPixels} pixels (${diffRatio.toFixed(
        6
      )}) exceeded maxDiffRatio ${comparisonOptions.maxDiffRatio}${
        comparisonOptions.maxDiffPixels === undefined
          ? ''
          : ` and maxDiffPixels ${comparisonOptions.maxDiffPixels}`
      }.`,
      failedResult
    );
  };
  return toLookSimilar;
};

const createSimilarityAssertion = (
  defaults: CaptureVisualDefaults,
  nextCounter: () => number,
  capture: CaptureImage,
  name: string
): ((
  expectedImage: CaptureExpectedImage,
  options?: CaptureSimilarityOptions
) => Promise<CaptureSimilarityResult>) => {
  const toHaveSimilarity = async (
    expectedImage: CaptureExpectedImage,
    options?: CaptureSimilarityOptions
  ): Promise<CaptureSimilarityResult> => {
    const context = await createContext(defaults, options, name, nextCounter());
    const actualPng = decodePng(capture.image);
    validateCaptureImage(capture, actualPng);
    const loadedExpectedImage = await loadExpectedImage(expectedImage);
    const expectedPng = decodePng(loadedExpectedImage.data);
    await saveActualAndMetadata(
      context,
      capture,
      name,
      'toHaveSimilarity',
      options,
      loadedExpectedImage
    );

    const comparisonOptions = resolveSimilarityOptions(options);
    const comparison = prepareComparison(actualPng, expectedPng, options);
    const similarity = ssim(
      {
        data: new Uint8ClampedArray(comparison.expectedData),
        height: comparison.height,
        width: comparison.width,
      },
      {
        data: new Uint8ClampedArray(comparison.actualData),
        height: comparison.height,
        width: comparison.width,
      }
    ).mssim;
    const { diffPixels, diffPng } = createDiffPng(comparison, 0.1);
    const diffRatio =
      comparison.totalPixels === 0 ? 0 : diffPixels / comparison.totalPixels;
    const result: CaptureSimilarityResult = {
      ...buildBaseResult(context),
      diffPixels,
      diffRatio,
      similarity,
      totalPixels: comparison.totalPixels,
    };

    if (similarity >= comparisonOptions.minSimilarity) {
      return result;
    }

    const failureArtifacts = await writeFailureArtifacts(
      context,
      loadedExpectedImage.data,
      diffPng
    );
    const failedResult: CaptureSimilarityResult = {
      ...result,
      ...failureArtifacts,
      pass: false,
    };
    throw createVisualError(
      `Capture image similarity ${similarity.toFixed(
        6
      )} is below minSimilarity ${comparisonOptions.minSimilarity}.`,
      failedResult
    );
  };
  return toHaveSimilarity;
};

const createContainTextAssertion = (
  defaults: CaptureVisualDefaults,
  nextCounter: () => number,
  workerController: OcrWorkerController,
  capture: CaptureImage,
  name: string
): ((
  expected: string | RegExp,
  options?: CaptureOcrAssertionOptions
) => Promise<CaptureOcrResult>) => {
  const toContainText = async (
    expected: string | RegExp,
    options?: CaptureOcrAssertionOptions
  ): Promise<CaptureOcrResult> => {
    const ocrText = await readCaptureText(
      defaults,
      nextCounter,
      workerController,
      capture,
      name,
      'toContainText',
      options,
      expected
    );
    return await ocrText.toContainText(expected, options);
  };
  return toContainText;
};

const createReadTextAssertion = (
  defaults: CaptureVisualDefaults,
  nextCounter: () => number,
  workerController: OcrWorkerController,
  capture: CaptureImage,
  name: string
): ((options?: CaptureOcrOptions) => Promise<CaptureOcrText>) => {
  const readText = async (
    options?: CaptureOcrOptions
  ): Promise<CaptureOcrText> => {
    return await readCaptureText(
      defaults,
      nextCounter,
      workerController,
      capture,
      name,
      'readText',
      options,
      undefined
    );
  };
  return readText;
};

/////////////////////////////////////////////////////////////////////////////////////////

/**
 * Creates capture visual assertion helpers with shared defaults.
 * @param defaults Default artifact, variant, and OCR settings.
 * @returns Capture assertion helper.
 */
export const createCaptureExpect = (
  defaults?: CaptureVisualDefaults
): CaptureExpect => {
  const resolvedDefaults = defaults ?? {};
  const workerController = createOcrWorkerController(resolvedDefaults.ocr);
  let counter = 0;
  const nextCounter = (): number => {
    const value = counter;
    counter += 1;
    return value;
  };
  const release = async (): Promise<void> => {
    await workerController.release();
  };
  const dispose = (): void => {
    void release();
  };
  const expectCapture = (
    capture: CaptureImage,
    name: string
  ): CaptureExpectation => ({
    readText: createReadTextAssertion(
      resolvedDefaults,
      nextCounter,
      workerController,
      capture,
      name
    ),
    toContainText: createContainTextAssertion(
      resolvedDefaults,
      nextCounter,
      workerController,
      capture,
      name
    ),
    toHaveSimilarity: createSimilarityAssertion(
      resolvedDefaults,
      nextCounter,
      capture,
      name
    ),
    toLookSimilar: createLookSimilarAssertion(
      resolvedDefaults,
      nextCounter,
      capture,
      name
    ),
  });
  return {
    expectCapture,
    release,
    [Symbol.dispose]: dispose,
    [Symbol.asyncDispose]: release,
  };
};

const defaultCaptureExpect = createCaptureExpect();

/**
 * Creates an assertion object for a captured image.
 * @param capture Capture to assert.
 * @param name Artifact name.
 * @returns Assertion object for the capture.
 */
export const expectCapture = (
  capture: CaptureImage,
  name: string
): CaptureExpectation => defaultCaptureExpect.expectCapture(capture, name);
