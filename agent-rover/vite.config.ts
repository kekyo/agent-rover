// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { builtinModules } from 'node:module';
import { chmod } from 'node:fs/promises';
import { resolve } from 'node:path';

import { defineConfig, type Plugin } from 'vite';
import prettierMax from 'prettier-max';
import screwUp from 'screw-up';
import dts from 'unplugin-dts/vite';

const nodeBuiltins = builtinModules.flatMap((name) => [name, `node:${name}`]);

export default defineConfig(({ mode }) => ({
  plugins: [
    prettierMax({
      typescript: 'tsconfig.test.json',
    }),
    screwUp({
      outputMetadataFile: true,
    }),
    ...(mode === 'cli'
      ? [
          {
            name: 'arctl-executable',
            writeBundle: async (options) => {
              await chmod(resolve(options.dir ?? 'dist', 'arctl.mjs'), 0o755);
            },
          } satisfies Plugin,
        ]
      : [
          dts({
            entryRoot: 'src',
          }),
        ]),
  ],
  build: {
    emptyOutDir: mode !== 'cli',
    lib: {
      entry:
        mode === 'cli'
          ? { arctl: 'src/arctl.ts' }
          : {
              index: 'src/index.ts',
              testing: 'src/testing.ts',
            },
      fileName: (format, entryName) =>
        `${entryName}.${format === 'es' ? 'mjs' : 'cjs'}`,
      formats: mode === 'cli' ? ['es'] : ['es', 'cjs'],
    },
    rolldownOptions: {
      external: [
        ...nodeBuiltins,
        'commander',
        'pngjs',
        'async-primitives',
        'pixelmatch',
        'ssim.js',
        'tesseract.js',
        '@tesseract.js-data/eng',
      ],
    },
    target: 'node20',
    sourcemap: true,
    minify: false,
  },
}));
