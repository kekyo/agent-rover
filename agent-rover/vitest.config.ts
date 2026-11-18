// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import { defineConfig } from 'vitest/config';

const projectRoot = dirname(fileURLToPath(import.meta.url));
const windowsSuites = [
  'tests/windows-*.integration.test.ts',
  'tests/native-agent-update.test.ts',
  'tests/agent-update-installation.test.ts',
];

export default defineConfig({
  test: {
    environment: 'node',
    exclude: ['**/node_modules/**', '**/dist/**'],
    fileParallelism: true,
    globals: true,
    hookTimeout: 30000,
    root: projectRoot,
    testTimeout: 30000,
    teardownTimeout: 30000,
    projects: [
      {
        extends: true,
        test: {
          name: 'unit',
          exclude: ['**/node_modules/**', '**/dist/**', ...windowsSuites],
        },
      },
      {
        extends: true,
        test: {
          name: 'windows',
          include: windowsSuites,
          // Windows tests share the native build, interactive desktop or bootstrap listener.
          fileParallelism: false,
          sequence: { groupOrder: 1 },
        },
      },
    ],
  },
  build: {
    target: 'node20',
  },
});
