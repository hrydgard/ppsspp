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

#pragma once

#include "Core/MIPS/IR/IRInst.h"

struct IRInstMeta {
	union {
		IRInst i;
		struct {
			IROp op;
			union {
				IRReg dest;
				IRReg src3;
			};
			IRReg src1;
			IRReg src2;
			u32 constant;
		};
	};
	IRMeta m;
};

static_assert(offsetof(IRInst, src2) == offsetof(IRInstMeta, src2));

inline IRInstMeta GetIRMeta(const IRInst &inst) {
	return { { inst }, *GetIRMeta(inst.op) };
}

// Inline, since the passes and the register allocators ask for every instruction.
inline bool IRReadsFrom(const IRInstMeta &inst, int reg, char type, bool *directly) {
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

inline bool IRReadsFromFPR(const IRInstMeta &inst, int reg, bool *directly = nullptr) {
	if (IRReadsFrom(inst, reg, 'F', directly))
		return true;

	// We also need to check V and 2.  Indirect reads already checked, don't check again.
	if (inst.m.types[1] == 'V' && reg >= inst.src1 && reg < inst.src1 + 4)
		return true;
	if (inst.m.types[1] == '2' && reg >= inst.src1 && reg < inst.src1 + 2)
		return true;
	if (inst.m.types[1] == 'M' && reg >= inst.src1 && reg < inst.src1 + 16)
		return true;
	if (inst.m.types[2] == 'V' && reg >= inst.src2 && reg < inst.src2 + 4)
		return true;
	if (inst.m.types[2] == '2' && reg >= inst.src2 && reg < inst.src2 + 2)
		return true;
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0) {
		if (inst.m.types[0] == 'V' && reg >= inst.src3 && reg < inst.src3 + 4)
			return true;
		if (inst.m.types[0] == '2' && reg >= inst.src3 && reg < inst.src3 + 2)
			return true;
	}
	return false;
}

inline int IRReadsFromList(const IRInstMeta &inst, IRReg regs[4], char type) {
	int c = 0;

	if (inst.m.types[1] == type)
		regs[c++] = inst.src1;
	if (inst.m.types[2] == type)
		regs[c++] = inst.src2;
	if ((inst.m.flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0 && inst.m.types[0] == type)
		regs[c++] = inst.src3;

	// Exits read every reg that lives on, and barriers may read anything.
	if ((inst.m.flags & (IRFLAG_EXIT | IRFLAG_BARRIER)) != 0)
		return -1;

	return c;
}

inline bool IRReadsFromGPR(const IRInstMeta &inst, int reg, bool *directly = nullptr) {
	return IRReadsFrom(inst, reg, 'G', directly);
}

inline int IRDestGPR(const IRInstMeta &inst) {
	if ((inst.m.flags & IRFLAG_SRC3) == 0 && inst.m.types[0] == 'G') {
		return inst.dest;
	}
	return -1;
}

inline bool IRWritesToGPR(const IRInstMeta &inst, int reg) {
	return IRDestGPR(inst) == reg;
}

inline bool IRWritesToFPR(const IRInstMeta &inst, int reg) {
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

inline int IRDestFPRs(const IRInstMeta &inst, IRReg regs[4]) {
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

inline int IRReadsFromGPRs(const IRInstMeta &inst, IRReg regs[4]) {
	return IRReadsFromList(inst, regs, 'G');
}

// A block (M) and a Vec4.
enum { IR_MAX_FPR_READS = 20 };
int IRReadsFromFPRs(const IRInstMeta &inst, IRReg regs[IR_MAX_FPR_READS]);

struct IRSituation {
	int lookaheadCount;
	int currentIndex;
	const IRInst *instructions;
	int numInstructions;
};

enum class IRUsage {
	UNKNOWN,
	UNUSED,
	READ,
	WRITE,
	CLOBBERED,
};

IRUsage IRNextGPRUsage(int gpr, const IRSituation &info);
IRUsage IRNextFPRUsage(int fpr, const IRSituation &info);
