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

#include <QImageWriter>
#include <QSysInfo>

#include "RequestHandler.h"
#include "rpc/TypedRequest.h"
#include "../websocketserver/WebSocketServer.h"
#include "../eventhandler/types/EventSubscription.h"
#include "../WebSocketApi.h"
#include "../obs-websocket.h"

/**
 * Gets data about the current plugin and RPC version.
 *
 * @responseField obsVersion            | String        | Current OBS Studio version
 * @responseField obsWebSocketVersion   | String        | Current obs-websocket version
 * @responseField rpcVersion            | Number        | Current latest obs-websocket RPC version
 * @responseField availableRequests     | Array<String> | Array of available RPC requests for the currently negotiated RPC version
 * @responseField supportedImageFormats | Array<String> | Image formats available in `GetSourceScreenshot` and `SaveSourceScreenshot` requests.
 * @responseField platform              | String        | Name of the platform. Usually `windows`, `macos`, or `ubuntu` (linux flavor). Not guaranteed to be any of those
 * @responseField platformDescription   | String        | Description of the platform, like `Windows 10 (10.0)`
 *
 * @requestType GetVersion
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::GetVersion(const Request &)
{
	std::vector<std::string> supportedImageFormats;
	for (const QByteArray &format : QImageWriter::supportedImageFormats())
		supportedImageFormats.push_back(format.toStdString());

	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::GetVersionResponse{
		.obsVersion = Utils::Obs::StringHelper::GetObsVersion(),
		.obsWebSocketVersion = OBS_WEBSOCKET_VERSION,
		.rpcVersion = OBS_WEBSOCKET_RPC_VERSION,
		.availableRequests = GetRequestList(),
		.supportedImageFormats = std::move(supportedImageFormats),
		.platform = QSysInfo::productType().toStdString(),
		.platformDescription = QSysInfo::prettyProductName().toStdString(),
	});
}

/**
 * Gets statistics about OBS, obs-websocket, and the current session.
 *
 * @responseField cpuUsage                         | Number | Current CPU usage in percent
 * @responseField memoryUsage                      | Number | Amount of memory in MB currently being used by OBS
 * @responseField availableDiskSpace               | Number | Available disk space on the device being used for recording storage
 * @responseField activeFps                        | Number | Current FPS being rendered
 * @responseField averageFrameRenderTime           | Number | Average time in milliseconds that OBS is taking to render a frame
 * @responseField renderSkippedFrames              | Number | Number of frames skipped by OBS in the render thread
 * @responseField renderTotalFrames                | Number | Total number of frames outputted by the render thread
 * @responseField outputSkippedFrames              | Number | Number of frames skipped by OBS in the output thread
 * @responseField outputTotalFrames                | Number | Total number of frames outputted by the output thread
 * @responseField webSocketSessionIncomingMessages | Number | Total number of messages received by obs-websocket from the client
 * @responseField webSocketSessionOutgoingMessages | Number | Total number of messages sent by obs-websocket to the client
 *
 * @requestType GetStats
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::GetStats(const Request &)
{
	json responseData = Utils::Obs::ObjectHelper::GetStats();

	if (_session) {
		responseData["webSocketSessionIncomingMessages"] = _session->IncomingMessages();
		responseData["webSocketSessionOutgoingMessages"] = _session->OutgoingMessages();
	} else {
		responseData["webSocketSessionIncomingMessages"] = nullptr;
		responseData["webSocketSessionOutgoingMessages"] = nullptr;
	}

	return RequestResult::Success(responseData);
}

/**
 * Custom event emitted by `BroadcastCustomEvent`.
 * 
 * @dataField eventData | Object | Custom event data
 *
 * @eventType CustomEvent
 * @eventSubscription General
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api events
 */

/**
 * Broadcasts a `CustomEvent` to all WebSocket clients. Receivers are clients which are identified and subscribed.
 *
 * @requestField eventData | Object | Data payload to emit to all receivers
 *
 * @requestType BroadcastCustomEvent
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::BroadcastCustomEvent(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::BroadcastCustomEventRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);

	auto webSocketServer = GetWebSocketServer();
	if (!webSocketServer)
		return RequestResult::Error(RequestStatus::RequestProcessingFailed, "Unable to send event due to internal error.");

	webSocketServer->BroadcastEvent(
		EventSubscription::General, "CustomEvent",
		ObsWebSocket::Protocol::FromJsonObject(std::get<0>(parsed).eventData.value().value()));

	return RequestResult::Success();
}

/**
 * Call a request registered to a vendor.
 *
 * A vendor is a unique name registered by a third-party plugin or script, which allows for custom requests and events to be added to obs-websocket.
 * If a plugin or script implements vendor requests or events, documentation is expected to be provided with them.
 *
 * @requestField vendorName   | String | Name of the vendor to use
 * @requestField requestType  | String | The request type to call
 * @requestField ?requestData | Object | Object containing appropriate request data | {}
 *
 * @responseField vendorName   | String | Echoed of `vendorName`
 * @responseField requestType  | String | Echoed of `requestType`
 * @responseField responseData | Object | Object containing appropriate response data. {} if request does not provide any response data
 *
 * @requestType CallVendorRequest
 * @complexity 3
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::CallVendorRequest(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::CallVendorRequestRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);
	const std::string &vendorName = req.vendorName.value().value();
	const std::string &requestType = req.requestType.value().value();

	OBSDataAutoRelease requestData =
		req.requestData.value().has_value()
			? Utils::Json::JsonToObsData(ObsWebSocket::Protocol::FromJsonObject(*req.requestData.value()))
			: obs_data_create();

	OBSDataAutoRelease obsResponseData = obs_data_create();

	auto webSocketApi = GetWebSocketApi();
	if (!webSocketApi)
		return RequestResult::Error(RequestStatus::RequestProcessingFailed,
					    "Unable to call request due to internal error.");

	auto ret = webSocketApi->PerformVendorRequest(vendorName, requestType, requestData, obsResponseData);
	switch (ret) {
	default:
	case WebSocketApi::RequestReturnCode::Normal:
		break;
	case WebSocketApi::RequestReturnCode::NoVendor:
		return RequestResult::Error(RequestStatus::ResourceNotFound, "No vendor was found by that name.");
	case WebSocketApi::RequestReturnCode::NoVendorRequest:
		return RequestResult::Error(RequestStatus::ResourceNotFound, "No request was found by that name.");
	}

	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::CallVendorRequestResponse{
		.vendorName = vendorName,
		.requestType = requestType,
		.responseData = ObsWebSocket::Protocol::ToJsonObject(Utils::Json::ObsDataToJson(obsResponseData)),
	});
}

/**
 * Gets an array of all hotkey names in OBS.
 *
 * Note: Hotkey functionality in obs-websocket comes as-is, and we do not guarantee support if things are broken. In 9/10 usages of hotkey requests, there exists a better, more reliable method via other requests.
 *
 * @responseField hotkeys | Array<String> | Array of hotkey names
 *
 * @requestType GetHotkeyList
 * @complexity 4
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::GetHotkeyList(const Request &)
{
	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::GetHotkeyListResponse{
		.hotkeys = Utils::Obs::ArrayHelper::GetHotkeyNameList(),
	});
}

/**
 * Triggers a hotkey using its name. See `GetHotkeyList`.
 *
 * Note: Hotkey functionality in obs-websocket comes as-is, and we do not guarantee support if things are broken. In 9/10 usages of hotkey requests, there exists a better, more reliable method via other requests.
 *
 * @requestField hotkeyName | String | Name of the hotkey to trigger
 * @requestField ?contextName | String | Name of context of the hotkey to trigger
 *
 * @requestType TriggerHotkeyByName
 * @complexity 4
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::TriggerHotkeyByName(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::TriggerHotkeyByNameRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);

	const std::string contextName =
		req.contextName.value().has_value() ? req.contextName.value()->value() : std::string{};
	obs_hotkey_t *hotkey = Utils::Obs::SearchHelper::GetHotkeyByName(req.hotkeyName.value().value(), contextName);
	if (!hotkey)
		return RequestResult::Error(RequestStatus::ResourceNotFound, "No hotkeys were found by that name.");

	obs_hotkey_trigger_routed_callback(obs_hotkey_get_id(hotkey), true);
	obs_hotkey_trigger_routed_callback(obs_hotkey_get_id(hotkey), false);

	return RequestResult::Success();
}

/**
 * Triggers a hotkey using a sequence of keys.
 *
 * Note: Hotkey functionality in obs-websocket comes as-is, and we do not guarantee support if things are broken. In 9/10 usages of hotkey requests, there exists a better, more reliable method via other requests.
 *
 * @requestField ?keyId                | String  | The OBS key ID to use. See https://github.com/obsproject/obs-studio/blob/master/libobs/obs-hotkeys.h | Not pressed
 * @requestField ?keyModifiers         | Object  | Object containing key modifiers to apply                                                             | Ignored
 * @requestField ?keyModifiers.shift   | Boolean | Press Shift                                                                                          | Not pressed
 * @requestField ?keyModifiers.control | Boolean | Press CTRL                                                                                           | Not pressed
 * @requestField ?keyModifiers.alt     | Boolean | Press ALT                                                                                            | Not pressed
 * @requestField ?keyModifiers.command | Boolean | Press CMD (Mac)                                                                                      | Not pressed
 *
 * @requestType TriggerHotkeyByKeySequence
 * @complexity 4
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::TriggerHotkeyByKeySequence(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::TriggerHotkeyByKeySequenceRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);

	obs_key_combination_t combo = {0};
	if (req.keyId.value().has_value())
		combo.key = obs_key_from_name(req.keyId.value()->value().c_str());

	if (req.keyModifiers.value().has_value()) {
		const auto &mods = *req.keyModifiers.value();
		uint32_t keyModifiers = 0;
		if (mods.shift.value().value())
			keyModifiers |= INTERACT_SHIFT_KEY;
		if (mods.control.value().value())
			keyModifiers |= INTERACT_CONTROL_KEY;
		if (mods.alt.value().value())
			keyModifiers |= INTERACT_ALT_KEY;
		if (mods.command.value().value())
			keyModifiers |= INTERACT_COMMAND_KEY;
		combo.modifiers = keyModifiers;
	}

	if (!combo.modifiers && (combo.key == OBS_KEY_NONE || combo.key >= OBS_KEY_LAST_VALUE))
		return RequestResult::Error(RequestStatus::CannotAct,
					    "Your provided request fields cannot be used to trigger a hotkey.");

	// Apparently things break when you don't start by setting the combo to false
	obs_hotkey_inject_event(combo, false);
	obs_hotkey_inject_event(combo, true);
	obs_hotkey_inject_event(combo, false);

	return RequestResult::Success();
}

/**
 * Sleeps for a time duration or number of frames. Only available in request batches with types `SERIAL_REALTIME` or `SERIAL_FRAME`.
 *
 * @requestField ?sleepMillis | Number | Number of milliseconds to sleep for (if `SERIAL_REALTIME` mode) | >= 0, <= 50000
 * @requestField ?sleepFrames | Number | Number of frames to sleep for (if `SERIAL_FRAME` mode) | >= 0, <= 10000
 *
 * @requestType Sleep
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category general
 * @api requests
 */
RequestResult RequestHandler::Sleep(const Request &request)
{
	if (request.ExecutionType != RequestBatchExecutionType::SerialRealtime &&
	    request.ExecutionType != RequestBatchExecutionType::SerialFrame)
		return RequestResult::Error(RequestStatus::UnsupportedRequestBatchExecutionType);

	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::SleepRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);

	if (request.ExecutionType == RequestBatchExecutionType::SerialRealtime) {
		if (!req.sleepMillis.value().has_value())
			return RequestResult::Error(RequestStatus::MissingRequestField,
						    "Your request is missing the `sleepMillis` field.");
		std::this_thread::sleep_for(std::chrono::milliseconds(req.sleepMillis.value()->value()));
		return RequestResult::Success();
	}

	if (!req.sleepFrames.value().has_value())
		return RequestResult::Error(RequestStatus::MissingRequestField,
					    "Your request is missing the `sleepFrames` field.");
	RequestResult ret = RequestResult::Success();
	ret.SleepFrames = static_cast<size_t>(req.sleepFrames.value()->value());
	return ret;
}
