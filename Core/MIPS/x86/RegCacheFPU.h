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

#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#undef MAP_NOINIT

// GPRs are numbered 0 to 31
// VFPU regs are numbered 32 to 159.
// Then we have some temp regs for VFPU handling from 160 to 175.

// Temp regs: 4 from S prefix, 4 from T prefix, 4 from D mask, and 4 for work (worst case.)
// But most of the time prefixes aren't used that heavily so we won't use all of them.

// PLANS FOR PROPER SIMD
// 1, 2, 3, and 4-vectors will be loaded into single XMM registers
// Matrices will be loaded into pairs, triads, or quads of XMM registers - simply by loading
// the columns or the rows one by one.

// On x86 this means that only one 4x4 matrix can be fully loaded at once but that's alright.
// We might want to keep "linearized" columns in memory.

// Implement optimized vec/matrix multiplications of all types and transposes that
// take into account in which XMM registers the values are. Fallback: Just dump out the values
// and do it the old way.

#include "ppsspp_config.h"

enum {
	TEMP0 = 32 + 128,
	NUM_MIPS_FPRS = 32 + 128 + NUM_X86_FPU_TEMPS,
};

#if PPSSPP_ARCH(AMD64)
#define NUM_X_FPREGS 16
#elif PPSSPP_ARCH(X86)
#define NUM_X_FPREGS 8
#endif

namespace MIPSAnalyst {
struct AnalysisResults;
};

struct X64CachedFPReg {
	union {
		int mipsReg;
		int mipsRegs[4];
	};
	bool dirty;
};

struct MIPSCachedFPReg {
	Gen::OpArg location;
	int lane;
	bool away;  // value not in source register (memory)
	u8 locked;
	// Only for temp regs.
	bool tempLocked;
};

struct FPURegCacheState {
	MIPSCachedFPReg regs[NUM_MIPS_FPRS];
	X64CachedFPReg xregs[NUM_X_FPREGS];
};

namespace MIPSComp {
	struct JitOptions;
	struct JitState;
}

enum {
	MAP_DIRTY = 1,
	MAP_NOINIT = 2 | MAP_DIRTY,
	// Only for MapRegsV, MapRegsVS.
	MAP_NOLOCK = 4,
};

// The PSP has 160 FP registers: 32 FPRs + 128 VFPU registers.
// Soon we will support them all.

class FPURegCache
{
public:
	FPURegCache();
	~FPURegCache() {}

	void Start(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo, MIPSAnalyst::AnalysisResults &stats, bool useRip);
	void MapReg(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	void StoreFromRegisterV(int preg) {
		StoreFromRegister(preg + 32);
	}
	Gen::OpArg GetDefaultLocation(int reg) const;
	void DiscardR(int freg);
	void DiscardV(int vreg) {
		DiscardR(vreg + 32);
	}
	void DiscardVS(int vreg);
	bool IsTempX(Gen::X64Reg xreg);
	int GetTempR();
	int GetTempV() {
		return GetTempR() - 32;
	}
	int GetTempVS(u8 *v, VectorSize vsz);

	void SetEmitter(Gen::XEmitter *emitter) {emit = emitter;}

	// Flushes one register and reuses the register for another one. Dirtyness is implied.
	void FlushRemap(int oldreg, int newreg);

	void Flush();
	int SanityCheck() const;

	const Gen::OpArg &R(int freg) const {return regs[freg].location;}
	const Gen::OpArg &V(int vreg) const {
		_dbg_assert_msg_(vregs[vreg].lane == 0, "SIMD reg %d used as V reg (use VS instead). pc=%08x", vreg, mips_->pc);
		return vregs[vreg].location;
	}
	const Gen::OpArg &VS(const u8 *vs) const {
		_dbg_assert_msg_(vregs[vs[0]].lane != 0, "V reg %d used as VS reg (use V instead). pc=%08x", vs[0], mips_->pc);
		return vregs[vs[0]].location;
	}

	Gen::X64Reg RX(int freg) const {
		if (regs[freg].away && regs[freg].location.IsSimpleReg())
			return regs[freg].location.GetSimpleReg();
		_assert_msg_(false, "Not so simple - f%i", freg);
		return (Gen::X64Reg)-1;
	}

	Gen::X64Reg VX(int vreg) const {
		_dbg_assert_msg_(vregs[vreg].lane == 0, "SIMD reg %d used as V reg (use VSX instead). pc=%08x", vreg, mips_->pc);
		if (vregs[vreg].away && vregs[vreg].location.IsSimpleReg())
			return vregs[vreg].location.GetSimpleReg();
		_assert_msg_(false, "Not so simple - v%i", vreg);
		return (Gen::X64Reg)-1;
	}

	Gen::X64Reg VSX(const u8 *vs) const {
		_dbg_assert_msg_(vregs[vs[0]].lane != 0, "V reg %d used as VS reg (use VX instead). pc=%08x", vs[0], mips_->pc);
		if (vregs[vs[0]].away && vregs[vs[0]].location.IsSimpleReg())
			return vregs[vs[0]].location.GetSimpleReg();
		_assert_msg_(false, "Not so simple - v%i", vs[0]);
		return (Gen::X64Reg)-1;
	}

	// Just to avoid coding mistakes, defined here to prevent compilation.
	void R(Gen::X64Reg r);

	// Register locking. Prevents them from being spilled.
	void SpillLock(int p1, int p2=0xff, int p3=0xff, int p4=0xff);
	void ReleaseSpillLock(int mipsreg);
	void ReleaseSpillLocks();

	bool IsMapped(int r) {
		return R(r).IsSimpleReg();
	}
	bool IsMappedV(int v) {
		return vregs[v].lane == 0 && V(v).IsSimpleReg();
	}
	bool IsMappedVS(u8 v) {
		return vregs[v].lane != 0 && VS(&v).IsSimpleReg();
	}
	bool IsMappedVS(const u8 *v, VectorSize vsz);
	bool CanMapVS(const u8 *v, VectorSize vsz);

	void MapRegV(int vreg, int flags);
	void MapRegsV(int vec, VectorSize vsz, int flags);
	void MapRegsV(const u8 *v, VectorSize vsz, int flags);
	void SpillLockV(int vreg) {
		SpillLock(vreg + 32);
	}
	void SpillLockV(const u8 *v, VectorSize vsz);
	void SpillLockV(int vec, VectorSize vsz);
	void ReleaseSpillLockV(int vreg) {
		ReleaseSpillLock(vreg + 32);
	}
	void ReleaseSpillLockV(const u8 *vec, VectorSize sz);

	// TODO: This may trash XMM0/XMM1 some day.
	void MapRegsVS(const u8 *v, VectorSize vsz, int flags);
	bool TryMapRegsVS(const u8 *v, VectorSize vsz, int flags);
	bool TryMapDirtyInVS(const u8 *vd, VectorSize vdsz, const u8 *vs, VectorSize vssz, bool avoidLoad = true);
	bool TryMapDirtyInInVS(const u8 *vd, VectorSize vdsz, const u8 *vs, VectorSize vssz, const u8 *vt, VectorSize vtsz, bool avoidLoad = true);
	// TODO: If s/t overlap differently, need read-only copies?  Maybe finalize d?  Major design flaw...
	// TODO: Matrix versions?  Cols/Rows?
	// No MapRegVS, that'd be silly.

	void SimpleRegsV(const u8 *v, VectorSize vsz, int flags);
	void SimpleRegsV(const u8 *v, MatrixSize msz, int flags);
	void SimpleRegV(const u8 v, int flags);

	void GetState(FPURegCacheState &state) const;
	void RestoreState(const FPURegCacheState& state);

	MIPSState *mips_ = nullptr;

	void FlushX(Gen::X64Reg reg);
	Gen::X64Reg GetFreeXReg();
	int GetFreeXRegs(Gen::X64Reg *regs, int n, bool spill = true);

	void Invariant() const;

private:
	const int *GetAllocationOrder(int &count);
	void SetupInitialRegs();

	// These are intentionally not public so the interface is "locked" or "unlocked", no levels.
	void ReduceSpillLock(int mreg);
	void ReduceSpillLockV(int vreg) {
		ReduceSpillLock(vreg + 32);
	}
	void ReduceSpillLockV(const u8 *vec, VectorSize sz);

	Gen::X64Reg LoadRegsVS(const u8 *v, int n);

	MIPSCachedFPReg regs[NUM_MIPS_FPRS]{};
	X64CachedFPReg xregs[NUM_X_FPREGS]{};
	MIPSCachedFPReg *vregs;

	bool useRip_;
	bool pendingFlush;
	bool initialReady = false;
	MIPSCachedFPReg regsInitial[NUM_MIPS_FPRS];
	X64CachedFPReg xregsInitial[NUM_X_FPREGS];

	Gen::XEmitter *emit = nullptr;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;
};
