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
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

using namespace Gen;


// GPRs are numbered 0 to 31
// VFPU regs are numbered 32 to 159.
// Then we have some temp regs for VFPU handling from 160 to 175.

// Temp regs: 4 from S prefix, 4 from T prefix, 4 from D mask, and 4 for work (worst case.)
// But most of the time prefixes aren't used that heavily so we won't use all of them.

enum {
	NUM_TEMPS = 16,
	TEMP0 = 32 + 128,
	NUM_MIPS_FPRS = 32 + 128 + NUM_TEMPS,
};

#ifdef _M_X64
#define NUM_X_FPREGS 16
#elif _M_IX86
#define NUM_X_FPREGS 8
#endif

struct X64CachedFPReg {
	int mipsReg;
	bool dirty;
};

struct MIPSCachedFPReg {
	OpArg location;
	bool away;  // value not in source register
	bool locked;
	// Only for temp regs.
	bool tempLocked;
};

enum {
	MAP_DIRTY = 1,
	MAP_NOINIT = 2,
};

// The PSP has 160 FP registers: 32 FPRs + 128 VFPU registers.
// Soon we will support them all.

class FPURegCache
{
public:
	FPURegCache();
	~FPURegCache() {}

	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);
	void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	void StoreFromRegisterV(int preg) {
		StoreFromRegister(preg + 32);
	}
	OpArg GetDefaultLocation(int reg) const;
	void DiscardR(int freg);
	void DiscardV(int vreg) {
		DiscardR(vreg + 32);
	}
	bool IsTempX(X64Reg xreg);
	int GetTempR();
	int GetTempV() {
		return GetTempR() - 32;
	}

	void SetEmitter(XEmitter *emitter) {emit = emitter;}

	void Flush();
	int SanityCheck() const;

	const OpArg &R(int freg) const {return regs[freg].location;}
	const OpArg &V(int vreg) const {return regs[32 + vreg].location;}

	X64Reg RX(int freg) const
	{
		if (regs[freg].away && regs[freg].location.IsSimpleReg()) 
			return regs[freg].location.GetSimpleReg(); 
		PanicAlert("Not so simple - f%i", freg); 
		return (X64Reg)-1;
	}

	X64Reg VX(int vreg) const
	{
		if (regs[vreg + 32].away && regs[vreg + 32].location.IsSimpleReg()) 
			return regs[vreg + 32].location.GetSimpleReg(); 
		PanicAlert("Not so simple - v%i", vreg); 
		return (X64Reg)-1;
	}

	// Register locking. Prevents them from being spilled.
	void SpillLock(int p1, int p2=0xff, int p3=0xff, int p4=0xff);
	void ReleaseSpillLock(int mipsrega);
	void ReleaseSpillLocks();

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

	MIPSState *mips;

private:
	X64Reg GetFreeXReg();
	void FlushX(X64Reg reg);
	const int *GetAllocationOrder(int &count);

	MIPSCachedFPReg regs[NUM_MIPS_FPRS];
	X64CachedFPReg xregs[NUM_X_FPREGS];
	MIPSCachedFPReg *vregs;

	// TEMP0, etc. are swapped in here if necessary (e.g. on x86.)
	static u32 tempValues[NUM_TEMPS];

	XEmitter *emit;
};
