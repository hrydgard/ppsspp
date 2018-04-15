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

#include "Common/StringUtils.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

JsonWriter &DebuggerRequest::Respond() {
	writer_.begin();
	writer_.writeString("event", name);
	DebuggerJsonAddTicket(writer_, data);

	responseBegun_ = true;
	return writer_;
}

void DebuggerRequest::Finish() {
	if (responseBegun_ && !responseSent_) {
		writer_.end();
		ws->Send(writer_.str());
		responseBegun_ = false;
		responseSent_ = true;
	}
}

static bool U32FromString(const char *str, uint32_t *out, bool allowFloat) {
	if (TryParse(str, out))
		return true;

	// Now let's try signed (the above parses only positive.)
	if (str[0] == '-' && TryParse(&str[1], out)) {
		*out = static_cast<uint32_t>(-static_cast<int>(*out));
		return true;
	}

	// We have to try float last because we use float bits.
	union {
		uint32_t u;
		float f;
	} bits;
	if (allowFloat && TryParse(str, &bits.f)) {
		*out = bits.u;
		return true;
	}

	return false;
}

bool DebuggerRequest::ParamU32(const char *name, uint32_t *out) {
	const JsonNode *node = data.get(name);
	if (!node) {
		Fail(StringFromFormat("Missing '%s' parameter", name));
		return false;
	}

	// TODO: For now, only supporting strings.  Switch to gason?
	// Otherwise we get overflow (signed integer parsing.)
	if (node->value.getTag() != JSON_STRING) {
		Fail(StringFromFormat("Invalid '%s' parameter type", name));
		return false;
	}

	if (U32FromString(node->value.toString(), out, false))
		return true;

	Fail(StringFromFormat("Could not parse '%s' parameter", name));
	return false;
}

bool DebuggerRequest::ParamU32OrFloatBits(const char *name, uint32_t *out) {
	const JsonNode *node = data.get(name);
	if (!node) {
		Fail(StringFromFormat("Missing '%s' parameter", name));
		return false;
	}

	// TODO: For now, only supporting strings and floats.  Switch to gason?
	// Otherwise we get overflow (signed integer parsing.)
	if (node->value.getTag() == JSON_NUMBER) {
		Fail(StringFromFormat("Could not parse '%s' parameter: outside 32 bit range (use string for float)", name));
		return false;
	}
	if (node->value.getTag() != JSON_STRING) {
		Fail(StringFromFormat("Invalid '%s' parameter type", name));
		return false;
	}

	if (U32FromString(node->value.toString(), out, true))
		return true;

	Fail(StringFromFormat("Could not parse '%s' parameter", name));
	return false;
}
