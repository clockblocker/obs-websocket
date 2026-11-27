#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include "protocol/ProtocolTypes.h"

namespace P = ObsWebSocket::Protocol;

namespace {

template <class T, class... Ps>
nlohmann::json SchemaJson()
{
	return nlohmann::json::parse(rfl::json::to_schema<T, Ps...>());
}

template <class Req, class Res>
nlohmann::json MakeRequestEntry(const char *category, const char *requestType)
{
	return nlohmann::json{
		{"category", category},
		{"requestType", requestType},
		{"requestSchema", SchemaJson<Req>()},
		{"responseSchema", SchemaJson<Res>()},
	};
}

nlohmann::json BuildProtocolSchemaBundle()
{
	nlohmann::json bundle;
	bundle["$schema"] = "https://json-schema.org/draft/2020-12/schema";
	bundle["title"] = "obs-websocket v5 Protocol Schema (generated from C++20 via reflect-cpp)";
	bundle["rpcVersion"] = 1;

	bundle["webSocketMessage"] = SchemaJson<P::WebSocketMessage, rfl::VariantsAsOneOf>();
	bundle["opCodes"] = {
		{"Hello", SchemaJson<P::HelloData>()},
		{"Identify", SchemaJson<P::IdentifyData>()},
		{"Identified", SchemaJson<P::IdentifiedData>()},
		{"Reidentify", SchemaJson<P::ReidentifyData>()},
		{"Event", SchemaJson<P::EventData>()},
		{"Request", SchemaJson<P::RequestMessageData>()},
		{"RequestResponse", SchemaJson<P::RequestResponseData>()},
		{"RequestBatch", SchemaJson<P::RequestBatchData>()},
		{"RequestBatchResponse", SchemaJson<P::RequestBatchResponseData>()},
	};

	nlohmann::json requests = nlohmann::json::object();
	auto add = [&](const char *category, const char *name, nlohmann::json entry) {
		requests[name] = std::move(entry);
		(void)category;
	};

	// General (8)
	add("general", "GetVersion", MakeRequestEntry<P::EmptyPayload, P::GetVersionResponse>("general", "GetVersion"));
	add("general", "GetStats", MakeRequestEntry<P::EmptyPayload, P::GetStatsResponse>("general", "GetStats"));
	add("general", "BroadcastCustomEvent",
	    MakeRequestEntry<P::BroadcastCustomEventRequest, P::EmptyPayload>("general", "BroadcastCustomEvent"));
	add("general", "CallVendorRequest",
	    MakeRequestEntry<P::CallVendorRequestRequest, P::CallVendorRequestResponse>("general", "CallVendorRequest"));
	add("general", "GetHotkeyList",
	    MakeRequestEntry<P::EmptyPayload, P::GetHotkeyListResponse>("general", "GetHotkeyList"));
	add("general", "TriggerHotkeyByName",
	    MakeRequestEntry<P::TriggerHotkeyByNameRequest, P::EmptyPayload>("general", "TriggerHotkeyByName"));
	add("general", "TriggerHotkeyByKeySequence",
	    MakeRequestEntry<P::TriggerHotkeyByKeySequenceRequest, P::EmptyPayload>("general", "TriggerHotkeyByKeySequence"));
	add("general", "Sleep", MakeRequestEntry<P::SleepRequest, P::EmptyPayload>("general", "Sleep"));

	// Stream (5)
	add("stream", "GetStreamStatus",
	    MakeRequestEntry<P::EmptyPayload, P::GetStreamStatusResponse>("stream", "GetStreamStatus"));
	add("stream", "ToggleStream", MakeRequestEntry<P::EmptyPayload, P::ToggleStreamResponse>("stream", "ToggleStream"));
	add("stream", "StartStream", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("stream", "StartStream"));
	add("stream", "StopStream", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("stream", "StopStream"));
	add("stream", "SendStreamCaption",
	    MakeRequestEntry<P::SendStreamCaptionRequest, P::EmptyPayload>("stream", "SendStreamCaption"));

	// Record (9)
	add("record", "GetRecordStatus",
	    MakeRequestEntry<P::EmptyPayload, P::GetRecordStatusResponse>("record", "GetRecordStatus"));
	add("record", "ToggleRecord", MakeRequestEntry<P::EmptyPayload, P::ToggleRecordResponse>("record", "ToggleRecord"));
	add("record", "StartRecord", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("record", "StartRecord"));
	add("record", "StopRecord", MakeRequestEntry<P::EmptyPayload, P::StopRecordResponse>("record", "StopRecord"));
	add("record", "ToggleRecordPause",
	    MakeRequestEntry<P::EmptyPayload, P::ToggleRecordPauseResponse>("record", "ToggleRecordPause"));
	add("record", "PauseRecord", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("record", "PauseRecord"));
	add("record", "ResumeRecord", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("record", "ResumeRecord"));
	add("record", "SplitRecordFile", MakeRequestEntry<P::EmptyPayload, P::EmptyPayload>("record", "SplitRecordFile"));
	add("record", "CreateRecordChapter",
	    MakeRequestEntry<P::CreateRecordChapterRequest, P::EmptyPayload>("record", "CreateRecordChapter"));

	// Ui (8)
	add("ui", "GetStudioModeEnabled",
	    MakeRequestEntry<P::EmptyPayload, P::GetStudioModeEnabledResponse>("ui", "GetStudioModeEnabled"));
	add("ui", "SetStudioModeEnabled",
	    MakeRequestEntry<P::SetStudioModeEnabledRequest, P::EmptyPayload>("ui", "SetStudioModeEnabled"));
	add("ui", "OpenInputPropertiesDialog",
	    MakeRequestEntry<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputPropertiesDialog"));
	add("ui", "OpenInputFiltersDialog",
	    MakeRequestEntry<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputFiltersDialog"));
	add("ui", "OpenInputInteractDialog",
	    MakeRequestEntry<P::OpenInputDialogRequest, P::EmptyPayload>("ui", "OpenInputInteractDialog"));
	add("ui", "GetMonitorList",
	    MakeRequestEntry<P::EmptyPayload, P::GetMonitorListResponse>("ui", "GetMonitorList"));
	add("ui", "OpenVideoMixProjector",
	    MakeRequestEntry<P::OpenVideoMixProjectorRequest, P::EmptyPayload>("ui", "OpenVideoMixProjector"));
	add("ui", "OpenSourceProjector",
	    MakeRequestEntry<P::OpenSourceProjectorRequest, P::EmptyPayload>("ui", "OpenSourceProjector"));

	// Scenes deprecated-field showcase (1)
	add("scenes", "GetCurrentProgramScene",
	    MakeRequestEntry<P::EmptyPayload, P::GetCurrentProgramSceneResponse>("scenes", "GetCurrentProgramScene"));

	bundle["requests"] = std::move(requests);
	return bundle;
}

} // namespace

int main(int argc, char **argv)
{
	std::string outPath;
	bool summaryOnly = false;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg(argv[i]);
		if (arg == "--out" && i + 1 < argc) {
			outPath = argv[++i];
		} else if (arg == "--summary") {
			summaryOnly = true;
		}
	}

	const nlohmann::json bundle = BuildProtocolSchemaBundle();
	if (summaryOnly) {
		std::cout << "Generated JSON Schema (draft 2020-12): "
			  << bundle["opCodes"].size() << " opcodes, "
			  << bundle["requests"].size() << " requests, "
			  << bundle.dump().size() << " bytes (compact)\n";
		return 0;
	}

	const std::string rendered = bundle.dump(2) + "\n";
	if (!outPath.empty()) {
		std::ofstream ofs(outPath);
		if (!ofs) {
			std::cerr << "Failed to open output file: " << outPath << "\n";
			return 1;
		}
		ofs << rendered;
		return 0;
	}

	std::cout << rendered;
	return 0;
}
