// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import os from 'node:os';
import { syncBuiltinESMExports } from 'node:module';

// Only subprocess tests load this module. Do not alter HOME or the user's file.
const directory = process.env.AGENT_ROVER_TEST_CONFIG_HOME;
if (!directory)
  throw new Error('Missing isolated test configuration directory.');
os.homedir = () => directory;
syncBuiltinESMExports();
