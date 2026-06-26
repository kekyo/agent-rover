// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import vitestConfig from '../vitest.config';

describe.concurrent('vitest configuration', () => {
  it('enables parallel execution for independent test files', () => {
    expect(vitestConfig.test?.fileParallelism).toBe(true);
  });
});
