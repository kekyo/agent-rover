// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    // These tests share the Wine desktop, inject process failures, and build
    // the same agent binaries. Their files cannot run independently.
    fileParallelism: false,
  },
});
