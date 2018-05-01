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

#include <algorithm>
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
	json.writeFloat("encoding", Memory::IsValidAddress(addr) ? Memory::Read_Instruction(addr).encoding : 0);
	if (l.totalSize >= 8 && Memory::IsValidRange(addr, l.totalSize)) {
		json.pushArray("macroEncoding");
		for (u32 off = 0; off < l.totalSize; off += 4) {
			json.writeFloat(Memory::Read_Instruction(addr + off).encoding);
		}
		json.pop();
	} else {
		json.writeRaw("macroEncoding", "null");
	}
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
		std::string targetSymbol;
		if (!l.info.isBranchToRegister) {
			targetSymbol = g_symbolMap->GetLabelString(l.info.branchTarget);
			json.writeFloat("targetAddress", l.info.branchTarget);
			json.writeRaw("register", "null");
		} else {
			json.writeRaw("targetAddress", "null");
			json.writeInt("register", l.info.branchRegisterNum);
		}
		json.writeBool("isLinked", l.info.isLinkedBranch);
		json.writeBool("isLikely", l.info.isLikelyBranch);
		if (targetSymbol.empty())
			json.writeRaw("symbol", "null");
		else
			json.writeString("symbol", targetSymbol);
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

// Request the current PSP memory base address (memory.base)
//
// WARNING: Avoid this unless you have a good reason.  Uses PPSSPP's address space.
//
// No parameters.
//
// Response (same event name):
//  - addressHex: string indicating base address in hexadecimal (may be 64 bit.)
void WebSocketDisasmState::Base(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.writeString("addressHex", StringFromFormat("%016llx", Memory::base));
}

// Disassemble a range of memory as CPU instructions (memory.disasm)
//
// Parameters (by count):
//  - address: number specifying the start address.
//  - count: number of lines to return (may be clamped to an internal limit.)
//  - displaySymbols: boolean true to show symbol names in instruction params.
//
// Parameters (by end address):
//  - address: number specifying the start address.
//  - end: number which must be after the start address (may be clamped to an internal limit.)
//  - displaySymbols: boolean true to show symbol names in instruction params.
//
// Response (same event name):
//  - range: object with result "start" and "end" properties, the addresses actually used.
//    (disassembly may have snapped to a nearby instruction.)
//  - branchGuides: array of objects:
//     - top: the earlier address as a number.
//     - bottom: the later address as a number.
//     - direction: "up", "down", or "right" depending on the flow of the branch.
//     - lane: number index to avoid overlapping guides.
//  - lines: array of objects:
//     - type: "opcode", "macro", "data", or "other".
//     - address: address of first actual instruction.
//     - addressSize: bytes used by this line (might be more than 4.)
//     - encoding: uint value of actual instruction (may differ from memory read when using jit.)
//     - macroEncoding: null, or an array of encodings if this line represents multiple instructions.
//     - name: string name of the instruction.
//     - params: formatted parameters for the instruction.
//     - (other info about the disassembled line.)
void WebSocketDisasmState::Disasm(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive()) {
		return req.Fail("CPU not started");
	}

	// In case of client errors, we limit the range to something that won't make us crash.
	static const uint32_t MAX_RANGE = 10000;

	uint32_t start, end;
	if (!req.ParamU32("address", &start))
		return;
	uint32_t count = 0;
	if (!req.ParamU32("count", &count, false, DebuggerParamType::OPTIONAL))
		return;
	if (count != 0) {
		count = std::min(count, MAX_RANGE);
		// Let's assume everything is two instructions.
		disasm_.analyze(start - 4, count * 8 + 8);
		start = disasm_.getStartAddress(start);
		if (start == -1)
			req.ParamU32("address", &start);
		end = disasm_.getNthNextAddress(start, count);
	} else if (req.ParamU32("end", &end)) {
		end = std::min(std::max(start, end), start + MAX_RANGE * 4);
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

		// These are pretty long, so let's grease the wheels a bit.
		if (i % 50 == 0)
			req.Flush();
	}
	json.pop();

	json.pushArray("branchGuides");
	auto branchGuides = disasm_.getBranchLines(start, end - start);
	for (auto bl : branchGuides)
		WriteBranchGuide(json, bl);
	json.pop();
}
