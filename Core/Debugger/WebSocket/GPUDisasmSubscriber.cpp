// Copyright (c) 2026- PPSSPP Project.

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

#include "Common/StringUtils.h"
#include "Core/Debugger/WebSocket/GPUDisasmSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/Core.h"
#include "Core/MemMap.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/GPUDebugInterface.h"

DebuggerSubscriber *WebSocketGPUDisasmInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, this is all global (the "gpu" global and raw memory reads).
	map["gpu.displaylist.disasm"] = &WebSocketGPUDisplayListDisasm;
	return nullptr;
}

// Disassemble a range of GE display list memory into named commands (gpu.displaylist.disasm)
//
// GE command words live in normal guest RAM (same as CPU code), so this decodes memory
// directly rather than requiring the CPU/GPU to be paused first - unlike gpu.buffer.*, which
// reads live host-side render target/texture data and does need CORE_STEPPING_CPU.
//
// Parameters (by count):
//  - address: number specifying the start address (a display list address, e.g. from
//    sceGe.cpp's "Starting DL execution at ..." log, or dlid via hle - not a CPU code address).
//  - count: number of GE command words to decode.
//  - compact: optional boolean, default false. See "lines" below.
//
// Parameters (by end address):
//  - address: number specifying the start address.
//  - end: number which must be after the start address (exclusive).
//  - compact: optional boolean, default false. See "lines" below.
//
// Response (same event name):
//  - lines: with compact=false (default), array of objects:
//     - address: address of this command word.
//     - cmd: unsigned integer, the command byte (op >> 24).
//     - op: unsigned integer, the raw 32-bit command word.
//     - desc: string description of the command (name and decoded parameters).
//    with compact=true, array of strings instead, one per command, formatted as
//    "AAAAAAAA  desc" - meant for skimming a display list by eye instead of parsing full JSON,
//    same idea as memory.disasm's own compact mode.
void WebSocketGPUDisplayListDisasm(DebuggerRequest &req) {
	// Mirrors memory.disasm's own limit - keeps a client typo (e.g. count=0xFFFFFFFF) from
	// blocking the debugger connection for an unreasonable amount of time.
	static const uint32_t MAX_RANGE = 10000;

	uint32_t start;
	if (!req.ParamU32("address", &start))
		return;
	uint32_t end;
	uint32_t count = 0;
	if (req.ParamU32("count", &count, false, DebuggerParamType::OPTIONAL) && count != 0) {
		count = std::min(count, MAX_RANGE);
		end = start + count * 4;
	} else if (req.ParamU32("end", &end, false, DebuggerParamType::OPTIONAL)) {
		end = std::max(start, end);
		if (end - start > MAX_RANGE * 4)
			end = start + MAX_RANGE * 4;
	} else {
		return req.Fail("Must specify either 'count' or 'end'");
	}

	bool compact = false;
	if (!req.ParamBool("compact", &compact, DebuggerParamType::OPTIONAL))
		return;

	// gpu and the memory it disassembles from are CPU-thread-owned, so do the read over there
	// rather than from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	bool active = false;
	std::vector<GPUDebugOp> ops;
	Core_RunOnCPUThread([&] {
		active = gpu && Memory::IsActive();
		if (active)
			ops = gpu->DisassembleOpRange(start, end);
	});
	if (!active)
		return req.Fail("No GPU active (game not booted?)");

	JsonWriter &json = req.Respond();
	json.pushArray("lines");
	for (const GPUDebugOp &op : ops) {
		if (compact) {
			json.writeString(StringFromFormat("%08x  %s", op.pc, op.desc.c_str()));
		} else {
			json.pushDict();
			json.writeUint("address", op.pc);
			json.writeUint("cmd", op.cmd);
			json.writeUint("op", op.op);
			json.writeString("desc", op.desc);
			json.pop();
		}
	}
	json.pop();
}
