// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_AGENT_VERSION_BANNER_H
#define AGENT_ROVER_AGENT_VERSION_BANNER_H

#include <string>

namespace agent_rover {

/**
 * Builds the version text displayed by the native agent.
 *
 * @return Version text including the Git commit hash when available.
 */
std::string BuildAgentVersionText();

/**
 * Builds the startup banner displayed by the native agent.
 *
 * @return Startup banner text without a trailing blank line.
 */
std::string BuildAgentVersionBanner();

/**
 * Writes the startup banner to stdout.
 */
void PrintAgentVersionBanner();

}  // namespace agent_rover

#endif
