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

// JIT replacefuncs can be for inline or "outline" replacement.
// With inline replacement, we recognize the call to the functions
// at jal time already. With outline replacement, we just replace the
// implementation.

// In both cases the jit needs to know how much to subtract downcount.
//
// If the replacement func returned a positive number, this will be treated
// as the number of cycles to subtract.
// If the replacement func returns -1, it will be assumed that the subtraction
// was done by the replacement func.

#pragma once

#include "Common/CommonTypes.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

typedef int (* ReplaceFunc)();

enum {
	REPFLAG_ALLOWINLINE = 1,
	// Note that this will re-execute in a funciton that loops at start.
	REPFLAG_HOOKENTER = 2,
	// Only hooks jr ra, so only use on funcs that have that.
	REPFLAG_HOOKEXIT = 4,
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
int GetReplacementFuncIndex(u64 hash, int funcSize);
const ReplacementTableEntry *GetReplacementFunc(int index);

void WriteReplaceInstructions(u32 address, u64 hash, int size);
void RestoreReplacedInstruction(u32 address);
void RestoreReplacedInstructions(u32 startAddr, u32 endAddr);
bool GetReplacedOpAt(u32 address, u32 *op);

// For savestates.  If you call SaveAndClearReplacements(), you must call RestoreSavedReplacements().
std::map<u32, u32> SaveAndClearReplacements();
void RestoreSavedReplacements(const std::map<u32, u32> &saved);
