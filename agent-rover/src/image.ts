// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import type { ImageComparisonOptions, ImageComparisonResult } from './index';
import { comparePngImages } from './capture-testing';

/** Compares two PNG images with pixel-difference tolerances. */
export const compareImages = (
  actualImage: Buffer,
  expectedImage: Buffer,
  options: ImageComparisonOptions = {}
): ImageComparisonResult =>
  comparePngImages(actualImage, expectedImage, {
    ...(options.maxDiffPixels !== undefined
      ? { maxDiffPixels: options.maxDiffPixels }
      : {}),
    maxDiffRatio: options.maxDiffRatio ?? 0,
    ...(options.threshold !== undefined
      ? { threshold: options.threshold }
      : {}),
  });
