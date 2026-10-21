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

// The Media Engine, the PSP's second Allegrex. The firmware runs its codecs on it, and homebrew
// whatever it likes. It runs in its own address space (Memory::CreateMESpace()), on the
// interpreter, a slice behind the main CPU: CoreTiming runs it for the cycles the main CPU has just
// used. Only emulated for homebrew, when enabled; commercial games get the HLE codecs.

#pragma once

#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

class PointerWrap;

namespace MeCore {

// The ME's interrupt controller's source for the main CPU's soft interrupt (0xBC100044).
enum : u32 {
	ME_INTR_SOFT = 0x80000000,
};

struct MEState {
	MIPSState cpu;
	// Held in reset until the main CPU releases it.
	bool inReset = true;
	// Stopped by halt, until an interrupt it doesn't mask is raised.
	bool halted = false;
	// The ME's clock is tickBase - cpu.downcount.
	s64 tickBase = 0;
	// mfic/mtic's interrupt enable, which is per CPU.
	u32 interruptEnable = 0;

	// Its interrupt controller (0xBC300000): raised sources, and the ones that reach the CPU (IP2).
	u32 intcRaised = 0;
	u32 intcEnabled = 0;

	// System registers (0xBC100000): those the ME code we run uses.
	u32 resetFlags = 0;      // 0x4C. Bit 2 holds the ME in reset.
	u32 busClock = 0;        // 0x50
	u32 nmiFlags = 0;        // 0x04
	u32 ramConfig = 0;       // 0x40. Bits 0-1: RAM size, 16 << n MB.
	u32 mutex = 0;           // 0x48. Bit 0 is the main CPU's, bit 1 the ME's.
	u32 memProtect[20]{};    // 0xBC000000-0xBC00004C

	// DMACplus (0xBC800000), which moves data between the CPUs' memories, and drives the LCD and the
	// colour space conversion. Only what homebrew's ME code uses.
	struct Dmacplus {
		u32 intTCStatus = 0;       // 0x04, channels that finished with their interrupt bit set
		u32 lcd[4]{};              // 0x100: framebuffer, pixel format, width, stride
		u32 csc[0x11]{};           // 0x120-0x160: colour space conversion
		u32 channels[4][5]{};      // 0x180-0x1FF: source, destination, next LLI, control, configuration
	} dmacplus;

	// Not saved: whether the code at an address is a wait loop (see IsWaitLoop), and where it ends.
	// 0 for not one. Cleared when the ME's code changes.
	std::unordered_map<u32, u32> waitLoopEnds;
	// Waiting for the main CPU, for the rest of this RunFor.
	bool waiting = false;
};

// Null when the ME isn't emulated.
extern MEState *g_meState;

// Whether the config and this boot (homebrew) want the ME.
bool Wanted(bool homebrew);
// Creates the ME, in reset. Fails on platforms without an ME address space.
bool Init();
void Shutdown();
// Creates or removes the ME to match the state.
void DoState(PointerWrap &p);

// Runs the ME for the cycles the main CPU has just run.
void RunFor(int cycles);
// Asserting reset stops the ME. Releasing it starts the ME at the reset vector.
void SetReset(bool asserted);
// The ME's clock.
u64 GetTicks();

// COP0 registers with behaviour on the ME: Compare (11), Cause (13).
u32 ReadCop0(int reg);
void WriteCop0(int reg, u32 value);
void Halt();

// Raises sources in the ME's interrupt controller.
void RaiseInterrupt(u32 sources);
// The ME interrupts the main CPU (it wrote 0xBC100044).
void InterruptMainCPU();

}  // namespace MeCore
