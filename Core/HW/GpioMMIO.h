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

// MMIO model of two small PSP hardware register blocks used by kernel-mode code that pokes
// them directly - grouped in one file since uofw documents both together (src/debug/syscon.c
// touches both while implementing the syscon comms protocol), and both are currently just
// plain read/write-back storage defaulting to zero, not real modeled hardware behavior.
//
// GpioMMIO: the GPIO controller (flash0:/kd/lowio.prx's sceGpio_driver pokes this). Base
// address and a couple of register offsets (0x000 port output, 0x040 port direction)
// confirmed against uofw's src/kd/lowio/gpio.c. Everything else - notably offset 0x048, which
// lowio.prx's own initialization polls waiting for bits 0-1 to clear (an interrupt-pending/
// busy-style status register, exact semantics not reverse-engineered) - defaults to zero,
// which is enough to satisfy that poll rather than spinning forever against the generic
// unknown-MMIO poison value (whose bit pattern happens to fail this kind of "wait for a
// status bit to clear" check). See docs/VSHBootInvestigation.md, Attempt 17.
//
// SysconSerialMMIO: the syscon co-processor's serial/UART-style comms interface (offset 0x008
// TX/RX data, 0x00C TX/RX status - bit 2 of status is "RX data available" per uofw). Real
// flash0:/kd/syscon.prx polls status waiting for bits to clear the same way lowio.prx does for
// GPIO; zero-by-default satisfies it. No actual serial protocol is modeled - real Syscon
// communication (battery/power/RTC-alarm state, etc.) would need much more than this to work
// correctly, this only prevents boot from hanging on the initial handshake.

#pragma once

#include "Common/CommonTypes.h"

namespace GpioMMIO {

constexpr u32 BASE_ADDRESS = 0xBE240000;
constexpr u32 SIZE = 0x100;

inline bool IsGpioAddress(u32 address) {
	return address >= BASE_ADDRESS && address < BASE_ADDRESS + SIZE;
}

u32 Read32(u32 address);
void Write32(u32 address, u32 value);

}  // namespace GpioMMIO

namespace SysconSerialMMIO {

constexpr u32 BASE_ADDRESS = 0xBE580000;
constexpr u32 SIZE = 0x100;

inline bool IsSysconSerialAddress(u32 address) {
	return address >= BASE_ADDRESS && address < BASE_ADDRESS + SIZE;
}

u32 Read32(u32 address);
void Write32(u32 address, u32 value);

}  // namespace SysconSerialMMIO
