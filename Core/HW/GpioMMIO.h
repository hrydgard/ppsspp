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
// implements the syscon comms protocol using both blocks together: GPIO pins 3/4 as a
// hardware "go"/"ready" handshake around the actual data transfer on the serial block).
//
// GpioMMIO: the GPIO controller (flash0:/kd/lowio.prx's sceGpio_driver pokes this, and
// syscon.prx's own comms handshake uses pins 3/4 directly). Base address and register
// offsets (0x000 port output, 0x004 port read, 0x008 port set, 0x00C port clear, 0x040 port
// direction, 0x010/0x014/0x018 interrupt mode, 0x020 interrupt-triggered status, 0x024
// interrupt acknowledge) confirmed against uofw's src/kd/lowio/gpio.c and src/debug/syscon.c.
// Plain read/write-back storage for anything not listed above (notably offset 0x048, which
// lowio.prx's own initialization polls waiting for bits 0-1 to clear - exact semantics not
// reverse-engineered, zero-by-default is enough to satisfy it) - not real modeled hardware
// behavior, just enough to avoid spinning against the generic unknown-MMIO poison value
// (whose bit pattern never satisfies this kind of "wait for a status bit to clear/set" poll).
// See docs/VSHBootInvestigation.md, Attempts 17-18.
//
// SysconSerialMMIO: the syscon co-processor's serial/UART-style comms interface used by
// flash0:/kd/syscon.prx's Syscon_cmd() (uofw src/debug/syscon.c) to exchange command/response
// packets with the real Syscon chip. Real hardware runs actual NEC 78K0 firmware on the other
// end (see e.g. JPCSP's jpcsp/memory/mmio/syscon/*.java, jpcsp/nec78k0/* - a full second CPU
// emulator, gated on a real, separately-dumped firmware binary most users don't have and
// JPCSP itself falls back from when absent). This implements the command/response protocol
// itself (packet framing, checksum) faithfully, but answers the small set of commands
// vsh_module's boot path is known to need (NOP, read/write clock, read/write alarm) with
// synthesized PPSSPP-side state rather than anything from real Syscon hardware - mirrors
// JPCSP's own "SysconEmulator.isEnabled() == false" fallback path
// (MMIOHandlerSyscon.startSysconCmd()'s `else` branch) more than real hardware.

#pragma once

#include "Common/CommonTypes.h"

namespace GpioMMIO {

constexpr u32 BASE_ADDRESS = 0xBE240000;
constexpr u32 SIZE = 0x100;

// Register offset of the "interrupt triggered" status word (what syscon.prx's Syscon_cmd()
// polls, along with all interrupt-capable GPIO pins - only pin 4, the syscon "response ready"
// line, is ever actually driven here since that's the only one this MMIO model needs).
constexpr u32 REG_INTR_STATUS = 0x020;

inline bool IsGpioAddress(u32 address) {
	return address >= BASE_ADDRESS && address < BASE_ADDRESS + SIZE;
}

u32 Read32(u32 address);
void Write32(u32 address, u32 value);

// Sets bits in the interrupt-triggered status register (offset 0x020) - used by
// SysconSerialMMIO to signal "response ready" on GPIO pin 4 the same way real Syscon
// hardware would (via the actual GPIO4 line raising, per uofw's Syscon_cmd()).
void SetInterruptStatusBits(u32 bits);

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
