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
#include <string>

#include "Common/CommonTypes.h"

#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/JitCommon/JitState.h"


// MIPS IR
// A very pragmatic intermediate representation for the recompilers.
// It's essentially just the MIPS instructions, but with an option to also have
// pseudo operations. Some MIPS instructions decompose into these for easier optimization.

// The main goals are:
//  * Compute accurate liveness information so that some redundant computations can be removed
//  * Move instructions around to permit various peephole optimizations
//  * Unfold delay slots into sequential instructions for ease of optimization
//  * 

// Flags
enum {
	IR_FLAG_SKIP = 1,

	// To be used on unfolded branches
	IR_FLAG_NO_DELAY_SLOT = 2,
	IR_FLAG_CMP_REPLACE_LEFT = 4,
	IR_FLAG_CMP_REPLACE_RIGHT = 8,
};

enum {
	PSEUDO_NONE,
	PSEUDO_SAVE_RA,
};

// Keep this as small as possible!
struct IREntry {
	u32 origAddress;  // Note - doesn't have to be contiguous.
	MIPSInfo info;  // not strictly needed as can be recomputed but speeds things up considerably so worth the space
	MIPSOpcode op;
	u32 flags;
	int pseudoInstr;  // 0 = no pseudo. Could be combined with flags?

	// We include LO, HI, VFPUCC, FPUFlag as mapped GPRs.
	u64 gprIn;
	u64 gprOut;
	u32 fprIn;
	u32 fprOut;

	// Register live state, as bitfields.
	u64 liveGPR;  // Bigger than 32 to accommodate pseudo-GPRs like HI and LO
	u32 liveFPR;

	// Clobbered state. Can discard registers marked as clobbered later.
	u64 clobberedGPR;
	u64 clobberedFPR;

	// u32 liveVPR[4];  // TODO: For now we assume all VPRs are live at all times.

	void MakeNOP() { op.encoding = 0; info = 0; }
	void MakePseudo(int pseudo) { pseudoInstr = pseudo; info = 0; }

	bool IsGPRAlive(int reg) const { return (liveGPR & (1ULL << reg)) != 0; }
	bool IsFPRAlive(int freg) const { return (liveFPR & (1UL << freg)) != 0; }
	bool IsGPRClobbered(int reg) const { return (clobberedGPR & (1ULL << reg)) != 0; }
	bool IsFPRClobbered(int freg) const { return (clobberedFPR & (1UL << freg)) != 0; }
};

namespace MIPSComp {

class IRBlock {
public:
	u32 address;
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

	std::vector<std::string> ToStringVector();
	IREntry &AddIREntry(u32 address);
	void RemoveLast();
};

void ExtractIR(const JitOptions &jo, u32 address, IRBlock *block);

}
