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

struct AgentUpdateSpec;
/** Parses the build identity, bounded size and release protocol of a prepare request. */
bool ReadAgentUpdateSpec(const std::string& json, AgentUpdateSpec* spec);
/** Reads a string field from an update request or catalog. */
bool FindJsonStringField(const std::string& json,const std::string& key,std::string* value);
/** Serializes a successful response with an already encoded JSON result. */
std::string SuccessResponseJson(const std::string& id,const std::string& result);
/** Escapes a UTF-8 string for JSON serialization. */
std::string JsonEscape(const std::string& value);
/** Matches historical images against the packaged catalog. */
bool MatchesLegacyAgent(const std::string& catalog, const AgentUpdateSpec& spec);

/** Records the random identifier of this server lifetime. */
void SetAgentInstanceId(const std::string& id);
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
/** Returns the cached workspace path without accessing configuration or storage.
 * @param id Request identifier. @param path Startup-selected UTF-8 directory.
 * @return JSON response. */
std::string CreateSafeBaseDirResponse(const std::string& id, const std::string& path);
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
 * @param safe_base_dir Startup-selected absolute Windows workspace directory.
 * @param transfers Incoming transfer store and configured file transfer limit.
 * @param recordings Connection-owned asynchronous video recording state.
 * @param outbound_chunks Receives binary chunks to send before the JSON response.
 * @param outbound_file Receives a file to send after the JSON response.
 * @return UTF-8 JSON response payload.
 */
std::string HandleJsonRequest(
    const std::string& payload,
    const std::string& safe_base_dir,
    BinaryTransferStore* transfers,
    VideoRecordingStore* recordings,
    std::vector<BinaryTransferChunk>* outbound_chunks,
    OutboundFileTransfer* outbound_file);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_JSON_PROTOCOL_H
