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

bool IRReadsFromFPR(const IRInstMeta &inst, int reg, bool *directly = nullptr);
bool IRReadsFromGPR(const IRInstMeta &inst, int reg, bool *directly = nullptr);
bool IRWritesToGPR(const IRInstMeta &inst, int reg);
bool IRWritesToFPR(const IRInstMeta &inst, int reg);
int IRDestGPR(const IRInstMeta &inst);
int IRDestFPRs(const IRInstMeta &inst, IRReg regs[4]);
int IRReadsFromGPRs(const IRInstMeta &inst, IRReg regs[4]);
int IRReadsFromFPRs(const IRInstMeta &inst, IRReg regs[16]);

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
