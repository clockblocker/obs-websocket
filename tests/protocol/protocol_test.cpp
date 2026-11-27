#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <rfl/nlohmann.hpp>

#include "protocol/ProtocolTypes.h"
#include "protocol/RequestCodec.h"

namespace P = ObsWebSocket::Protocol;

namespace {

int gTestsRun = 0;
int gTestsPassed = 0;
int gAssertions = 0;

struct TestFailure {
	std::string message;
};

#define CHECK(cond)                                                                                       \
	do {                                                                                              \
		++gAssertions;                                                                            \
		if (!(cond)) {                                                                            \
			std::ostringstream oss;                                                           \
			oss << __FILE__ << ":" << __LINE__ << ": assertion failed: " #cond;               \
			throw TestFailure{oss.str()};                                                     \
		}                                                                                         \
	} while (false)

#define CHECK_EQ(lhs, rhs)                                                                                \
	do {                                                                                              \
		++gAssertions;                                                                            \
		const auto &_lhs = (lhs);                                                                 \
		const auto &_rhs = (rhs);                                                                 \
		if (!(_lhs == _rhs)) {                                                                    \
			std::ostringstream oss;                                                           \
			oss << __FILE__ << ":" << __LINE__ << ": expected " #lhs " == " #rhs              \
			    << " (got '" << _lhs << "' vs '" << _rhs << "')";                             \
			throw TestFailure{oss.str()};                                                     \
		}                                                                                         \
	} while (false)

void RunTest(const char *name, void (*fn)())
{
	++gTestsRun;
	try {
		fn();
		++gTestsPassed;
		std::cout << "[PASS] " << name << "\n";
	} catch (const TestFailure &f) {
		std::cerr << "[FAIL] " << name << ": " << f.message << "\n";
	} catch (const std::exception &e) {
		std::cerr << "[FAIL] " << name << ": uncaught exception: " << e.what() << "\n";
	}
}

// ============================================================================
// Suite A: <rfl/nlohmann.hpp> bridge & WebSocket v5 opcodes
// ============================================================================

void TestNlohmannBridgeOpCodes()
{
	// Hello (Op 0) with auth
	const P::HelloData hello{
		.obsStudioVersion = "31.0.0",
		.obsWebSocketVersion = "5.7.6",
		.rpcVersion = 1,
		.authentication = P::HelloAuthentication{.challenge = "chal==", .salt = "salt=="},
	};
	const nlohmann::json helloJson = P::EncodeJson(hello);
	CHECK_EQ(helloJson["obsStudioVersion"].get<std::string>(), std::string("31.0.0"));
	CHECK_EQ(helloJson["authentication"]["salt"].get<std::string>(), std::string("salt=="));
	const auto helloBack = P::DecodeJson<P::HelloData>(helloJson);
	CHECK(helloBack.has_value());
	CHECK_EQ(helloBack->rpcVersion.value(), 1);
	CHECK(helloBack->authentication.value().has_value());
	CHECK_EQ(helloBack->authentication.value()->challenge.value(), std::string("chal=="));

	// Identify (Op 1) with default eventSubscriptions
	const nlohmann::json identifyIn = {{"rpcVersion", 1}};
	const auto identifyParsed = P::DecodeJson<P::IdentifyData>(identifyIn);
	CHECK(identifyParsed.has_value());
	CHECK_EQ(identifyParsed->rpcVersion.value(), 1);
	CHECK(!identifyParsed->authentication.value().has_value());
	CHECK_EQ(identifyParsed->eventSubscriptions.value().value(),
		 static_cast<uint64_t>(EventSubscription::All));

	// Identified (Op 2)
	const nlohmann::json identifiedJson = P::EncodeJson(P::IdentifiedData{.negotiatedRpcVersion = 1});
	CHECK_EQ(identifiedJson["negotiatedRpcVersion"].get<int>(), 1);

	// Reidentify (Op 3) with default eventSubscriptions
	const auto reidParsed = P::DecodeJson<P::ReidentifyData>(nlohmann::json::object());
	CHECK(reidParsed.has_value());
	CHECK_EQ(reidParsed->eventSubscriptions.value().value(),
		 static_cast<uint64_t>(EventSubscription::All));

	// Event (Op 5)
	const P::EventData ev{
		.eventType = P::NonEmptyString("StudioModeStateChanged"),
		.eventIntent = static_cast<uint64_t>(EventSubscription::Ui),
		.eventData = P::ToJsonObject(nlohmann::json{{"studioModeEnabled", true}}),
	};
	const nlohmann::json evJson = P::EncodeJson(ev);
	CHECK_EQ(evJson["eventType"].get<std::string>(), std::string("StudioModeStateChanged"));
	CHECK_EQ(evJson["eventData"]["studioModeEnabled"].get<bool>(), true);

	// Request (Op 6) & RequestResponse (Op 7)
	const nlohmann::json reqIn = {
		{"requestType", "SetStudioModeEnabled"},
		{"requestId", "req-42"},
		{"requestData", {{"studioModeEnabled", true}}},
	};
	const auto reqParsed = P::DecodeJson<P::RequestMessageData>(reqIn);
	CHECK(reqParsed.has_value());
	CHECK_EQ(reqParsed->requestType.value().value(), std::string("SetStudioModeEnabled"));
	CHECK_EQ(reqParsed->requestId.value(), std::string("req-42"));
	CHECK(reqParsed->requestData.value().has_value());

	const P::RequestResponseData resp{
		.requestType = "SetStudioModeEnabled",
		.requestId = "req-42",
		.requestStatus =
			P::RequestStatusPayload{
				.result = true,
				.code = static_cast<int>(RequestStatus::Success),
				.comment = std::nullopt,
			},
		.responseData = std::nullopt,
	};
	const nlohmann::json respJson = P::EncodeJson(resp);
	CHECK_EQ(respJson["requestStatus"]["result"].get<bool>(), true);
	CHECK_EQ(respJson["requestStatus"]["code"].get<int>(), 100);
	CHECK(!respJson["requestStatus"].contains("comment"));
	CHECK(!respJson.contains("responseData"));

	// RequestBatch (Op 8) with DefaultVal fields omitted
	const nlohmann::json batchIn = {
		{"requestId", "batch-1"},
		{"requests",
		 nlohmann::json::array({{{"requestType", "GetVersion"}},
					{{"requestType", "Sleep"},
					 {"requestData", {{"sleepMillis", 50}}}}})},
	};
	const auto batchParsed = P::DecodeJson<P::RequestBatchData>(batchIn);
	CHECK(batchParsed.has_value());
	CHECK_EQ(batchParsed->haltOnFailure.value().value(), false);
	CHECK_EQ(batchParsed->executionType.value().value(),
		 static_cast<int8_t>(RequestBatchExecutionType::SerialRealtime));
	CHECK_EQ(batchParsed->requests.value().size(), size_t{2});

	// RequestBatchResponse (Op 9)
	const P::RequestBatchResponseData batchResp{
		.requestId = "batch-1",
		.results = std::vector<P::RequestBatchResultItem>{P::RequestBatchResultItem{
			.requestType = "GetVersion",
			.requestId = std::nullopt,
			.requestStatus = P::RequestStatusPayload{.result = true, .code = 100, .comment = std::nullopt},
			.responseData = std::nullopt,
		}},
	};
	const nlohmann::json batchRespJson = P::EncodeJson(batchResp);
	CHECK_EQ(batchRespJson["results"].size(), size_t{1});
}

void TestNlohmannBridgeRequestsAndResponses()
{
	// GetVersionResponse
	const P::GetVersionResponse ver{
		.obsVersion = "31.0.0",
		.obsWebSocketVersion = "5.7.6",
		.rpcVersion = 1,
		.availableRequests = std::vector<std::string>{"GetVersion", "GetStats"},
		.supportedImageFormats = std::vector<std::string>{"png", "jpg"},
		.platform = "macos",
		.platformDescription = "macOS 15",
	};
	const nlohmann::json verJson = P::EncodeJson(ver);
	const auto verBack = P::DecodeJson<P::GetVersionResponse>(verJson);
	CHECK(verBack.has_value());
	CHECK_EQ(verBack->availableRequests.value().size(), size_t{2});
	CHECK_EQ(verBack->platform.value(), std::string("macos"));

	// TriggerHotkeyByKeySequenceRequest with partial keyModifiers (DefaultVal<bool>)
	const nlohmann::json hotkeyIn = {
		{"keyId", "OBS_KEY_A"},
		{"keyModifiers", {{"shift", true}, {"command", true}}},
	};
	const auto hotkeyParsed = P::ParseRequestData<P::TriggerHotkeyByKeySequenceRequest>(true, hotkeyIn);
	CHECK(std::holds_alternative<P::TriggerHotkeyByKeySequenceRequest>(hotkeyParsed));
	const auto &hk = std::get<P::TriggerHotkeyByKeySequenceRequest>(hotkeyParsed);
	CHECK(hk.keyId.value().has_value());
	CHECK_EQ(hk.keyId.value()->value(), std::string("OBS_KEY_A"));
	CHECK(hk.keyModifiers.value().has_value());
	CHECK_EQ(hk.keyModifiers.value()->shift.value().value(), true);
	CHECK_EQ(hk.keyModifiers.value()->control.value().value(), false);
	CHECK_EQ(hk.keyModifiers.value()->alt.value().value(), false);
	CHECK_EQ(hk.keyModifiers.value()->command.value().value(), true);

	// OpenVideoMixProjectorRequest with Literal + DefaultVal<MonitorIndex>
	const nlohmann::json projIn = {{"videoMixType", "OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM"}};
	const auto projParsed = P::ParseRequestData<P::OpenVideoMixProjectorRequest>(true, projIn);
	CHECK(std::holds_alternative<P::OpenVideoMixProjectorRequest>(projParsed));
	const auto &proj = std::get<P::OpenVideoMixProjectorRequest>(projParsed);
	CHECK_EQ(proj.videoMixType.value().str(), std::string("OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM"));
	CHECK_EQ(proj.monitorIndex.value().value().value(), -1);
	CHECK(!proj.projectorGeometry.value().has_value());

	// GetMonitorListResponse with typed MonitorInfo array
	const P::GetMonitorListResponse monResp{
		.monitors = std::vector<P::MonitorInfo>{P::MonitorInfo{
			.monitorName = "Built-in Retina Display(0)",
			.monitorIndex = 0,
			.monitorWidth = 3024,
			.monitorHeight = 1964,
			.monitorPositionX = 0,
			.monitorPositionY = 0,
		}},
	};
	const nlohmann::json monJson = P::EncodeJson(monResp);
	CHECK_EQ(monJson["monitors"].size(), size_t{1});
	CHECK_EQ(monJson["monitors"][0]["monitorWidth"].get<int>(), 3024);

	// GetCurrentProgramSceneResponse (with rfl::Deprecated fields)
	const P::GetCurrentProgramSceneResponse curScene{
		.sceneName = "Main Scene",
		.sceneUuid = "00000000-1111-2222-3333-444444444444",
		.currentProgramSceneName = "Main Scene",
		.currentProgramSceneUuid = "00000000-1111-2222-3333-444444444444",
	};
	const nlohmann::json curSceneJson = P::EncodeJson(curScene);
	CHECK_EQ(curSceneJson["sceneName"].get<std::string>(), std::string("Main Scene"));
	CHECK_EQ(curSceneJson["currentProgramSceneName"].get<std::string>(), std::string("Main Scene"));
}

// ============================================================================
// Suite B: rfl::json::Raw (#67) zero-copy wire envelope & arbitrary passthrough
// ============================================================================

void TestJsonRawWireEnvelopeAndPassthrough()
{
	// 1. WireEnvelope parses `op` while keeping `d` raw for lazy dispatch
	const std::string wireFrame =
		R"({"op":6,"d":{"requestType":"CallVendorRequest","requestId":"v-1","requestData":{"vendorName":"acme","requestType":"Ping","requestData":{"nested":[1,true,"ok",null],"big":9007199254740991}}}})";
	const auto env = P::DecodeWireJson<P::WireEnvelope>(wireFrame);
	CHECK(env.has_value());
	CHECK_EQ(env->op.value(), static_cast<uint8_t>(WebSocketOpCode::Request));

	// Lazily decode inner `d` as RequestMessageData
	const auto reqMsg = P::DecodeWireJson<P::RequestMessageData>(env->d.value().str());
	CHECK(reqMsg.has_value());
	CHECK_EQ(reqMsg->requestType.value().value(), std::string("CallVendorRequest"));
	CHECK_EQ(reqMsg->requestId.value(), std::string("v-1"));

	// Round-trip WireEnvelope verbatim
	const std::string reserialized = P::EncodeWireJson(*env);
	CHECK_EQ(nlohmann::json::parse(reserialized), nlohmann::json::parse(wireFrame));

	// 2. RawBroadcastCustomEventRequest preserves arbitrary JSON verbatim
	const std::string customWire = R"({"eventData":{"plugin":"demo","values":[1,2.5,false,{"deep":"ok"}]}})";
	const auto rawBroadcast = P::DecodeWireJson<P::RawBroadcastCustomEventRequest>(customWire);
	CHECK(rawBroadcast.has_value());
	CHECK_EQ(nlohmann::json::parse(rawBroadcast->eventData.value().str())["values"][3]["deep"].get<std::string>(),
		 std::string("ok"));
	CHECK_EQ(nlohmann::json::parse(P::EncodeWireJson(*rawBroadcast)), nlohmann::json::parse(customWire));

	// 3. RawCallVendorRequest / RawCallVendorResponse arbitrary payload passthrough
	const std::string vendorWire =
		R"({"vendorName":"obs-shaderfilter","requestType":"Reload","requestData":{"pass":2,"uniforms":{"gain":0.75}}})";
	const auto rawVendor = P::DecodeWireJson<P::RawCallVendorRequest>(vendorWire);
	CHECK(rawVendor.has_value());
	CHECK_EQ(rawVendor->vendorName.value().value(), std::string("obs-shaderfilter"));
	CHECK(rawVendor->requestData.value().has_value());

	const auto validatedRaw = rfl::json::Raw::from_string(R"({"ok":true,"shaderTimeMs":1.25})");
	CHECK(validatedRaw.has_value());
	const P::RawCallVendorResponse rawVendorResp{
		.vendorName = rawVendor->vendorName.value().value(),
		.requestType = rawVendor->requestType.value().value(),
		.responseData = *validatedRaw,
	};
	const nlohmann::json respParsed = nlohmann::json::parse(P::EncodeWireJson(rawVendorResp));
	CHECK_EQ(respParsed["vendorName"].get<std::string>(), std::string("obs-shaderfilter"));
	CHECK_EQ(respParsed["responseData"]["ok"].get<bool>(), true);

	// Invalid raw JSON is rejected by Raw::from_string
	CHECK(!rfl::json::Raw::from_string("{unterminated").has_value());
}

// ============================================================================
// Suite C: rfl::ErrorKind (#70) -> exact obs-websocket RequestStatus & CloseCode
// ============================================================================

void TestErrorKindMappingViaYyjson()
{
	// Missing field -> ErrorKind::missing_field -> MissingRequestField (300)
	{
		const auto res = P::DecodeWireJson<P::CallVendorRequestRequest>(R"({"vendorName":"v"})");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::missing_field);
		const auto mapped = P::MapErrorToRequestStatus(res.error());
		CHECK_EQ(static_cast<int>(mapped.statusCode), static_cast<int>(RequestStatus::MissingRequestField));
		CHECK(mapped.comment.find("requestType") != std::string::npos);
	}

	// Wrong type -> ErrorKind::invalid_type -> InvalidRequestFieldType (401)
	{
		const auto res = P::DecodeWireJson<P::SetStudioModeEnabledRequest>(R"({"studioModeEnabled":"yes"})");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::invalid_type);
		const auto mapped = P::MapErrorToRequestStatus(res.error());
		CHECK_EQ(static_cast<int>(mapped.statusCode), static_cast<int>(RequestStatus::InvalidRequestFieldType));
	}

	// Numeric out of range -> ErrorKind::constraint_violation -> RequestFieldOutOfRange (402)
	{
		const auto res = P::DecodeWireJson<P::SleepRequest>(R"({"sleepMillis":50001})");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::constraint_violation);
		const auto mapped = P::MapErrorToRequestStatus(res.error());
		CHECK_EQ(static_cast<int>(mapped.statusCode), static_cast<int>(RequestStatus::RequestFieldOutOfRange));
	}

	// Empty string -> ErrorKind::constraint_violation (Size) -> RequestFieldEmpty (403)
	{
		const auto res = P::DecodeWireJson<P::TriggerHotkeyByNameRequest>(R"({"hotkeyName":""})");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::constraint_violation);
		const auto mapped = P::MapErrorToRequestStatus(res.error());
		CHECK_EQ(static_cast<int>(mapped.statusCode), static_cast<int>(RequestStatus::RequestFieldEmpty));
	}

	// Invalid enum literal -> ErrorKind::invalid_value -> InvalidRequestField (400)
	{
		const auto res = P::DecodeWireJson<P::OpenVideoMixProjectorRequest>(R"({"videoMixType":"NOT_A_MIX"})");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::invalid_value);
		const auto mapped = P::MapErrorToRequestStatus(res.error());
		CHECK_EQ(static_cast<int>(mapped.statusCode), static_cast<int>(RequestStatus::InvalidRequestField));
	}

	// Malformed JSON -> ErrorKind::syntax_error -> WebSocketCloseCode::MessageDecodeError (4002)
	{
		const auto res = P::DecodeWireJson<P::WireEnvelope>("{bad json");
		CHECK(!res.has_value());
		CHECK(res.error().kind() == rfl::ErrorKind::syntax_error);
		const auto closeErr = P::MapErrorToCloseCode(res.error());
		CHECK_EQ(static_cast<int>(closeErr.closeCode), static_cast<int>(WebSocketCloseCode::MessageDecodeError));
	}

	// WebSocketCloseCode mapping for IdentifyData
	{
		const auto missing = P::DecodeWireJson<P::IdentifyData>(R"({})");
		CHECK(!missing.has_value());
		CHECK_EQ(static_cast<int>(P::MapErrorToCloseCode(missing.error()).closeCode),
			 static_cast<int>(WebSocketCloseCode::MissingDataField));

		const auto badType = P::DecodeWireJson<P::IdentifyData>(R"({"rpcVersion":"one"})");
		CHECK(!badType.has_value());
		CHECK_EQ(static_cast<int>(P::MapErrorToCloseCode(badType.error()).closeCode),
			 static_cast<int>(WebSocketCloseCode::InvalidDataFieldType));

		const auto badVal = P::DecodeWireJson<P::IdentifyData>(R"({"rpcVersion":-5})");
		CHECK(!badVal.has_value());
		CHECK_EQ(static_cast<int>(P::MapErrorToCloseCode(badVal.error()).closeCode),
			 static_cast<int>(WebSocketCloseCode::InvalidDataFieldValue));
	}
}

void TestErrorMappingViaNlohmannRequestBridge()
{
	// Missing requestData entirely -> MissingRequestData (301)
	{
		const auto res = P::ParseRequestData<P::SetStudioModeEnabledRequest>(false, nlohmann::json(nullptr));
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::MissingRequestData));
	}

	// Missing required field inside object -> MissingRequestField (300)
	{
		const auto res = P::ParseRequestData<P::SetStudioModeEnabledRequest>(true, nlohmann::json::object());
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::MissingRequestField));
	}

	// Wrong type -> InvalidRequestFieldType (401)
	{
		const auto res = P::ParseRequestData<P::SetStudioModeEnabledRequest>(
			true, nlohmann::json{{"studioModeEnabled", 123}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::InvalidRequestFieldType));
	}

	// Empty string -> RequestFieldEmpty (403)
	{
		const auto res = P::ParseRequestData<P::CreateRecordChapterRequest>(
			true, nlohmann::json{{"chapterName", ""}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::RequestFieldEmpty));
	}

	// Empty object for BroadcastCustomEvent -> RequestFieldEmpty (403)
	{
		const auto res = P::ParseRequestData<P::BroadcastCustomEventRequest>(
			true, nlohmann::json{{"eventData", nlohmann::json::object()}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::RequestFieldEmpty));
	}

	// Non-object for BroadcastCustomEvent -> InvalidRequestFieldType (401)
	{
		const auto res = P::ParseRequestData<P::BroadcastCustomEventRequest>(
			true, nlohmann::json{{"eventData", nlohmann::json::array({1, 2})}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::InvalidRequestFieldType));
	}

	// Out-of-range monitorIndex -> RequestFieldOutOfRange (402)
	{
		const auto res = P::ParseRequestData<P::OpenVideoMixProjectorRequest>(
			true,
			nlohmann::json{{"videoMixType", "OBS_WEBSOCKET_VIDEO_MIX_TYPE_PREVIEW"}, {"monitorIndex", 10}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::RequestFieldOutOfRange));
	}

	// Out-of-range sleepFrames -> RequestFieldOutOfRange (402)
	{
		const auto res = P::ParseRequestData<P::SleepRequest>(true, nlohmann::json{{"sleepFrames", -1}});
		CHECK(std::holds_alternative<P::MappedRequestError>(res));
		CHECK_EQ(static_cast<int>(std::get<P::MappedRequestError>(res).statusCode),
			 static_cast<int>(RequestStatus::RequestFieldOutOfRange));
	}

	// SendStreamCaption explicitly allows empty string (upstream allowEmpty=true)
	{
		const auto res = P::ParseRequestData<P::SendStreamCaptionRequest>(
			true, nlohmann::json{{"captionText", ""}});
		CHECK(std::holds_alternative<P::SendStreamCaptionRequest>(res));
		CHECK_EQ(std::get<P::SendStreamCaptionRequest>(res).captionText.value(), std::string(""));
	}
}

// ============================================================================
// Suite D: Dual JSON + MessagePack round-trips from the exact same structs
// ============================================================================

template <class T>
void AssertDualJsonMsgPackRoundTrip(const T &original)
{
	const nlohmann::json asJson = P::EncodeJson(original);
	const std::vector<std::uint8_t> asMsgPack = P::EncodeMsgPack(original);
	CHECK(!asMsgPack.empty());

	const auto fromJson = P::DecodeJson<T>(asJson);
	CHECK(fromJson.has_value());
	const auto fromMsgPack = P::DecodeMsgPack<T>(asMsgPack);
	CHECK(fromMsgPack.has_value());

	// Both decoded structs must re-encode to identical JSON documents
	CHECK_EQ(P::EncodeJson(*fromJson), asJson);
	CHECK_EQ(P::EncodeJson(*fromMsgPack), asJson);
}

void TestDualJsonAndMsgPackCodecs()
{
	AssertDualJsonMsgPackRoundTrip(P::HelloData{
		.obsStudioVersion = "31.0.0",
		.obsWebSocketVersion = "5.7.6",
		.rpcVersion = 1,
		.authentication = P::HelloAuthentication{.challenge = "abc", .salt = "xyz"},
	});

	AssertDualJsonMsgPackRoundTrip(P::IdentifyData{
		.rpcVersion = 1,
		.authentication = std::string("auth-token"),
		.eventSubscriptions = static_cast<uint64_t>(EventSubscription::General | EventSubscription::Ui),
	});

	AssertDualJsonMsgPackRoundTrip(P::GetStreamStatusResponse{
		.outputActive = true,
		.outputReconnecting = false,
		.outputTimecode = "00:12:34.567",
		.outputDuration = 754567,
		.outputCongestion = 0.25f,
		.outputBytes = 987654321,
		.outputSkippedFrames = 3,
		.outputTotalFrames = 45000,
	});

	AssertDualJsonMsgPackRoundTrip(P::GetRecordStatusResponse{
		.outputActive = true,
		.outputPaused = false,
		.outputTimecode = "00:05:00.000",
		.outputDuration = 300000,
		.outputBytes = 123456789,
	});

	AssertDualJsonMsgPackRoundTrip(P::GetMonitorListResponse{
		.monitors =
			std::vector<P::MonitorInfo>{
				P::MonitorInfo{"Display 1(0)", 0, 2560, 1440, 0, 0},
				P::MonitorInfo{"Display 2(1)", 1, 1920, 1080, 2560, 0},
			},
	});

	AssertDualJsonMsgPackRoundTrip(P::CallVendorRequestResponse{
		.vendorName = "vendor-a",
		.requestType = "DoThing",
		.responseData = P::ToJsonObject(nlohmann::json{{"count", 7}, {"tags", {"a", "b"}}}),
	});

	// Corrupted MsgPack returns ErrorKind::syntax_error
	const auto badMp = P::DecodeMsgPack<P::IdentifyData>({0xC1});
	CHECK(!badMp.has_value());
	CHECK(badMp.error().kind() == rfl::ErrorKind::syntax_error);
}

// ============================================================================
// Suite E: JSON Schema features (VariantsAsOneOf, Deprecated, DefaultVal, Validators)
// ============================================================================

void TestSchemaFeatures()
{
	// 1. VariantsAsOneOf (#14) emits oneOf for WebSocketMessage (vs anyOf without it)
	const nlohmann::json oneOfSchema =
		nlohmann::json::parse(rfl::json::to_schema<P::WebSocketMessage, rfl::VariantsAsOneOf>());
	CHECK_EQ(oneOfSchema["$schema"].get<std::string>(),
		 std::string("https://json-schema.org/draft/2020-12/schema"));
	CHECK(oneOfSchema.contains("oneOf"));
	CHECK(!oneOfSchema.contains("anyOf"));
	CHECK_EQ(oneOfSchema["oneOf"].size(), size_t{9});

	const nlohmann::json defaultAnyOfSchema =
		nlohmann::json::parse(rfl::json::to_schema<P::WebSocketMessage>());
	CHECK(defaultAnyOfSchema.contains("anyOf"));
	CHECK_EQ(defaultAnyOfSchema["anyOf"].size(), size_t{9});

	// 2. rfl::Deprecated emits deprecated=true + deprecationMessage on GetCurrentProgramSceneResponse
	const nlohmann::json sceneSchema =
		nlohmann::json::parse(rfl::json::to_schema<P::GetCurrentProgramSceneResponse>());
	const std::string ref = sceneSchema["$ref"].get<std::string>();
	const std::string defName = ref.substr(std::string("#/$defs/").size());
	const auto &props = sceneSchema["$defs"][defName]["properties"];
	CHECK(!props["sceneName"].contains("deprecated"));
	CHECK_EQ(props["currentProgramSceneName"]["deprecated"].get<bool>(), true);
	CHECK_EQ(props["currentProgramSceneName"]["deprecationMessage"].get<std::string>(),
		 std::string("Use `sceneName` instead"));
	CHECK_EQ(props["currentProgramSceneUuid"]["deprecated"].get<bool>(), true);

	// 3. DefaultVal + Validator + Literal on OpenVideoMixProjectorRequest
	const nlohmann::json projSchema =
		nlohmann::json::parse(rfl::json::to_schema<P::OpenVideoMixProjectorRequest>());
	const std::string pDef = projSchema["$ref"].get<std::string>().substr(std::string("#/$defs/").size());
	const auto &pObj = projSchema["$defs"][pDef];
	const auto &pProps = pObj["properties"];
	CHECK_EQ(pProps["videoMixType"]["enum"].size(), size_t{3});
	CHECK_EQ(pProps["monitorIndex"]["default"].get<int>(), -1);
	CHECK(pProps["monitorIndex"].contains("allOf"));
	// Only videoMixType is required; monitorIndex (DefaultVal) & projectorGeometry (optional) are optional
	CHECK_EQ(pObj["required"].size(), size_t{1});
	CHECK_EQ(pObj["required"][0].get<std::string>(), std::string("videoMixType"));
}

// ============================================================================
// Suite F: Automated structural parity against docs/generated/protocol.json
// ============================================================================

const nlohmann::json &ResolveDef(const nlohmann::json &schemaDoc, const nlohmann::json &node)
{
	if (node.is_object() && node.contains("$ref") && node["$ref"].is_string()) {
		const std::string ref = node["$ref"].get<std::string>();
		const std::string prefix = "#/$defs/";
		CHECK(ref.rfind(prefix, 0) == 0);
		const std::string key = ref.substr(prefix.size());
		CHECK(schemaDoc.contains("$defs") && schemaDoc["$defs"].contains(key));
		return schemaDoc["$defs"].at(key);
	}
	return node;
}

// Unwraps optional `anyOf: [T, {"type":"null"}]` produced by reflect-cpp for std::optional<T>.
const nlohmann::json &UnwrapNullable(const nlohmann::json &schemaDoc, const nlohmann::json &propSchema)
{
	if (propSchema.is_object() && propSchema.contains("anyOf") && propSchema["anyOf"].is_array() &&
	    propSchema["anyOf"].size() == 2) {
		for (const auto &alt : propSchema["anyOf"]) {
			if (!(alt.is_object() && alt.contains("type") && alt["type"] == "null")) {
				return ResolveDef(schemaDoc, alt);
			}
		}
	}
	return ResolveDef(schemaDoc, propSchema);
}

bool SchemaMatchesProtocolType(const nlohmann::json &schemaDoc, const nlohmann::json &propSchema,
			       const std::string &protocolType)
{
	const nlohmann::json &inner = UnwrapNullable(schemaDoc, propSchema);
	auto checkType = [&](const nlohmann::json &node, const std::string &expected) -> bool {
		if (node.contains("type") && node["type"].is_string()) {
			const std::string t = node["type"].get<std::string>();
			if (expected == "Number")
				return t == "number" || t == "integer";
			if (expected == "String")
				return t == "string";
			if (expected == "Boolean")
				return t == "boolean";
			if (expected == "Object")
				return t == "object";
		}
		if (expected == "String" && node.contains("enum") && node["enum"].is_array())
			return true;
		if (expected == "Number" && node.contains("allOf") && node["allOf"].is_array()) {
			for (const auto &sub : node["allOf"]) {
				if (sub.contains("type") && (sub["type"] == "integer" || sub["type"] == "number"))
					return true;
			}
		}
		return false;
	};

	if (protocolType == "Array<String>") {
		if (!inner.contains("type") || inner["type"] != "array" || !inner.contains("items"))
			return false;
		return checkType(ResolveDef(schemaDoc, inner["items"]), "String");
	}
	if (protocolType == "Array<Object>") {
		if (!inner.contains("type") || inner["type"] != "array" || !inner.contains("items"))
			return false;
		return checkType(ResolveDef(schemaDoc, inner["items"]), "Object");
	}
	return checkType(inner, protocolType);
}

struct RegisteredRequest {
	std::string category;
	std::string requestType;
	nlohmann::json requestSchema;
	nlohmann::json responseSchema;
};

template <class Req, class Res>
RegisteredRequest Reg(const char *category, const char *name)
{
	return RegisteredRequest{
		.category = category,
		.requestType = name,
		.requestSchema = nlohmann::json::parse(rfl::json::to_schema<Req>()),
		.responseSchema = nlohmann::json::parse(rfl::json::to_schema<Res>()),
	};
}

void CheckNumericBounds(const nlohmann::json &schemaDoc, const nlohmann::json &propSchema,
			const std::string &restrictions)
{
	const nlohmann::json &inner = UnwrapNullable(schemaDoc, propSchema);
	CHECK(inner.contains("allOf") && inner["allOf"].is_array());
	bool foundMin = false;
	bool foundMax = false;
	double minVal = 0;
	double maxVal = 0;
	for (const auto &clause : inner["allOf"]) {
		if (clause.contains("minimum")) {
			foundMin = true;
			minVal = clause["minimum"].get<double>();
		}
		if (clause.contains("maximum")) {
			foundMax = true;
			maxVal = clause["maximum"].get<double>();
		}
	}
	if (restrictions == ">= 0, <= 50000") {
		CHECK(foundMin && minVal == 0.0);
		CHECK(foundMax && maxVal == 50000.0);
	} else if (restrictions == ">= 0, <= 10000") {
		CHECK(foundMin && minVal == 0.0);
		CHECK(foundMax && maxVal == 10000.0);
	}
}

void TestStructuralParityAgainstProtocolJson()
{
	std::ifstream ifs(OBS_WEBSOCKET_PROTOCOL_JSON);
	CHECK(ifs.is_open());
	const nlohmann::json protocolDoc = nlohmann::json::parse(ifs);
	CHECK(protocolDoc.contains("requests") && protocolDoc["requests"].is_array());

	const std::vector<RegisteredRequest> registry = {
		// general (8)
		Reg<P::EmptyPayload, P::GetVersionResponse>("general", "GetVersion"),
		Reg<P::EmptyPayload, P::GetStatsResponse>("general", "GetStats"),
		Reg<P::BroadcastCustomEventRequest, P::EmptyPayload>("general", "BroadcastCustomEvent"),
		Reg<P::CallVendorRequestRequest, P::CallVendorRequestResponse>("general", "CallVendorRequest"),
		Reg<P::EmptyPayload, P::GetHotkeyListResponse>("general", "GetHotkeyList"),
		Reg<P::TriggerHotkeyByNameRequest, P::EmptyPayload>("general", "TriggerHotkeyByName"),
		Reg<P::TriggerHotkeyByKeySequenceRequest, P::EmptyPayload>("general", "TriggerHotkeyByKeySequence"),
		Reg<P::SleepRequest, P::EmptyPayload>("general", "Sleep"),
		// stream (5)
		Reg<P::EmptyPayload, P::GetStreamStatusResponse>("stream", "GetStreamStatus"),
		Reg<P::EmptyPayload, P::ToggleStreamResponse>("stream", "ToggleStream"),
		Reg<P::EmptyPayload, P::EmptyPayload>("stream", "StartStream"),
		Reg<P::EmptyPayload, P::EmptyPayload>("stream", "StopStream"),
		Reg<P::SendStreamCaptionRequest, P::EmptyPayload>("stream", "SendStreamCaption"),
		// record (9)
		Reg<P::EmptyPayload, P::GetRecordStatusResponse>("record", "GetRecordStatus"),
		Reg<P::EmptyPayload, P::ToggleRecordResponse>("record", "ToggleRecord"),
		Reg<P::EmptyPayload, P::EmptyPayload>("record", "StartRecord"),
		Reg<P::EmptyPayload, P::StopRecordResponse>("record", "StopRecord"),
		Reg<P::EmptyPayload, P::ToggleRecordPauseResponse>("record", "ToggleRecordPause"),
		Reg<P::EmptyPayload, P::EmptyPayload>("record", "PauseRecord"),
		Reg<P::EmptyPayload, P::EmptyPayload>("record", "ResumeRecord"),
		Reg<P::EmptyPayload, P::EmptyPayload>("record", "SplitRecordFile"),
		Reg<P::CreateRecordChapterRequest, P::EmptyPayload>("record", "CreateRecordChapter"),
		// ui (8)
		Reg<P::EmptyPayload, P::GetStudioModeEnabledResponse>("ui", "GetStudioModeEnabled"),
		Reg<P::SetStudioModeEnabledRequest, P::EmptyPayload>("ui", "SetStudioModeEnabled"),
		Reg<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputPropertiesDialog"),
		Reg<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputFiltersDialog"),
		Reg<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputInteractDialog"),
		Reg<P::EmptyPayload, P::GetMonitorListResponse>("ui", "GetMonitorList"),
		Reg<P::OpenVideoMixProjectorRequest, P::EmptyPayload>("ui", "OpenVideoMixProjector"),
		Reg<P::OpenSourceProjectorRequest, P::EmptyPayload>("ui", "OpenSourceProjector"),
		// scenes deprecated-field showcase (1)
		Reg<P::EmptyPayload, P::GetCurrentProgramSceneResponse>("scenes", "GetCurrentProgramScene"),
	};

	std::map<std::string, const RegisteredRequest *> byName;
	for (const auto &entry : registry)
		byName[entry.requestType] = &entry;

	// Index protocol.json requests
	std::map<std::string, const nlohmann::json *> protoByName;
	const std::set<std::string> fullCategories = {"general", "stream", "record", "ui"};
	for (const auto &req : protocolDoc["requests"]) {
		const std::string name = req["requestType"].get<std::string>();
		const std::string cat = req["category"].get<std::string>();
		protoByName[name] = &req;
		if (fullCategories.count(cat)) {
			// Every single request in general, stream, record, ui must be in our C++ registry!
			CHECK(byName.count(name) == 1);
		}
	}

	int verifiedRequests = 0;
	int verifiedReqFields = 0;
	int verifiedResFields = 0;
	int upstreamDriftsDetected = 0;

	for (const auto &entry : registry) {
		CHECK(protoByName.count(entry.requestType) == 1);
		const nlohmann::json &protoReq = *protoByName[entry.requestType];
		CHECK_EQ(protoReq["category"].get<std::string>(), entry.category);

		// 1. Verify requestFields
		const nlohmann::json &reqRoot = ResolveDef(entry.requestSchema, entry.requestSchema);
		const nlohmann::json reqProps = reqRoot.value("properties", nlohmann::json::object());
		std::set<std::string> reqRequired;
		if (reqRoot.contains("required") && reqRoot["required"].is_array()) {
			for (const auto &k : reqRoot["required"])
				reqRequired.insert(k.get<std::string>());
		}

		std::set<std::string> seenTopReqFields;
		for (const auto &field : protoReq["requestFields"]) {
			const std::string valueName = field["valueName"].get<std::string>();
			const std::string valueType = field["valueType"].get<std::string>();
			const std::string valueDesc = field["valueDescription"].get<std::string>();
			const bool valueOptional = field["valueOptional"].get<bool>();

			const size_t dot = valueName.find('.');
			if (dot == std::string::npos) {
				seenTopReqFields.insert(valueName);
				CHECK(reqProps.contains(valueName));
				const auto &prop = reqProps.at(valueName);
				CHECK(SchemaMatchesProtocolType(entry.requestSchema, prop, valueType));
				CHECK_EQ(reqRequired.count(valueName) == 0, valueOptional);
				CHECK(prop.contains("description"));
				CHECK_EQ(prop["description"].get<std::string>(), valueDesc);
				if (field.contains("valueRestrictions") && field["valueRestrictions"].is_string()) {
					CheckNumericBounds(entry.requestSchema, prop,
							   field["valueRestrictions"].get<std::string>());
				}
			} else {
				// Nested object property (e.g., keyModifiers.shift)
				const std::string parentName = valueName.substr(0, dot);
				const std::string childName = valueName.substr(dot + 1);
				CHECK(reqProps.contains(parentName));
				const nlohmann::json &parentObj =
					UnwrapNullable(entry.requestSchema, reqProps.at(parentName));
				CHECK(parentObj.contains("properties") && parentObj["properties"].contains(childName));
				const auto &childProp = parentObj["properties"].at(childName);
				CHECK(SchemaMatchesProtocolType(entry.requestSchema, childProp, valueType));
				CHECK(childProp.contains("description"));
				CHECK_EQ(childProp["description"].get<std::string>(), valueDesc);
				std::set<std::string> parentRequired;
				if (parentObj.contains("required") && parentObj["required"].is_array()) {
					for (const auto &k : parentObj["required"])
						parentRequired.insert(k.get<std::string>());
				}
				CHECK_EQ(parentRequired.count(childName) == 0, valueOptional);
			}
			++verifiedReqFields;
		}
		// No extra undocumented request fields in C++ struct
		CHECK_EQ(reqProps.size(), seenTopReqFields.size());

		// 2. Verify responseFields
		const nlohmann::json &resRoot = ResolveDef(entry.responseSchema, entry.responseSchema);
		const nlohmann::json resProps = resRoot.value("properties", nlohmann::json::object());

		std::set<std::string> seenResFields;
		for (const auto &field : protoReq["responseFields"]) {
			const std::string valueName = field["valueName"].get<std::string>();
			const std::string valueType = field["valueType"].get<std::string>();
			const std::string valueDesc = field["valueDescription"].get<std::string>();

			seenResFields.insert(valueName);
			CHECK(resProps.contains(valueName));
			const auto &prop = resProps.at(valueName);
			CHECK(SchemaMatchesProtocolType(entry.responseSchema, prop, valueType));
			CHECK(prop.contains("description"));
			CHECK_EQ(prop["description"].get<std::string>(), valueDesc);
			++verifiedResFields;
		}

		// Catch known upstream docblock omission: ToggleRecordPause returns `outputPaused`
		// in RequestHandler_Record.cpp, but upstream protocol.json omitted @responseField.
		if (entry.requestType == "ToggleRecordPause") {
			CHECK(resProps.contains("outputPaused"));
			CHECK_EQ(seenResFields.size(), size_t{0});
			++upstreamDriftsDetected;
		} else {
			CHECK_EQ(resProps.size(), seenResFields.size());
		}

		++verifiedRequests;
	}

	CHECK_EQ(verifiedRequests, 31);
	CHECK_EQ(upstreamDriftsDetected, 1);

	std::cout << "  Parity verified: " << verifiedRequests << " requests, " << verifiedReqFields
		  << " request fields, " << verifiedResFields << " response fields against protocol.json "
		  << "(" << upstreamDriftsDetected << " real upstream docblock drift detected: "
		  << "ToggleRecordPause.outputPaused)\n";
}

} // namespace

int main()
{
	RunTest("nlohmann_bridge_opcodes", TestNlohmannBridgeOpCodes);
	RunTest("nlohmann_bridge_requests_and_responses", TestNlohmannBridgeRequestsAndResponses);
	RunTest("json_raw_wire_envelope_and_passthrough", TestJsonRawWireEnvelopeAndPassthrough);
	RunTest("error_kind_mapping_yyjson", TestErrorKindMappingViaYyjson);
	RunTest("error_mapping_nlohmann_request_bridge", TestErrorMappingViaNlohmannRequestBridge);
	RunTest("dual_json_and_msgpack_codecs", TestDualJsonAndMsgPackCodecs);
	RunTest("schema_features_oneof_deprecated_defaults", TestSchemaFeatures);
	RunTest("structural_parity_against_protocol_json", TestStructuralParityAgainstProtocolJson);

	std::cout << "\nSummary: " << gTestsPassed << "/" << gTestsRun << " test suites passed ("
		  << gAssertions << " assertions)\n";
	return gTestsPassed == gTestsRun ? 0 : 1;
}
