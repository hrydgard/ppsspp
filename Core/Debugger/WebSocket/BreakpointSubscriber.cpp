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
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/LineInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/WebSocket/BreakpointSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPSDebugInterface.h"

static const char *BreakpointKindToString(BreakpointKind kind) {
	switch (kind) {
	case BreakpointKind::Exec: return "exec";
	case BreakpointKind::Memory: return "memory";
	case BreakpointKind::Register: return "register";
	default: return "none";
	}
}

void WriteBreakpointHit(JsonWriter &json, const BreakpointHit &hit) {
	json.pushDict("hit");
	json.writeString("kind", BreakpointKindToString(hit.kind));
	json.writeUint("pc", hit.pc);
	json.writeUint("address", hit.address);
	json.writeUint("hits", hit.numHits);
	json.writeBool("logged", hit.logged);
	json.writeBool("paused", hit.paused);
	if (hit.condition.empty())
		json.writeNull("condition");
	else
		json.writeString("condition", hit.condition);

	// Resolved here rather than left to the client: it's one symbol map lookup at break time, and
	// it saves a round trip at exactly the moment the client is trying to show something.
	const std::string symbol = g_symbolMap->GetDescription(hit.address);
	if (symbol.empty())
		json.writeNull("symbol");
	else
		json.writeString("symbol", symbol);

	// Only when the game shipped an unstripped ELF with DWARF in it - see LineInfo.h. Keyed on pc
	// rather than address, since for a memory breakpoint the interesting source location is the
	// instruction that did the access, not the data it touched.
	std::string file;
	int line = 0;
	if (g_lineInfo.Lookup(hit.pc, &file, &line)) {
		json.writeString("file", file);
		json.writeInt("line", line);
	} else {
		json.writeNull("file");
		json.writeNull("line");
	}

	if (hit.kind == BreakpointKind::Memory) {
		json.writeInt("size", hit.size);
		json.writeString("access", hit.write ? "write" : "read");
		json.writeString("source", hit.source);
	}
	if (hit.kind == BreakpointKind::Register) {
		json.writeInt("register", hit.reg);
		json.writeString("registerName", MIPSDebugInterface::GetRegName(0, hit.reg));
	}

	// Which breakpoint it was, as opposed to what was touched. Those differ for every memcheck
	// with a range, and this is what matches the entries in cpu.breakpoint.list / etc. A register
	// breakpoint isn't identified by an address at all, so it gets no range rather than a
	// meaningless zero one - "register" above is its identity.
	if (hit.kind == BreakpointKind::Exec || hit.kind == BreakpointKind::Memory) {
		json.pushDict("breakpoint");
		json.writeUint("start", hit.rangeStart);
		json.writeUint("end", hit.rangeEnd);
		json.end();
	}

	json.end();
}

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

	map["cpu.regBreakpoint.add"] = &WebSocketRegBreakpointAdd;
	map["cpu.regBreakpoint.update"] = &WebSocketRegBreakpointUpdate;
	map["cpu.regBreakpoint.remove"] = &WebSocketRegBreakpointRemove;
	map["cpu.regBreakpoint.list"] = &WebSocketRegBreakpointList;

	return nullptr;
}

// Resolves a GPR by name (e.g. "s3", case-insensitive) or 0-31 index. Interpreter-only feature -
// see RegBreakpoint in Breakpoints.h - has no effect while running under a JIT backend.
static bool ParseRegBreakpointReg(DebuggerRequest &req, int *reg) {
	if (req.HasParam("name")) {
		std::string name;
		if (!req.ParamString("name", &name))
			return false;
		for (int i = 0; i < 32; ++i) {
			if (!strcasecmp(name.c_str(), MIPSDebugInterface::GetRegName(0, i).c_str())) {
				*reg = i;
				return true;
			}
		}
		req.Fail(StringFromFormat("Unknown register name: %s", name.c_str()));
		return false;
	}

	uint32_t regU32;
	if (!req.ParamU32("register", &regU32))
		return false;
	if (regU32 >= 32) {
		req.Fail("Invalid 'register' parameter, must be 0-31");
		return false;
	}
	*reg = (int)regU32;
	return true;
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
		}
		hasLogFormat = req.HasParam("logFormat");
		if (hasLogFormat) {
			if (!req.ParamString("logFormat", &logFormat))
				return false;
		}

		return true;
	}

	// Compiled on the CPU thread rather than in Parse(): resolving symbols in an expression goes
	// through g_symbolMap, which is CPU-thread-owned and destroyed on shutdown.
	bool CompileCondition(std::string *error) {
		if (!hasCondition || condition.empty())
			return true;
		if (initExpression(currentDebugMIPS, condition.c_str(), compiledCondition))
			return true;
		*error = StringFromFormat("Could not parse expression syntax: %s", getExpressionError());
		return false;
	}

	void Apply() {
		if (hasCondition && !condition.empty()) {
			BreakPointCond cond;
			cond.debug = currentDebugMIPS;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			g_breakpoints.ChangeBreakPointAddCond(address, cond);
		} else if (hasCondition && condition.empty()) {
			g_breakpoints.ChangeBreakPointRemoveCond(address);
		}

		if (hasLogFormat) {
			g_breakpoints.ChangeBreakPointLogFormat(address, logFormat);
		}

		// TODO: Fix this interface.
		if (hasLog && !hasEnabled) {
			g_breakpoints.IsAddressBreakPoint(address, &enabled);
			hasEnabled = true;
		}
		if (hasLog && hasEnabled) {
			BreakAction result = BREAK_ACTION_NONE;
			if (log)
				result |= BREAK_ACTION_LOG;
			if (enabled)
				result |= BREAK_ACTION_PAUSE;
			g_breakpoints.ChangeBreakPoint(address, result);
		} else if (hasEnabled) {
			g_breakpoints.ChangeBreakPoint(address, enabled);
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

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		g_breakpoints.AddBreakPoint(params.address);
		params.Apply();
	});
	if (!error.empty())
		return req.Fail(error);
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

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	bool found = false;
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		bool enabled;
		found = g_breakpoints.IsAddressBreakPoint(params.address, &enabled);
		if (found)
			params.Apply();
	});

	if (!error.empty())
		return req.Fail(error);
	if (!found)
		return req.Fail("Breakpoint not found");
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

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		g_breakpoints.RemoveBreakPoint(address);
	});
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
//     - log: boolean, whether to log when this breakpoint trips.
//     - condition: null, or string expression to evaluate - breakpoint does not trip if false.
//     - logFormat: null, or string to log when breakpoint trips, may include {expression} parts.
//     - symbol: null, or string label or symbol at breakpoint address.
//     - code: string disassembly of breakpoint address.
//     - hits: unsigned integer, how many times this breakpoint's address has been reached
//       (and any condition passed) since it was added - useful for confirming a breakpoint is
//       actually being reached at all, independently of whether log/enabled is set.
void WebSocketCPUBreakpointList(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	// Route the breakpoint/symbol/disassembly reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		json.pushArray("breakpoints");
		// No filtering needed - the internal breakpoint behind step-over/run-until isn't in here.
		std::vector<BreakPoint> bps = g_breakpoints.GetBreakpoints();
		for (const BreakPoint &bp : bps) {
			json.pushDict();
			json.writeUint("address", bp.addr);
			json.writeBool("enabled", bp.IsEnabled());
			json.writeBool("log", (bp.action & BREAK_ACTION_LOG) != 0);
			json.writeUint("hits", bp.numHits);
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

			DisassemblyLineInfo line;
			g_disassemblyManager.getLine(g_disassemblyManager.getStartAddress(bp.addr), true, line, currentDebugMIPS);
			json.writeString("code", line.name + " " + line.params);

			json.pop();
		}
		json.pop();
	});
}

struct WebSocketMemoryBreakpointParams {
	uint32_t address = 0;
	uint32_t end = 0;

	// These flags indicate whether the corresponding parameter was present in the request.
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
		}
		hasLogFormat = req.HasParam("logFormat");
		if (hasLogFormat) {
			if (!req.ParamString("logFormat", &logFormat))
				return false;
		}

		return true;
	}

	BreakAction Action(bool adding) {
		int bits = BREAK_ACTION_PAUSE | BREAK_ACTION_LOG;
		if (adding || (hasLog && hasEnabled)) {
			bits = (enabled ? BREAK_ACTION_PAUSE : 0) | (log ? BREAK_ACTION_LOG : 0);
		} else {
			MemCheck prev;
			if (g_breakpoints.GetMemCheck(address, end, &prev))
				bits = prev.action;

			if (hasEnabled)
				bits = (bits & ~BREAK_ACTION_PAUSE) | (enabled ? BREAK_ACTION_PAUSE : 0);
			if (hasLog)
				bits = (bits & ~BREAK_ACTION_LOG) | (log ? BREAK_ACTION_LOG : 0);
		}

		return BreakAction(bits);
	}

	// Compiled on the CPU thread rather than in Parse(): resolving symbols in an expression goes
	// through g_symbolMap, which is CPU-thread-owned and destroyed on shutdown.
	bool CompileCondition(std::string *error) {
		if (!hasCondition || condition.empty())
			return true;
		if (initExpression(currentDebugMIPS, condition.c_str(), compiledCondition))
			return true;
		*error = StringFromFormat("Could not parse expression syntax: %s", getExpressionError());
		return false;
	}

	void Apply() {
		if (hasCondition && !condition.empty()) {
			BreakPointCond cond;
			cond.debug = currentDebugMIPS;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			g_breakpoints.ChangeMemCheckAddCond(address, end, cond);
		} else if (hasCondition && condition.empty()) {
			g_breakpoints.ChangeMemCheckRemoveCond(address, end);
		}
		if (hasLogFormat) {
			g_breakpoints.ChangeMemCheckLogFormat(address, end, logFormat);
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

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		g_breakpoints.AddMemCheck(params.address, params.end, params.cond, params.Action(true));
		params.Apply();
	});
	if (!error.empty())
		return req.Fail(error);
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

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	bool found = false;
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		MemCheck mc;
		found = g_breakpoints.GetMemCheck(params.address, params.end, &mc);
		if (found) {
			g_breakpoints.ChangeMemCheck(params.address, params.end, params.cond, params.Action(true));
			params.Apply();
		}
	});

	if (!error.empty())
		return req.Fail(error);
	if (!found)
		return req.Fail("Breakpoint not found");
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
	// Matches the check in WebSocketMemoryBreakpointParams::Parse() (used by add/update) -
	// without it, a crafted size could wrap address + size below address.
	if (address + size < address)
		return req.Fail("Size is too large");

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		g_breakpoints.RemoveMemCheck(address, size == 0 ? 0 : address + size);
	});
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

	// Route the breakpoint/symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		json.pushArray("breakpoints");
		std::vector<MemCheck> mcs = g_breakpoints.GetMemChecks();
		for (const MemCheck &mc : mcs) {
			json.pushDict();
			json.writeUint("address", mc.start);
			json.writeUint("size", mc.end == 0 ? 0 : mc.end - mc.start);
			json.writeBool("enabled", (mc.action & BREAK_ACTION_PAUSE));
			json.writeBool("log", (mc.action & BREAK_ACTION_LOG) != 0);
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
	});
}

struct WebSocketRegBreakpointParams {
	int reg = 0;
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

		if (!ParseRegBreakpointReg(req, &reg))
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
		}
		hasLogFormat = req.HasParam("logFormat");
		if (hasLogFormat) {
			if (!req.ParamString("logFormat", &logFormat))
				return false;
		}

		return true;
	}

	// Compiled on the CPU thread rather than in Parse(): resolving symbols in an expression goes
	// through g_symbolMap, which is CPU-thread-owned and destroyed on shutdown.
	bool CompileCondition(std::string *error) {
		if (!hasCondition || condition.empty())
			return true;
		if (initExpression(currentDebugMIPS, condition.c_str(), compiledCondition))
			return true;
		*error = StringFromFormat("Could not parse expression syntax: %s", getExpressionError());
		return false;
	}

	void Apply() {
		if (hasCondition && !condition.empty()) {
			BreakPointCond cond;
			cond.debug = currentDebugMIPS;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			g_breakpoints.ChangeRegBreakpointAddCond(reg, cond);
		} else if (hasCondition && condition.empty()) {
			g_breakpoints.ChangeRegBreakpointRemoveCond(reg);
		}

		if (hasLogFormat) {
			g_breakpoints.ChangeRegBreakpointLogFormat(reg, logFormat);
		}

		if (hasLog && !hasEnabled) {
			RegBreakpoint bp;
			if (g_breakpoints.GetRegBreakpoint(reg, &bp))
				enabled = bp.IsEnabled();
			hasEnabled = true;
		}
		if (hasLog && hasEnabled) {
			BreakAction result = BREAK_ACTION_NONE;
			if (log)
				result |= BREAK_ACTION_LOG;
			if (enabled)
				result |= BREAK_ACTION_PAUSE;
			g_breakpoints.ChangeRegBreakpoint(reg, result);
		} else if (hasEnabled) {
			g_breakpoints.ChangeRegBreakpoint(reg, enabled);
		}
	}
};

// Add a new register write breakpoint (cpu.regBreakpoint.add)
//
// Interpreter-only for now - see RegBreakpoint in Core/Debugger/Breakpoints.h. Has no effect
// while running under a JIT backend (force the interpreter core, e.g. -i on the command line).
//
// Parameters:
//  - register: unsigned integer 0-31 GPR index to break on write to. Ignored if name given.
//  - name: string register name (e.g. "s3"), case-insensitive. Takes priority over 'register'.
//  - enabled: optional boolean, whether to actually enter stepping when this breakpoint trips.
//  - log: optional boolean, whether to log when this breakpoint trips.
//  - condition: optional string expression to evaluate - breakpoint does not trip if false.
//  - logFormat: optional string to log when breakpoint trips, may include {expression} parts.
//
// Response (same event name) with no extra data.
//
// Note: will replace any register breakpoint already set on the same register.
void WebSocketRegBreakpointAdd(DebuggerRequest &req) {
	WebSocketRegBreakpointParams params;
	if (!params.Parse(req))
		return;

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		g_breakpoints.AddRegBreakpoint(params.reg);
		params.Apply();
	});
	if (!error.empty())
		return req.Fail(error);
	req.Respond();
}

// Update a register write breakpoint (cpu.regBreakpoint.update)
//
// Parameters: same as cpu.regBreakpoint.add.
//
// Response (same event name) with no extra data.
void WebSocketRegBreakpointUpdate(DebuggerRequest &req) {
	WebSocketRegBreakpointParams params;
	if (!params.Parse(req))
		return;

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	bool found = false;
	std::string error;
	Core_RunOnCPUThread([&] {
		if (!params.CompileCondition(&error))
			return;
		RegBreakpoint bp;
		found = g_breakpoints.GetRegBreakpoint(params.reg, &bp);
		if (found)
			params.Apply();
	});

	if (!error.empty())
		return req.Fail(error);
	if (!found)
		return req.Fail("Breakpoint not found");
	req.Respond();
}

// Remove a register write breakpoint (cpu.regBreakpoint.remove)
//
// Parameters:
//  - register: unsigned integer 0-31 GPR index. Ignored if name given.
//  - name: string register name (e.g. "s3"), case-insensitive. Takes priority over 'register'.
//
// Response (same event name) with no extra data.
void WebSocketRegBreakpointRemove(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	int reg;
	if (!ParseRegBreakpointReg(req, &reg))
		return;

	// Route the actual breakpoint manipulation to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		g_breakpoints.RemoveRegBreakpoint(reg);
	});
	req.Respond();
}

// List all register write breakpoints (cpu.regBreakpoint.list)
//
// No parameters.
//
// Response (same event name):
//  - breakpoints: array of objects, each with properties:
//     - register: unsigned integer 0-31 GPR index.
//     - name: string register name (e.g. "s3").
//     - enabled: boolean, whether to actually enter stepping when this breakpoint trips.
//     - log: boolean, whether to log when this breakpoint trips.
//     - hits: unsigned integer, number of times this breakpoint has tripped (regardless of
//       whether it paused - i.e. even with enabled false, if log is true.)
//     - condition: null, or string expression to evaluate - breakpoint does not trip if false.
//     - logFormat: null, or string to log when breakpoint trips, may include {expression} parts.
void WebSocketRegBreakpointList(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive()) {
		return req.Fail("CPU not started");
	}

	// Route the breakpoint reads to the CPU thread instead of poking at them directly from this
	// WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		json.pushArray("breakpoints");
		std::vector<RegBreakpoint> bps = g_breakpoints.GetRegBreakpoints();
		for (const RegBreakpoint &bp : bps) {
			json.pushDict();
			json.writeInt("register", bp.reg);
			json.writeString("name", MIPSDebugInterface::GetRegName(0, bp.reg));
			json.writeBool("enabled", bp.IsEnabled());
			json.writeBool("log", (bp.result & BREAK_ACTION_LOG) != 0);
			json.writeUint("hits", bp.numHits);
			if (bp.hasCond)
				json.writeString("condition", bp.cond.expressionString);
			else
				json.writeNull("condition");
			if (!bp.logFormat.empty())
				json.writeString("logFormat", bp.logFormat);
			else
				json.writeNull("logFormat");

			json.pop();
		}
		json.pop();
	});
}
