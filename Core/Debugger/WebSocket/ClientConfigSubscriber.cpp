// Copyright (c) 2023- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Core/Debugger/WebSocket/ClientConfigSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Common/StringUtils.h"

DebuggerSubscriber *WebSocketClientConfigInit(DebuggerEventHandlerMap & map) {
	map["broadcast.config.get"] = &WebSocketBroadcastConfigGet;
	map["broadcast.config.set"] = &WebSocketBroadcastConfigSet;

	return nullptr;
}


// Request the current client broadcast configuration (broadcast.config.get)
//
// No parameters.
//
// Response (same event name):
//  - disallowed: object with optional boolean fields:
//     - logger: whether logger events are disallowed
//     - game: whether game events are disallowed
//     - stepping: whether stepping events are disallowed
//     - input: whether input events are disallowed
void WebSocketBroadcastConfigGet(DebuggerRequest & req) {
	JsonWriter &json = req.Respond();
	const auto& disallowed_config = req.client->disallowed;

	json.pushDict("disallowed");

	for (const auto[name, status] : disallowed_config) {
		if (status)
			json.writeBool(name, true);
	}

	json.end();
}

// Update the current client broadcast configuration (broadcast.config.set)
//
// Parameters:
//  - disallowed: object with boolean fields (all of them are optional):
//     - logger: new logger config state
//     - game: new game config state
//     - stepping: new stepping config state
//     - input: new input config state
//
// Response (same event name):
//  - disallowed: object with optional boolean fields:
//     - logger: whether logger events are now disallowed
//     - game: whether game events are now disallowed
//     - stepping: whether stepping events are now disallowed
//     - input: whether input events are now disallowed
void WebSocketBroadcastConfigSet(DebuggerRequest & req) {
	JsonWriter &json = req.Respond();
	auto& disallowed_config = req.client->disallowed;

	const JsonNode *jsonDisallowed = req.data.get("disallowed");
	if (!jsonDisallowed) {
		return req.Fail("Missing 'disallowed' parameter");
	}
	if (jsonDisallowed->value.getTag() != JSON_OBJECT) {
		return req.Fail("Invalid 'disallowed' parameter type");
	}

	for (const JsonNode *broadcaster : jsonDisallowed->value) {
		auto it = disallowed_config.find(broadcaster->key);
		if (it == disallowed_config.end()) {
			return req.Fail(StringFromFormat("Unsupported 'disallowed' object key '%s'", broadcaster->key));
		}

		if (broadcaster->value.getTag() == JSON_TRUE) {
			it->second = true;
		}
		else if (broadcaster->value.getTag() == JSON_FALSE) {
			it->second = false;
		}
		else if (broadcaster->value.getTag() != JSON_NULL) {
			return req.Fail(StringFromFormat("Unsupported 'disallowed' object type for key '%s'", broadcaster->key));
		}
	}

	json.pushDict("disallowed");

	for (const auto[name, status] : disallowed_config) {
		if (status)
			json.writeBool(name, true);
	}

	json.end();
}
