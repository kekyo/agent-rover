// agent-rover - Windows startup configuration
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_AGENT_CONFIGURATION_H
#define AGENT_ROVER_AGENT_CONFIGURATION_H

#include "command_line.h"

namespace agent_rover {

/**
 * Loads and resolves the public process's startup configuration once.
 * @param argc Argument count including argv[0].
 * @param argv Argument vector including argv[0].
 * @return Validated startup options, help, or a redacted diagnostic.
 * @remarks HOME takes precedence over GetUserProfileDirectoryW. A missing INI
 * is normal; other read failures stop startup. Internal helpers must not call this.
 */
AgentCommandLineParseResult LoadAgentConfiguration(int argc, const wchar_t* const argv[]);

}
#endif
