// Copyright (c) 2016- PPSSPP Project.

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

#include "Core/MIPS/IR/IRAnalysis.h"

// For std::min
#include <algorithm>


static bool IRReadsFrom(const IRInstMeta &inst, int reg, char type, bool *directly) {
	if (inst.m.types[1] == type && inst.src1 == reg) {
		if (directly)
			*directly = true;
		return true;
	}
	if (inst.m.types[2] == type && inst.src2 == reg) {
		if (directly)
			*directly = true;
		return true;
	}
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && inst.m.types[0] == type && inst.src3 == reg) {
		if (directly)
			*directly = true;
		return true;
	}

	if (directly)
		*directly = false;
	if ((inst.m.flags & (IRFLAG_EXIT | IRFLAG_BARRIER)) != 0)
		return true;
	return false;
}

bool IRReadsFromFPR(const IRInstMeta &inst, int reg, bool *directly) {
	if (IRReadsFrom(inst, reg, 'F', directly))
		return true;

	// We also need to check V and 2.  Indirect reads already checked, don't check again.
	if (inst.m.types[1] == 'V' && reg >= inst.src1 && reg < inst.src1 + 4)
		return true;
	if (inst.m.types[1] == '2' && reg >= inst.src1 && reg < inst.src1 + 2)
		return true;
	if (inst.m.types[2] == 'V' && reg >= inst.src2 && reg < inst.src2 + 4)
		return true;
	if (inst.m.types[2] == '2' && reg >= inst.src2 && reg < inst.src2 + 2)
		return true;
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0) {
		if (inst.m.types[0] == 'V' && reg >= inst.src3 && reg <= inst.src3 + 4)
			return true;
		if (inst.m.types[0] == '2' && reg >= inst.src3 && reg <= inst.src3 + 2)
			return true;
	}
	return false;
}

static int IRReadsFromList(const IRInstMeta &inst, IRReg regs[4], char type) {
	int c = 0;

	if (inst.m.types[1] == type)
		regs[c++] = inst.src1;
	if (inst.m.types[2] == type)
		regs[c++] = inst.src2;
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && inst.m.types[0] == type)
		regs[c++] = inst.src3;

	if (inst.op == IROp::Interpret || inst.op == IROp::CallReplacement || inst.op == IROp::Syscall || inst.op == IROp::Break)
		return -1;
	if (inst.op == IROp::Breakpoint || inst.op == IROp::MemoryCheck)
		return -1;

	return c;
}

bool IRReadsFromGPR(const IRInstMeta &inst, int reg, bool *directly) {
	return IRReadsFrom(inst, reg, 'G', directly);
}

int IRDestGPR(const IRInstMeta &inst) {
	if ((inst.m.flags & IRFLAG_SRC3) == 0 && inst.m.types[0] == 'G') {
		return inst.dest;
	}
	return -1;
}

bool IRWritesToGPR(const IRInstMeta &inst, int reg) {
	return IRDestGPR(inst) == reg;
}

bool IRWritesToFPR(const IRInstMeta &inst, int reg) {
	// Doesn't write to anything.
	if ((inst.m.flags & IRFLAG_SRC3) != 0)
		return false;

	if (inst.m.types[0] == 'F' && reg == inst.dest)
		return true;
	if (inst.m.types[0] == 'V' && reg >= inst.dest && reg < inst.dest + 4)
		return true;
	if (inst.m.types[0] == '2' && reg >= inst.dest && reg < inst.dest + 2)
		return true;
	return false;
}

int IRDestFPRs(const IRInstMeta &inst, IRReg regs[4]) {
	// Doesn't write to anything.
	if ((inst.m.flags & IRFLAG_SRC3) != 0)
		return 0;

	if (inst.m.types[0] == 'F') {
		regs[0] = inst.dest;
		return 1;
	}
	if (inst.m.types[0] == 'V') {
		for (int i = 0; i < 4; ++i)
			regs[i] = inst.dest + i;
		return 4;
	}
	if (inst.m.types[0] == '2') {
		for (int i = 0; i < 2; ++i)
			regs[i] = inst.dest + i;
		return 2;
	}
	return 0;
}

int IRReadsFromGPRs(const IRInstMeta &inst, IRReg regs[4]) {
	return IRReadsFromList(inst, regs, 'G');
}

int IRReadsFromFPRs(const IRInstMeta &inst, IRReg regs[16]) {
	int c = IRReadsFromList(inst, regs, 'F');
	if (c != 0)
		return c;

	// We also need to check V and 2.  Indirect reads already checked, don't check again.
	if (inst.m.types[1] == 'V' || inst.m.types[1] == '2') {
		for (int i = 0; i < (inst.m.types[1] == 'V' ? 4 : 2); ++i)
			regs[c++] = inst.src1 + i;
	}
	if (inst.m.types[2] == 'V' || inst.m.types[2] == '2') {
		for (int i = 0; i < (inst.m.types[2] == 'V' ? 4 : 2); ++i)
			regs[c++] = inst.src2 + i;
	}
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0) {
		if (inst.m.types[0] == 'V' || inst.m.types[0] == '2') {
			for (int i = 0; i < (inst.m.types[0] == 'V' ? 4 : 2); ++i)
				regs[c++] = inst.src3 + i;
		}
	}
	return c;
}

IRUsage IRNextGPRUsage(int gpr, const IRSituation &info) {
	// Exclude any "special" regs from this logic for now.
	if (gpr >= 32)
		return IRUsage::UNKNOWN;

	int count = std::min(info.numInstructions - info.currentIndex, info.lookaheadCount);
	for (int i = 0; i < count; ++i) {
		const IRInstMeta inst = GetIRMeta(info.instructions[info.currentIndex + i]);
		if (IRReadsFromGPR(inst, gpr))
			return IRUsage::READ;
		// We say WRITE when the current instruction writes.  It's not useful for spilling.
		if (IRDestGPR(inst) == gpr)
			return i == 0 ? IRUsage::WRITE : IRUsage::CLOBBERED;
	}

	return IRUsage::UNUSED;
}

IRUsage IRNextFPRUsage(int fpr, const IRSituation &info) {
	// Let's only pay attention to standard FP regs and temps.
	// See MIPS.h for these offsets.
	if (fpr < 0 || (fpr >= 160 && fpr < 192) || fpr >= 208)
		return IRUsage::UNKNOWN;

	int count = std::min(info.numInstructions - info.currentIndex, info.lookaheadCount);
	for (int i = 0; i < count; ++i) {
		const IRInstMeta inst = GetIRMeta(info.instructions[info.currentIndex + i]);

		if (IRReadsFromFPR(inst, fpr)) {
			// Special case a broadcast that clobbers it.
			if (inst.op == IROp::Vec4Shuffle && inst.src2 == 0 && inst.dest == inst.src1)
				return inst.src1 == fpr ? IRUsage::READ : IRUsage::CLOBBERED;

			// If this is an exit reading a temp, ignore it.
			if (fpr < IRVTEMP_PFX_S || (GetIRMeta(inst.op)->flags & IRFLAG_EXIT) == 0)
				return IRUsage::READ;
		}
		// We say WRITE when the current instruction writes.  It's not useful for spilling.
		if (IRWritesToFPR(inst, fpr)) {
			return i == 0 ? IRUsage::WRITE : IRUsage::CLOBBERED;
		}
	}

	// This means we only had exits and hit the end.
	if (fpr >= IRVTEMP_PFX_S && count == info.numInstructions - info.currentIndex)
		return IRUsage::CLOBBERED;

	return IRUsage::UNUSED;
}
