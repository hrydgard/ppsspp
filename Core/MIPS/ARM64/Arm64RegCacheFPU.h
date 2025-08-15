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

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Common/Arm64Emitter.h"

namespace Arm64JitConstants {

enum {
	NUM_TEMPS = 16,
	TEMP0 = 32 + 128,
	TOTAL_MAPPABLE_MIPSFPUREGS = 32 + 128 + NUM_TEMPS,
};

enum {
	MAP_READ = 0,
	MAP_MTX_TRANSPOSED = 16,
	MAP_PREFER_LOW = 16,
	MAP_PREFER_HIGH = 32,

	// Force is not yet correctly implemented, if the reg is already mapped it will not move
	MAP_FORCE_LOW = 64,  // Only map Q0-Q7  (and probably not Q0-Q3 as they are S registers so that leaves Q8-Q15)
	MAP_FORCE_HIGH = 128,  // Only map Q8-Q15
};

}

namespace MIPSAnalyst {
struct AnalysisResults;
};

struct FPURegARM64 {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct FPURegQuad64 {
	int mipsVec;
	VectorSize sz;
	u8 vregs[4];
	bool isDirty;
	bool spillLock;
	bool isTemp;
};

struct FPURegMIPS {
	// Where is this MIPS register?
	Arm64JitConstants::RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	u32 reg;
	int lane;

	bool spillLock;  // if true, this register cannot be spilled.
	bool tempLock;
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

namespace MIPSComp {
	struct JitOptions;
	struct JitState;
}

class Arm64RegCacheFPU {
public:
	Arm64RegCacheFPU(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo);
	~Arm64RegCacheFPU() {}

	void Init(Arm64Gen::ARM64XEmitter *emitter, Arm64Gen::ARM64FloatEmitter *fp);

	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSReg reg, MIPSReg reg2 = -1, MIPSReg reg3 = -1, MIPSReg reg4 = -1);
	void SpillLockV(MIPSReg r) { SpillLock(r + 32); }

	void ReleaseSpillLocksAndDiscardTemps();
	void ReleaseSpillLock(int mipsreg) {
		mr[mipsreg].spillLock = false;
	}
	void ReleaseSpillLockV(int mipsreg) {
		ReleaseSpillLock(mipsreg + 32);
	}

	void SetImm(MIPSReg reg, u32 immVal);
	bool IsImm(MIPSReg reg) const;
	u32 GetImm(MIPSReg reg) const;

	// Returns an ARM register containing the requested MIPS register.
	Arm64Gen::ARM64Reg MapReg(MIPSReg reg, int mapFlags = 0);
	void MapInIn(MIPSReg rd, MIPSReg rs);
	void MapDirty(MIPSReg rd);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	bool IsMapped(MIPSReg r);
	bool IsMappedV(MIPSReg r) { return IsMapped((MIPSReg)(r + 32)); }
	bool IsInRAM(MIPSReg r);
	bool IsInRAMV(MIPSReg r) { return IsInRAM((MIPSReg)(r + 32)); }
	void FlushArmReg(Arm64Gen::ARM64Reg r);
	void FlushR(MIPSReg r);
	void DiscardR(MIPSReg r);
	Arm64Gen::ARM64Reg R(int preg); // Returns a cached register

	void MapRegV(int vreg, int flags = 0);
	void LoadToRegV(Arm64Gen::ARM64Reg armReg, int vreg);
	void MapInInV(int rt, int rs);
	void MapDirtyInV(int rd, int rs, bool avoidLoad = true);
	void MapDirtyInInV(int rd, int rs, int rt, bool avoidLoad = true);

	bool IsTempX(Arm64Gen::ARM64Reg r) const;
	MIPSReg GetTempV() { return GetTempR() - 32; }
	// VFPU registers as single VFP registers.
	Arm64Gen::ARM64Reg V(int vreg) { return R(vreg + 32); }
	 
	void FlushAll();

	// This one is allowed at any point.
	void FlushV(MIPSReg r);

	// NOTE: These require you to release spill locks manually!
	void MapRegsAndSpillLockV(int vec, VectorSize vsz, int flags);
	void MapRegsAndSpillLockV(const u8 *v, VectorSize vsz, int flags);

	void SpillLockV(const u8 *v, VectorSize vsz);
	void SpillLockV(int vec, VectorSize vsz);

	void SetEmitter(Arm64Gen::ARM64XEmitter *emitter, Arm64Gen::ARM64FloatEmitter *fp) { emit_ = emitter; fp_ = fp; }

	int GetMipsRegOffset(MIPSReg r);
	int GetMipsRegOffsetV(MIPSReg r) {
		return GetMipsRegOffset(r + 32);
	}

private:
	Arm64Gen::ARM64Reg ARM64RegForFlush(int r);
	MIPSReg GetTempR();
	const Arm64Gen::ARM64Reg *GetMIPSAllocationOrder(int &count);

	void SetupInitialRegs();

	MIPSState *mips_;
	Arm64Gen::ARM64XEmitter *emit_;
	Arm64Gen::ARM64FloatEmitter *fp_;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;

	int numARMFpuReg_;

	enum {
		// On ARM64, each of the 32 registers are full 128-bit. No sharing of components!
		MAX_ARMFPUREG = 32,
		NUM_MIPSFPUREG = Arm64JitConstants::TOTAL_MAPPABLE_MIPSFPUREGS,
	};

	FPURegARM64 ar[MAX_ARMFPUREG];
	FPURegMIPS mr[NUM_MIPSFPUREG];
	FPURegMIPS *vr;

	bool pendingFlush;
	bool initialReady = false;
	FPURegARM64 arInitial[MAX_ARMFPUREG];
	FPURegMIPS mrInitial[NUM_MIPSFPUREG];
};
