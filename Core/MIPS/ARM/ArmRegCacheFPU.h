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

#pragma once

#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "Common/ArmEmitter.h"
#include "Core/MIPS/ARM/ArmRegCache.h"

using namespace ArmGen;

enum {
	TOTAL_MAPPABLE_MIPSFPUREGS = 32 + 128,
};

struct FPURegARM {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct FPURegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	ARMReg reg;
	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};


class ArmRegCacheFPU
{
public:
	ArmRegCacheFPU(MIPSState *mips);
	~ArmRegCacheFPU() {}

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
	void MapDirty(MIPSReg rd);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void FlushArmReg(ARMReg r);
	void FlushMipsReg(MIPSReg r);

	void FlushAll();

	ARMReg R(int preg); // Returns a cached register

	void SetEmitter(ARMXEmitter *emitter) { emit = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

private:
	int GetMipsRegOffset(MIPSReg r);
	int GetMipsRegOffsetV(MIPSReg r) {
		return GetMipsRegOffset(r + 32);
	}
	MIPSState *mips_;
	ARMXEmitter *emit;
	u32 compilerPC_;

	enum {
		NUM_ARMFPUREG = 16,  // TODO: Support 32, which you have with NEON
		NUM_MIPSFPUREG = TOTAL_MAPPABLE_MIPSFPUREGS,
	};

	RegARM ar[NUM_ARMFPUREG];
	RegMIPS mr[NUM_MIPSFPUREG];
};
