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
#include <cctype>

#include "Common/Data/Encoding/Utf8.h"

#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/WebSocket/DisasmSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAsm.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Reporting.h"

class WebSocketDisasmState : public DebuggerSubscriber {
public:
	WebSocketDisasmState() {
		g_disassemblyManager.setCpu(currentDebugMIPS);
	}
	~WebSocketDisasmState() {
		g_disassemblyManager.clear();
	}

	void Base(DebuggerRequest &req);
	void Disasm(DebuggerRequest &req);
	void SearchDisasm(DebuggerRequest &req);
	void Assemble(DebuggerRequest &req);

protected:
	void WriteDisasmLine(JsonWriter &json, const DisassemblyLineInfo &l);
	void WriteBranchGuide(JsonWriter &json, const BranchLine &l);
	std::string FormatDisasmLineCompact(const DisassemblyLineInfo &l, DebugInterface *cpuDebug);
};

DebuggerSubscriber *WebSocketDisasmInit(DebuggerEventHandlerMap &map) {
	WebSocketDisasmState *p = new WebSocketDisasmState();
	map["memory.base"] = [p](DebuggerRequest &req) { p->Base(req); };
	map["memory.disasm"] = [p](DebuggerRequest &req) { p->Disasm(req); };
	map["memory.searchDisasm"] = [p](DebuggerRequest &req) { p->SearchDisasm(req); };
	map["memory.assemble"] = [p](DebuggerRequest &req) { p->Assemble(req); };
	return p;
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

void WebSocketDisasmState::WriteDisasmLine(JsonWriter &json, const DisassemblyLineInfo &l) {
	u32 addr = l.info.opcodeAddress;
	json.pushDict();
	if (l.type == DISTYPE_OPCODE)
		json.writeString("type", "opcode");
	else if (l.type == DISTYPE_DATA)
		json.writeString("type", "data");
	else if (l.type == DISTYPE_OTHER)
		json.writeString("type", "other");

	json.writeUint("address", addr);
	json.writeInt("addressSize", l.totalSize);
	json.writeUint("encoding", Memory::IsValidAddress(addr) ? Memory::Read_Instruction(addr).encoding : 0);
	if (l.totalSize >= 8 && Memory::IsValidRange(addr, l.totalSize)) {
		json.pushArray("macroEncoding");
		for (u32 off = 0; off < l.totalSize; off += 4) {
			json.writeUint(Memory::Read_Instruction(addr + off).encoding);
		}
		json.pop();
	} else {
		json.writeNull("macroEncoding");
	}
	int c = currentDebugMIPS->getColor(addr, false) & 0x00FFFFFF;
	json.writeString("backgroundColor", StringFromFormat("#%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, c >> 16));
	json.writeString("name", l.name);
	json.writeString("params", l.params);

	const std::string addressSymbol = g_symbolMap->GetLabelString(addr);
	if (addressSymbol.empty())
		json.writeNull("symbol");
	else
		json.writeString("symbol", addressSymbol);

	const u32 funcAddress = g_symbolMap->GetFunctionStart(addr);
	const std::string funcName = g_symbolMap->GetLabelString(funcAddress);
	if (funcName.empty())
		json.writeNull("function");
	else
		json.writeString("function", funcName);

	if (l.type == DISTYPE_DATA) {
		u32 dataStart = g_symbolMap->GetDataStart(addr);
		if (dataStart == -1)
			dataStart = addr;
		const std::string dataLabel = g_symbolMap->GetLabelString(dataStart);

		json.pushDict("dataSymbol");
		json.writeUint("start", dataStart);
		if (dataLabel.empty())
			json.writeNull("label");
		else
			json.writeString("label", dataLabel);
		json.pop();
	} else {
		json.writeNull("dataSymbol");
	}

	bool enabled = false;
	int breakpointOffset = -1;
	for (u32 i = 0; i < l.totalSize; i += 4) {
		if (g_breakpoints.IsAddressBreakPoint(addr + i, &enabled))
			breakpointOffset = i;
		if (breakpointOffset != -1 && enabled)
			break;
	}
	// TODO: Account for bp inside macro?
	if (breakpointOffset != -1) {
		json.pushDict("breakpoint");
		json.writeBool("enabled", enabled);
		json.writeUint("address", addr + breakpointOffset);
		BreakPointCond *cond = g_breakpoints.GetBreakPointCondition(addr + breakpointOffset);
		if (cond)
			json.writeString("condition", cond->expressionString);
		else
			json.writeNull("condition");
		json.pop();
	} else {
		json.writeNull("breakpoint");
	}

	// This is always the current execution's PC.
	json.writeBool("isCurrentPC", currentDebugMIPS->GetPC() == addr);
	if (l.info.isBranch) {
		json.pushDict("branch");
		std::string targetSymbol;
		if (!l.info.isBranchToRegister) {
			targetSymbol = g_symbolMap->GetLabelString(l.info.branchTarget);
			json.writeUint("targetAddress", l.info.branchTarget);
			json.writeNull("register");
		} else {
			json.writeNull("targetAddress");
			json.writeInt("register", l.info.branchRegisterNum);
		}
		json.writeBool("isLinked", l.info.isLinkedBranch);
		json.writeBool("isLikely", l.info.isLikelyBranch);
		if (targetSymbol.empty())
			json.writeNull("symbol");
		else
			json.writeString("symbol", targetSymbol);
		json.pop();
	} else {
		json.writeNull("branch");
	}

	if (l.info.hasRelevantAddress) {
		json.pushDict("relevantData");
		json.writeUint("address", l.info.relevantAddress);
		if (Memory::IsValidRange(l.info.relevantAddress, 4))
			json.writeUint("uintValue", Memory::ReadUnchecked_U32(l.info.relevantAddress));
		else
			json.writeNull("uintValue");
		if (IsLikelyStringAt(l.info.relevantAddress))
			json.writeString("stringValue", Memory::GetCharPointer(l.info.relevantAddress));
		else
			json.writeNull("stringValue");
		json.pop();
	} else {
		json.writeNull("relevantData");
	}

	if (l.info.isConditional)
		json.writeBool("conditionMet", l.info.conditionMet);
	else
		json.writeNull("conditionMet");

	if (l.info.isDataAccess) {
		json.pushDict("dataAccess");
		json.writeUint("address", l.info.dataAddress);
		json.writeInt("size", l.info.dataSize);
		
		std::string dataSymbol = g_symbolMap->GetLabelString(l.info.dataAddress);
		std::string valueSymbol;
		if (!Memory::IsValidRange(l.info.dataAddress, l.info.dataSize))
			json.writeNull("uintValue");
		else if (l.info.dataSize == 1)
			json.writeUint("uintValue", Memory::ReadUnchecked_U8(l.info.dataAddress));
		else if (l.info.dataSize == 2)
			json.writeUint("uintValue", Memory::ReadUnchecked_U16(l.info.dataAddress));
		else if (l.info.dataSize >= 4) {
			u32 data = Memory::ReadUnchecked_U32(l.info.dataAddress);
			valueSymbol = g_symbolMap->GetLabelString(data);
			json.writeUint("uintValue", data);
		}

		if (!dataSymbol.empty())
			json.writeString("symbol", dataSymbol);
		else
			json.writeNull("symbol");
		if (!valueSymbol.empty())
			json.writeString("valueSymbol", valueSymbol);
		else
			json.writeNull("valueSymbol");
		json.pop();
	} else {
		json.writeNull("dataAccess");
	}

	json.pop();
}

// One line of "ADDR  name params" text, with a leading marker column ('>' = current PC,
// '*' = enabled breakpoint here, 'o' = disabled breakpoint here) and the symbol name inlined
// if known. Meant for compact=true: full-JSON disasm lines (WriteDisasmLine above, ~15 fields
// each) are precise but slow to skim by hand - every manual disassembly read this session (see
// docs/VSHBootInvestigation.md) ended up needing a throwaway script to reduce them to exactly
// this, and at least once that script's own parsing bug produced misleading output. Doing the
// reduction here instead means there's one correct implementation instead of a new ad hoc one
// each time.
std::string WebSocketDisasmState::FormatDisasmLineCompact(const DisassemblyLineInfo &l, DebugInterface *cpuDebug) {
	u32 addr = l.info.opcodeAddress;
	bool enabled = false;
	bool hasBreakpoint = false;
	for (u32 i = 0; i < l.totalSize; i += 4) {
		if (g_breakpoints.IsAddressBreakPoint(addr + i, &enabled)) {
			hasBreakpoint = true;
			if (enabled)
				break;
		}
	}

	char marker = ' ';
	if (cpuDebug->GetPC() == addr)
		marker = '>';
	else if (hasBreakpoint)
		marker = enabled ? '*' : 'o';

	std::string line = StringFromFormat("%c %08x  ", marker, addr);
	const std::string symbol = g_symbolMap->GetLabelString(addr);
	if (!symbol.empty()) {
		line += symbol;
		line += ": ";
	}
	line += l.name;
	if (!l.params.empty()) {
		line += " ";
		line += l.params;
	}
	return line;
}

void WebSocketDisasmState::WriteBranchGuide(JsonWriter &json, const BranchLine &l) {
	json.pushDict();
	json.writeUint("top", l.first);
	json.writeUint("bottom", l.second);
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
	Reporting::NotifyDebugger();
	json.writeString("addressHex", StringFromFormat("%016llx", (uint64_t)(uintptr_t)Memory::base));
}

// Disassemble a range of memory as CPU instructions (memory.disasm)
//
// Parameters (by count):
//  - thread: optional number indicating the thread id for branch info.
//  - address: number specifying the start address.
//  - count: number of lines to return (may be clamped to an internal limit.)
//  - displaySymbols: boolean true to show symbol names in instruction params.
//  - compact: optional boolean, default false. See "lines" below.
//
// Parameters (by end address):
//  - thread: optional number indicating the thread id for branch info.
//  - address: number specifying the start address.
//  - end: number which must be after the start address (may be clamped to an internal limit.)
//  - displaySymbols: boolean true to show symbol names in instruction params.
//  - compact: optional boolean, default false. See "lines" below.
//
// Response (same event name):
//  - range: object with result "start" and "end" properties, the addresses actually used.
//    (disassembly may have snapped to a nearby instruction.)
//  - branchGuides: array of objects:
//     - top: the earlier address as a number.
//     - bottom: the later address as a number.
//     - direction: "up", "down", or "right" depending on the flow of the branch.
//     - lane: number index to avoid overlapping guides.
//  - lines: with compact=false (default), array of objects:
//     - type: "opcode", "macro", "data", or "other".
//     - address: address of first actual instruction.
//     - addressSize: bytes used by this line (might be more than 4.)
//     - encoding: uint value of actual instruction (may differ from memory read when using jit.)
//     - macroEncoding: null, or an array of encodings if this line represents multiple instructions.
//     - name: string name of the instruction.
//     - params: formatted parameters for the instruction.
//     - (other info about the disassembled line.)
//    with compact=true, array of strings instead, one per line, formatted as
//    "M AAAAAAAA  [symbol: ]name params" where M is '>' for the current PC, '*'/'o' for an
//    enabled/disabled breakpoint at that address, or ' ' otherwise - meant for skimming a
//    range by eye by hand instead of parsing the full per-field JSON.
void WebSocketDisasmState::Disasm(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	// In case of client errors, we limit the range to something that won't make us crash.
	static const uint32_t MAX_RANGE = 10000;

	// Route the disassembly manager/symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		DebugInterface *cpuDebug = CPUFromRequest(req);
		if (!cpuDebug)
			return;

		uint32_t start, end;
		if (!req.ParamU32("address", &start))
			return;
		uint32_t count = 0;
		if (!req.ParamU32("count", &count, false, DebuggerParamType::OPTIONAL))
			return;
		if (count != 0) {
			count = std::min(count, MAX_RANGE);
			// Let's assume everything is two instructions.
			g_disassemblyManager.analyze(start - 4, count * 8 + 8);
			start = g_disassemblyManager.getStartAddress(start);
			if (start == -1)
				req.ParamU32("address", &start);
			end = g_disassemblyManager.getNthNextAddress(start, count);
		} else if (req.ParamU32("end", &end)) {
			end = std::max(start, end);
			if (end - start > MAX_RANGE * 4)
				end = start + MAX_RANGE * 4;
			// Let's assume everything is two instructions at most.
			g_disassemblyManager.analyze(start - 4, end - start + 8);
			start = g_disassemblyManager.getStartAddress(start);
			if (start == -1)
				req.ParamU32("address", &start);
			// Correct end and calculate count based on it.
			// This accounts for macros as one line, although two instructions.
			u32 stop = end;
			u32 next = start;
			count = 0;
			if (stop < start) {
				for (next = start; next > stop; next = g_disassemblyManager.getNthNextAddress(next, 1)) {
					count++;
				}
			}
			for (end = next; end < stop && end >= next; end = g_disassemblyManager.getNthNextAddress(end, 1)) {
				count++;
			}
		} else {
			// Error message already sent.
			return;
		}

		bool displaySymbols = true;
		if (!req.ParamBool("displaySymbols", &displaySymbols, DebuggerParamType::OPTIONAL))
			return;
		bool compact = false;
		if (!req.ParamBool("compact", &compact, DebuggerParamType::OPTIONAL))
			return;

		JsonWriter &json = req.Respond();
		json.pushDict("range");
		json.writeUint("start", start);
		json.writeUint("end", end);
		json.pop();

		json.pushArray("lines");
		DisassemblyLineInfo line;
		uint32_t addr = start;
		for (uint32_t i = 0; i < count; ++i) {
			g_disassemblyManager.getLine(addr, displaySymbols, line, cpuDebug);
			if (compact)
				json.writeString(FormatDisasmLineCompact(line, cpuDebug));
			else
				WriteDisasmLine(json, line);
			addr += line.totalSize;

			// These are pretty long, so let's grease the wheels a bit.
			if (i % 50 == 0)
				req.Flush();
		}
		json.pop();

		json.pushArray("branchGuides");
		std::vector<BranchLine> branchGuides = g_disassemblyManager.getBranchLines(start, end - start);
		for (const BranchLine &bl : branchGuides)
			WriteBranchGuide(json, bl);
		json.pop();
	});
}

// Search disassembly for some text (memory.searchDisasm)
//
// Parameters:
//  - thread: optional number indicating the thread id (may not affect search much.)
//  - address: starting address as a number.
//  - end: optional end address as a number (otherwise uses start address.)
//  - match: string to search for.
//  - displaySymbols: optional, specify false to hide symbols in the searched parameters.
//  - findAll: optional boolean, default false. When true, scans the whole range instead of
//    stopping at the first match - e.g. for "every jal/jalr/j targeting this address" call-graph
//    style queries (search for the target's hex address, or its symbol name if it has one),
//    where the first match alone isn't the answer. Capped at 1000 results; a huge range with a
//    very common match (e.g. an empty/near-universal 'match' string) will still stop there
//    rather than building an unbounded response.
//
// Response (same event name):
//  - address: number address of the first match, or null if none was found (same as before
//    findAll existed - set from the same scan even when findAll is true).
//  - addresses: array of numbers, every match found. With findAll=false this has at most one
//    entry (the same value as "address"); with findAll=true, up to 1000.
void WebSocketDisasmState::SearchDisasm(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	uint32_t start;
	if (!req.ParamU32("address", &start))
		return;
	uint32_t end = start;
	if (!req.ParamU32("end", &end, false, DebuggerParamType::OPTIONAL))
		return;
	std::string match;
	if (!req.ParamString("match", &match))
		return;
	bool displaySymbols = true;
	if (!req.ParamBool("displaySymbols", &displaySymbols, DebuggerParamType::OPTIONAL))
		return;
	bool findAll = false;
	if (!req.ParamBool("findAll", &findAll, DebuggerParamType::OPTIONAL))
		return;
	static const size_t MAX_RESULTS = 1000;

	bool loopSearch = end <= start;
	start = RoundMemAddressUp(start);
	if ((end <= start) != loopSearch) {
		// We must've passed end by rounding up.
		JsonWriter &json = req.Respond();
		json.writeNull("address");
		json.pushArray("addresses");
		json.pop();
		return;
	}

	// We do this after the check in case both were in unused memory.
	end = RoundMemAddressUp(end);

	std::transform(match.begin(), match.end(), match.begin(), ::tolower);

	// Route the disassembly manager/symbol reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	// Note: for a very large [start, end) range, this scan itself can take a while - unlike memory.search
	// there's no size cap here, so a huge range will block the CPU thread's own frame pump for its duration.
	Core_RunOnCPUThread([&] {
		DebugInterface *cpuDebug = CPUFromRequest(req);
		if (!cpuDebug)
			return;

		DisassemblyLineInfo line;
		std::vector<uint32_t> matches;
		uint32_t addr = start;
		do {
			g_disassemblyManager.getLine(addr, displaySymbols, line, cpuDebug);
			const std::string addressSymbol = g_symbolMap->GetLabelString(addr);

			std::string mergeForSearch;
			// Address+space (9) + symbol + colon+space (2) + name + space(1) + params = 12 fixed size worst case.
			mergeForSearch.resize(12 + addressSymbol.size() + line.name.size() + line.params.size());

			sprintf(&mergeForSearch[0], "%08x ", addr);
			std::string::iterator inserter = mergeForSearch.begin() + 9;
			if (!addressSymbol.empty()) {
				inserter = std::transform(addressSymbol.begin(), addressSymbol.end(), inserter, ::tolower);
				*inserter++ = ':';
				*inserter++ = ' ';
			}
			inserter = std::transform(line.name.begin(), line.name.end(), inserter, ::tolower);
			*inserter++ = ' ';
			inserter = std::transform(line.params.begin(), line.params.end(), inserter, ::tolower);

			if (mergeForSearch.find(match) != mergeForSearch.npos) {
				matches.push_back(addr);
				if (!findAll || matches.size() >= MAX_RESULTS)
					break;
			}

			addr = RoundMemAddressUp(addr + line.totalSize);
		} while (addr != end);

		JsonWriter &json = req.Respond();
		if (!matches.empty())
			json.writeUint("address", matches[0]);
		else
			json.writeNull("address");
		json.pushArray("addresses");
		for (uint32_t m : matches)
			json.writeUint(m);
		json.pop();
	});
}

// Assemble an instruction (memory.assemble)
//
// Parameters:
//  - address: number indicating the address to write to.
//  - code: string containing the instruction to assemble.
//
// Response (same event name):
//  - encoding: resulting encoding at this address.  Always returns one value, even for macros.
void WebSocketDisasmState::Assemble(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive()) {
		return req.Fail("CPU not started");
	}

	uint32_t address;
	if (!req.ParamU32("address", &address))
		return;
	std::string code;
	if (!req.ParamString("code", &code))
		return;

	// Route the actual code assembly to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	std::string error;
	uint32_t encoding = 0;
	bool ok = false;
	Core_RunOnCPUThread([&] {
		ok = MipsAssembleOpcode(code, currentDebugMIPS, address, &error);
		if (ok)
			encoding = Memory::Read_Instruction(address).encoding;
	});

	if (!ok) {
		return req.Fail(StringFromFormat("Could not assemble: %s", error.c_str()));
	}

	JsonWriter &json = req.Respond();
	Reporting::NotifyDebugger();
	json.writeUint("encoding", encoding);
}
