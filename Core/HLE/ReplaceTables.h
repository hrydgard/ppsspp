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


// Regular replacement funcs are just C functions. These take care of their
// own parameter parsing using the old school PARAM macros.
// The return value is the number of cycles to eat.

// Replacement functions are replaced by function hash, also checking the size to reduce
// collisions. This is not really super safe, and we probably should restrict them by
// game ID, really...

// JIT replacefuncs can be for inline or "outline" replacement.
// With inline replacement, we recognize the call to the functions
// at jal time already. With outline replacement, we just replace the
// implementation, which gets jumped to from other functions.

// In both cases the jit needs to know how much to subtract downcount.
//
// If the replacement func returned a positive number, this will be treated
// as the number of cycles to subtract.
// If the replacement func returns -1, it will be assumed that the subtraction
// was done by the replacement func.

#pragma once

#include <map>

#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

typedef int (* ReplaceFunc)();

enum {
	// Used to keep things around but disable them.
	REPFLAG_DISABLED = 0x02,
	// Note that this will re-execute in a function that loops at start.
	REPFLAG_HOOKENTER = 0x04,
	// Only hooks jr ra, so only use on funcs that have that.
	REPFLAG_HOOKEXIT = 0x08,
	// Function may take a lot of time and execute in slices (executed multiple times.)
	REPFLAG_SLICED = 0x10,
};

// Kind of similar to HLE functions but with different data.
struct ReplacementTableEntry {
	const char *name;
	ReplaceFunc replaceFunc;
	MIPSComp::MIPSReplaceFunc jitReplaceFunc;
	int flags;
	s32 hookOffset;
};

void Replacement_Init();
void Replacement_Shutdown();

int GetNumReplacementFuncs();
std::vector<int> GetReplacementFuncIndexes(u64 hash, int funcSize);
const ReplacementTableEntry *GetReplacementFunc(size_t index);

// Installed replacements never touch PSP memory. A hooked instruction gets a nonzero block shadow
// entry: the JIT's block there, or the hook value if there's none. The CPU cores look up every
// instruction with a nonzero entry when they run or compile it, and substitute the CallRepl
// pseudo-op.
void WriteReplaceInstructions(u32 address, u64 hash, int size);
void RestoreReplacedInstruction(u32 address);
void RestoreReplacedInstructions(u32 startAddr, u32 endAddr);
// Drops hooks in functions the game has changed. Called when the icache is invalidated over them.
void Replacement_CheckRange(u32 address, u32 length);

// The JIT's hook value, which its dispatcher sends to the compiler. Rewrites the hooks' entries.
void Replacement_SetBlockShadowHook(u32 value);
u32 Replacement_GetBlockShadowHook();
bool Replacement_IsHooked(u32 address);

// Only call for a nonzero block shadow entry. Returns the CallRepl pseudo-op, or the instruction in
// memory if there's no hook there or the game has overwritten the hooked instruction (which also
// drops the hook). Address must be valid.
MIPSOpcode GetReplacementOpAt(u32 address);

// What the CPU runs at a valid address: the CallRepl pseudo-op if hooked, otherwise memory.
inline MIPSOpcode ReadExecutedOp(u32 address) {
	if (*Memory::GetBlockShadowEntry(address) != 0) {
		return GetReplacementOpAt(address);
	}
	return Memory::ReadUnchecked_Instruction(address);
}

