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
	map["client.config.get"] = &WebSocketClientConfigGet;
	map["client.config.set"] = &WebSocketClientConfigSet;

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

	for (const auto &[name, status] : disallowed_config) {
		if (status)
			json.writeBool(name, true);
	}

	json.end();
}

// Request the current per-connection client settings (client.config.get)
//
// No parameters.
//
// Response (same event name):
//  - acknowledgeDeferred: boolean, whether "deferred" events are being sent.
void WebSocketClientConfigGet(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeBool("acknowledgeDeferred", req.client->acknowledgeDeferred);
}

// Update the per-connection client settings (client.config.set)
//
// Parameters (all optional):
//  - acknowledgeDeferred: boolean, whether to send a "deferred" event when a request is accepted
//    but only finishes later (cpu.resume, cpu.stepInto and friends, gpu.stats.feed,
//    input.buttons.press.)  Defaults to false.
//
// Response (same event name):
//  - acknowledgeDeferred: boolean, the setting as it now stands.
//
// Turning this on lets a client tell "accepted, the result comes in a later event" from "dropped
// on the floor", without hardcoding which events those are.  It can't be the default, and can't
// simply reuse the request's own event name, because that's what the later event already uses:
// a client waiting for a bare {"event":"cpu.resume"} would believe the game was running while it
// was still stopped, and input.buttons.press answers with the request's own ticket when the press
// completes, so an early reply under that name would be indistinguishable even with a ticket.
void WebSocketClientConfigSet(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();

	bool acknowledgeDeferred = req.client->acknowledgeDeferred;
	if (!req.ParamBool("acknowledgeDeferred", &acknowledgeDeferred, DebuggerParamType::OPTIONAL))
		return;
	req.client->acknowledgeDeferred = acknowledgeDeferred;

	json.writeBool("acknowledgeDeferred", req.client->acknowledgeDeferred);
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

	for (const auto &[name, status] : disallowed_config) {
		if (status)
			json.writeBool(name, true);
	}

	json.end();
}
