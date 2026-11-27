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
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/error_format.hpp>
#include <rfl/json.hpp>
#include <rfl/nlohmann.hpp>

#include "../requesthandler/types/RequestStatus.h"
#include "../websocketserver/types/WebSocketCloseCode.h"
#include "ProtocolTypes.h"

namespace ObsWebSocket::Protocol {

struct MappedRequestError {
	RequestStatus::RequestStatus statusCode;
	std::string comment;
};

struct MappedCloseError {
	WebSocketCloseCode::WebSocketCloseCode closeCode;
	std::string closeReason;
};

/// Maps a structured rfl::Error (via ErrorKind + detail path) to obs-websocket's
/// exact RequestStatus codes:
///   - missing_field        -> RequestStatus::MissingRequestField (300)
///   - invalid_type         -> RequestStatus::InvalidRequestFieldType (401)
///   - constraint_violation -> RequestStatus::RequestFieldEmpty (403) for Size<Minimum<1>>
///                             or RequestStatus::RequestFieldOutOfRange (402) for numeric bounds
///   - invalid_value        -> RequestStatus::InvalidRequestField (400) (e.g. bad Literal/enum)
///                             or RequestStatus::RequestFieldOutOfRange (402) on integer overflow
///
/// Note: rfl::json::Reader populates ErrorKind::invalid_type / invalid_value directly.
/// rfl::nlohmann::Reader currently leaves type-cast errors as ErrorKind::other, so a
/// narrow fallback classifies those until rfl::nlohmann::Reader tags ErrorKind natively.
inline MappedRequestError MapErrorToRequestStatus(const rfl::Error &err)
{
	const auto details = err.details();
	const rfl::ErrorDetail *primary = details.empty() ? nullptr : &details.front();
	const rfl::ErrorKind kind = primary ? primary->kind : err.kind();
	const std::string fieldPath = primary ? rfl::format_path(primary->path) : std::string{};
	const std::string &msg = primary ? primary->message : err.what();

	auto makeComment = [&](const std::string &fallback) -> std::string {
		if (!fieldPath.empty())
			return std::string("Field `") + fieldPath + "`: " + msg;
		return msg.empty() ? fallback : msg;
	};

	switch (kind) {
	case rfl::ErrorKind::missing_field:
		return {RequestStatus::MissingRequestField,
			fieldPath.empty() ? makeComment("A required request field is missing.")
					  : std::string("Your request is missing the `") + fieldPath + "` field."};

	case rfl::ErrorKind::invalid_type:
		return {RequestStatus::InvalidRequestFieldType,
			makeComment("A request field has the wrong data type.")};

	case rfl::ErrorKind::constraint_violation:
		if (msg.find("Size validation failed") != std::string::npos) {
			return {RequestStatus::RequestFieldEmpty,
				fieldPath.empty() ? makeComment("A request field must not be empty.")
						  : std::string("The field value of `") + fieldPath +
							    "` must not be empty."};
		}
		return {RequestStatus::RequestFieldOutOfRange,
			makeComment("A request field is outside of the allowed range.")};

	case rfl::ErrorKind::invalid_value:
		if (msg.find("out of range") != std::string::npos ||
		    msg.find("negative integer") != std::string::npos) {
			return {RequestStatus::RequestFieldOutOfRange,
				makeComment("A request field is outside of the allowed range.")};
		}
		return {RequestStatus::InvalidRequestField,
			makeComment("A request field has an invalid value.")};

	case rfl::ErrorKind::invalid_length:
		return {RequestStatus::InvalidRequestField,
			makeComment("A request field has an invalid length.")};

	case rfl::ErrorKind::unknown_field:
	case rfl::ErrorKind::duplicate_field:
		return {RequestStatus::InvalidRequestField, makeComment("Invalid request field.")};

	case rfl::ErrorKind::syntax_error:
		return {RequestStatus::MissingRequestData, makeComment("Malformed request payload.")};

	case rfl::ErrorKind::other:
	default:
		// Fallback for rfl::nlohmann::Reader type/range errors (currently tagged ErrorKind::other).
		if (msg.find("out of range") != std::string::npos ||
		    msg.find("negative integer") != std::string::npos) {
			return {RequestStatus::RequestFieldOutOfRange,
				makeComment("A request field is outside of the allowed range.")};
		}
		if (msg.find("Could not cast") != std::string::npos) {
			return {RequestStatus::InvalidRequestFieldType,
				makeComment("A request field has the wrong data type.")};
		}
		return {RequestStatus::InvalidRequestField, makeComment("Invalid request field.")};
	}
}

/// Maps a structured rfl::Error during WebSocket v5 frame/opcode decoding to a
/// WebSocketCloseCode.
inline MappedCloseError MapErrorToCloseCode(const rfl::Error &err)
{
	const auto details = err.details();
	const rfl::ErrorDetail *primary = details.empty() ? nullptr : &details.front();
	const rfl::ErrorKind kind = primary ? primary->kind : err.kind();
	const std::string &msg = primary ? primary->message : err.what();

	switch (kind) {
	case rfl::ErrorKind::syntax_error:
		return {WebSocketCloseCode::MessageDecodeError, err.what()};
	case rfl::ErrorKind::missing_field:
		return {WebSocketCloseCode::MissingDataField, err.what()};
	case rfl::ErrorKind::invalid_type:
		return {WebSocketCloseCode::InvalidDataFieldType, err.what()};
	case rfl::ErrorKind::invalid_value:
	case rfl::ErrorKind::constraint_violation:
	case rfl::ErrorKind::invalid_length:
		return {WebSocketCloseCode::InvalidDataFieldValue, err.what()};
	case rfl::ErrorKind::other:
	default:
		if (msg.find("Could not cast") != std::string::npos)
			return {WebSocketCloseCode::InvalidDataFieldType, err.what()};
		return {WebSocketCloseCode::InvalidDataFieldValue, err.what()};
	}
}

/// Decodes a typed protocol struct from a nlohmann::json object.
template <class T, class... Ps>
inline rfl::Result<T> DecodeJson(const nlohmann::json &data)
{
	return rfl::nlohmann::read<T, Ps...>(data);
}

/// Decodes a typed protocol struct directly from UTF-8 JSON wire bytes via yyjson.
template <class T, class... Ps>
inline rfl::Result<T> DecodeWireJson(std::string_view wireJson)
{
	return rfl::json::read<T, Ps...>(wireJson);
}

/// Encodes a typed protocol struct into nlohmann::json.
template <class T, class... Ps>
inline nlohmann::json EncodeJson(const T &value)
{
	return rfl::nlohmann::write<Ps...>(value);
}

/// Encodes a typed protocol struct directly into UTF-8 JSON wire text via yyjson.
template <class T, class... Ps>
inline std::string EncodeWireJson(const T &value)
{
	return rfl::json::write<Ps...>(value);
}

/// Encodes a typed protocol struct into MessagePack bytes matching obs-websocket's
/// `obswebsocket.msgpack` subprotocol (nlohmann::json::to_msgpack).
template <class T, class... Ps>
inline std::vector<std::uint8_t> EncodeMsgPack(const T &value)
{
	return nlohmann::json::to_msgpack(rfl::nlohmann::write<Ps...>(value));
}

/// Decodes a typed protocol struct from MessagePack bytes (`obswebsocket.msgpack`).
template <class T, class... Ps>
inline rfl::Result<T> DecodeMsgPack(const std::vector<std::uint8_t> &bytes)
{
	try {
		const nlohmann::json j = nlohmann::json::from_msgpack(bytes);
		return rfl::nlohmann::read<T, Ps...>(j);
	} catch (const nlohmann::json::parse_error &e) {
		return rfl::error(std::string("Unable to decode MsgPack: ") + e.what(),
				  rfl::ErrorKind::syntax_error);
	}
}

/// Parses and validates a Request's `requestData` into typed struct `T`, preserving
/// obs-websocket's `MissingRequestData` distinction when `hasRequestData` is false
/// and a required field is missing.
template <class T, class... Ps>
inline std::variant<T, MappedRequestError> ParseRequestData(bool hasRequestData,
							    const nlohmann::json &requestData)
{
	const nlohmann::json emptyObj = nlohmann::json::object();
	const nlohmann::json &input = hasRequestData && requestData.is_object() ? requestData : emptyObj;

	auto res = rfl::nlohmann::read<T, Ps...>(input);
	if (!res) {
		auto mapped = MapErrorToRequestStatus(res.error());
		if (!hasRequestData && mapped.statusCode == RequestStatus::MissingRequestField) {
			return MappedRequestError{
				RequestStatus::MissingRequestData,
				"Your request data is missing or invalid (non-object)",
			};
		}
		return mapped;
	}
	return std::move(*res);
}

/// Converts a nlohmann::json object into an rfl::Object<nlohmann::json>.
inline JsonObject ToJsonObject(const nlohmann::json &obj)
{
	JsonObject out;
	if (!obj.is_object())
		return out;
	for (auto it = obj.begin(); it != obj.end(); ++it)
		out[it.key()] = it.value();
	return out;
}

/// Converts an rfl::Object<nlohmann::json> back into a nlohmann::json object.
inline nlohmann::json FromJsonObject(const JsonObject &obj)
{
	nlohmann::json out = nlohmann::json::object();
	for (const auto &[k, v] : obj)
		out[k] = v;
	return out;
}

} // namespace ObsWebSocket::Protocol
