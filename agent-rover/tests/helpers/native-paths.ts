// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

interface NativeTestPaths {
  readonly agentsDirectory: string;
  readonly repositoryDirectory: string;
  readonly windowsAgentDirectory: string;
}

export const nativeTestPaths = (moduleUrl: string): NativeTestPaths => {
  const repositoryDirectory = dirname(
    dirname(dirname(fileURLToPath(moduleUrl)))
  );
  return {
    agentsDirectory: join(repositoryDirectory, 'agents'),
    repositoryDirectory,
    windowsAgentDirectory: join(repositoryDirectory, 'agents', 'windows'),
  };
};
