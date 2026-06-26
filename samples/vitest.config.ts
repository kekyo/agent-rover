// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import { defineProject } from 'vitest/config';

const projectRoot = dirname(fileURLToPath(import.meta.url));

export default defineProject({
  test: {
    name: 'samples',
    environment: 'node',
    exclude: ['**/node_modules/**', '**/dist/**', '**/test-results/**'],
    fileParallelism: false,
    hookTimeout: 60000,
    include: ['src/**/*.test.ts'],
    root: projectRoot,
    sequence: {
      groupOrder: 1,
    },
    testTimeout: 60000,
  },
  build: {
    target: 'node20',
  },
});
