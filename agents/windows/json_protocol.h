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

/** Sets the cached video capability after isolated detection.
 * @param supported Whether Media Foundation was successfully detected. */
void SetAgentVideoCapability(bool supported);
/** Decodes the envelope without executing an operation.
 * @param payload JSON request. @param id Request identifier. @param method Operation name.
 * @return Whether a bounded request envelope was decoded. */
bool ReadAgentRequest(const std::string& payload, std::string* id, std::string* method);
/** Reads a bounded recording duration for the operation scheduler.
 * @param payload Request JSON. @return Duration up to ten minutes, or zero if invalid. */
uint32_t ReadAgentVideoDuration(const std::string& payload);
/** Builds a capability response using cached data only.
 * @param id Request identifier. @return JSON response. */
std::string CreateCapabilitiesResponse(const std::string& id);
/** Builds an operation failure response without exposing request contents.
 * @param id Request identifier. @param code Stable failure code.
 * @param message Diagnostic description. @return JSON response. */
std::string CreateAgentFailure(const std::string& id, const std::string& code, const std::string& message);
/** Describes a generated response without logging results, messages, paths or request contents.
 * @param payload Internally generated response JSON. @return Bounded outcome and native failure fields. */
std::string CreateAgentResultLog(const std::string& payload);

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
