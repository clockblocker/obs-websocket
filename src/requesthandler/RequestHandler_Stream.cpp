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

#include "RequestHandler.h"
#include "rpc/TypedRequest.h"

/**
 * Gets the status of the stream output.
 *
 * @responseField outputActive        | Boolean | Whether the output is active
 * @responseField outputReconnecting  | Boolean | Whether the output is currently reconnecting
 * @responseField outputTimecode      | String  | Current formatted timecode string for the output
 * @responseField outputDuration      | Number  | Current duration in milliseconds for the output
 * @responseField outputCongestion    | Number  | Congestion of the output
 * @responseField outputBytes         | Number  | Number of bytes sent by the output
 * @responseField outputSkippedFrames | Number  | Number of frames skipped by the output's process
 * @responseField outputTotalFrames   | Number  | Total number of frames delivered by the output's process
 *
 * @requestType GetStreamStatus
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::GetStreamStatus(const Request &)
{
	OBSOutputAutoRelease streamOutput = obs_frontend_get_streaming_output();

	uint64_t outputDuration = Utils::Obs::NumberHelper::GetOutputDuration(streamOutput);

	float outputCongestion = obs_output_get_congestion(streamOutput);
	if (std::isnan(outputCongestion)) // libobs does not handle NaN, so we're handling it here
		outputCongestion = 0.0f;

	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::GetStreamStatusResponse{
		.outputActive = obs_output_active(streamOutput),
		.outputReconnecting = obs_output_reconnecting(streamOutput),
		.outputTimecode = Utils::Obs::StringHelper::DurationToTimecode(outputDuration),
		.outputDuration = outputDuration,
		.outputCongestion = outputCongestion,
		.outputBytes = static_cast<uint64_t>(obs_output_get_total_bytes(streamOutput)),
		.outputSkippedFrames = obs_output_get_frames_dropped(streamOutput),
		.outputTotalFrames = obs_output_get_total_frames(streamOutput),
	});
}

/**
 * Toggles the status of the stream output.
 *
 * @responseField outputActive | Boolean | New state of the stream output
 *
 * @requestType ToggleStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::ToggleStream(const Request &)
{
	const bool nowActive = !obs_frontend_streaming_active();
	if (nowActive)
		obs_frontend_streaming_start();
	else
		obs_frontend_streaming_stop();

	return ObsWebSocket::MakeResponse(ObsWebSocket::Protocol::ToggleStreamResponse{.outputActive = nowActive});
}

/**
 * Starts the stream output.
 *
 * @requestType StartStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::StartStream(const Request &)
{
	if (obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputRunning);

	// TODO: Call signal directly to perform blocking wait
	obs_frontend_streaming_start();

	return RequestResult::Success();
}

/**
 * Stops the stream output.
 *
 * @requestType StopStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::StopStream(const Request &)
{
	if (!obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputNotRunning);

	// TODO: Call signal directly to perform blocking wait
	obs_frontend_streaming_stop();

	return RequestResult::Success();
}

/**
 * Sends CEA-608 caption text over the stream output.
 *
 * @requestField captionText | String | Caption text
 *
 * @requestType SendStreamCaption
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category stream
 * @api requests
 */
RequestResult RequestHandler::SendStreamCaption(const Request &request)
{
	auto parsed = ObsWebSocket::ParseRequest<ObsWebSocket::Protocol::SendStreamCaptionRequest>(request);
	if (auto *err = std::get_if<RequestResult>(&parsed))
		return std::move(*err);

	if (!obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputNotRunning);

	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();

	// 0.0 means no delay until the next caption can be sent
	obs_output_output_caption_text2(output, std::get<0>(parsed).captionText.value().c_str(), 0.0);

	return RequestResult::Success();
}
