// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

declare module '@tesseract.js-data/eng' {
  /** Bundled English traineddata metadata for Tesseract.js. */
  interface EnglishLanguageData {
    /** Tesseract language code. */
    readonly code: 'eng';
    /** Whether the bundled traineddata is gzip-compressed. */
    readonly gzip: boolean;
    /** Directory containing the bundled traineddata. */
    readonly langPath: string;
  }

  const data: EnglishLanguageData;
  export = data;
}
