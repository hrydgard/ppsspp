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

#include <cmath>
#include <limits>
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

	// We have to try float last because we use float bits, so 1.0 != 1.
	if (allowFloat) {
		union {
			uint32_t u;
			float f;
		} bits;
		if (TryParse(str, &bits.f)) {
			*out = bits.u;
			return true;
		}

		if (!strcasecmp(str, "nan")) {
			*out = 0x7FC00000;
			return true;
		} else if (!strcasecmp(str, "infinity") || !strcasecmp(str, "inf")) {
			*out = 0x7F800000;
			return true;
		} else if (!strcasecmp(str, "-infinity") || !strcasecmp(str, "-inf")) {
			*out = 0xFF800000;
			return true;
		}
	}

	return false;
}

bool DebuggerRequest::ParamU32(const char *name, uint32_t *out) {
	const JsonNode *node = data.get(name);
	if (!node) {
		Fail(StringFromFormat("Missing '%s' parameter", name));
		return false;
	}

	if (node->value.getTag() == JSON_NUMBER) {
		double val = node->value.toNumber();
		bool isInteger = trunc(val) == val;
		if (!isInteger) {
			Fail(StringFromFormat("Could not parse '%s' parameter: integer required", name));
			return false;
		}

		if (val < 0 && val >= std::numeric_limits<int32_t>::min()) {
			// Convert to unsigned representation.
			*out = (uint32_t)(int32_t)val;
			return true;
		} else if (val >= 0 && val <= std::numeric_limits<uint32_t>::max()) {
			*out = (uint32_t)val;
			return true;
		}

		Fail(StringFromFormat("Could not parse '%s' parameter: outside 32 bit range", name));
		return false;
	}
	if (node->value.getTag() != JSON_STRING) {
		Fail(StringFromFormat("Invalid '%s' parameter type", name));
		return false;
	}

	if (U32FromString(node->value.toString(), out, false))
		return true;

	Fail(StringFromFormat("Could not parse '%s' parameter: integer required", name));
	return false;
}

bool DebuggerRequest::ParamU32OrFloatBits(const char *name, uint32_t *out) {
	const JsonNode *node = data.get(name);
	if (!node) {
		Fail(StringFromFormat("Missing '%s' parameter", name));
		return false;
	}

	if (node->value.getTag() == JSON_NUMBER) {
		double val = node->value.toNumber();
		bool isInteger = trunc(val) == val;

		// JSON doesn't give a great way to differentiate ints and floats.
		// Let's play it safe and require a string.
		if (!isInteger) {
			Fail(StringFromFormat("Could not parse '%s' parameter: use a string for non integer values", name));
			return false;
		}

		if (val < 0 && val >= std::numeric_limits<int32_t>::min()) {
			// Convert to unsigned representation.
			*out = (uint32_t)(int32_t)val;
			return true;
		} else if (val >= 0 && val <= std::numeric_limits<uint32_t>::max()) {
			*out = (uint32_t)val;
			return true;
		}

		Fail(StringFromFormat("Could not parse '%s' parameter: outside 32 bit range (use string for float)", name));
		return false;
	}
	if (node->value.getTag() != JSON_STRING) {
		Fail(StringFromFormat("Invalid '%s' parameter type", name));
		return false;
	}

	if (U32FromString(node->value.toString(), out, true))
		return true;

	Fail(StringFromFormat("Could not parse '%s' parameter: number expected", name));
	return false;
}
