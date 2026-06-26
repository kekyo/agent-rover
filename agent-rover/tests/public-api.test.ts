// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { describe, expect, it } from 'vitest';

import { packageName } from '../src/index';

describe('public package entry', () => {
  it('exposes the package name from the library entry point', () => {
    expect(packageName).toBe('agent-rover');
  });
});
