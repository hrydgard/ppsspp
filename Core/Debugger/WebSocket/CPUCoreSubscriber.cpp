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
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/WebSocket/CPUCoreSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Reporting.h"

DebuggerSubscriber *WebSocketCPUCoreInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["cpu.stepping"] = &WebSocketCPUStepping;
	map["cpu.resume"] = &WebSocketCPUResume;
	map["cpu.status"] = &WebSocketCPUStatus;
	map["cpu.getAllRegs"] = &WebSocketCPUGetAllRegs;
	map["cpu.getReg"] = &WebSocketCPUGetReg;
	map["cpu.setReg"] = &WebSocketCPUSetReg;
	map["cpu.evaluate"] = &WebSocketCPUEvaluate;

	return nullptr;
}

static std::string RegValueAsFloat(uint32_t u) {
	union {
		uint32_t u;
		float f;
	} bits = { u };
	return StringFromFormat("%f", bits.f);
}

static DebugInterface *CPUFromRequest(DebuggerRequest &req) {
	if (!req.HasParam("thread"))
		return currentDebugMIPS;

	u32 uid;
	if (!req.ParamU32("thread", &uid))
		return nullptr;

	DebugInterface *cpuDebug = KernelDebugThread((SceUID)uid);
	if (!cpuDebug)
		req.Fail("Thread could not be found");
	return cpuDebug;
}

// Begin stepping and pause the CPU (cpu.stepping)
//
// No parameters.
//
// No immediate response.  Once CPU is stepping, a "cpu.stepping" event will be sent.
void WebSocketCPUStepping(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}
	if (!Core_IsStepping() && Core_IsActive()) {
		Core_EnableStepping(true, "cpu.stepping", 0);
	}
}

// Stop stepping and resume the CPU (cpu.resume)
//
// No parameters.
//
// No immediate response.  Once CPU is stepping, a "cpu.resume" event will be sent.
void WebSocketCPUResume(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}
	if (!Core_IsStepping() || coreState == CORE_POWERDOWN) {
		return req.Fail("CPU not stepping");
	}

	CBreakPoints::SetSkipFirst(currentMIPS->pc);
	if (currentMIPS->inDelaySlot) {
		Core_DoSingleStep();
	}
	Core_EnableStepping(false);
}

// Request the current CPU status (cpu.status)
//
// No parameters.
//
// Response (same event name):
//  - stepping: boolean, CPU currently stepping.
//  - paused: boolean, CPU paused or not started yet.
//  - pc: number value of PC register (inaccurate unless stepping.)
//  - ticks: number of CPU cycles into emulation.
void WebSocketCPUStatus(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeBool("stepping", PSP_IsInited() && Core_IsStepping() && coreState != CORE_POWERDOWN);
	json.writeBool("paused", GetUIState() != UISTATE_INGAME);
	// Avoid NULL deference.
	json.writeUint("pc", PSP_IsInited() ? currentMIPS->pc : 0);
	// A double ought to be good enough for a 156 day debug session.
	json.writeFloat("ticks", PSP_IsInited() ? CoreTiming::GetTicks() : 0);
}

// Retrieve all regs and their values (cpu.getAllRegs)
//
// Parameters:
//  - thread: optional number indicating the thread id to get regs for.
//
// Response (same event name):
//  - categories: array of objects:
//     - id: "category" property to use for other events.
//     - name: a category name, such as "GPR".
//     - registerNames: array of string names of the registers (size varies per category.)
//     - uintValues: array of unsigned integer values for the registers.
//     - floatValues: array of strings showing float representation.  May be "nan", "inf", or "-inf".
void WebSocketCPUGetAllRegs(DebuggerRequest &req) {
	auto cpuDebug = CPUFromRequest(req);
	if (!cpuDebug)
		return;

	JsonWriter &json = req.Respond();

	json.pushArray("categories");
	for (int c = 0; c < cpuDebug->GetNumCategories(); ++c) {
		json.pushDict();
		json.writeInt("id", c);
		json.writeString("name", cpuDebug->GetCategoryName(c));

		int total = cpuDebug->GetNumRegsInCategory(c);

		json.pushArray("registerNames");
		for (int r = 0; r < total; ++r)
			json.writeString(cpuDebug->GetRegName(c, r));
		if (c == 0) {
			json.writeString("pc");
			json.writeString("hi");
			json.writeString("lo");
		}
		json.pop();

		json.pushArray("uintValues");
		// Writing as floating point to avoid negatives.  Actually double, so safe.
		for (int r = 0; r < total; ++r)
			json.writeUint(cpuDebug->GetRegValue(c, r));
		if (c == 0) {
			json.writeUint(cpuDebug->GetPC());
			json.writeUint(cpuDebug->GetHi());
			json.writeUint(cpuDebug->GetLo());
		}
		json.pop();

		json.pushArray("floatValues");
		// Note: String so it can have Infinity and NaN.
		for (int r = 0; r < total; ++r)
			json.writeString(RegValueAsFloat(cpuDebug->GetRegValue(c, r)));
		if (c == 0) {
			json.writeString(RegValueAsFloat(cpuDebug->GetPC()));
			json.writeString(RegValueAsFloat(cpuDebug->GetHi()));
			json.writeString(RegValueAsFloat(cpuDebug->GetLo()));
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
	const char *name = req.data.getStringOr("name", nullptr);
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

// Retrieve the value of a single register (cpu.getReg)
//
// Parameters (by name):
//  - thread: optional number indicating the thread id to get from.
//  - name: string name of register to lookup.
//
// Parameters (by category id and index, ignored if name specified):
//  - thread: optional number indicating the thread id to get from.
//  - category: id of category for the register.
//  - register: index into array of registers.
//
// Response (same event name):
//  - category: id of category for the register.
//  - register: index into array of registers.
//  - uintValue: value in register.
//  - floatValue: string showing float representation.  May be "nan", "inf", or "-inf".
void WebSocketCPUGetReg(DebuggerRequest &req) {
	auto cpuDebug = CPUFromRequest(req);
	if (!cpuDebug)
		return;

	int cat, reg;
	uint32_t val;
	switch (ValidateCatReg(req, &cat, &reg)) {
	case DebuggerRegType::NORMAL:
		val = cpuDebug->GetRegValue(cat, reg);
		break;

	case DebuggerRegType::PC:
		val = cpuDebug->GetPC();
		break;
	case DebuggerRegType::HI:
		val = cpuDebug->GetHi();
		break;
	case DebuggerRegType::LO:
		val = cpuDebug->GetLo();
		break;

	case DebuggerRegType::INVALID:
		// Error response already sent.
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeInt("category", cat);
	json.writeInt("register", reg);
	json.writeUint("uintValue", val);
	json.writeString("floatValue", RegValueAsFloat(val));
}

// Update the value of a single register (cpu.setReg)
//
// Parameters (by name):
//  - thread: optional number indicating the thread id to update.
//  - name: string name of register to lookup.
//  - value: number (uint values only) or string to set to.  Values may include
//    "0x1234", "1.5", "nan", "-inf", etc.  For a float, use a string with decimal e.g. "1.0".
//
// Parameters (by category id and index, ignored if name specified):
//  - thread: optional number indicating the thread id to update.
//  - category: id of category for the register.
//  - register: index into array of registers.
//  - value: number (uint values only) or string to set to.  Values may include
//    "0x1234", "1.5", "nan", "-inf", etc.  For a float, use a string with decimal e.g. "1.0".
//
// Response (same event name):
//  - category: id of category for the register.
//  - register: index into array of registers.
//  - uintValue: new value in register.
//  - floatValue: string showing float representation.  May be "nan", "inf", or "-inf".
//
// NOTE: Cannot be called unless the CPU is currently stepping.
void WebSocketCPUSetReg(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}
	if (!Core_IsStepping()) {
		return req.Fail("CPU currently running (cpu.stepping first)");
	}

	auto cpuDebug = CPUFromRequest(req);
	if (!cpuDebug)
		return;

	uint32_t val;
	if (!req.ParamU32("value", &val, true)) {
		// Already sent error.
		return;
	}

	int cat, reg;
	switch (ValidateCatReg(req, &cat, &reg)) {
	case DebuggerRegType::NORMAL:
		if (cat == 0 && reg == 0 && val != 0) {
			return req.Fail("Cannot change reg zero");
		}
		cpuDebug->SetRegValue(cat, reg, val);
		// In case part of it was ignored (e.g. flags reg.)
		val = cpuDebug->GetRegValue(cat, reg);
		break;

	case DebuggerRegType::PC:
		cpuDebug->SetPC(val);
		break;
	case DebuggerRegType::HI:
		cpuDebug->SetHi(val);
		break;
	case DebuggerRegType::LO:
		cpuDebug->SetLo(val);
		break;

	case DebuggerRegType::INVALID:
		// Error response already sent.
		return;
	}

	Reporting::NotifyDebugger();

	JsonWriter &json = req.Respond();
	// Repeat it back just to avoid confusion on how it parsed.
	json.writeInt("category", cat);
	json.writeInt("register", reg);
	json.writeUint("uintValue", val);
	json.writeString("floatValue", RegValueAsFloat(val));
}

// Evaluate an expression (cpu.evaluate)
//
// Parameters:
//  - thread: optional number indicating the thread id to update.
//  - expression: string containing labels, operators, regs, etc.
//
// Response (same event name):
//  - uintValue: value in register.
//  - floatValue: string showing float representation.  May be "nan", "inf", or "-inf".
void WebSocketCPUEvaluate(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	auto cpuDebug = CPUFromRequest(req);
	if (!cpuDebug)
		return;

	std::string exp;
	if (!req.ParamString("expression", &exp)) {
		// Already sent error.
		return;
	}

	u32 val;
	PostfixExpression postfix;
	if (!cpuDebug->initExpression(exp.c_str(), postfix)) {
		return req.Fail(StringFromFormat("Could not parse expression syntax: %s", getExpressionError()));
	}
	if (!cpuDebug->parseExpression(postfix, val)) {
		return req.Fail(StringFromFormat("Could not evaluate expression: %s", getExpressionError()));
	}

	JsonWriter &json = req.Respond();
	json.writeUint("uintValue", val);
	json.writeString("floatValue", RegValueAsFloat(val));
}
