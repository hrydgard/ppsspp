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

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

class PointerWrap;

namespace MeCore {

struct MEState {
	MIPSState cpu;
	// Held in reset until the main CPU releases it.
	bool inReset = true;
	// The ME's clock is tickBase - cpu.downcount.
	s64 tickBase = 0;
	// mfic/mtic's interrupt enable, which is per CPU.
	u32 interruptEnable = 0;
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
// Count (COP0 register 9) on the ME.
u64 GetTicks();

}  // namespace MeCore
