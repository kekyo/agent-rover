// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

/** Isolated configuration for CLI and package subprocess tests. */
export interface ConfigurationFixture {
  /** Empty working directory, separate from the configured home directory. */
  readonly workingDirectory: string;
  /** Environment that loads the test-only home directory shim. */
  readonly environment: NodeJS.ProcessEnv;
  /** Removes the isolated configuration directory. */
  readonly close: () => Promise<void>;
}

/**
 * Creates a real configuration file for a subprocess without modifying HOME.
 * @param text Complete configuration text.
 * @returns Subprocess environment and cleanup.
 */
export const createConfigurationFixture = async (
  text: string
): Promise<ConfigurationFixture> => {
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-cli-config-'));
  const workingDirectory = join(directory, 'working');
  await mkdir(workingDirectory);
  await writeFile(join(directory, '.agent-rover'), text);
  const preload = new URL('./configuration-home.mjs', import.meta.url).href;
  return {
    workingDirectory,
    environment: {
      ...process.env,
      AGENT_ROVER_HOST: undefined,
      AGENT_ROVER_PORT: undefined,
      AGENT_ROVER_AUTH_TOKEN: undefined,
      AGENT_ROVER_TEST_CONFIG_HOME: directory,
      NODE_OPTIONS:
        `${process.env.NODE_OPTIONS ?? ''} --import=${preload}`.trim(),
    },
    close: async () => {
      await rm(directory, { recursive: true, force: true });
    },
  };
};
