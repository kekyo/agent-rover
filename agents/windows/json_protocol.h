// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_JSON_PROTOCOL_H
#define AGENT_ROVER_WINDOWS_AGENT_JSON_PROTOCOL_H

#include <string>
#include <vector>

#include "binary_transfer.h"
#include "video_recording.h"

namespace agent_rover {

/**
 * Creates the startup event sent immediately after a TCP client connects.
 *
 * @return JSON protocol event payload.
 */
std::string CreateReadyEventJson();

/**
 * Handles one JSON request payload.
 *
 * @param payload UTF-8 JSON request payload.
 * @param transfers Binary transfer store used by file.write requests.
 * @param recordings Connection-owned asynchronous video recording state.
 * @param outbound_chunks Receives binary chunks to send before the JSON response.
 * @param outbound_file Receives a file to send after the JSON response.
 * @return UTF-8 JSON response payload.
 */
std::string HandleJsonRequest(
    const std::string& payload,
    BinaryTransferStore* transfers,
    VideoRecordingStore* recordings,
    std::vector<BinaryTransferChunk>* outbound_chunks,
    OutboundFileTransfer* outbound_file);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_JSON_PROTOCOL_H
