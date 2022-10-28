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

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Common/ArmEmitter.h"

namespace ArmJitConstants {

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

struct FPURegARM {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct FPURegQuad {
	int mipsVec;
	VectorSize sz;
	u8 vregs[4];
	bool isDirty;
	bool spillLock;
	bool isTemp;
};

struct FPURegMIPS {
	// Where is this MIPS register?
	ArmJitConstants::RegMIPSLoc loc;
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

class ArmRegCacheFPU {
public:
	ArmRegCacheFPU(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo);
	~ArmRegCacheFPU() {}

	void Init(ArmGen::ARMXEmitter *emitter);

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
	ArmGen::ARMReg MapReg(MIPSReg reg, int mapFlags = 0);
	void MapInIn(MIPSReg rd, MIPSReg rs);
	void MapDirty(MIPSReg rd);
	void MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad = true);
	bool IsMapped(MIPSReg r);
	void FlushArmReg(ArmGen::ARMReg r);
	void FlushR(MIPSReg r);
	void DiscardR(MIPSReg r);
	ArmGen::ARMReg R(int preg); // Returns a cached register

	// VFPU register as single ARM VFP registers. Must not be used in the upcoming NEON mode!
	void MapRegV(int vreg, int flags = 0);
	void LoadToRegV(ArmGen::ARMReg armReg, int vreg);
	void MapInInV(int rt, int rs);
	void MapDirtyInV(int rd, int rs, bool avoidLoad = true);
	void MapDirtyInInV(int rd, int rs, int rt, bool avoidLoad = true);

	bool IsTempX(ArmGen::ARMReg r) const;
	MIPSReg GetTempV() { return GetTempR() - 32; }
	// VFPU registers as single VFP registers.
	ArmGen::ARMReg V(int vreg) { return R(vreg + 32); }
	 
	int FlushGetSequential(int a);
	void FlushAll();

	// This one is allowed at any point.
	void FlushV(MIPSReg r);

	// VFPU registers mapped to match NEON quads (and doubles, for pairs and singles)
	// Here we return the ARM register directly instead of providing a "V" accessor
	// and so on. Might switch to this model for the other regallocs later.

	// Quad mapping does NOT look into the ar array. Instead we use the qr array to keep
	// track of what's in each quad.

	// Note that we automatically spill-lock EVERY Q REGISTER we map, unlike other types.
	// Need to explicitly allow spilling to get spilling.
	ArmGen::ARMReg QMapReg(int vreg, VectorSize sz, int flags);

	// TODO
	// Maps a matrix as a set of columns (yes, even transposed ones, always columns
	// as those are faster to load/flush). When possible it will map into consecutive
	// quad registers, enabling blazing-fast full-matrix loads, transposed or not.
	void QMapMatrix(ArmGen::ARMReg *regs, int matrix, MatrixSize mz, int flags);

	ArmGen::ARMReg QAllocTemp(VectorSize sz);
	
	void QAllowSpill(int quad);
	void QFlush(int quad);
	void QLoad4x4(MIPSGPReg regPtr, int vquads[4]);
	//void FlushQWithV(MIPSReg r);

	// NOTE: These require you to release spill locks manually!
	void MapRegsAndSpillLockV(int vec, VectorSize vsz, int flags);
	void MapRegsAndSpillLockV(const u8 *v, VectorSize vsz, int flags);

	void SpillLockV(const u8 *v, VectorSize vsz);
	void SpillLockV(int vec, VectorSize vsz);

	void SetEmitter(ArmGen::ARMXEmitter *emitter) { emit_ = emitter; }

	int GetMipsRegOffset(MIPSReg r);

private:
	bool Consecutive(int v1, int v2) const;
	bool Consecutive(int v1, int v2, int v3) const;
	bool Consecutive(int v1, int v2, int v3, int v4) const;

	MIPSReg GetTempR();
	const ArmGen::ARMReg *GetMIPSAllocationOrder(int &count);
	int GetMipsRegOffsetV(MIPSReg r) {
		return GetMipsRegOffset(r + 32);
	}
	// This one WILL get a free quad as long as you haven't spill-locked them all.
	int QGetFreeQuad(int start, int count, const char *reason);

	void SetupInitialRegs();

	MIPSState *mips_;
	ArmGen::ARMXEmitter *emit_;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;

	int qTime_;

	enum {
		// With NEON, we have 64 S = 32 D = 16 Q registers. Only the first 32 S registers
		// are individually mappable though.
		NUM_ARMFPUREG = 32,
		NUM_ARMQUADS = 16,
		NUM_MIPSFPUREG = ArmJitConstants::TOTAL_MAPPABLE_MIPSFPUREGS,
	};

	FPURegARM ar[NUM_ARMFPUREG];
	FPURegMIPS mr[NUM_MIPSFPUREG];
	FPURegQuad qr[NUM_ARMQUADS];
	FPURegMIPS *vr;

	bool pendingFlush;
	bool initialReady = false;
	FPURegARM arInitial[NUM_ARMFPUREG];
	FPURegMIPS mrInitial[NUM_MIPSFPUREG];
};
