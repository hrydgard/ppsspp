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


/**
PPC reg cache based on arm version
**/

#pragma once

#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "ppcEmitter.h"
#include "Core/MIPS/PPC/PpcRegCache.h"

using namespace PpcGen;

typedef int MIPSReg;

struct VPURegPPC {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct VPURegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	u32 imm;
	PPCReg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};
namespace MIPSComp {
	struct PpcJitOptions;
}

class PpcRegCacheVPU
{
public:
	PpcRegCacheVPU(MIPSState *mips, MIPSComp::PpcJitOptions *options);
	~PpcRegCacheVPU() {}

	void Init(PPCXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSReg reg, MIPSReg reg2 = -1, MIPSReg reg3 = -1, MIPSReg reg4 = -1);
	void ReleaseSpillLock(MIPSReg reg);
	void ReleaseSpillLocks();

	void SetImm(MIPSReg reg, u32 immVal);
	bool IsImm(MIPSReg reg) const;
	u32 GetImm(MIPSReg reg) const;

	// Returns an ARM register containing the requested MIPS register.
	PPCReg MapReg(MIPSReg reg, int mapFlags = 0);
	void MapInIn(MIPSReg rd, MIPSReg rs);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void MapDirtyDirtyInIn(MIPSReg rd1, MIPSReg rd2, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void FlushPpcReg(PPCReg r);
	void FlushR(MIPSReg r);
	void FlushBeforeCall();
	void FlushAll();

	PPCReg R(int preg); // Returns a cached register

	void SetEmitter(PPCXEmitter *emitter) { emit_ = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSReg r);

private:
	const PPCReg *GetMIPSAllocationOrder(int &count);
		
	MIPSState *mips_;
	MIPSComp::PpcJitOptions *options_;
	PPCXEmitter *emit_;
	u32 compilerPC_;

	enum {
		NUM_PPCVPUREG = 128,
		NUM_MIPSVPUREG = 32,
	};

	VPURegPPC ar[NUM_PPCVPUREG];
	VPURegMIPS mr[NUM_MIPSVPUREG];
};
