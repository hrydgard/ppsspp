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


static bool IRReadsFrom(const IRInst &inst, int reg, char type, bool directly = false) {
	const IRMeta *m = GetIRMeta(inst.op);

	if (m->types[1] == type && inst.src1 == reg) {
		return true;
	}
	if (m->types[2] == type && inst.src2 == reg) {
		return true;
	}
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && m->types[0] == type && inst.src3 == reg) {
		return true;
	}
	if (!directly) {
		if (inst.op == IROp::Interpret || inst.op == IROp::CallReplacement || inst.op == IROp::Syscall || inst.op == IROp::Break)
			return true;
		if (inst.op == IROp::Breakpoint || inst.op == IROp::MemoryCheck)
			return true;
	}
	return false;
}

bool IRReadsFromFPR(const IRInst &inst, int reg, bool directly) {
	if (IRReadsFrom(inst, reg, 'F', directly))
		return true;

	const IRMeta *m = GetIRMeta(inst.op);

	// We also need to check V and 2.  Indirect reads already checked, don't check again.
	if (m->types[1] == 'V' && reg >= inst.src1 && reg < inst.src1 + 4)
		return true;
	if (m->types[1] == '2' && reg >= inst.src1 && reg < inst.src1 + 2)
		return true;
	if (m->types[2] == 'V' && reg >= inst.src2 && reg < inst.src2 + 4)
		return true;
	if (m->types[2] == '2' && reg >= inst.src2 && reg < inst.src2 + 2)
		return true;
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0) {
		if (m->types[0] == 'V' && reg >= inst.src3 && reg <= inst.src3 + 4)
			return true;
		if (m->types[0] == '2' && reg >= inst.src3 && reg <= inst.src3 + 2)
			return true;
	}
	return false;
}

static int IRReadsFromList(const IRInst &inst, IRReg regs[4], char type) {
	const IRMeta *m = GetIRMeta(inst.op);
	int c = 0;

	if (m->types[1] == type)
		regs[c++] = inst.src1;
	if (m->types[2] == type)
		regs[c++] = inst.src2;
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && m->types[0] == type)
		regs[c++] = inst.src3;

	if (inst.op == IROp::Interpret || inst.op == IROp::CallReplacement || inst.op == IROp::Syscall || inst.op == IROp::Break)
		return -1;
	if (inst.op == IROp::Breakpoint || inst.op == IROp::MemoryCheck)
		return -1;

	return c;
}

bool IRReadsFromGPR(const IRInst &inst, int reg, bool directly) {
	return IRReadsFrom(inst, reg, 'G', directly);
}

int IRDestGPR(const IRInst &inst) {
	const IRMeta *m = GetIRMeta(inst.op);

	if ((m->flags & IRFLAG_SRC3) == 0 && m->types[0] == 'G') {
		return inst.dest;
	}
	return -1;
}

bool IRWritesToGPR(const IRInst &inst, int reg) {
	return IRDestGPR(inst) == reg;
}

bool IRWritesToFPR(const IRInst &inst, int reg) {
	const IRMeta *m = GetIRMeta(inst.op);

	// Doesn't write to anything.
	if ((m->flags & IRFLAG_SRC3) != 0)
		return false;

	if (m->types[0] == 'F' && reg == inst.dest)
		return true;
	if (m->types[0] == 'V' && reg >= inst.dest && reg < inst.dest + 4)
		return true;
	if (m->types[0] == '2' && reg >= inst.dest && reg < inst.dest + 2)
		return true;
	return false;
}

int IRDestFPRs(const IRInst &inst, IRReg regs[4]) {
	const IRMeta *m = GetIRMeta(inst.op);

	// Doesn't write to anything.
	if ((m->flags & IRFLAG_SRC3) != 0)
		return 0;

	if (m->types[0] == 'F') {
		regs[0] = inst.dest;
		return 1;
	}
	if (m->types[0] == 'V') {
		for (int i = 0; i < 4; ++i)
			regs[i] = inst.dest + i;
		return 4;
	}
	if (m->types[0] == '2') {
		for (int i = 0; i < 2; ++i)
			regs[i] = inst.dest + i;
		return 2;
	}
	return 0;
}

int IRReadsFromGPRs(const IRInst &inst, IRReg regs[4]) {
	return IRReadsFromList(inst, regs, 'G');
}

int IRReadsFromFPRs(const IRInst &inst, IRReg regs[16]) {
	int c = IRReadsFromList(inst, regs, 'F');
	if (c != 0)
		return c;

	const IRMeta *m = GetIRMeta(inst.op);

	// We also need to check V and 2.  Indirect reads already checked, don't check again.
	if (m->types[1] == 'V' || m->types[1] == '2') {
		for (int i = 0; i < (m->types[1] == 'V' ? 4 : 2); ++i)
			regs[c++] = inst.src1 + i;
	}
	if (m->types[2] == 'V' || m->types[2] == '2') {
		for (int i = 0; i < (m->types[2] == 'V' ? 4 : 2); ++i)
			regs[c++] = inst.src2 + i;
	}
	if ((m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0) {
		if (m->types[0] == 'V' || m->types[0] == '2') {
			for (int i = 0; i < (m->types[0] == 'V' ? 4 : 2); ++i)
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
		const IRInst inst = info.instructions[info.currentIndex + i];
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
		const IRInst inst = info.instructions[info.currentIndex + i];

		if (IRReadsFromFPR(inst, fpr))
			return IRUsage::READ;
		// We say WRITE when the current instruction writes.  It's not useful for spilling.
		if (IRWritesToFPR(inst, fpr)) {
			return i == 0 ? IRUsage::WRITE : IRUsage::CLOBBERED;
		}
	}

	return IRUsage::UNUSED;
}
