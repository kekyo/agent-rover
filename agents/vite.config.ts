// agent-rover - Build the Windows distribution command for Node.js
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
import { builtinModules } from 'node:module';
import { defineConfig } from 'vite';
import prettierMax from 'prettier-max';

export default defineConfig({
  plugins: [prettierMax({ typescript: 'tsconfig.json' })],
  build: {
    outDir: '.build/pack',
    lib: {
      entry: 'scripts/pack-windows.ts',
      formats: ['es'],
      fileName: () => 'pack-windows.mjs',
    },
    target: 'node20',
    minify: false,
    rolldownOptions: {
      external: [
        ...builtinModules.flatMap((name) => [name, `node:${name}`]),
        'funcity',
      ],
    },
  },
});
