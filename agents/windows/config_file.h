// agent-rover - Local configuration document editing
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_CONFIG_FILE_H
#define AGENT_ROVER_CONFIG_FILE_H
#include <string>

namespace agent_rover {
/**
 * Exclusively creates a missing UTF-16LE configuration template and opens it.
 * @param path Absolute configuration file path selected at public startup.
 * @param error Receives a diagnostic and manual Notepad instructions on failure.
 * @return Whether the shell accepted the open request.
 * @remarks Existing files are never rewritten. Call only from the isolated
 * config-editor process: shell extensions and file metadata may block.
 */
bool OpenAgentConfigurationFile(const std::wstring& path, std::wstring* error);
}
#endif
