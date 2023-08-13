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

bool IRReadsFromFPR(const IRInst &inst, int reg, bool directly = false);
bool IRReadsFromGPR(const IRInst &inst, int reg, bool directly = false);
bool IRWritesToGPR(const IRInst &inst, int reg);
bool IRWritesToFPR(const IRInst &inst, int reg);
int IRDestGPR(const IRInst &inst);
int IRDestFPRs(const IRInst &inst, IRReg regs[4]);
int IRReadsFromGPRs(const IRInst &inst, IRReg regs[4]);
int IRReadsFromFPRs(const IRInst &inst, IRReg regs[16]);

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
