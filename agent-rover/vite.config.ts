// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { builtinModules } from 'node:module';

import { defineConfig } from 'vite';
import prettierMax from 'prettier-max';
import screwUp from 'screw-up';
import dts from 'unplugin-dts/vite';

const nodeBuiltins = builtinModules.flatMap((name) => [name, `node:${name}`]);

export default defineConfig({
  plugins: [
    prettierMax({
      typescript: 'tsconfig.test.json',
    }),
    screwUp({
      outputMetadataFile: true,
    }),
    dts({
      entryRoot: 'src',
    }),
  ],
  build: {
    lib: {
      entry: {
        index: 'src/index.ts',
        testing: 'src/testing.ts',
      },
      fileName: (format, entryName) =>
        `${entryName}.${format === 'es' ? 'mjs' : 'cjs'}`,
      formats: ['es', 'cjs'],
    },
    rolldownOptions: {
      external: [
        ...nodeBuiltins,
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
});
