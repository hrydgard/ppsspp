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
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Debugger/WebSocket/MemorySubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

DebuggerSubscriber *WebSocketMemoryInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["memory.read_u8"] = &WebSocketMemoryReadU8;
	map["memory.read_u16"] = &WebSocketMemoryReadU16;
	map["memory.read_u32"] = &WebSocketMemoryReadU32;
	map["memory.write_u8"] = &WebSocketMemoryWriteU8;
	map["memory.write_u16"] = &WebSocketMemoryWriteU16;
	map["memory.write_u32"] = &WebSocketMemoryWriteU32;

	return nullptr;
}

// Read a byte from memory (memory.read_u8)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
void WebSocketMemoryReadU8(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U8(addr));
}

// Read two bytes from memory (memory.read_u16)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
void WebSocketMemoryReadU16(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U16(addr));
}

// Read four bytes from memory (memory.read_u32)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
void WebSocketMemoryReadU32(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U32(addr));
}

// Write a byte to memory (memory.write_u8)
//
// Parameters:
//  - address: unsigned integer
//  - value: unsigned integer
//
// Response (same event name):
//  - value: new value, unsigned integer
void WebSocketMemoryWriteU8(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr, val;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!req.ParamU32("value", &val, false)) {
		req.Fail("No value given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	Memory::Write_U8(val, addr);

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U8(addr));
}

// Write two bytes to memory (memory.write_u16)
//
// Parameters:
//  - address: unsigned integer
//  - value: unsigned integer
//
// Response (same event name):
//  - value: new value, unsigned integer
void WebSocketMemoryWriteU16(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr, val;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!req.ParamU32("value", &val, false)) {
		req.Fail("No value given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	Memory::Write_U16(val, addr);

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U16(addr));
}

// Write four bytes to memory (memory.write_u32)
//
// Parameters:
//  - address: unsigned integer
//  - value: unsigned integer
//
// Response (same event name):
//  - value: new value, unsigned integer
void WebSocketMemoryWriteU32(DebuggerRequest &req) {
	auto memLock = Memory::Lock();
	uint32_t addr, val;

	if (!req.ParamU32("address", &addr, false)) {
		req.Fail("No address given");
		return;
	}

	if (!req.ParamU32("value", &val, false)) {
		req.Fail("No value given");
		return;
	}

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	Memory::Write_U32(val, addr);

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U32(addr));
}
