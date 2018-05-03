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
#include "Core/Debugger/WebSocket/BreakpointSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MIPS/MIPSDebugInterface.h"

void *WebSocketBreakpointInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["cpu.breakpoint.add"] = &WebSocketCPUBreakpointAdd;
	map["cpu.breakpoint.update"] = &WebSocketCPUBreakpointUpdate;
	map["cpu.breakpoint.remove"] = &WebSocketCPUBreakpointRemove;
	map["cpu.breakpoint.list"] = &WebSocketCPUBreakpointList;

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

void WebSocketCPUBreakpointAdd(DebuggerRequest &req) {
	WebSocketCPUBreakpointParams params;
	if (!params.Parse(req))
		return;

	CBreakPoints::AddBreakPoint(params.address);
	params.Apply();
	req.Respond();
}

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
		json.pop();
	}
	json.pop();
}
