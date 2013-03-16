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

#pragma once

#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "ArmEmitter.h"

using namespace ArmGen;

#define CTXREG (R10)

// R2 to R8: mapped MIPS regs
// R9 = code pointers
// R10 = MIPS context
// R11 = base pointer

// Special MIPS registers: 
enum {
	MIPSREG_HI = 32,
	MIPSREG_LO = 33,
	TOTAL_MAPPABLE_MIPSREGS = 34,
};

typedef int MIPSReg;

struct RegARM {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

enum RegMIPSLoc {
	ML_IMM,
	ML_ARMREG,
	ML_MEM,
};

struct RegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	u32 imm;
	ARMReg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

#undef MAP_DIRTY
#undef MAP_NOINIT
// Initing is the default so the flag is reversed.
enum {
	MAP_DIRTY = 1,
	MAP_NOINIT = 2,
};

class ArmRegCache
{
public:
	ArmRegCache(MIPSState *mips);
	~ArmRegCache() {}

	void Init(ARMXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSReg reg, MIPSReg reg2 = -1, MIPSReg reg3 = -1, MIPSReg reg4 = -1);
	void ReleaseSpillLocks();

	void SetImm(MIPSReg reg, u32 immVal);
	bool IsImm(MIPSReg reg) const;
	u32 GetImm(MIPSReg reg) const;

	// Returns an ARM register containing the requested MIPS register.
	ARMReg MapReg(MIPSReg reg, int mapFlags = 0);
	void MapInIn(MIPSReg rd, MIPSReg rs);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void MapDirtyDirtyInIn(MIPSReg rd1, MIPSReg rd2, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void FlushArmReg(ARMReg r);
	void FlushR(MIPSReg r);

	void FlushAll();

	ARMReg R(int preg); // Returns a cached register

	void SetEmitter(ARMXEmitter *emitter) { emit = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSReg r);

private:
	MIPSState *mips_;
	ARMXEmitter *emit;
	u32 compilerPC_;

	enum {
		NUM_ARMREG = 16,
		NUM_MIPSREG = TOTAL_MAPPABLE_MIPSREGS,
	};

	RegARM ar[NUM_ARMREG];
	RegMIPS mr[NUM_MIPSREG];
};
