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
#include <cstring>
#include <mutex>
#include "Common/Data/Encoding/Base64.h"
#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Debugger/WebSocket/MemorySubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Reporting.h"
#include "Core/System.h"

DebuggerSubscriber *WebSocketMemoryInit(DebuggerEventHandlerMap &map) {
	// No need to bind or alloc state, these are all global.
	map["memory.read_u8"] = &WebSocketMemoryReadU8;
	map["memory.read_u16"] = &WebSocketMemoryReadU16;
	map["memory.read_u32"] = &WebSocketMemoryReadU32;
	map["memory.read"] = &WebSocketMemoryRead;
	map["memory.readString"] = &WebSocketMemoryReadString;
	map["memory.write_u8"] = &WebSocketMemoryWriteU8;
	map["memory.write_u16"] = &WebSocketMemoryWriteU16;
	map["memory.write_u32"] = &WebSocketMemoryWriteU32;
	map["memory.write"] = &WebSocketMemoryWrite;

	return nullptr;
}

struct AutoDisabledReplacements {
	AutoDisabledReplacements() {}
	AutoDisabledReplacements(AutoDisabledReplacements &&other);
	AutoDisabledReplacements(const AutoDisabledReplacements &) = delete;
	AutoDisabledReplacements &operator =(const AutoDisabledReplacements &) = delete;
	~AutoDisabledReplacements();

	Memory::MemoryInitedLock *lock = nullptr;
	std::map<u32, u32> replacements;
	std::vector<u32> emuhacks;
	bool saved = false;
	bool wasStepping = false;
};

// Important: Only use keepReplacements when reading, not writing.
static AutoDisabledReplacements LockMemoryAndCPU(uint32_t addr, bool keepReplacements) {
	AutoDisabledReplacements result;
	if (Core_IsStepping()) {
		result.wasStepping = true;
	} else {
		Core_EnableStepping(true, "memory.access", addr);
		Core_WaitInactive();
	}

	result.lock = new Memory::MemoryInitedLock();
	if (!keepReplacements) {
		result.saved = true;
		// Okay, save so we can restore later.
		result.replacements = SaveAndClearReplacements();
		std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
		if (MIPSComp::jit)
			result.emuhacks = MIPSComp::jit->SaveAndClearEmuHackOps();
	}
	return result;
}

AutoDisabledReplacements::AutoDisabledReplacements(AutoDisabledReplacements &&other) {
	lock = other.lock;
	other.lock = nullptr;
	replacements = std::move(other.replacements);
	emuhacks = std::move(other.emuhacks);
	saved = other.saved;
	other.saved = false;
	wasStepping = other.wasStepping;
	other.wasStepping = true;
}

AutoDisabledReplacements::~AutoDisabledReplacements() {
	if (saved) {
		std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
		if (MIPSComp::jit)
			MIPSComp::jit->RestoreSavedEmuHackOps(emuhacks);
		RestoreSavedReplacements(replacements);
	}
	if (!wasStepping)
		Core_EnableStepping(false);
	delete lock;
}

// Read a byte from memory (memory.read_u8)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
void WebSocketMemoryReadU8(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

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
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

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
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U32(addr));
}

// Read bytes from memory (memory.read)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - size: unsigned integer specifying size of memory range.
//  - replacements: optional, false to ignore PPSSPP replacements in MIPS code.
//
// Response (same event name):
//  - base64: base64 encode of binary data.
void WebSocketMemoryRead(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;
	uint32_t size;
	if (!req.ParamU32("size", &size))
		return;
	bool replacements = true;
	if (!req.ParamBool("replacements", &replacements, DebuggerParamType::OPTIONAL))
		return;

	auto memLock = LockMemoryAndCPU(addr, replacements);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	else if (!Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	JsonWriter &json = req.Respond();
	// Start a value without any actual data yet...
	json.writeRaw("base64", "");
	req.Flush();

	// Now we'll write it directly to the stream.
	req.ws->AddFragment(false, "\"");
	// 65535 is an "even" number of base64 characters.
	static const size_t CHUNK_SIZE = 65535;
	for (size_t i = 0; i < size; i += CHUNK_SIZE) {
		size_t left = std::min(size - i, CHUNK_SIZE);
		req.ws->AddFragment(false, Base64Encode(Memory::GetPointerUnchecked(addr) + i, left));
	}
	req.ws->AddFragment(false, "\"");
}

// Read a NUL terminated string from memory (memory.readString)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - type: optional, 'utf-8' (default) or 'base64'.
//
// Response (same event name) for 'utf8':
//  - value: string value read.
//
// Response (same event name) for 'base64':
//  - base64: base64 encode of binary data, not including NUL.
void WebSocketMemoryReadString(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	std::string type = "utf-8";
	if (!req.ParamString("type", &type, DebuggerParamType::OPTIONAL))
		return;
	if (type != "utf-8" && type != "base64")
		return req.Fail("Invalid type, must be either utf-8 or base64");

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");

	// Let's try to avoid crashing and get a safe length.
	const uint8_t *p = Memory::GetPointerUnchecked(addr);
	size_t longest = Memory::ValidSize(addr, Memory::g_MemorySize);
	size_t len = strnlen((const char *)p, longest);

	JsonWriter &json = req.Respond();
	if (type == "utf-8") {
		json.writeString("value", std::string((const char *)p, len));
	} else if (type == "base64") {
		json.writeString("base64", Base64Encode(p, len));
	}
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
	uint32_t addr, val;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}
	if (!req.ParamU32("value", &val, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	currentMIPS->InvalidateICache(addr, 1);
	Memory::Write_U8(val, addr);
	Reporting::NotifyDebugger();

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
	uint32_t addr, val;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}
	if (!req.ParamU32("value", &val, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	currentMIPS->InvalidateICache(addr, 2);
	Memory::Write_U16(val, addr);
	Reporting::NotifyDebugger();

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
	uint32_t addr, val;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}
	if (!req.ParamU32("value", &val, false)) {
		return;
	}

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	if (!Memory::IsValidAddress(addr)) {
		req.Fail("Invalid address");
		return;
	}
	currentMIPS->InvalidateICache(addr, 4);
	Memory::Write_U32(val, addr);
	Reporting::NotifyDebugger();

	JsonWriter &json = req.Respond();
	json.writeUint("value", Memory::Read_U32(addr));
}

// Write bytes to memory (memory.write)
//
// Parameters:
//  - address: unsigned integer address for the start of the memory range.
//  - base64: data to write, encoded as base64 string
//
// Response (same event name) with no extra data.
void WebSocketMemoryWrite(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;
	std::string encoded;
	if (!req.ParamString("base64", &encoded))
		return;

	auto memLock = LockMemoryAndCPU(addr, true);
	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	std::vector<uint8_t> value = Base64Decode(&encoded[0], encoded.size());
	uint32_t size = (uint32_t)value.size();

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	else if (value.size() != (size_t)size || !Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	currentMIPS->InvalidateICache(addr, size);
	Memory::MemcpyUnchecked(addr, &value[0], size);
	Reporting::NotifyDebugger();
	req.Respond();
}
