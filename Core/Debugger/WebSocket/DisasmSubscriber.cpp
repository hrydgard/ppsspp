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

#include "base/stringutil.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/WebSocket/DisasmSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSDebugInterface.h"

struct WebSocketDisasmState {
	WebSocketDisasmState() {
		disasm_.setCpu(currentDebugMIPS);
	}

	void Base(DebuggerRequest &req);
	void Disasm(DebuggerRequest &req);

protected:
	void WriteDisasmLine(JsonWriter &json, const DisassemblyLineInfo &l);
	void WriteBranchGuide(JsonWriter &json, const BranchLine &l);

	DisassemblyManager disasm_;
};

void *WebSocketDisasmInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketDisasmState();
	map["memory.base"] = std::bind(&WebSocketDisasmState::Base, p, std::placeholders::_1);
	map["memory.disasm"] = std::bind(&WebSocketDisasmState::Disasm, p, std::placeholders::_1);

	return p;
}

void WebSocketDisasmShutdown(void *p) {
	delete static_cast<WebSocketDisasmState *>(p);
}

void WebSocketDisasmState::WriteDisasmLine(JsonWriter &json, const DisassemblyLineInfo &l) {
	u32 addr = l.info.opcodeAddress;
	json.pushDict();
	if (l.type == DISTYPE_OPCODE)
		json.writeString("type", "opcode");
	else if (l.type == DISTYPE_MACRO)
		json.writeString("type", "macro");
	else if (l.type == DISTYPE_DATA)
		json.writeString("type", "data");
	else if (l.type == DISTYPE_OTHER)
		json.writeString("type", "other");

	json.writeFloat("address", addr);
	json.writeInt("addressSize", l.totalSize);
	json.writeFloat("encoding", l.info.encodedOpcode.encoding);
	int c = currentDebugMIPS->getColor(addr) & 0x00FFFFFF;
	json.writeString("backgroundColor", StringFromFormat("#%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, c >> 16));
	json.writeString("name", l.name);
	json.writeString("params", l.params);

	const std::string addressSymbol = g_symbolMap->GetLabelString(addr);
	if (addressSymbol.empty())
		json.writeRaw("symbol", "null");
	else
		json.writeString("symbol", addressSymbol);

	bool enabled;
	// TODO: Account for bp inside macro?
	if (CBreakPoints::IsAddressBreakPoint(addr, &enabled)) {
		json.pushDict("breakpoint");
		json.writeBool("enabled", enabled);
		auto cond = CBreakPoints::GetBreakPointCondition(addr);
		if (cond)
			json.writeString("expression", cond->expressionString);
		else
			json.writeRaw("expression", "null");
		json.pop();
	} else {
		json.writeRaw("breakpoint", "null");
	}

	json.writeBool("isCurrentPC", currentDebugMIPS->GetPC() == addr);
	if (l.info.isBranch) {
		json.pushDict("branch");
		if (!l.info.isBranchToRegister) {
			json.writeFloat("targetAddress", l.info.branchTarget);
			json.writeRaw("register", "null");
		} else {
			json.writeRaw("targetAddress", "null");
			json.writeInt("register", l.info.branchRegisterNum);
		}
		json.writeBool("isLinked", l.info.isLinkedBranch);
		json.writeBool("isLikely", l.info.isLikelyBranch);
		json.pop();
	} else {
		json.writeRaw("branch", "null");
	}

	if (l.info.hasRelevantAddress) {
		json.pushDict("relevantData");
		json.writeFloat("address", l.info.relevantAddress);
		if (Memory::IsValidRange(l.info.relevantAddress, 4))
			json.writeFloat("uintValue", Memory::ReadUnchecked_U32(l.info.relevantAddress));
		else
			json.writeRaw("uintValue", "null");
		json.pop();
	} else {
		json.writeRaw("relevantData", "null");
	}

	if (l.info.isConditional)
		json.writeBool("conditionMet", l.info.conditionMet);
	else
		json.writeRaw("conditionMet", "null");

	if (l.info.isDataAccess) {
		json.pushDict("dataAccess");
		json.writeFloat("address", l.info.dataAddress);
		json.writeInt("size", l.info.dataSize);
		
		std::string dataSymbol = g_symbolMap->GetLabelString(l.info.dataAddress);
		std::string valueSymbol;
		if (!Memory::IsValidRange(l.info.dataAddress, l.info.dataSize))
			json.writeRaw("uintValue", "null");
		else if (l.info.dataSize == 1)
			json.writeFloat("uintValue", Memory::ReadUnchecked_U8(l.info.dataAddress));
		else if (l.info.dataSize == 2)
			json.writeFloat("uintValue", Memory::ReadUnchecked_U16(l.info.dataAddress));
		else if (l.info.dataSize >= 4) {
			u32 data = Memory::ReadUnchecked_U32(l.info.dataAddress);
			valueSymbol = g_symbolMap->GetLabelString(data);
			json.writeFloat("uintValue", data);
		}

		if (!dataSymbol.empty())
			json.writeString("symbol", dataSymbol);
		else
			json.writeRaw("symbol", "null");
		if (!valueSymbol.empty())
			json.writeString("valueSymbol", valueSymbol);
		else
			json.writeRaw("valueSymbol", "null");
		json.pop();
	} else {
		json.writeRaw("dataAccess", "null");
	}

	json.pop();
}

void WebSocketDisasmState::WriteBranchGuide(JsonWriter &json, const BranchLine &l) {
	json.pushDict();
	json.writeFloat("top", l.first);
	json.writeFloat("bottom", l.second);
	if (l.type == LINE_UP)
		json.writeString("direction", "up");
	else if (l.type == LINE_DOWN)
		json.writeString("direction", "down");
	else if (l.type == LINE_RIGHT)
		json.writeString("direction", "right");
	json.writeInt("lane", l.laneIndex);
	json.pop();
}

void WebSocketDisasmState::Base(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeString("addressHex", StringFromFormat("%016llx", Memory::base));
}

void WebSocketDisasmState::Disasm(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive()) {
		return req.Fail("CPU not started");
	}

	uint32_t start, end;
	if (!req.ParamU32("address", &start))
		return;
	uint32_t count = 0;
	if (!req.ParamU32("count", &count, false, DebuggerParamType::OPTIONAL))
		return;
	if (count != 0) {
		// Let's assume everything is two instructions.
		disasm_.analyze(start - 4, count * 8 + 8);
		start = disasm_.getStartAddress(start);
		if (start == -1)
			req.ParamU32("address", &start);
		end = disasm_.getNthNextAddress(start, count);
	} else if (req.ParamU32("end", &end)) {
		// Let's assume everything is two instructions.
		disasm_.analyze(start - 4, end - start + 8);
		start = disasm_.getStartAddress(start);
		if (start == -1)
			req.ParamU32("address", &start);

		// Correct end and calculate count based on it.
		// This accounts for macros as one line, although two instructions.
		u32 stop = end;
		count = 0;
		for (end = start; end < stop; end = disasm_.getNthNextAddress(end, 1)) {
			count++;
		}
	} else {
		// Error message already sent.
		return;
	}

	bool displaySymbols = true;
	if (!req.ParamBool("displaySymbols", &displaySymbols, DebuggerParamType::OPTIONAL))
		return;

	JsonWriter &json = req.Respond();
	json.pushDict("range");
	json.writeFloat("start", start);
	json.writeFloat("end", end);
	json.pop();

	json.pushArray("lines");
	DisassemblyLineInfo line;
	u32 addr = start;
	for (u32 i = 0; i < count; ++i) {
		disasm_.getLine(addr, displaySymbols, line);
		WriteDisasmLine(json, line);
		addr += line.totalSize;
	}
	json.pop();

	json.pushArray("branchGuides");
	auto branchGuides = disasm_.getBranchLines(start, end - start);
	for (auto bl : branchGuides)
		WriteBranchGuide(json, bl);
	json.pop();
}
