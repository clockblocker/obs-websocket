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

#include <utility>
#include <variant>

#include "Request.h"
#include "RequestResult.h"
#include "../../protocol/RequestCodec.h"

namespace ObsWebSocket {

/// Parses and validates a Request into typed struct T, returning either T or a
/// ready-to-return RequestResult::Error with the mapped obs-websocket RequestStatus.
template <class T, class... Ps>
inline std::variant<T, RequestResult> ParseRequest(const Request &request)
{
	auto parsed = Protocol::ParseRequestData<T, Ps...>(request.HasRequestData, request.RequestData);
	if (auto *err = std::get_if<Protocol::MappedRequestError>(&parsed))
		return RequestResult::Error(err->statusCode, err->comment);
	return std::move(std::get<T>(parsed));
}

/// Serializes a typed response struct directly into RequestResult::Success via <rfl/nlohmann.hpp>.
template <class T, class... Ps>
inline RequestResult MakeResponse(const T &response)
{
	return RequestResult::Success(Protocol::EncodeJson<T, Ps...>(response));
}

} // namespace ObsWebSocket
