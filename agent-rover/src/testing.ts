// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

export type {
  AgentRoverWaitError,
  AgentRoverWaitErrorCode,
  AgentRoverWaitOptions,
} from './wait';
export type {
  CaptureExpect,
  CaptureExpectation,
  CaptureExpectedImage,
  CaptureImage,
  CaptureLookSimilarOptions,
  CaptureLookSimilarResult,
  CaptureOcrAssertionOptions,
  CaptureOcrAttempt,
  CaptureOcrCacheMethod,
  CaptureOcrDefaults,
  CaptureOcrOptions,
  CaptureOcrPageSegmentationMode,
  CaptureOcrPreprocessOptions,
  CaptureOcrResult,
  CaptureOcrText,
  CaptureOcrTextAssertionOptions,
  CaptureOcrTextMatch,
  CaptureOcrWorkerMode,
  CaptureOcrWord,
  CapturePixelRegion,
  CaptureResultOutputOptions,
  CaptureSimilarityOptions,
  CaptureSimilarityResult,
  CaptureVisualComparisonOptions,
  CaptureVisualDefaults,
  CaptureVisualError,
  CaptureVisualResult,
} from './capture-testing';
export { createCaptureExpect, expectCapture } from './capture-testing';
export {
  currentWaitDeadlineMs,
  effectiveWaitTimeoutMs,
  runWithWaitDeadline,
  toPass,
  waitForResult,
} from './wait';
