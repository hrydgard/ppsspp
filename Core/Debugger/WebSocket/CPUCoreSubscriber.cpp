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
#include "Core/Core.h"
#include "Core/Debugger/WebSocket/CPUCoreSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"

static std::string RegValueAsFloat(uint32_t u) {
	union {
		uint32_t u;
		float f;
	} bits = { u };
	return StringFromFormat("%f", bits.f);
}

void WebSocketCPUGetAllRegs(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();

	json.pushArray("categories");
	for (int c = 0; c < currentDebugMIPS->GetNumCategories(); ++c) {
		json.pushDict();
		json.writeInt("id", c);
		json.writeString("name", currentDebugMIPS->GetCategoryName(c));

		int total = currentDebugMIPS->GetNumRegsInCategory(c);

		json.pushArray("names");
		for (int r = 0; r < total; ++r)
			json.writeString(currentDebugMIPS->GetRegName(c, r));
		if (c == 0) {
			json.writeString("pc");
			json.writeString("hi");
			json.writeString("lo");
		}
		json.pop();

		json.pushArray("intValues");
		// Writing as floating point to avoid negatives.  Actually double, so safe.
		for (int r = 0; r < total; ++r)
			json.writeFloat(currentDebugMIPS->GetRegValue(c, r));
		if (c == 0) {
			json.writeFloat(currentDebugMIPS->GetPC());
			json.writeFloat(currentDebugMIPS->GetHi());
			json.writeFloat(currentDebugMIPS->GetLo());
		}
		json.pop();

		json.pushArray("floatValues");
		// Note: String so it can have Infinity and NaN.
		for (int r = 0; r < total; ++r)
			json.writeString(RegValueAsFloat(currentDebugMIPS->GetRegValue(c, r)));
		if (c == 0) {
			json.writeString(RegValueAsFloat(currentDebugMIPS->GetPC()));
			json.writeString(RegValueAsFloat(currentDebugMIPS->GetHi()));
			json.writeString(RegValueAsFloat(currentDebugMIPS->GetLo()));
		}
		json.pop();

		json.pop();
	}
	json.pop();
}

enum class DebuggerRegType {
	INVALID,
	NORMAL,
	PC,
	HI,
	LO,
};

static DebuggerRegType ValidateRegName(DebuggerRequest &req, const std::string &name, int *cat, int *reg) {
	if (name == "pc") {
		*cat = 0;
		*reg = 32;
		return DebuggerRegType::PC;
	}
	if (name == "hi") {
		*cat = 0;
		*reg = 33;
		return DebuggerRegType::HI;
	}
	if (name == "lo") {
		*cat = 0;
		*reg = 34;
		return DebuggerRegType::LO;
	}

	for (int c = 0; c < currentDebugMIPS->GetNumCategories(); ++c) {
		int total = currentDebugMIPS->GetNumRegsInCategory(c);
		for (int r = 0; r < total; ++r) {
			if (name == currentDebugMIPS->GetRegName(c, r)) {
				*cat = c;
				*reg = r;
				return DebuggerRegType::NORMAL;
			}
		}
	}

	req.Fail("Invalid 'name' parameter");
	return DebuggerRegType::INVALID;
}

static DebuggerRegType ValidateCatReg(DebuggerRequest &req, int *cat, int *reg) {
	const char *name = req.data.getString("name", nullptr);
	if (name)
		return ValidateRegName(req, name, cat, reg);

	*cat = req.data.getInt("category", -1);
	*reg = req.data.getInt("register", -1);

	if (*cat < 0 || *cat >= currentDebugMIPS->GetNumCategories()) {
		req.Fail("Invalid 'category' parameter");
		return DebuggerRegType::INVALID;
	}

	// TODO: We fake it for GPR... not sure yet if this is a good thing.
	if (*cat == 0) {
		// Intentionally retains the reg value.
		if (*reg == 32)
			return DebuggerRegType::PC;
		if (*reg == 33)
			return DebuggerRegType::HI;
		if (*reg == 34)
			return DebuggerRegType::LO;
	}

	if (*reg < 0 || *reg >= currentDebugMIPS->GetNumRegsInCategory(*cat)) {
		req.Fail("Invalid 'register' parameter");
		return DebuggerRegType::INVALID;
	}

	return DebuggerRegType::NORMAL;
}

void WebSocketCPUGetReg(DebuggerRequest &req) {
	int cat, reg;
	uint32_t val;
	switch (ValidateCatReg(req, &cat, &reg)) {
	case DebuggerRegType::NORMAL:
		val = currentDebugMIPS->GetRegValue(cat, reg);
		break;

	case DebuggerRegType::PC:
		val = currentDebugMIPS->GetPC();
		break;
	case DebuggerRegType::HI:
		val = currentDebugMIPS->GetHi();
		break;
	case DebuggerRegType::LO:
		val = currentDebugMIPS->GetLo();
		break;

	case DebuggerRegType::INVALID:
		// Error response already sent.
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeInt("category", cat);
	json.writeInt("register", reg);
	json.writeFloat("intValue", val);
	json.writeString("floatValue", RegValueAsFloat(val));
}

void WebSocketCPUSetReg(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}
	if (!Core_IsStepping()) {
		return req.Fail("CPU currently running (cpu.interrupt first)");
	}

	uint32_t val;
	if (!req.ParamU32OrFloatBits("value", &val)) {
		// Already sent error.
		return;
	}

	int cat, reg;
	switch (ValidateCatReg(req, &cat, &reg)) {
	case DebuggerRegType::NORMAL:
		currentDebugMIPS->SetRegValue(cat, reg, val);
		break;

	case DebuggerRegType::PC:
		currentDebugMIPS->SetPC(val);
		break;
	case DebuggerRegType::HI:
		currentDebugMIPS->SetHi(val);
		break;
	case DebuggerRegType::LO:
		currentDebugMIPS->SetLo(val);
		break;

	case DebuggerRegType::INVALID:
		// Error response already sent.
		return;
	}

	JsonWriter &json = req.Respond();
	// Repeat it back just to avoid confusion on how it parsed.
	json.writeInt("category", cat);
	json.writeInt("register", reg);
	json.writeFloat("intValue", val);
	json.writeString("floatValue", RegValueAsFloat(val));
}
