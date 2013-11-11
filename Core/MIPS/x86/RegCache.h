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

using namespace Gen;

#ifdef _M_X64
#define NUM_X_REGS 16
#elif _M_IX86
#define NUM_X_REGS 8
#endif

// TODO: Add more cachable regs, like HI, LO
#define NUM_MIPS_GPRS 32

struct MIPSCachedReg {
	OpArg location;
	bool away;  // value not in source register
	bool locked;
};

struct X64CachedReg {
	MIPSGPReg mipsReg;
	bool dirty;
	bool free;
	bool allocLocked;
};

struct GPRRegCacheState {
	MIPSCachedReg regs[NUM_MIPS_GPRS];
	X64CachedReg xregs[NUM_X_REGS];
};

class GPRRegCache
{
public:
	GPRRegCache();
	~GPRRegCache() {}
	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);

	void DiscardRegContentsIfCached(MIPSGPReg preg);
	void SetEmitter(XEmitter *emitter) {emit = emitter;}

	void FlushR(X64Reg reg); 
	void FlushLockX(X64Reg reg) {
		FlushR(reg);
		LockX(reg);
	}
	void FlushLockX(X64Reg reg1, X64Reg reg2) {
		FlushR(reg1); FlushR(reg2);
		LockX(reg1); LockX(reg2);
	}
	void Flush();
	void FlushBeforeCall();
	int SanityCheck() const;
	void KillImmediate(MIPSGPReg preg, bool doLoad, bool makeDirty);

	void MapReg(MIPSGPReg preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(MIPSGPReg preg);

	const OpArg &R(MIPSGPReg preg) const {return regs[preg].location;}
	X64Reg RX(MIPSGPReg preg) const
	{
		if (regs[preg].away && regs[preg].location.IsSimpleReg()) 
			return regs[preg].location.GetSimpleReg(); 
		PanicAlert("Not so simple - %i", preg); 
		return (X64Reg)-1;
	}
	OpArg GetDefaultLocation(MIPSGPReg reg) const;

	// Register locking.
	void Lock(MIPSGPReg p1, MIPSGPReg p2 = MIPS_REG_INVALID, MIPSGPReg p3 = MIPS_REG_INVALID, MIPSGPReg p4 = MIPS_REG_INVALID);
	void LockX(int x1, int x2=0xff, int x3=0xff, int x4=0xff);
	void UnlockAll();
	void UnlockAllX();

	void SetImm(MIPSGPReg preg, u32 immValue);
	bool IsImm(MIPSGPReg preg) const;
	u32 GetImm(MIPSGPReg preg) const;

	void GetState(GPRRegCacheState &state) const;
	void RestoreState(const GPRRegCacheState state);

	MIPSState *mips;

private:
	X64Reg GetFreeXReg();
	const int *GetAllocationOrder(int &count);

	MIPSCachedReg regs[NUM_MIPS_GPRS];
	X64CachedReg xregs[NUM_X_REGS];

	XEmitter *emit;
};
