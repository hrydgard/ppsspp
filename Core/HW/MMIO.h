// Copyright (c) 2012- PPSSPP Project.

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

// Memory-mapped hardware registers, for kernel code that pokes the hardware directly (the firmware
// modules the VSH boot runs). Each block of registers is a device covering an address range; the
// interpreter sends an access there (Memory::IsMMIOAccess) to the device, or logs it as unhandled.

#pragma once

#include "Common/CommonTypes.h"

class MIPSState;
class PointerWrap;

class MMIODevice {
public:
	MMIODevice(const char *name, u32 base, u32 size) : name_(name), base_(base), size_(size) {}
	virtual ~MMIODevice() {}

	// offset is from the base. size is 1, 2 or 4. The CPU doing the access is passed for registers
	// that answer each CPU differently. Returns false for an access the device doesn't handle.
	virtual bool Read(MIPSState *mips, u32 offset, int size, u32 *value) = 0;
	virtual bool Write(MIPSState *mips, u32 offset, int size, u32 value) = 0;
	// To the power-on state.
	virtual void Reset() = 0;
	virtual void DoState(PointerWrap &p) = 0;

	const char *Name() const { return name_; }
	bool Contains(u32 address) const { return address - base_ < size_; }
	u32 Base() const { return base_; }

private:
	const char *name_;
	u32 base_;
	u32 size_;
};

namespace MMIO {

// What an unhandled read returns. Distinctive (not 0) so it's obvious in a live trace when some later
// computation's input traces back to an unimplemented register, rather than looking like an
// ordinary, legitimate zero.
constexpr u32 UNHANDLED_READ = 0x1337BEEF;

// Registers the devices, in their power-on state.
void Init();
void Shutdown();
void DoState(PointerWrap &p);

// For an address Memory::IsMMIOAccess() accepts, from kernel code (the caller checks). size is 1, 2
// or 4. An access no device handles is logged, and reads as UNHANDLED_READ.
u32 Read(MIPSState *mips, u32 address, int size);
void Write(MIPSState *mips, u32 address, int size, u32 value);

}  // namespace MMIO
