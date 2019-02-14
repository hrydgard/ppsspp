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

#pragma once

#include <cassert>
#include <string>
#include "json/json_reader.h"
#include "json/json_writer.h"
#include "net/websocket_server.h"
#include "Common/Log.h"

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(UWP)
// Enum name overlapped with UWP macro, quick hack to disable it
#undef OPTIONAL
#endif

using namespace json;

static inline void DebuggerJsonAddTicket(JsonWriter &writer, const JsonGet &data) {
	const JsonNode *value = data.get("ticket");
	if (value)
		writer.writeRaw("ticket", json_stringify(value));
}

struct DebuggerErrorEvent {
	DebuggerErrorEvent(const std::string m, LogTypes::LOG_LEVELS l, const JsonGet data = JsonValue(JSON_NULL))
		: message(m), level(l) {
		// Need to format right away, before it's out of scope.
		if (data) {
			const JsonNode *value = data.get("ticket");
			if (value)
				ticketRaw = json_stringify(value);
		}
	}

	std::string message;
	LogTypes::LOG_LEVELS level;
	std::string ticketRaw;

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "error");
		j.writeString("message", message);
		j.writeInt("level", level);
		if (!ticketRaw.empty()) {
			j.writeRaw("ticket", ticketRaw);
		}
		j.end();
		return j.str();
	}
};

enum class DebuggerParamType {
	REQUIRED,
	OPTIONAL,
	REQUIRED_LOOSE,
	OPTIONAL_LOOSE,
};

struct DebuggerRequest {
	DebuggerRequest(const char *n, net::WebSocketServer *w, const JsonGet &d)
		: name(n), ws(w), data(d) {
	}

	const char *name;
	net::WebSocketServer *ws;
	const JsonGet data;

	void Fail(const std::string &message) {
		ws->Send(DebuggerErrorEvent(message, LogTypes::LERROR, data));
		responseSent_ = true;
	}

	bool HasParam(const char *name, bool ignoreNull = false);
	bool ParamU32(const char *name, uint32_t *out, bool allowFloatBits = false, DebuggerParamType type = DebuggerParamType::REQUIRED);
	bool ParamBool(const char *name, bool *out, DebuggerParamType type = DebuggerParamType::REQUIRED);
	bool ParamString(const char *name, std::string *out, DebuggerParamType type = DebuggerParamType::REQUIRED);

	JsonWriter &Respond();
	void Flush();
	bool Finish();

private:
	JsonWriter writer_;
	bool responseBegun_ = false;
	bool responseSent_ = false;
	bool responsePartial_ = false;
};

class DebuggerSubscriber {
public:
	virtual ~DebuggerSubscriber() {}

	// Subscribers can also broadcast if they have simple cases to.
	virtual void Broadcast(net::WebSocketServer *ws) {}
};

typedef std::function<void(DebuggerRequest &req)> DebuggerEventHandler;
typedef std::unordered_map<std::string, DebuggerEventHandler> DebuggerEventHandlerMap;
