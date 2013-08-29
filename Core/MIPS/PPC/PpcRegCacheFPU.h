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
#include "Common/PpcEmitter.h"
#include "Core/MIPS/PPC/PpcRegCache.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

using namespace PpcGen;

enum {
	NUM_TEMPS = 16,
	TEMP0 = 32 + 128,
	TOTAL_MAPPABLE_MIPSFPUREGS = 32 + 128 + NUM_TEMPS,
};

struct FPURegPPC {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct FPURegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	int reg;
	bool spillLock;  // if true, this register cannot be spilled.
	bool tempLock;
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};


class PpcRegCacheFPU
{
public:
	PpcRegCacheFPU(MIPSState *mips);
	~PpcRegCacheFPU() {}

	void Init(PPCXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the ppc register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSReg reg, MIPSReg reg2 = -1, MIPSReg reg3 = -1, MIPSReg reg4 = -1);
	void SpillLockV(MIPSReg r) { SpillLock(r + 32); }

	void ReleaseSpillLocksAndDiscardTemps();
	void ReleaseSpillLock(int mipsreg)
	{
		mr[mipsreg].spillLock = false;
	}
	void ReleaseSpillLockV(int mipsreg) {
		ReleaseSpillLock(mipsreg + 32);
	}

	void SetImm(MIPSReg reg, u32 immVal);
	bool IsImm(MIPSReg reg) const;
	u32 GetImm(MIPSReg reg) const;

	// Returns an PPC register containing the requested MIPS register.
	PPCReg MapReg(MIPSReg reg, int mapFlags = 0);
	void MapInIn(MIPSReg rd, MIPSReg rs);
	void MapInInV(int rt, int rs);
	void MapDirtyInV(int rd, int rs, bool avoidLoad = true);
	void MapDirtyInInV(int rd, int rs, int rt, bool avoidLoad = true);
	void MapDirty(MIPSReg rd);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	void FlushPpcReg(PPCReg r);
	void FlushR(MIPSReg r);
	void FlushV(MIPSReg r) { FlushR(r + 32); }
	void DiscardR(MIPSReg r);
	void DiscardV(MIPSReg r) { DiscardR(r + 32);}
	bool IsTempX(PPCReg r) const;

	MIPSReg GetTempR();
	MIPSReg GetTempV() { return GetTempR() - 32; }
	 
	void FlushAll();

	PPCReg R(int preg); // Returns a cached register
	
	// VFPU registers
	
	PPCReg V(int vreg) { return R(vreg + 32); }

	void MapRegV(int vreg, int flags = 0);

	void LoadToRegV(PPCReg ppcReg, int vreg);

	// NOTE: These require you to release spill locks manually!
	void MapRegsAndSpillLockV(int vec, VectorSize vsz, int flags);
	void MapRegsAndSpillLockV(const u8 *v, VectorSize vsz, int flags);

	void SpillLockV(const u8 *v, VectorSize vsz);
	void SpillLockV(int vec, VectorSize vsz);

	void SetEmitter(PPCXEmitter *emitter) { emit_ = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSReg r);
	int GetMipsRegOffsetV(MIPSReg r) {
		return GetMipsRegOffset(r + 32);
	}

private:
	MIPSState *mips_;
	PPCXEmitter *emit_;
	u32 compilerPC_;

	enum {
		NUM_PPCFPUREG = 32,
		NUM_MIPSFPUREG = TOTAL_MAPPABLE_MIPSFPUREGS,
	};

	RegPPC ar[NUM_PPCFPUREG];
	FPURegMIPS mr[NUM_MIPSFPUREG];
	FPURegMIPS *vr;
};
