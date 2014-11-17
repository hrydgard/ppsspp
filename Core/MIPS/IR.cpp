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

#include <Windows.h>

#include <algorithm>

#include "Core/MIPS/IR.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSCodeUtils.h"

void ExtractIR(u32 address, IRBlock *block) {
	block->entries.clear();

	block->analysis = MIPSAnalyst::Analyze(address);

	// TODO: This loop could easily follow branches and whatnot, perform inlining and so on.

	int exitInInstructions = -1;
	while (true) {
		IREntry e;
		MIPSOpcode op = Memory::ReadUnchecked_Instruction(address, false);
		e.origAddress = address;
		e.op = op;
		e.info = MIPSGetInfo(op);
		block->entries.push_back(e);

		if (e.info & DELAYSLOT) {
			exitInInstructions = 2;
		}

		address += 4;

		if (exitInInstructions > 0)
			exitInInstructions--;
		if (exitInInstructions == 0)
			break;
	}

	// Okay, now let's work backwards and compute liveness information.

	// TODO: By following calling conventions etc, it may be possible to eliminate
	// additional register liveness from "jr ra" upwards. However, this is not guaranteed to work on all games.

	u64 gprLiveness = 0xFFFFFFFFF;  // note - nine Fs
	u32 fprLiveness = 0xFFFFFFFF;
	for (int i = block->entries.size() - 1; i >= 0; i--) {
		IREntry &e = block->entries[i];
		e.liveGPR = gprLiveness;
		e.liveFPR = fprLiveness;

		//if (e.info & OUT_RT) {
		//	int rt = 
		//}
	}

	/*
	for (int i = 0; i < (int)block->entries.size(); i++) {
		IREntry &e = block->entries[i];
		char buffer[256];
		MIPSDisAsm(e.op, e.origAddress, buffer, true);
		OutputDebugStringA(buffer);
		OutputDebugStringA("\n");
	}
	OutputDebugStringA("===\n");
	*/
}

IRBlock::RegisterUsage IRBlock::DetermineInOutUsage(u64 inFlag, u64 outFlag, int pos, int instrs) {
	const u32 start = pos;
	u32 end = pos + instrs;
	bool canClobber = true;
	while (pos < end) {
		const MIPSOpcode op = entries[pos].op;
		const MIPSInfo info = entries[pos].info;

		// Yes, used.
		if (info & inFlag)
			return USAGE_INPUT;

		// Clobbered, so not used.
		if (info & outFlag)
			return canClobber ? USAGE_CLOBBERED : USAGE_UNKNOWN;

		// Bail early if we hit a branch (could follow each path for continuing?)
		if ((info & IS_CONDBRANCH) || (info & IS_JUMP)) {
			// Still need to check the delay slot (so end after it.)
			// We'll assume likely are taken.
			end = pos + 2;
			// The reason for the start != addr check is that we compile delay slots before branches.
			// That means if we're starting at the branch, it's not safe to allow the delay slot
			// to clobber, since it might have already been compiled.
			// As for LIKELY, we don't know if it'll run the branch or not.
			canClobber = (info & LIKELY) == 0 && start != pos;
		}
		pos++;
	}
	return USAGE_UNKNOWN;
}

IRBlock::RegisterUsage IRBlock::DetermineRegisterUsage(MIPSGPReg reg, int pos, int instrs) {
	switch (reg) {
	case MIPS_REG_HI:
		return DetermineInOutUsage(IN_HI, OUT_HI, pos, instrs);
	case MIPS_REG_LO:
		return DetermineInOutUsage(IN_LO, OUT_LO, pos, instrs);
	case MIPS_REG_FPCOND:
		return DetermineInOutUsage(IN_FPUFLAG, OUT_FPUFLAG, pos, instrs);
	case MIPS_REG_VFPUCC:
		return DetermineInOutUsage(IN_VFPU_CC, OUT_VFPU_CC, pos, instrs);
	default:
		break;
	}

	if (reg > 32) {
		return USAGE_UNKNOWN;
	}

	const u32 start = pos;
	int end = pos + instrs;
	if (end > entries.size())
		end = entries.size();
	bool canClobber = true;
	while (pos < end) {
		const MIPSOpcode op = entries[pos].op;
		const MIPSInfo info = entries[pos].info;

		// Yes, used.
		if ((info & IN_RS) && (MIPS_GET_RS(op) == reg))
			return USAGE_INPUT;
		if ((info & IN_RT) && (MIPS_GET_RT(op) == reg))
			return USAGE_INPUT;

		// Clobbered, so not used.
		bool clobbered = false;
		if ((info & OUT_RT) && (MIPS_GET_RT(op) == reg))
			clobbered = true;
		if ((info & OUT_RD) && (MIPS_GET_RD(op) == reg))
			clobbered = true;
		if ((info & OUT_RA) && (reg == MIPS_REG_RA))
			clobbered = true;
		if (clobbered) {
			if (!canClobber || (info & IS_CONDMOVE))
				return USAGE_UNKNOWN;
			return USAGE_CLOBBERED;
		}

		// Bail early if we hit a branch (could follow each path for continuing?)
		if ((info & IS_CONDBRANCH) || (info & IS_JUMP)) {
			// Still need to check the delay slot (so end after it.)
			// We'll assume likely are taken.
			end = pos + 2;
			// The reason for the start != addr check is that we compile delay slots before branches.
			// That means if we're starting at the branch, it's not safe to allow the delay slot
			// to clobber, since it might have already been compiled.
			// As for LIKELY, we don't know if it'll run the branch or not.
			canClobber = (info & LIKELY) == 0 && start != pos;
		}
		pos++;
	}
	return USAGE_UNKNOWN;
}

bool IRBlock::IsRegisterUsed(MIPSGPReg reg, int pos, int instrs) {
	return DetermineRegisterUsage(reg, pos, instrs) == USAGE_INPUT;
}

bool IRBlock::IsRegisterClobbered(MIPSGPReg reg, int pos, int instrs) {
	return DetermineRegisterUsage(reg, pos, instrs) == USAGE_CLOBBERED;
}

const char *IRBlock::DisasmAt(int pos) {
	static char temp[256];
	MIPSDisAsm(entries[pos].op, 0, temp);
	return temp;
}
