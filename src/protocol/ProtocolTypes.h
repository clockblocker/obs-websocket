/*
obs-websocket
Copyright (C) 2016-2021 Stephane Lepin <stephane.lepin@gmail.com>
Copyright (C) 2020-2021 Kyle Manning <tt2468@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/Deprecated.hpp>
#include <rfl/json.hpp>
#include <rfl/nlohmann.hpp>

#include "../eventhandler/types/EventSubscription.h"
#include "../requesthandler/types/RequestBatchExecutionType.h"
#include "../requesthandler/types/RequestStatus.h"
#include "../websocketserver/types/WebSocketOpCode.h"

namespace ObsWebSocket::Protocol {

// ============================================================================
// Common validated aliases & JSON object wrappers
// ============================================================================

using NonEmptyString = rfl::Validator<std::string, rfl::Size<rfl::Minimum<1>>>;
using SleepMillis = rfl::Validator<int64_t, rfl::Minimum<0>, rfl::Maximum<50000>>;
using SleepFrames = rfl::Validator<int64_t, rfl::Minimum<0>, rfl::Maximum<10000>>;
using MonitorIndex = rfl::Validator<int, rfl::Minimum<-1>, rfl::Maximum<9>>;

using JsonObject = rfl::Object<nlohmann::json>;
using NonEmptyJsonObject = rfl::Validator<JsonObject, rfl::Size<rfl::Minimum<1>>>;

using VideoMixType = rfl::Literal<
	"OBS_WEBSOCKET_VIDEO_MIX_TYPE_PREVIEW",
	"OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM",
	"OBS_WEBSOCKET_VIDEO_MIX_TYPE_MULTIVIEW">;

struct EmptyPayload {};

// ============================================================================
// WebSocket v5 Wire Envelopes (JSON fast-path with rfl::json::Raw & nlohmann)
// ============================================================================

/// Zero-copy JSON wire envelope: parses `op` while keeping `d` as raw JSON text
/// for lazy opcode-specific decoding via rfl::json::Raw.
struct WireEnvelope {
	rfl::Description<"WebSocketOpCode operation code", uint8_t> op;
	rfl::Description<"Raw JSON object payload for the operation", rfl::json::Raw> d;
};

/// Dual-codec envelope (JSON + MsgPack) backed by nlohmann::json.
struct JsonEnvelope {
	rfl::Description<"WebSocketOpCode operation code", uint8_t> op;
	rfl::Description<"Object payload for the operation", nlohmann::json> d;
};

// ============================================================================
// WebSocket v5 OpCode Payloads (OpCodes 0, 1, 2, 3, 5, 6, 7, 8, 9)
// ============================================================================

struct HelloAuthentication {
	rfl::Description<"Base64-encoded challenge string", std::string> challenge;
	rfl::Description<"Base64-encoded salt string", std::string> salt;
};

/// OpCode 0: Hello
struct HelloData {
	rfl::Description<"Current OBS Studio version", std::string> obsStudioVersion;
	rfl::Description<"Current obs-websocket version", std::string> obsWebSocketVersion;
	rfl::Description<"Current obs-websocket RPC version", uint8_t> rpcVersion;
	rfl::Description<"Authentication parameters if authentication is required",
			 std::optional<HelloAuthentication>>
		authentication;
};

/// OpCode 1: Identify
struct IdentifyData {
	rfl::Description<"Requested obs-websocket RPC version", uint8_t> rpcVersion;
	rfl::Description<"Authentication string computed from challenge and salt",
			 std::optional<std::string>>
		authentication;
	rfl::Description<"Bitmask of EventSubscription values to subscribe to",
			 rfl::DefaultVal<uint64_t>>
		eventSubscriptions = static_cast<uint64_t>(EventSubscription::All);
};

/// OpCode 2: Identified
struct IdentifiedData {
	rfl::Description<"Negotiated obs-websocket RPC version", uint8_t> negotiatedRpcVersion;
};

/// OpCode 3: Reidentify
struct ReidentifyData {
	rfl::Description<"Updated bitmask of EventSubscription values",
			 rfl::DefaultVal<uint64_t>>
		eventSubscriptions = static_cast<uint64_t>(EventSubscription::All);
};

/// OpCode 5: Event
struct EventData {
	rfl::Description<"Name of the emitted event", NonEmptyString> eventType;
	rfl::Description<"Original EventSubscription bit required to receive this event", uint64_t>
		eventIntent;
	rfl::Description<"Event-specific data object", std::optional<JsonObject>> eventData;
};

/// OpCode 6: Request
struct RequestMessageData {
	rfl::Description<"Name of the request to execute", NonEmptyString> requestType;
	rfl::Description<"Client-chosen identifier echoed in RequestResponse", std::string> requestId;
	rfl::Description<"Request-specific input parameters", std::optional<JsonObject>> requestData;
};

/// Nested status object in RequestResponse (OpCode 7)
struct RequestStatusPayload {
	rfl::Description<"True if the request succeeded (code == 100)", bool> result;
	rfl::Description<"Numeric RequestStatus code", int> code;
	rfl::Description<"Optional human-readable error detail", std::optional<std::string>> comment;
};

/// OpCode 7: RequestResponse
struct RequestResponseData {
	rfl::Description<"Mirrored requestType from the Request message", std::string> requestType;
	rfl::Description<"Mirrored requestId from the Request message", std::string> requestId;
	rfl::Description<"Result status of the request", RequestStatusPayload> requestStatus;
	rfl::Description<"Request-specific response data object", std::optional<JsonObject>>
		responseData;
};

/// Individual item inside RequestBatch.requests
struct RequestBatchItem {
	rfl::Description<"Name of the request to execute", std::string> requestType;
	rfl::Description<"Optional identifier mirrored in the batch response item",
			 std::optional<std::string>>
		requestId;
	rfl::Description<"Request-specific input parameters", std::optional<JsonObject>> requestData;
	rfl::Description<"Variable substitutions applied before execution", std::optional<JsonObject>>
		inputVariables;
	rfl::Description<"Response fields captured into batch variables", std::optional<JsonObject>>
		outputVariables;
};

/// OpCode 8: RequestBatch
struct RequestBatchData {
	rfl::Description<"Client-chosen identifier echoed in RequestBatchResponse", std::string>
		requestId;
	rfl::Description<"Whether to halt batch processing on the first failed request",
			 rfl::DefaultVal<bool>>
		haltOnFailure = false;
	rfl::Description<"RequestBatchExecutionType mode (0=SerialRealtime, 1=SerialFrame, 2=Parallel)",
			 rfl::DefaultVal<int8_t>>
		executionType = static_cast<int8_t>(RequestBatchExecutionType::SerialRealtime);
	rfl::Description<"Initial batch variable map", std::optional<JsonObject>> variables;
	rfl::Description<"Ordered list of requests to execute", std::vector<RequestBatchItem>> requests;
};

/// Individual result inside RequestBatchResponse.results
struct RequestBatchResultItem {
	rfl::Description<"Mirrored requestType", std::string> requestType;
	rfl::Description<"Mirrored optional requestId", std::optional<std::string>> requestId;
	rfl::Description<"Result status of this batched request", RequestStatusPayload> requestStatus;
	rfl::Description<"Request-specific response data object", std::optional<JsonObject>>
		responseData;
};

/// OpCode 9: RequestBatchResponse
struct RequestBatchResponseData {
	rfl::Description<"Mirrored requestId from RequestBatch", std::string> requestId;
	rfl::Description<"Ordered list of per-request results", std::vector<RequestBatchResultItem>>
		results;
};

// Typed per-opcode framed messages for discriminated schema generation (VariantsAsOneOf)
struct HelloMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Hello)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Hello);
	HelloData d;
};

struct IdentifyMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Identify)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Identify);
	IdentifyData d;
};

struct IdentifiedMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Identified)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Identified);
	IdentifiedData d;
};

struct ReidentifyMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Reidentify)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Reidentify);
	ReidentifyData d;
};

struct EventMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Event)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Event);
	EventData d;
};

struct RequestMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::Request)>> op =
		static_cast<uint8_t>(WebSocketOpCode::Request);
	RequestMessageData d;
};

struct RequestResponseMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::RequestResponse)>>
		op = static_cast<uint8_t>(WebSocketOpCode::RequestResponse);
	RequestResponseData d;
};

struct RequestBatchMessage {
	rfl::Validator<uint8_t, rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::RequestBatch)>>
		op = static_cast<uint8_t>(WebSocketOpCode::RequestBatch);
	RequestBatchData d;
};

struct RequestBatchResponseMessage {
	rfl::Validator<uint8_t,
		       rfl::EqualTo<static_cast<uint8_t>(WebSocketOpCode::RequestBatchResponse)>>
		op = static_cast<uint8_t>(WebSocketOpCode::RequestBatchResponse);
	RequestBatchResponseData d;
};

using WebSocketMessage = rfl::Variant<
	HelloMessage,
	IdentifyMessage,
	IdentifiedMessage,
	ReidentifyMessage,
	EventMessage,
	RequestMessage,
	RequestResponseMessage,
	RequestBatchMessage,
	RequestBatchResponseMessage>;

// ============================================================================
// Raw JSON passthrough structs (rfl::json::Raw fast path for arbitrary JSON)
// ============================================================================

struct RawBroadcastCustomEventRequest {
	rfl::Description<"Data payload to emit to all receivers", rfl::json::Raw> eventData;
};

struct RawCallVendorRequest {
	rfl::Description<"Name of the vendor to use", NonEmptyString> vendorName;
	rfl::Description<"The request type to call", NonEmptyString> requestType;
	rfl::Description<"Object containing appropriate request data", std::optional<rfl::json::Raw>>
		requestData;
};

struct RawCallVendorResponse {
	rfl::Description<"Echoed of `vendorName`", std::string> vendorName;
	rfl::Description<"Echoed of `requestType`", std::string> requestType;
	rfl::Description<"Object containing appropriate response data. {} if request does not provide any response data",
			 rfl::json::Raw>
		responseData;
};

// ============================================================================
// Category: general (8 requests)
// ============================================================================

struct GetVersionResponse {
	rfl::Description<"Current OBS Studio version", std::string> obsVersion;
	rfl::Description<"Current obs-websocket version", std::string> obsWebSocketVersion;
	rfl::Description<"Current latest obs-websocket RPC version", int> rpcVersion;
	rfl::Description<"Array of available RPC requests for the currently negotiated RPC version",
			 std::vector<std::string>>
		availableRequests;
	rfl::Description<"Image formats available in `GetSourceScreenshot` and `SaveSourceScreenshot` requests.",
			 std::vector<std::string>>
		supportedImageFormats;
	rfl::Description<"Name of the platform. Usually `windows`, `macos`, or `ubuntu` (linux flavor). Not guaranteed to be any of those",
			 std::string>
		platform;
	rfl::Description<"Description of the platform, like `Windows 10 (10.0)`", std::string>
		platformDescription;
};

struct GetStatsResponse {
	rfl::Description<"Current CPU usage in percent", double> cpuUsage;
	rfl::Description<"Amount of memory in MB currently being used by OBS", double> memoryUsage;
	rfl::Description<"Available disk space on the device being used for recording storage",
			 double>
		availableDiskSpace;
	rfl::Description<"Current FPS being rendered", double> activeFps;
	rfl::Description<"Average time in milliseconds that OBS is taking to render a frame", double>
		averageFrameRenderTime;
	rfl::Description<"Number of frames skipped by OBS in the render thread", uint64_t>
		renderSkippedFrames;
	rfl::Description<"Total number of frames outputted by the render thread", uint64_t>
		renderTotalFrames;
	rfl::Description<"Number of frames skipped by OBS in the output thread", uint64_t>
		outputSkippedFrames;
	rfl::Description<"Total number of frames outputted by the output thread", uint64_t>
		outputTotalFrames;
	rfl::Description<"Total number of messages received by obs-websocket from the client",
			 uint64_t>
		webSocketSessionIncomingMessages;
	rfl::Description<"Total number of messages sent by obs-websocket to the client", uint64_t>
		webSocketSessionOutgoingMessages;
};

struct BroadcastCustomEventRequest {
	rfl::Description<"Data payload to emit to all receivers", NonEmptyJsonObject> eventData;
};

struct CallVendorRequestRequest {
	rfl::Description<"Name of the vendor to use", NonEmptyString> vendorName;
	rfl::Description<"The request type to call", NonEmptyString> requestType;
	rfl::Description<"Object containing appropriate request data", std::optional<JsonObject>>
		requestData;
};

struct CallVendorRequestResponse {
	rfl::Description<"Echoed of `vendorName`", std::string> vendorName;
	rfl::Description<"Echoed of `requestType`", std::string> requestType;
	rfl::Description<"Object containing appropriate response data. {} if request does not provide any response data",
			 JsonObject>
		responseData;
};

struct GetHotkeyListResponse {
	rfl::Description<"Array of hotkey names", std::vector<std::string>> hotkeys;
};

struct TriggerHotkeyByNameRequest {
	rfl::Description<"Name of the hotkey to trigger", NonEmptyString> hotkeyName;
	rfl::Description<"Name of context of the hotkey to trigger", std::optional<NonEmptyString>>
		contextName;
};

struct KeyModifiers {
	rfl::Description<"Press Shift", rfl::DefaultVal<bool>> shift = false;
	rfl::Description<"Press CTRL", rfl::DefaultVal<bool>> control = false;
	rfl::Description<"Press ALT", rfl::DefaultVal<bool>> alt = false;
	rfl::Description<"Press CMD (Mac)", rfl::DefaultVal<bool>> command = false;
};

struct TriggerHotkeyByKeySequenceRequest {
	rfl::Description<"The OBS key ID to use. See https://github.com/obsproject/obs-studio/blob/master/libobs/obs-hotkeys.h",
			 std::optional<NonEmptyString>>
		keyId;
	rfl::Description<"Object containing key modifiers to apply", std::optional<KeyModifiers>>
		keyModifiers;
};

struct SleepRequest {
	rfl::Description<"Number of milliseconds to sleep for (if `SERIAL_REALTIME` mode)",
			 std::optional<SleepMillis>>
		sleepMillis;
	rfl::Description<"Number of frames to sleep for (if `SERIAL_FRAME` mode)",
			 std::optional<SleepFrames>>
		sleepFrames;
};

// ============================================================================
// Category: stream (5 requests)
// ============================================================================

struct GetStreamStatusResponse {
	rfl::Description<"Whether the output is active", bool> outputActive;
	rfl::Description<"Whether the output is currently reconnecting", bool> outputReconnecting;
	rfl::Description<"Current formatted timecode string for the output", std::string>
		outputTimecode;
	rfl::Description<"Current duration in milliseconds for the output", uint64_t>
		outputDuration;
	rfl::Description<"Congestion of the output", float> outputCongestion;
	rfl::Description<"Number of bytes sent by the output", uint64_t> outputBytes;
	rfl::Description<"Number of frames skipped by the output's process", int>
		outputSkippedFrames;
	rfl::Description<"Total number of frames delivered by the output's process", int>
		outputTotalFrames;
};

struct ToggleStreamResponse {
	rfl::Description<"New state of the stream output", bool> outputActive;
};

struct SendStreamCaptionRequest {
	// Upstream ValidateString("captionText", ..., /*allowEmpty=*/true) permits empty strings.
	rfl::Description<"Caption text", std::string> captionText;
};

// ============================================================================
// Category: record (9 requests)
// ============================================================================

struct GetRecordStatusResponse {
	rfl::Description<"Whether the output is active", bool> outputActive;
	rfl::Description<"Whether the output is paused", bool> outputPaused;
	rfl::Description<"Current formatted timecode string for the output", std::string>
		outputTimecode;
	rfl::Description<"Current duration in milliseconds for the output", uint64_t>
		outputDuration;
	rfl::Description<"Number of bytes sent by the output", uint64_t> outputBytes;
};

struct ToggleRecordResponse {
	rfl::Description<"The new active state of the output", bool> outputActive;
};

struct StopRecordResponse {
	rfl::Description<"File name for the saved recording", std::string> outputPath;
};

struct ToggleRecordPauseResponse {
	// Note: RequestHandler_Record.cpp returns `outputPaused`, though upstream
	// docs/generated/protocol.json omitted the @responseField docblock comment.
	rfl::Description<"The new paused state of the output", bool> outputPaused;
};

struct CreateRecordChapterRequest {
	rfl::Description<"Name of the new chapter", std::optional<NonEmptyString>> chapterName;
};

// ============================================================================
// Category: ui (8 requests)
// ============================================================================

struct GetStudioModeEnabledResponse {
	rfl::Description<"Whether studio mode is enabled", bool> studioModeEnabled;
};

struct SetStudioModeEnabledRequest {
	rfl::Description<"True == Enabled, False == Disabled", bool> studioModeEnabled;
};

struct OpenInputDialogRequest {
	rfl::Description<"Name of the input to open the dialog of", std::optional<NonEmptyString>>
		inputName;
	rfl::Description<"UUID of the input to open the dialog of", std::optional<NonEmptyString>>
		inputUuid;
};

struct MonitorInfo {
	rfl::Description<"Monitor name with index suffix", std::string> monitorName;
	rfl::Description<"Zero-based monitor index", int> monitorIndex;
	rfl::Description<"Monitor width in pixels", int> monitorWidth;
	rfl::Description<"Monitor height in pixels", int> monitorHeight;
	rfl::Description<"Monitor X position in virtual desktop coordinates", int> monitorPositionX;
	rfl::Description<"Monitor Y position in virtual desktop coordinates", int> monitorPositionY;
};

struct GetMonitorListResponse {
	rfl::Description<"a list of detected monitors with some information",
			 std::vector<MonitorInfo>>
		monitors;
};

struct OpenVideoMixProjectorRequest {
	rfl::Description<"Type of mix to open", VideoMixType> videoMixType;
	rfl::Description<"Monitor index, use `GetMonitorList` to obtain index",
			 rfl::DefaultVal<MonitorIndex>>
		monitorIndex = MonitorIndex(-1);
	rfl::Description<"Size/Position data for a windowed projector, in Qt Base64 encoded format. Mutually exclusive with `monitorIndex`",
			 std::optional<NonEmptyString>>
		projectorGeometry;
};

struct OpenSourceProjectorRequest {
	rfl::Description<"UUID of the canvas the source is in, if using the sourceName field",
			 std::optional<NonEmptyString>>
		canvasUuid;
	rfl::Description<"Name of the source to open a projector for", std::optional<NonEmptyString>>
		sourceName;
	rfl::Description<"UUID of the source to open a projector for", std::optional<NonEmptyString>>
		sourceUuid;
	rfl::Description<"Monitor index, use `GetMonitorList` to obtain index",
			 rfl::DefaultVal<MonitorIndex>>
		monitorIndex = MonitorIndex(-1);
	rfl::Description<"Size/Position data for a windowed projector, in Qt Base64 encoded format. Mutually exclusive with `monitorIndex`",
			 std::optional<NonEmptyString>>
		projectorGeometry;
};

// ============================================================================
// Category: scenes (deprecated-field showcase from RequestHandler_Scenes.cpp)
// ============================================================================

struct GetCurrentProgramSceneResponse {
	rfl::Description<"Current program scene name", std::string> sceneName;
	rfl::Description<"Current program scene UUID", std::string> sceneUuid;
	rfl::Deprecated<"Use `sceneName` instead", "Current program scene name (Deprecated)",
			std::string>
		currentProgramSceneName;
	rfl::Deprecated<"Use `sceneUuid` instead", "Current program scene UUID (Deprecated)",
			std::string>
		currentProgramSceneUuid;
};

} // namespace ObsWebSocket::Protocol
