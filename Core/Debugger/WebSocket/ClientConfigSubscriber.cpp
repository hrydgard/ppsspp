// Copyright (c) 2018- PPSSPP Project.

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
//  - allowed: object with boolean fields:
//     - logger: whether logger events are allowed
//     - game: whether game events are allowed
//     - stepping: whether stepping events are allowed
//     - input: whether input events are allowed
void WebSocketBroadcastConfigGet(DebuggerRequest & req) {
	JsonWriter &json = req.Respond();
	const auto& allowed_config = req.client->allowed;

	json.pushDict("allowed");

	json.writeBool("logger", allowed_config.at("logger"));
	json.writeBool("game", allowed_config.at("game"));
	json.writeBool("stepping", allowed_config.at("stepping"));
	json.writeBool("input", allowed_config.at("input"));

	json.end();
}

// Update the current client broadcast configuration (broadcast.config.set)
//
// Parameters:
//  - allowed: object with boolean fields (all of them are optional):
//     - logger: new logger config state
//     - game: new game config state
//     - stepping: new stepping config state
//     - input: new input config state
//
// Response (same event name):
//  - allowed: object with boolean fields:
//     - logger: whether logger events are now allowed
//     - game: whether game events are now allowed
//     - stepping: whether stepping events are bow allowed
//     - input: whether input events are now allowed
void WebSocketBroadcastConfigSet(DebuggerRequest & req) {
	JsonWriter &json = req.Respond();
	// WebSocketClientInfo& client = req.client;
	auto& allowed_config = req.client->allowed;

	const JsonNode *jsonAllowed = req.data.get("allowed");
	if (!jsonAllowed) {
		return req.Fail("Missing 'allowed' parameter");
	}
	if (jsonAllowed->value.getTag() != JSON_OBJECT) {
		return req.Fail("Invalid 'allowed' parameter type");
	}

	for (const JsonNode *broadcaster : jsonAllowed->value) {
		auto it = allowed_config.find(broadcaster->key);
		if (it == allowed_config.end()) {
			return req.Fail(StringFromFormat("Unsupported 'allowed' object key '%s'", broadcaster->key));
		}

		if (broadcaster->value.getTag() == JSON_TRUE) {
			it->second = true;
		}
		else if (broadcaster->value.getTag() == JSON_FALSE) {
			it->second = false;
		}
		else if (broadcaster->value.getTag() != JSON_NULL) {
			return req.Fail(StringFromFormat("Unsupported 'allowed' object type for key '%s'", broadcaster->key));
		}
	}

	json.pushDict("allowed");

	json.writeBool("logger", allowed_config.at("logger"));
	json.writeBool("game", allowed_config.at("game"));
	json.writeBool("stepping", allowed_config.at("stepping"));
	json.writeBool("input", allowed_config.at("input"));

	json.end();
}
