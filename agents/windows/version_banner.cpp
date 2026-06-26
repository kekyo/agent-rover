// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "version_banner.h"

#include <cstdio>
#include <string>

#include "version.h"

namespace agent_rover {

std::string BuildAgentVersionText() {
  std::string version = AGENT_ROVER_AGENT_VERSION;
  const std::string commit = AGENT_ROVER_AGENT_GIT_COMMIT_HASH;
  if (!commit.empty() && commit != "unknown") {
    version += "-";
    version += commit;
  }
  return version;
}

std::string BuildAgentVersionBanner() {
  return "agent-rover native windows agent [" + BuildAgentVersionText() + "]\n"
         "Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)\n"
         "https://github.com/kekyo/agent-rover\n"
         "Licence: Under MIT.";
}

void PrintAgentVersionBanner() {
  const std::string banner = BuildAgentVersionBanner();
  std::printf("%s\n\n", banner.c_str());
}

}  // namespace agent_rover
