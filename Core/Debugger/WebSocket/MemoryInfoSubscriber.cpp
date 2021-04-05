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

#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/WebSocket/MemoryInfoSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"

class WebSocketMemoryInfoState : public DebuggerSubscriber {
public:
	WebSocketMemoryInfoState() {
	}
	~WebSocketMemoryInfoState() override {
		UpdateOverride(false);
	}

	void Mapping(DebuggerRequest &req);
	void Config(DebuggerRequest &req);

protected:
	void UpdateOverride(bool flag);

	bool detailOverride_ = false;
};

DebuggerSubscriber *WebSocketMemoryInfoInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketMemoryInfoState();
	map["memory.mapping"] = std::bind(&WebSocketMemoryInfoState::Mapping, p, std::placeholders::_1);
	map["memory.info.config"] = std::bind(&WebSocketMemoryInfoState::Config, p, std::placeholders::_1);

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
