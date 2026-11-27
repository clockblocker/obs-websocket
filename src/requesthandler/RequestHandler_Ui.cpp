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

#include <QGuiApplication>
#include <QScreen>
#include <QRect>

#include "RequestHandler.h"
#include "rpc/TypedRequest.h"

/**
 * Gets whether studio is enabled.
 *
 * @responseField studioModeEnabled | Boolean | Whether studio mode is enabled
 *
 * @requestType GetStudioModeEnabled
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::GetStudioModeEnabled(const Request &)
{
	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::GetStudioModeEnabledResponse{
		.studioModeEnabled = obs_frontend_preview_program_mode_active(),
	});
}

/**
 * Enables or disables studio mode
 *
 * @requestField studioModeEnabled | Boolean | True == Enabled, False == Disabled
 *
 * @requestType SetStudioModeEnabled
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::SetStudioModeEnabled(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::SetStudioModeEnabledRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);

	bool studioModeEnabled = std::get<0>(parsed).studioModeEnabled.value();
	// Avoid queueing tasks if nothing will change
	if (obs_frontend_preview_program_mode_active() != studioModeEnabled) {
		// Queue the task inside of the UI thread to prevent race conditions (`wait` = true keeps stack ref valid)
		obs_queue_task(
			OBS_TASK_UI,
			[](void *param) {
				auto enabled = (bool *)param;
				obs_frontend_set_preview_program_mode(*enabled);
			},
			&studioModeEnabled, true);
	}

	return RequestResult::Success();
}

/**
 * Opens the properties dialog of an input.
 *
 * @requestField ?inputName | String | Name of the input to open the dialog of
 * @requestField ?inputUuid | String | UUID of the input to open the dialog of
 *
 * @requestType OpenInputPropertiesDialog
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::OpenInputPropertiesDialog(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.AcquireInput(statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	obs_frontend_open_source_properties(input);

	return RequestResult::Success();
}

/**
 * Opens the filters dialog of an input.
 *
 * @requestField ?inputName | String | Name of the input to open the dialog of
 * @requestField ?inputUuid | String | UUID of the input to open the dialog of
 *
 * @requestType OpenInputFiltersDialog
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::OpenInputFiltersDialog(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.AcquireInput(statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	obs_frontend_open_source_filters(input);

	return RequestResult::Success();
}

/**
 * Opens the interact dialog of an input.
 *
 * @requestField ?inputName | String | Name of the input to open the dialog of
 * @requestField ?inputUuid | String | UUID of the input to open the dialog of
 *
 * @requestType OpenInputInteractDialog
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::OpenInputInteractDialog(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.AcquireInput(statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	if (!(obs_source_get_output_flags(input) & OBS_SOURCE_INTERACTION))
		return RequestResult::Error(RequestStatus::InvalidResourceState,
					    "The specified input does not support interaction.");

	obs_frontend_open_source_interaction(input);

	return RequestResult::Success();
}

/**
 * Gets a list of connected monitors and information about them.
 *
 * @responseField monitors | Array<Object> | a list of detected monitors with some information
 *
 * @requestType GetMonitorList
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::GetMonitorList(const Request &)
{
	std::vector<ObsWebSocket::Protocol::MonitorInfo> monitors;
	QList<QScreen *> screensList = QGuiApplication::screens();
	monitors.reserve(static_cast<size_t>(screensList.size()));
	for (int screenIndex = 0; screenIndex < screensList.size(); screenIndex++) {
		QScreen const *screen = screensList[screenIndex];
		const QRect screenGeometry = screen->geometry();
		monitors.push_back(ObsWebSocket::Protocol::MonitorInfo{
			.monitorName = screen->name().toStdString() + '(' + std::to_string(screenIndex) + ')',
			.monitorIndex = screenIndex,
			.monitorWidth = screenGeometry.width(),
			.monitorHeight = screenGeometry.height(),
			.monitorPositionX = screenGeometry.x(),
			.monitorPositionY = screenGeometry.y(),
		});
	}
	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::GetMonitorListResponse{
		.monitors = std::move(monitors),
	});
}

/**
 * Opens a projector for a specific output video mix.
 *
 * Mix types:
 *
 * - `OBS_WEBSOCKET_VIDEO_MIX_TYPE_PREVIEW`
 * - `OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM`
 * - `OBS_WEBSOCKET_VIDEO_MIX_TYPE_MULTIVIEW`
 *
 * Note: This request serves to provide feature parity with 4.x. It is very likely to be changed/deprecated in a future release.
 *
 * @requestField videoMixType            | String | Type of mix to open
 * @requestField ?monitorIndex      | Number | Monitor index, use `GetMonitorList` to obtain index | None | -1: Opens projector in windowed mode
 * @requestField ?projectorGeometry | String | Size/Position data for a windowed projector, in Qt Base64 encoded format. Mutually exclusive with `monitorIndex` | N/A
 *
 * @requestType OpenVideoMixProjector
 * @complexity 3
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::OpenVideoMixProjector(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::OpenVideoMixProjectorRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);

	const std::string_view videoMixType = req.videoMixType.value().str();
	const char *projectorType = "Preview";
	if (videoMixType == "OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM")
		projectorType = "StudioProgram";
	else if (videoMixType == "OBS_WEBSOCKET_VIDEO_MIX_TYPE_MULTIVIEW")
		projectorType = "Multiview";

	const int monitorIndex = req.monitorIndex.value().value().value();
	std::string projectorGeometry;
	if (req.projectorGeometry.value().has_value()) {
		if (monitorIndex != -1)
			return RequestResult::Error(RequestStatus::TooManyRequestFields,
						    "`monitorIndex` and `projectorGeometry` are mutually exclusive.");
		projectorGeometry = req.projectorGeometry.value()->value();
	}

	obs_frontend_open_projector(projectorType, monitorIndex, projectorGeometry.c_str(), nullptr);

	return RequestResult::Success();
}

/**
 * Opens a projector for a source.
 *
 * Note: This request serves to provide feature parity with 4.x. It is very likely to be changed/deprecated in a future release.
 *
 * @requestField ?canvasUuid        | String | UUID of the canvas the source is in, if using the sourceName field
 * @requestField ?sourceName        | String | Name of the source to open a projector for
 * @requestField ?sourceUuid        | String | UUID of the source to open a projector for
 * @requestField ?monitorIndex      | Number | Monitor index, use `GetMonitorList` to obtain index | None | -1: Opens projector in windowed mode
 * @requestField ?projectorGeometry | String | Size/Position data for a windowed projector, in Qt Base64 encoded format. Mutually exclusive with `monitorIndex` | N/A
 *
 * @requestType OpenSourceProjector
 * @complexity 3
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category ui
 * @api requests
 */
RequestResult RequestHandler::OpenSourceProjector(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease source = request.AcquireSource("canvasUuid", "sourceName", "sourceUuid", statusCode, comment);
	if (!source)
		return RequestResult::Error(statusCode, comment);

	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::OpenSourceProjectorRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);
	const auto &req = std::get<0>(parsed);

	const int monitorIndex = req.monitorIndex.value().value().value();
	std::string projectorGeometry;
	if (req.projectorGeometry.value().has_value()) {
		if (monitorIndex != -1)
			return RequestResult::Error(RequestStatus::TooManyRequestFields,
						    "`monitorIndex` and `projectorGeometry` are mutually exclusive.");
		projectorGeometry = req.projectorGeometry.value()->value();
	}

	obs_frontend_open_projector("Source", monitorIndex, projectorGeometry.c_str(), obs_source_get_name(source));

	return RequestResult::Success();
}
