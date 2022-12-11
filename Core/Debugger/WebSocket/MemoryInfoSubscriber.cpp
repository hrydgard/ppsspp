// Copyright (c) 2021- PPSSPP Project.

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
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/WebSocket/MemoryInfoSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"

class WebSocketMemoryInfoState : public DebuggerSubscriber {
public:
	WebSocketMemoryInfoState() {
	}
	~WebSocketMemoryInfoState() {
		UpdateOverride(false);
	}

	void Mapping(DebuggerRequest &req);
	void Config(DebuggerRequest &req);
	void Set(DebuggerRequest &req);
	void List(DebuggerRequest &req);
	void Search(DebuggerRequest &req);

protected:
	void UpdateOverride(bool flag);

	bool detailOverride_ = false;
};

DebuggerSubscriber *WebSocketMemoryInfoInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketMemoryInfoState();
	map["memory.mapping"] = std::bind(&WebSocketMemoryInfoState::Mapping, p, std::placeholders::_1);
	map["memory.info.config"] = std::bind(&WebSocketMemoryInfoState::Config, p, std::placeholders::_1);
	map["memory.info.set"] = std::bind(&WebSocketMemoryInfoState::Set, p, std::placeholders::_1);
	map["memory.info.list"] = std::bind(&WebSocketMemoryInfoState::List, p, std::placeholders::_1);
	map["memory.info.search"] = std::bind(&WebSocketMemoryInfoState::Search, p, std::placeholders::_1);

	return p;
}

void WebSocketMemoryInfoState::UpdateOverride(bool flag) {
	if (detailOverride_ && !flag)
		MemBlockReleaseDetailed();
	if (!detailOverride_ && flag)
		MemBlockOverrideDetailed();
	detailOverride_ = flag;
}

// List memory map data (memory.mapping)
//
// No parameters.
//
// Response (same event name):
//  - ranges: array of objects:
//     - type: one of "ram", "vram", "sram".
//     - subtype: "primary" or "mirror".
//     - name: string, friendly name.
//     - address: number, start address of range.
//     - size: number, in bytes.
//
// Note: Even if you set false, may stay enabled if set by user or another debug session.
void WebSocketMemoryInfoState::Mapping(DebuggerRequest &req) {
	struct MemRange {
		const char *type;
		const char *subtype;
		const char *name;
		const uint32_t address;
		const uint32_t size;
	};
	constexpr uint32_t kernelMemorySize = PSP_GetKernelMemoryEnd() - PSP_GetKernelMemoryBase();
	constexpr uint32_t volatileMemorySize = PSP_GetVolatileMemoryEnd() - PSP_GetVolatileMemoryStart();
	static constexpr MemRange ranges[] = {
		{ "sram", "primary", "Scratchpad", PSP_GetScratchpadMemoryBase(), Memory::SCRATCHPAD_SIZE },
		{ "vram", "primary", "VRAM", PSP_GetVidMemBase(), Memory::VRAM_SIZE },
		{ "vram", "mirror", "VRAM (Swizzled)", PSP_GetVidMemBase() + Memory::VRAM_SIZE * 1, Memory::VRAM_SIZE },
		{ "vram", "mirror", "VRAM (Mirror)", PSP_GetVidMemBase() + Memory::VRAM_SIZE * 2, Memory::VRAM_SIZE },
		{ "vram", "mirror", "VRAM (Swizzled + Interleaved)", PSP_GetVidMemBase() + Memory::VRAM_SIZE * 3, Memory::VRAM_SIZE },
		{ "ram", "primary", "Kernel Memory", 0x80000000 | PSP_GetKernelMemoryBase(), kernelMemorySize },
		{ "ram", "primary", "Volatile Memory", PSP_GetVolatileMemoryStart(), volatileMemorySize },
		// Size is specially calculated.
		{ "ram", "primary", "User Memory", PSP_GetUserMemoryBase(), 0 },
	};

	JsonWriter &json = req.Respond();
	json.pushArray("ranges");
	for (auto range : ranges) {
		uint32_t size = range.size;
		if (size == 0) {
			size = Memory::g_MemorySize;
			if (size == 0) {
				size = Memory::RAM_NORMAL_SIZE;
			}
			size -= kernelMemorySize + volatileMemorySize;
		}
		json.pushDict();
		json.writeString("type", range.type);
		json.writeString("subtype", range.subtype);
		json.writeString("name", range.name);
		json.writeUint("address", range.address);
		json.writeUint("size", size);
		json.pop();

		// Also write the uncached range.
		json.pushDict();
		json.writeString("type", range.type);
		json.writeString("subtype", "mirror");
		json.writeString("name", std::string("Uncached ") + range.name);
		json.writeUint("address", 0x40000000 | range.address);
		json.writeUint("size", size);
		json.pop();
	}
	json.pop();
}

// Update memory info tracking config (memory.info.config)
//
// Parameters:
//  - detailed: optional, boolean to force enable detailed tracking (perf impact.)
//
// Response (same event name):
//  - detailed: boolean state of tracking before any changes.
//
// Note: Even if you set false, may stay enabled if set by user or another debug session.
void WebSocketMemoryInfoState::Config(DebuggerRequest &req) {
	bool setDetailed = req.HasParam("detailed");
	bool detailed = false;
	if (!req.ParamBool("detailed", &detailed, DebuggerParamType::OPTIONAL))
		return;

	JsonWriter &json = req.Respond();
	json.writeBool("detailed", MemBlockInfoDetailed());

	if (setDetailed)
		UpdateOverride(detailed);
}

static MemBlockFlags FlagFromType(const std::string &type) {
	if (type == "write")
		return MemBlockFlags::WRITE;
	if (type == "texture")
		return MemBlockFlags::TEXTURE;
	if (type == "alloc")
		return MemBlockFlags::ALLOC;
	if (type == "suballoc")
		return MemBlockFlags::SUB_ALLOC;
	if (type == "free")
		return MemBlockFlags::FREE;
	if (type == "subfree")
		return MemBlockFlags::SUB_FREE;
	return MemBlockFlags::SKIP_MEMCHECK;
}

static std::string TypeFromFlag(const MemBlockFlags &flag) {
	if (flag & MemBlockFlags::WRITE)
		return "write";
	else if (flag & MemBlockFlags::TEXTURE)
		return "texture";
	else if (flag & MemBlockFlags::ALLOC)
		return "alloc";
	else if (flag & MemBlockFlags::SUB_ALLOC)
		return "suballoc";
	return "error";
}

// Update memory info tagging (memory.info.set)
//
// Parameters:
//  - address: number representing start address of the range to modify.
//  - size: number, bytes from start address.
//  - type: string, one of:
//     - "write" for last modification information.
//     - "texture" for last texture usage information.
//     - "alloc" for allocation information.
//     - "suballoc" for allocations within an existing allocation.
//     - "free" to mark a previous allocation and its suballocations freed (ignores tag.)
//     - "subfree" to mark a previous suballocation freed (ignores tag.)
//  - tag: string label to give the memory.  Optional if type if free or subfree.
//  - pc: optional, number indicating PC address for this tag.
//
// Response (same event name) with no extra data.
//
// Note: Only one tag per type is maintained for any given memory address.
// Small extent info may be ignored unless detailed tracking enabled (see memory.info.config.)
void WebSocketMemoryInfoState::Set(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	std::string type;
	if (!req.ParamString("type", &type))
		return;
	std::string tag;
	if (type != "free" && type != "subfree") {
		if (!req.ParamString("tag", &tag))
			return;
	}
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;
	uint32_t size;
	if (!req.ParamU32("size", &size))
		return;
	uint32_t pc = currentMIPS->pc;
	if (!req.ParamU32("pc", &pc, false, DebuggerParamType::OPTIONAL))
		return;

	MemBlockFlags flags = MemBlockFlags::SKIP_MEMCHECK | FlagFromType(type);
	if (flags == MemBlockFlags::SKIP_MEMCHECK)
		return req.Fail("Invaid type - expecting write, texture, alloc, suballoc, free, or subfree");

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	else if (!Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	NotifyMemInfoPC(flags, addr, size, pc, tag.c_str(), tag.size());
	req.Respond();
}

// List memory info tags for address range (memory.info.list)
//
// Parameters:
//  - address: number representing start address of the range.
//  - size: number, bytes from start address.
//  - type: optional string to limit information to one of:
//     - "write" for last modification information.
//     - "texture" for last texture usage information.
//     - "alloc" for allocation information.
//     - "suballoc" for allocations within an existing allocation.
//
// Response (same event name):
//  - extents: array of objects:
//     - type: one of the above type string values.
//     - address: number (may be outside requested range if overlapping.)
//     - size: number (may be outside requested range if overlapping.)
//     - ticks: number indicating tick counter as of last tag.
//     - pc: number address of last tag.
//     - tag: string tag for this memory extent.
//     - allocated: boolean, if this extent is marked as allocated (for alloc/suballoc types.)
void WebSocketMemoryInfoState::List(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	std::string type;
	if (!req.ParamString("type", &type, DebuggerParamType::OPTIONAL))
		return;
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;
	uint32_t size;
	if (!req.ParamU32("size", &size))
		return;

	// Allow type to be omitted.
	MemBlockFlags flags = MemBlockFlags::SKIP_MEMCHECK | FlagFromType(type);
	if (flags == MemBlockFlags::SKIP_MEMCHECK && req.HasParam("type"))
		return req.Fail("Invaid type - expecting write, texture, alloc, suballoc, free, or subfree");

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	else if (!Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	std::vector<MemBlockInfo> results;
	if (flags == MemBlockFlags::SKIP_MEMCHECK)
		results = FindMemInfo(addr, size);
	else
		results = FindMemInfoByFlag(flags, addr, size);

	JsonWriter &json = req.Respond();
	json.pushArray("extents");
	for (const auto &result : results) {
		json.pushDict();
		json.writeString("type", TypeFromFlag(result.flags));
		json.writeUint("address", result.start);
		json.writeUint("size", result.size);
		json.writeFloat("ticks", result.ticks);
		json.writeUint("pc", result.pc);
		json.writeString("tag", result.tag);
		json.writeBool("allocated", result.allocated);
		json.pop();
	}
	json.pop();
}

// Search memory info tags for a string (memory.info.search)
//
// Parameters:
//  - address: optional number representing start address of the range.
//  - end: optional end address as a number (otherwise uses start address.)
//  - match: string to search for within tag.
//  - type: optional string to limit information to one of:
//     - "write" for last modification information.
//     - "texture" for last texture usage information.
//     - "alloc" for allocation information.
//     - "suballoc" for allocations within an existing allocation.
//
// Response (same event name):
//  - extent: null, or matching object containing:
//     - type: one of the above type string values.
//     - address: number (may be outside requested range if overlapping.)
//     - size: number (may be outside requested range if overlapping.)
//     - ticks: number indicating tick counter as of last tag.
//     - pc: number address of last tag.
//     - tag: string tag for this memory extent.
//     - allocated: boolean, if this extent is marked as allocated (for alloc/suballoc types.)
//
// Note: may not be fast.
void WebSocketMemoryInfoState::Search(DebuggerRequest &req) {
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	uint32_t start = 0;
	if (!req.ParamU32("address", &start, false, DebuggerParamType::OPTIONAL))
		return;
	uint32_t end = start;
	if (!req.ParamU32("end", &end, false, DebuggerParamType::OPTIONAL))
		return;
	std::string type;
	if (!req.ParamString("type", &type, DebuggerParamType::OPTIONAL))
		return;
	std::string match;
	if (!req.ParamString("match", &match))
		return;

	// Allow type to be omitted.
	MemBlockFlags flags = MemBlockFlags::SKIP_MEMCHECK | FlagFromType(type);
	if (flags == MemBlockFlags::SKIP_MEMCHECK && req.HasParam("type"))
		return req.Fail("Invaid type - expecting write, texture, alloc, suballoc, free, or subfree");

	start = RoundMemAddressUp(start);
	end = RoundMemAddressUp(end);
	std::transform(match.begin(), match.end(), match.begin(), ::tolower);

	bool found = false;
	MemBlockInfo foundResult;

	uint32_t addr = start;
	constexpr uint32_t CHUNK_SIZE = 0x1000;
	do {
		uint32_t chunk_end = addr + CHUNK_SIZE;
		if (addr < end && chunk_end >= end) {
			chunk_end = end;
		}

		std::vector<MemBlockInfo> results;
		if (flags == MemBlockFlags::SKIP_MEMCHECK)
			results = FindMemInfo(addr, chunk_end - addr);
		else
			results = FindMemInfoByFlag(flags, addr, chunk_end - addr);

		for (const auto &result : results) {
			std::string lowercase = result.tag;
			std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(), ::tolower);

			if (lowercase.find(match) != lowercase.npos) {
				found = true;
				foundResult = result;
				break;
			}
		}
		addr = RoundMemAddressUp(chunk_end);
	} while (!found && addr != end);

	JsonWriter &json = req.Respond();
	if (found) {
		json.pushDict("extent");
		json.writeString("type", TypeFromFlag(foundResult.flags));
		json.writeUint("address", foundResult.start);
		json.writeUint("size", foundResult.size);
		json.writeFloat("ticks", foundResult.ticks);
		json.writeUint("pc", foundResult.pc);
		json.writeString("tag", foundResult.tag);
		json.writeBool("allocated", foundResult.allocated);
		json.pop();
	} else {
		json.writeNull("extent");
	}
}
