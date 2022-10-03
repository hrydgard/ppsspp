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

#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"

inline void DebuggerJsonAddTicket(JsonWriter &writer, const JsonGet &data) {
	const JsonNode *value = data.get("ticket");
	if (value)
		writer.writeRaw("ticket", json_stringify(value));
}

JsonWriter &DebuggerRequest::Respond() {
	writer_.begin();
	writer_.writeString("event", name);
	DebuggerJsonAddTicket(writer_, data);

	responseBegun_ = true;
	return writer_;
}

bool DebuggerRequest::Finish() {
	if (responseBegun_ && !responseSent_) {
		writer_.end();
		if (responsePartial_)
			ws->AddFragment(true, writer_.str());
		else
			ws->Send(writer_.str());
		responseBegun_ = false;
		responseSent_ = true;
		responsePartial_ = false;
	}

	return responseSent_;
}

void DebuggerRequest::Flush() {
	ws->AddFragment(false, writer_.flush());
	responsePartial_ = true;
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

bool DebuggerRequest::HasParam(const char *name, bool ignoreNull) {
	const JsonNode *node = data.get(name);
	if (!node) {
		return false;
	}
	if (node->value.getTag() == JSON_NULL) {
		return !ignoreNull;
	}
	return true;
}

bool DebuggerRequest::ParamU32(const char *name, uint32_t *out, bool allowFloatBits, DebuggerParamType type) {
	bool allowLoose = type == DebuggerParamType::REQUIRED_LOOSE || type == DebuggerParamType::OPTIONAL_LOOSE;
	bool required = type == DebuggerParamType::REQUIRED || type == DebuggerParamType::REQUIRED_LOOSE;

	const JsonNode *node = data.get(name);
	if (!node) {
		if (required)
			Fail(StringFromFormat("Missing '%s' parameter", name));
		return !required;
	}

	auto tag = node->value.getTag();
	if (tag == JSON_NUMBER) {
		double val = node->value.toNumber();
		bool isInteger = trunc(val) == val;
		if (!isInteger && !allowLoose) {
			// JSON doesn't give a great way to differentiate ints and floats.
			// Let's play it safe and require a string.
			if (allowFloatBits)
				Fail(StringFromFormat("Could not parse '%s' parameter: use a string for non integer values", name));
			else
				Fail(StringFromFormat("Could not parse '%s' parameter: integer required", name));
			return false;
		} else if (!isInteger && allowFloatBits) {
			union {
				float f;
				uint32_t u;
			} bits = { (float)val };
			*out = bits.u;
			return true;
		}

		if (val < 0 && val >= std::numeric_limits<int32_t>::min()) {
			// Convert to unsigned representation.
			*out = (uint32_t)(int32_t)val;
			return true;
		} else if (val >= 0 && val <= std::numeric_limits<uint32_t>::max()) {
			*out = (uint32_t)val;
			return true;
		} else if (allowLoose) {
			*out = val >= 0 ? std::numeric_limits<uint32_t>::max() : std::numeric_limits<uint32_t>::min();
			return true;
		}

		if (allowFloatBits)
			Fail(StringFromFormat("Could not parse '%s' parameter: outside 32 bit range (use string for float)", name));
		else
			Fail(StringFromFormat("Could not parse '%s' parameter: outside 32 bit range", name));
		return false;
	}
	if (tag != JSON_STRING) {
		if (type == DebuggerParamType::REQUIRED || tag != JSON_NULL) {
			Fail(StringFromFormat("Invalid '%s' parameter type", name));
			return false;
		}
		return true;
	}

	if (U32FromString(node->value.toString(), out, allowFloatBits))
		return true;

	if (allowFloatBits)
		Fail(StringFromFormat("Could not parse '%s' parameter: number expected", name));
	else
		Fail(StringFromFormat("Could not parse '%s' parameter: integer required", name));
	return false;
}

bool DebuggerRequest::ParamBool(const char *name, bool *out, DebuggerParamType type) {
	bool allowLoose = type == DebuggerParamType::REQUIRED_LOOSE || type == DebuggerParamType::OPTIONAL_LOOSE;
	bool required = type == DebuggerParamType::REQUIRED || type == DebuggerParamType::REQUIRED_LOOSE;

	const JsonNode *node = data.get(name);
	if (!node) {
		if (required)
			Fail(StringFromFormat("Missing '%s' parameter", name));
		return !required;
	}

	auto tag = node->value.getTag();
	if (tag == JSON_NUMBER) {
		double val = node->value.toNumber();
		if (val == 1.0 || val == 0.0 || allowLoose) {
			*out = val != 0.0;
			return true;
		}

		Fail(StringFromFormat("Could not parse '%s' parameter: should be true/1 or false/0", name));
		return false;
	}
	if (tag == JSON_TRUE) {
		*out = true;
		return true;
	}
	if (tag == JSON_FALSE) {
		*out = false;
		return true;
	}
	if (tag != JSON_STRING) {
		if (type == DebuggerParamType::REQUIRED || tag != JSON_NULL) {
			Fail(StringFromFormat("Invalid '%s' parameter type", name));
			return false;
		}
		return true;
	}

	const std::string s = node->value.toString();
	if (s == "1" || s == "true") {
		*out = true;
		return true;
	}
	if (s == "0" || s == "false" || (s.empty() && allowLoose)) {
		*out = false;
		return true;
	}

	if (allowLoose) {
		*out = true;
		return true;
	}

	Fail(StringFromFormat("Could not parse '%s' parameter: boolean required", name));
	return false;
}

bool DebuggerRequest::ParamString(const char *name, std::string *out, DebuggerParamType type) {
	bool allowLoose = type == DebuggerParamType::REQUIRED_LOOSE || type == DebuggerParamType::OPTIONAL_LOOSE;
	bool required = type == DebuggerParamType::REQUIRED || type == DebuggerParamType::REQUIRED_LOOSE;

	const JsonNode *node = data.get(name);
	if (!node) {
		if (required)
			Fail(StringFromFormat("Missing '%s' parameter", name));
		return !required;
	}

	auto tag = node->value.getTag();
	if (tag == JSON_STRING) {
		*out = node->value.toString();
		return true;
	} else if (!allowLoose) {
		if (required || tag != JSON_NULL) {
			Fail(StringFromFormat("Invalid '%s' parameter type", name));
			return false;
		}
		return true;
	}

	// For loose, let's allow a few things.
	if (tag == JSON_TRUE) {
		*out = "true";
		return true;
	} else if (tag == JSON_FALSE) {
		*out = "false";
		return true;
	} else if (tag == JSON_NULL) {
		if (required) {
			out->clear();
		}
		return true;
	} else if (tag == JSON_NUMBER) {
		// Will have a decimal place, though.
		*out = StringFromFormat("%f", node->value.toNumber());
		return true;
	}

	Fail(StringFromFormat("Invalid '%s' parameter type", name));
	return false;
}

uint32_t RoundMemAddressUp(uint32_t addr) {
	if (addr < PSP_GetScratchpadMemoryBase())
		return PSP_GetScratchpadMemoryBase();
	else if (addr >= PSP_GetScratchpadMemoryEnd() && addr < PSP_GetVidMemBase())
		return PSP_GetVidMemBase();
	else if (addr >= PSP_GetVidMemEnd() && addr < PSP_GetKernelMemoryBase())
		return PSP_GetKernelMemoryBase();
	else if (addr >= PSP_GetUserMemoryEnd())
		return PSP_GetScratchpadMemoryBase();
	return addr;
}
