// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_GUI_H
#define AGENT_ROVER_WINDOWS_AGENT_GUI_H
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include "tcp_server.h"

namespace agent_rover {
/** Tray and log controls owned exclusively by the GUI dispatcher. */
struct AgentGui;
/** Shared GUI lifetime. */
using AgentGuiHandle = std::shared_ptr<AgentGui>;
/** Creates a hidden, resizable log viewer and registers the tray icon.
 * @param options Listen/authentication settings. @param exit Requests bounded server shutdown.
 * @return GUI owner. @throws std::runtime_error When initialization fails. */
AgentGuiHandle CreateAgentGui(const ServerOptions& options, std::function<void()> exit);
/** Coalesces log changes into one pending UI refresh.
 * @param gui Viewer to update. */
void NotifyAgentGuiLog(const AgentGuiHandle& gui);
/** Updates visible status text without performing file I/O.
 * @param gui Viewer. @param status Display text. */
void SetAgentGuiStatus(const AgentGuiHandle& gui, const std::string& status);
/** Configures the log-directory menu action.
 * @param gui Viewer. @param open Nonblocking callback opening the directory. */
void SetAgentGuiOpenLogs(const AgentGuiHandle& gui, std::function<void()> open);
}  // namespace agent_rover
#endif
