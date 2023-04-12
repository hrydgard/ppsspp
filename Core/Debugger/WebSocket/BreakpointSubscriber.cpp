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
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/WebSocket/BreakpointSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPSDebugInterface.h"

DebuggerSubscriber *WebSocketBreakpointInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["cpu.breakpoint.add"] = &WebSocketCPUBreakpointAdd;
	map["cpu.breakpoint.update"] = &WebSocketCPUBreakpointUpdate;
	map["cpu.breakpoint.remove"] = &WebSocketCPUBreakpointRemove;
	map["cpu.breakpoint.list"] = &WebSocketCPUBreakpointList;

	map["memory.breakpoint.add"] = &WebSocketMemoryBreakpointAdd;
	map["memory.breakpoint.update"] = &WebSocketMemoryBreakpointUpdate;
	map["memory.breakpoint.remove"] = &WebSocketMemoryBreakpointRemove;
	map["memory.breakpoint.list"] = &WebSocketMemoryBreakpointList;

	return nullptr;
}

struct WebSocketCPUBreakpointParams {
	uint32_t address = 0;
	bool hasEnabled = false;
	bool hasLog = false;
	bool hasCondition = false;
	bool hasLogFormat = false;

	bool enabled;
	bool log;
	std::string condition;
	PostfixExpression compiledCondition;
	std::string logFormat;

	bool Parse(DebuggerRequest &req) {
		if (!currentDebugMIPS->isAlive()) {
			req.Fail("CPU not started");
			return false;
		}

		if (!req.ParamU32("address", &address))
			return false;

		hasEnabled = req.HasParam("enabled");
		if (hasEnabled) {
			if (!req.ParamBool("enabled", &enabled))
				return false;
		}
		hasLog = req.HasParam("log");
		if (hasLog) {
			if (!req.ParamBool("log", &log))
				return false;
		}
		hasCondition = req.HasParam("condition");
		if (hasCondition) {
			if (!req.ParamString("condition", &condition))
				return false;
			if (!currentDebugMIPS->initExpression(condition.c_str(), compiledCondition)) {
				req.Fail(StringFromFormat("Could not parse expression syntax: %s", getExpressionError()));
				return false;
			}
		}
		hasLogFormat = req.HasParam("logFormat");
		if (hasLogFormat) {
			if (!req.ParamString("logFormat", &logFormat))
				return false;
		}

		return true;
	}

	void Apply() {
		if (hasCondition && !condition.empty()) {
			BreakPointCond cond;
			cond.debug = currentDebugMIPS;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			CBreakPoints::ChangeBreakPointAddCond(address, cond);
		} else if (hasCondition && condition.empty()) {
			CBreakPoints::ChangeBreakPointRemoveCond(address);
		}

		if (hasLogFormat) {
			CBreakPoints::ChangeBreakPointLogFormat(address, logFormat);
		}

		// TODO: Fix this interface.
		if (hasLog && !hasEnabled) {
			CBreakPoints::IsAddressBreakPoint(address, &enabled);
			hasEnabled = true;
		}
		if (hasLog && hasEnabled) {
			BreakAction result = BREAK_ACTION_IGNORE;
			if (log)
				result |= BREAK_ACTION_LOG;
			if (enabled)
				result |= BREAK_ACTION_PAUSE;
			CBreakPoints::ChangeBreakPoint(address, result);
		} else if (hasEnabled) {
			CBreakPoints::ChangeBreakPoint(address, enabled);
		}
	}
};

// Add a new CPU instruction breakpoint (cpu.breakpoint.add)
//
// Parameters:
//  - address: unsigned integer address of instruction to break at.
//  - enabled: optional boolean, whether to actually enter stepping when this breakpoint trips.
//  - log: optional boolean, whether to log when this breakpoint trips.
//  - condition: optional string expression to evaluate - breakpoint does not trip if false.
//  - logFormat: optional string to log when breakpoint trips, may include {expression} parts.
//
// Response (same event name) with no extra data.
//
// Note: will replace any breakpoint at the same address.
void WebSocketCPUBreakpointAdd(DebuggerRequest &req) {
	WebSocketCPUBreakpointParams params;
	if (!params.Parse(req))
		return;

	CBreakPoints::AddBreakPoint(params.address);
	params.Apply();
	req.Respond();
}

// Update a CPU instruction breakpoint (cpu.breakpoint.update)
//
// Parameters:
//  - address: unsigned integer address of instruction to break at.
//  - enabled: optional boolean, whether to actually enter stepping when this breakpoint trips.
//  - log: optional boolean, whether to log when this breakpoint trips.
//  - condition: optional string expression to evaluate - breakpoint does not trip if false.
//  - logFormat: optional string to log when breakpoint trips, may include {expression} parts.
//
// Response (same event name) with no extra data.
void WebSocketCPUBreakpointUpdate(DebuggerRequest &req) {
	WebSocketCPUBreakpointParams params;
	if (!params.Parse(req))
		return;
	bool enabled;
	if (!CBreakPoints::IsAddressBreakPoint(params.address, &enabled))
		return req.Fail("Breakpoint not found");

	params.Apply();
	req.Respond();
}

// Remove a CPU instruction breakpoint (cpu.breakpoint.remove)
//
// Parameters:
//  - address: unsigned integer address of instruction to break at.
//
// Response (same event name) with no extra data.
void WebSocketCPUBreakpointRemove(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	uint32_t address;
	if (!req.ParamU32("address", &address))
		return;

	CBreakPoints::RemoveBreakPoint(address);
	req.Respond();
}

// List all CPU instruction breakpoints (cpu.breakpoint.list)
//
// No parameters.
//
// Response (same event name):
//  - breakpoints: array of objects, each with properties:
//     - address: unsigned integer address of instruction to break at.
//     - enabled: boolean, whether to actually enter stepping when this breakpoint trips.
//     - log: optional boolean, whether to log when this breakpoint trips.
//     - condition: null, or string expression to evaluate - breakpoint does not trip if false.
//     - logFormat: null, or string to log when breakpoint trips, may include {expression} parts.
//     - symbol: null, or string label or symbol at breakpoint address.
//     - code: string disassembly of breakpoint address.
void WebSocketCPUBreakpointList(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	JsonWriter &json = req.Respond();
	json.pushArray("breakpoints");
	auto bps = CBreakPoints::GetBreakpoints();
	for (const auto &bp : bps) {
		if (bp.temporary)
			continue;

		json.pushDict();
		json.writeUint("address", bp.addr);
		json.writeBool("enabled", bp.IsEnabled());
		json.writeBool("log", (bp.result & BREAK_ACTION_LOG) != 0);
		if (bp.hasCond)
			json.writeString("condition", bp.cond.expressionString);
		else
			json.writeNull("condition");
		if (!bp.logFormat.empty())
			json.writeString("logFormat", bp.logFormat);
		else
			json.writeNull("logFormat");
		std::string symbol = g_symbolMap->GetLabelString(bp.addr);
		if (symbol.empty())
			json.writeNull("symbol");
		else
			json.writeString("symbol", symbol);

		DisassemblyManager manager;
		DisassemblyLineInfo line;
		manager.getLine(manager.getStartAddress(bp.addr), true, line);
		json.writeString("code", line.name + " " + line.params);

		json.pop();
	}
	json.pop();
}

struct WebSocketMemoryBreakpointParams {
	uint32_t address = 0;
	uint32_t end = 0;
	bool hasEnabled = false;
	bool hasLog = false;
	bool hasCond = false;
	bool hasCondition = false;
	bool hasLogFormat = false;

	bool enabled = true;
	bool log = true;
	MemCheckCondition cond = MEMCHECK_READWRITE;
	std::string condition;
	PostfixExpression compiledCondition;
	std::string logFormat;

	bool Parse(DebuggerRequest &req) {
		if (!currentDebugMIPS->isAlive()) {
			req.Fail("CPU not started");
			return false;
		}

		if (!req.ParamU32("address", &address))
			return false;
		uint32_t size;
		if (!req.ParamU32("size", &size))
			return false;
		if (address + size < address) {
			req.Fail("Size is too large");
			return false;
		}
		end = size == 0 ? 0 : address + size;

		hasEnabled = req.HasParam("enabled");
		if (hasEnabled) {
			if (!req.ParamBool("enabled", &enabled))
				return false;
		}
		hasLog = req.HasParam("log");
		if (hasLog) {
			if (!req.ParamBool("log", &log))
				return false;
		}
		hasCond = req.HasParam("read") || req.HasParam("write") || req.HasParam("change");
		if (hasCond) {
			bool read = false, write = false, change = false;
			if (!req.ParamBool("read", &read, DebuggerParamType::OPTIONAL) || !req.ParamBool("write", &write, DebuggerParamType::OPTIONAL) || !req.ParamBool("change", &change, DebuggerParamType::OPTIONAL))
				return false;
			int bits = (read ? MEMCHECK_READ : 0) | (write ? MEMCHECK_WRITE : 0) | (change ? MEMCHECK_WRITE_ONCHANGE : 0);
			cond = MemCheckCondition(bits);
		}
		hasCondition = req.HasParam("condition");
		if (hasCondition) {
			if (!req.ParamString("condition", &condition))
				return false;
			if (!currentDebugMIPS->initExpression(condition.c_str(), compiledCondition)) {
				req.Fail(StringFromFormat("Could not parse expression syntax: %s", getExpressionError()));
				return false;
			}
		}
		hasLogFormat = req.HasParam("logFormat");
		if (hasLogFormat) {
			if (!req.ParamString("logFormat", &logFormat))
				return false;
		}

		return true;
	}

	BreakAction Result(bool adding) {
		int bits = MEMCHECK_READWRITE;
		if (adding || (hasLog && hasEnabled)) {
			bits = (enabled ? BREAK_ACTION_PAUSE : 0) | (log ? BREAK_ACTION_LOG : 0);
		} else {
			MemCheck prev;
			if (CBreakPoints::GetMemCheck(address, end, &prev))
				bits = prev.result;

			if (hasEnabled)
				bits = (bits & ~BREAK_ACTION_PAUSE) | (enabled ? BREAK_ACTION_PAUSE : 0);
			if (hasLog)
				bits = (bits & ~BREAK_ACTION_LOG) | (log ? BREAK_ACTION_LOG : 0);
		}

		return BreakAction(bits);
	}

	void Apply() {
		if (hasCondition && !condition.empty()) {
			BreakPointCond cond;
			cond.debug = currentDebugMIPS;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			CBreakPoints::ChangeMemCheckAddCond(address, end, cond);
		} else if (hasCondition && condition.empty()) {
			CBreakPoints::ChangeMemCheckRemoveCond(address, end);
		}
		if (hasLogFormat) {
			CBreakPoints::ChangeMemCheckLogFormat(address, end, logFormat);
		}
	}
};

// Add a new memory breakpoint (memory.breakpoint.add)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - size: unsigned integer specifying size of memory range.
//  - enabled: optional boolean, whether to actually enter stepping when this breakpoint trips.
//  - log: optional boolean, whether to log when this breakpoint trips.
//  - read: optional boolean, whether to trip on any read to this address.
//  - write: optional boolean, whether to trip on any write to this address.
//  - change: optional boolean, whether to trip on a write to this address which modifies data
//    (or any write that may modify data.)
//  - condition: optional string expression to evaluate - breakpoint does not trip if false.
//  - logFormat: optional string to log when breakpoint trips, may include {expression} parts.
//
// Response (same event name) with no extra data.
//
// Note: will replace any breakpoint that has the same start address and size.
void WebSocketMemoryBreakpointAdd(DebuggerRequest &req) {
	WebSocketMemoryBreakpointParams params;
	if (!params.Parse(req))
		return;

	CBreakPoints::AddMemCheck(params.address, params.end, params.cond, params.Result(true));
	params.Apply();
	req.Respond();
}

// Update a memory breakpoint (memory.breakpoint.update)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - size: unsigned integer specifying size of memory range.
//  - enabled: optional boolean, whether to actually enter stepping when this breakpoint trips.
//  - log: optional boolean, whether to log when this breakpoint trips.
//  - read: optional boolean, whether to trip on any read to this address.
//  - write: optional boolean, whether to trip on any write to this address.
//  - change: optional boolean, whether to trip on a write to this address which modifies data
//    (or any write that may modify data.)
//  - condition: optional string expression to evaluate - breakpoint does not trip if false.
//  - logFormat: optional string to log when breakpoint trips, may include {expression} parts.
//
// Response (same event name) with no extra data.
void WebSocketMemoryBreakpointUpdate(DebuggerRequest &req) {
	WebSocketMemoryBreakpointParams params;
	if (!params.Parse(req))
		return;

	MemCheck mc;
	if (!CBreakPoints::GetMemCheck(params.address, params.end, &mc))
		return req.Fail("Breakpoint not found");

	CBreakPoints::ChangeMemCheck(params.address, params.end, params.cond, params.Result(true));
	params.Apply();
	req.Respond();
}

// Remove a memory breakpoint (memory.breakpoint.remove)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - size: unsigned integer specifying size of memory range.
//
// Response (same event name) with no extra data.
void WebSocketMemoryBreakpointRemove(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	uint32_t address;
	if (!req.ParamU32("address", &address))
		return;
	uint32_t size;
	if (!req.ParamU32("size", &size))
		return;

	CBreakPoints::RemoveMemCheck(address, size == 0 ? 0 : address + size);
	req.Respond();
}

// List all memory breakpoints (memory.breakpoint.list)
//
// No parameters.
//
// Response (same event name):
//  - breakpoints: array of objects, each with properties:
//     - address: unsigned integer address for the start of the memory range.
//     - size: unsigned integer specifying size of memory range.
//     - enabled: boolean, whether to actually enter stepping when this breakpoint trips.
//     - log: optional boolean, whether to log when this breakpoint trips.
//     - read: optional boolean, whether to trip on any read to this address.
//     - write: optional boolean, whether to trip on any write to this address.
//     - change: optional boolean, whether to trip on a write to this address which modifies data
//       (or any write that may modify data.)
//     - condition: null, or string expression to evaluate - breakpoint does not trip if false.
//     - logFormat: null, or string to log when breakpoint trips, may include {expression} parts.
//     - symbol: null, or string label or symbol at breakpoint address.
void WebSocketMemoryBreakpointList(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	JsonWriter &json = req.Respond();
	json.pushArray("breakpoints");
	auto mcs = CBreakPoints::GetMemChecks();
	for (const auto &mc : mcs) {
		json.pushDict();
		json.writeUint("address", mc.start);
		json.writeUint("size", mc.end == 0 ? 0 : mc.end - mc.start);
		json.writeBool("enabled", mc.IsEnabled());
		json.writeBool("log", (mc.result & BREAK_ACTION_LOG) != 0);
		json.writeBool("read", (mc.cond & MEMCHECK_READ) != 0);
		json.writeBool("write", (mc.cond & MEMCHECK_WRITE) != 0);
		json.writeBool("change", (mc.cond & MEMCHECK_WRITE_ONCHANGE) != 0);
		json.writeUint("hits", mc.numHits);
		if (mc.hasCondition)
			json.writeString("condition", mc.condition.expressionString);
		else
			json.writeNull("condition");
		if (!mc.logFormat.empty())
			json.writeString("logFormat", mc.logFormat);
		else
			json.writeNull("logFormat");
		std::string symbol = g_symbolMap->GetLabelString(mc.start);
		if (symbol.empty())
			json.writeNull("symbol");
		else
			json.writeString("symbol", symbol);

		json.pop();
	}
	json.pop();
}
