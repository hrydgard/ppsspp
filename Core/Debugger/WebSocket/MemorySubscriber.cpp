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
#include "Common/Data/Encoding/Utf8.h"
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
	map["memory.search"] = &WebSocketMemorySearch;

	return nullptr;
}

struct AutoDisabledReplacements {
	AutoDisabledReplacements() {}
	AutoDisabledReplacements(AutoDisabledReplacements &&other);
	AutoDisabledReplacements(const AutoDisabledReplacements &) = delete;
	AutoDisabledReplacements &operator =(const AutoDisabledReplacements &) = delete;
	~AutoDisabledReplacements();

	std::map<u32, u32> replacements;
	std::vector<u32> emuhacks;
	bool saved = false;
};

// Call this from within a Core_RunOnCPUThread() callback - see Core_RunOnCPUThread() in Core.h.
// No longer needs to pause a running CPU to do this safely: we're already running on the CPU thread
// by the time this is called, so nothing else can be concurrently executing MIPS code or touching the
// JIT's emuhack ops on this thread while we hold onto them below.
//
// Deliberately does NOT take a CoreShutdownLock: memory teardown only ever happens on the
// CPU thread too, so there's nothing to guard against, and taking it here deadlocked against the
// Win32 debugger's paint handlers. See the lock ordering section in AGENTS.md.
//
// Important: Only use keepReplacements=false when reading, not writing.
static AutoDisabledReplacements LockMemory(bool keepReplacements) {
	AutoDisabledReplacements result;
	if (!keepReplacements) {
		result.saved = true;
		// Okay, save so we can restore later.
		result.replacements = SaveAndClearReplacements();
		if (MIPSComp::jit) {
			result.emuhacks = MIPSComp::jit->SaveAndClearEmuHackOps();
		}
	}
	return result;
}

AutoDisabledReplacements::AutoDisabledReplacements(AutoDisabledReplacements &&other) {
	replacements = std::move(other.replacements);
	emuhacks = std::move(other.emuhacks);
	saved = other.saved;
	other.saved = false;
}

AutoDisabledReplacements::~AutoDisabledReplacements() {
	if (saved) {
		if (MIPSComp::jit)
			MIPSComp::jit->RestoreSavedEmuHackOps(emuhacks);
		RestoreSavedReplacements(replacements);
	}
}

// Read a byte from memory (memory.read_u8)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
//  - uintValue: the same number under the name cpu.getReg/cpu.getAllRegs use, so a client can
//    read either without special-casing which event it came from
void WebSocketMemoryReadU8(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");

	// Route the actual memory read to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U8(addr));
		// Alias: cpu.getReg and cpu.getAllRegs call this uintValue. Same number, both names.
		json.writeUint("uintValue", Memory::ReadUnchecked_U8(addr));
	});
}

// Read two bytes from memory (memory.read_u16)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
//  - uintValue: the same number under the name cpu.getReg/cpu.getAllRegs use, so a client can
//    read either without special-casing which event it came from
void WebSocketMemoryReadU16(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, 2))
		return req.Fail("Invalid address");

	// Route the actual memory read to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U16(addr));
		json.writeUint("uintValue", Memory::ReadUnchecked_U16(addr));
	});
}

// Read four bytes from memory (memory.read_u32)
//
// Parameters:
//  - address: unsigned integer
//
// Response (same event name):
//  - value: unsigned integer
//  - uintValue: the same number under the name cpu.getReg/cpu.getAllRegs use, so a client can
//    read either without special-casing which event it came from
void WebSocketMemoryReadU32(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr, false)) {
		return;
	}

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, 4))
		return req.Fail("Invalid address");

	// Route the actual memory read to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U32(addr));
		json.writeUint("uintValue", Memory::ReadUnchecked_U32(addr));
	});
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

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr/size, not on anything CPU-thread-owned, so fail fast here rather
	// than making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	// Route the actual memory read to the CPU thread instead of poking at it directly from this
	// WebSocket handler thread - see Core_RunOnCPUThread() in Core.h. Only the raw copy (which
	// needs replacements/emuhacks disabled) happens on the CPU thread - the base64 encoding itself
	// happens back on this WebSocket thread afterward, so it doesn't block the CPU thread's frame
	// pump for a large 'size'.
	std::vector<uint8_t> raw(size);
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(replacements);
		if (size != 0)
			memcpy(raw.data(), Memory::GetPointerUnchecked(addr), size);
	});

	JsonWriter &json = req.Respond();
	// Start a value without any actual data yet...
	json.writeRaw("base64", "");
	req.Flush();

	// Now we'll write it directly to the stream.
	req.ws->AddFragment(false, "\"");
	// 65535 is an "even" number of base64 characters.
	static const size_t CHUNK_SIZE = 65535;
	for (size_t i = 0; i < raw.size(); i += CHUNK_SIZE) {
		size_t left = std::min(raw.size() - i, CHUNK_SIZE);
		req.ws->AddFragment(false, Base64Encode(&raw[i], left));
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
//  - value: string value read.  Since this reads arbitrary emulated memory, which is under no
//    obligation to hold text at all, any byte sequence that isn't valid UTF-8 is replaced with
//    U+FFFD.  Use 'base64' if you need the bytes exactly as they are.
//
// Response (same event name) for 'base64':
//  - base64: base64 encode of binary data, not including NUL.
void WebSocketMemoryReadString(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	std::string type = "utf-8";
	if (!req.ParamString("type", &type, DebuggerParamType::OPTIONAL))
		return;
	if (type != "utf-8" && type != "base64")
		return req.Fail("Invalid type, must be either utf-8 or base64");

	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");

	// Route the actual memory read to the CPU thread instead of poking at it directly from this
	// WebSocket handler thread - see Core_RunOnCPUThread() in Core.h. Only the raw copy happens on
	// the CPU thread - the base64 encoding itself happens back on this WebSocket thread afterward.
	std::string raw;
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		// Let's try to avoid crashing and get a safe length.
		const uint8_t *p = Memory::GetPointerUnchecked(addr);
		size_t longest = Memory::ClampValidSizeAt(addr, Memory::g_MemorySize);
		size_t len = strnlen((const char *)p, longest);
		raw.assign((const char *)p, len);
	});

	JsonWriter &json = req.Respond();
	if (type == "utf-8") {
		// Must not go out raw: WebSocket text frames are required to be valid UTF-8, so a stray
		// byte from some non-text address would make a conforming client (browsers included) drop
		// the connection - taking down the whole debugger session over one bad read.
		json.writeString("value", ReplaceInvalidUTF8(raw));
	} else if (type == "base64") {
		json.writeString("base64", Base64Encode((const uint8_t *)raw.data(), raw.size()));
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

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");

	// Route the actual memory write to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		Memory::WriteUnchecked_U8(val, addr);
		currentMIPS->InvalidateICacheRangeDeferred(addr, 1);
		Reporting::NotifyDebugger();

		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U8(addr));
		// Alias: cpu.getReg and cpu.getAllRegs call this uintValue. Same number, both names.
		json.writeUint("uintValue", Memory::ReadUnchecked_U8(addr));
	});
}

// Write two bytes to memory (memory.write_u16)
//
// Parameters:
//  - address: unsigned integer (can be unaligned! But not recommended. Should maybe disallow).
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

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, 2))
		return req.Fail("Invalid address");

	// Route the actual memory write to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		Memory::WriteUnchecked_U16(val, addr);
		currentMIPS->InvalidateICacheRangeDeferred(addr, 2);
		Reporting::NotifyDebugger();

		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U16(addr));
		json.writeUint("uintValue", Memory::ReadUnchecked_U16(addr));
	});
}

// Write four bytes to memory (memory.write_u32)
//
// Parameters:
//  - address: unsigned integer (can be unaligned! But not recommended. Should maybe disallow).
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

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr, not on anything CPU-thread-owned, so fail fast here rather than
	// making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidRange(addr, 4))
		return req.Fail("Invalid address");

	// Route the actual memory write to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		Memory::WriteUnchecked_U32(val, addr);
		currentMIPS->InvalidateICacheRangeDeferred(addr, 4);
		Reporting::NotifyDebugger();

		JsonWriter &json = req.Respond();
		json.writeUint("value", Memory::ReadUnchecked_U32(addr));
		json.writeUint("uintValue", Memory::ReadUnchecked_U32(addr));
	});
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

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");

	// Decoding doesn't touch anything CPU-thread-owned either, so do it - and the address/size
	// validation that depends on it - here, before we bother queuing to the CPU thread at all.
	std::vector<uint8_t> value = Base64Decode(&encoded[0], encoded.size());
	uint32_t size = (uint32_t)value.size();

	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	if (value.size() != (size_t)size || !Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	// Route the actual memory write to the CPU thread instead of poking at it directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);
		currentMIPS->InvalidateICacheRangeDeferred(addr, size);
		if (size != 0) {
			Memory::MemcpyUnchecked(addr, &value[0], size);
		}
		Reporting::NotifyDebugger();
		req.Respond();
	});
}

// Search memory for a value or byte pattern (memory.search)
//
// Useful for reverse engineering - e.g. narrowing down where a known value (health,
// ammo, a position) lives, or finding a byte signature.
//
// Parameters:
//  - address: unsigned integer address for the start of the range to search.
//  - size: unsigned integer size in bytes of the range to search.
//  - type: string, one of 'u8', 'u16', 'u32', 'float', or 'bytes'.
//  - value: for 'u8'/'u16'/'u32', an unsigned integer to match exactly.
//    For 'float', a JSON string (e.g. "1.5") - use a string so integers aren't confused
//    with floats, same convention as cpu.setReg.
//  - base64: for 'bytes', the byte pattern to match, base64 encoded.
//  - maskBase64: optional for 'bytes', base64 encoded, same length as 'base64'.  A 0x00
//    byte means "don't care" at that position, any other byte value means "must match
//    exactly" at that position.  Defaults to matching every byte of 'base64' exactly.
//  - align: optional unsigned integer, only check offsets from 'address' that are a
//    multiple of this many bytes.  Defaults to the size of 'type' in bytes (or 1 for
//    'bytes'.)
//  - maxResults: optional unsigned integer, stop after this many matches (default 1000,
//    hard cap 100000.)
//
// Response (same event name):
//  - matches: array of unsigned integer addresses where a match was found.
//  - truncated: boolean, true if 'maxResults' was hit before the whole range was searched.
void WebSocketMemorySearch(DebuggerRequest &req) {
	uint32_t addr;
	if (!req.ParamU32("address", &addr))
		return;
	uint32_t size;
	if (!req.ParamU32("size", &size))
		return;
	std::string type;
	if (!req.ParamString("type", &type))
		return;

	if (!currentDebugMIPS->isAlive() || !Memory::IsActive())
		return req.Fail("CPU not started");
	// This only depends on addr/size, not on anything CPU-thread-owned, so fail fast here rather
	// than making a round trip through the queue for a request we already know is invalid.
	if (!Memory::IsValidAddress(addr))
		return req.Fail("Invalid address");
	if (!Memory::IsValidRange(addr, size))
		return req.Fail("Invalid size");

	// None of the rest of the parameter parsing/validation below touches anything CPU-thread-owned
	// either, so it all happens here too, before we bother queuing to the CPU thread at all.
	uint32_t align = 1;
	uint32_t needleSize = 0;
	uint32_t needleValue = 0;
	std::vector<uint8_t> needleBytes;
	std::vector<uint8_t> maskBytes;

	if (type == "u8" || type == "u16" || type == "u32") {
		needleSize = type == "u8" ? 1 : type == "u16" ? 2 : 4;
		align = needleSize;
		if (!req.ParamU32("value", &needleValue, false))
			return;
	} else if (type == "float") {
		needleSize = 4;
		align = 4;
		// allowFloatBits: accepts a string like "1.5" and gives us its raw bit pattern.
		if (!req.ParamU32("value", &needleValue, true))
			return;
	} else if (type == "bytes") {
		std::string encoded;
		if (!req.ParamString("base64", &encoded))
			return;
		needleBytes = Base64Decode(&encoded[0], encoded.size());
		if (needleBytes.empty())
			return req.Fail("'base64' must decode to at least one byte");
		needleSize = (uint32_t)needleBytes.size();
		align = 1;

		if (req.HasParam("maskBase64")) {
			std::string maskEncoded;
			if (!req.ParamString("maskBase64", &maskEncoded))
				return;
			maskBytes = Base64Decode(&maskEncoded[0], maskEncoded.size());
			if (maskBytes.size() != needleBytes.size())
				return req.Fail("'maskBase64' must decode to the same length as 'base64'");
		}
	} else {
		return req.Fail("Invalid 'type', must be u8, u16, u32, float, or bytes");
	}

	if (needleSize > size)
		return req.Fail("'size' is smaller than the pattern/value being searched for");

	if (!req.ParamU32("align", &align, false, DebuggerParamType::OPTIONAL))
		return;
	if (align == 0)
		return req.Fail("'align' must not be zero");

	uint32_t maxResults = 1000;
	if (!req.ParamU32("maxResults", &maxResults, false, DebuggerParamType::OPTIONAL))
		return;
	if (maxResults == 0)
		maxResults = 1000;
	else if (maxResults > 100000)
		maxResults = 100000;

	// Route the actual memory scan to the CPU thread instead of poking at it directly from this
	// WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	// Note: 'size' has no cap beyond valid memory range, so a very large scan will still block the
	// CPU thread's own frame pump for its duration - at least the parameter validation above no
	// longer costs a round trip through the queue first.
	Core_RunOnCPUThread([&] {
		AutoDisabledReplacements memLock = LockMemory(true);

		const uint8_t *base = Memory::GetPointerUnchecked(addr);
		std::vector<uint32_t> matches;
		bool truncated = false;
		for (uint32_t offset = 0; offset + needleSize <= size; offset += align) {
			bool match;
			if (!needleBytes.empty()) {
				match = true;
				for (uint32_t i = 0; i < needleSize; ++i) {
					uint8_t mask = maskBytes.empty() ? 0xFF : maskBytes[i];
					if ((base[offset + i] & mask) != (needleBytes[i] & mask)) {
						match = false;
						break;
					}
				}
			} else {
				uint32_t actual = base[offset];
				if (needleSize >= 2)
					actual |= base[offset + 1] << 8;
				if (needleSize >= 4)
					actual |= (base[offset + 2] << 16) | (base[offset + 3] << 24);
				match = actual == needleValue;
			}

			if (match) {
				if (matches.size() >= maxResults) {
					truncated = true;
					break;
				}
				matches.push_back(addr + offset);
			}
		}

		JsonWriter &json = req.Respond();
		json.pushArray("matches");
		for (uint32_t m : matches)
			json.writeUint(m);
		json.pop();
		json.writeBool("truncated", truncated);
	});
}
