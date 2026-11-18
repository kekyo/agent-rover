// agent-rover - Isolated local configuration editor requests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#ifndef AGENT_ROVER_CONFIG_EDITOR_H
#define AGENT_ROVER_CONFIG_EDITOR_H
#include "async_io.h"
#include <functional>

namespace agent_rover {
/** Dispatcher-owned local editor request and helper lifetime. */
struct ConfigEditor;
/** Shared editor request owner. */
using ConfigEditorHandle = std::shared_ptr<ConfigEditor>;
/** Creates an idle editor action without accessing the file or shell.
 * @param path Resolved UTF-8 INI path. @param failed Local error display callback.
 * @return Editor action owner. */
ConfigEditorHandle CreateConfigEditor(std::string path, std::function<void(const std::wstring&)> failed);
/** Requests one isolated shell open; repeated requests while busy are coalesced.
 * @param editor Local action owner. */
void RequestConfigEditor(const ConfigEditorHandle& editor);
/** Cancels and reaps the helper without terminating the editing application.
 * @param editor Local action owner. @return Completion promise. */
cardio::promise<void> StopConfigEditor(ConfigEditorHandle editor);
/** Runs the private one-request editor process. It never starts a TCP server.
 * @return Process exit code. */
int RunConfigEditorWorker();
}
#endif
