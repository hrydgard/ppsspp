#pragma once

// Copyright (c) 2014- PPSSPP Project.

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

#include <vector>

#include "Common/CommonTypes.h"

#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"


// MIPS IR
// A very pragmatic intermediate representation for the recompilers.
// It's essentially just the MIPS instructions, but with an option to also have
// pseudo operations. Some MIPS instructions decompose into these for easier optimization.

// The main goals are:
//  * Compute accurate liveness information so that some redundant computations can be removed
//  * Move instructions around to permit various peephole optimizations
//  * Unfold delay slots into sequential instructions for ease of optimization
//  * 


struct IREntry {
	u32 origAddress;  // Note - doesn't have to be contiguous.
	MIPSInfo info;
	MIPSOpcode op;
	// u32 flags;
	// u32 pseudoInst;

	// Register live state, as bitfields.
	u64 liveGPR;  // Bigger than 32 to accommodate pseudo-GPRs like HI and LO
	u32 liveFPR;
	// u32 liveVPR[4];  // TODO: For now we assume all VPRs are live at all times.
};

class IRBlock {
public:
	std::vector<IREntry> entries;
	MIPSAnalyst::AnalysisResults analysis;

	enum RegisterUsage {
		USAGE_CLOBBERED,
		USAGE_INPUT,
		USAGE_UNKNOWN,
	};

	RegisterUsage DetermineInOutUsage(u64 inFlag, u64 outFlag, int pos, int instrs);
	RegisterUsage DetermineRegisterUsage(MIPSGPReg reg, int pos, int instrs);

	// This tells us if the reg is used within instrs of addr (also includes likely delay slots.)
	bool IsRegisterUsed(MIPSGPReg reg, int pos, int instrs);
	bool IsRegisterClobbered(MIPSGPReg reg, int pos, int instrs);

	// TODO: Change this awful interface
	const char *DisasmAt(int pos);
};

void ExtractIR(u32 address, IRBlock *block);