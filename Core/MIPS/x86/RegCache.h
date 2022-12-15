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

#include "ppsspp_config.h"
#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"

namespace X64JitConstants {
#if PPSSPP_ARCH(AMD64)
	const Gen::X64Reg MEMBASEREG = Gen::RBX;
	const Gen::X64Reg CTXREG = Gen::R14;
	const Gen::X64Reg JITBASEREG = Gen::R15;
#else
	const Gen::X64Reg CTXREG = Gen::EBP;
#endif

		// This must be one of EAX, EBX, ECX, EDX as they have 8-bit subregisters.
	const Gen::X64Reg TEMPREG = Gen::EAX;
	const int NUM_MIPS_GPRS = 36;

#if PPSSPP_ARCH(AMD64)
	const u32 NUM_X_REGS = 16;
#elif PPSSPP_ARCH(X86)
	const u32 NUM_X_REGS = 8;
#endif
}

namespace MIPSAnalyst {
struct AnalysisResults;
};

struct MIPSCachedReg {
	Gen::OpArg location;
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
	MIPSCachedReg regs[X64JitConstants::NUM_MIPS_GPRS];
	X64CachedReg xregs[X64JitConstants::NUM_X_REGS];
};

namespace MIPSComp {
	struct JitOptions;
	struct JitState;
}

class GPRRegCache
{
public:
	GPRRegCache();
	~GPRRegCache() {}
	void Start(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo, MIPSAnalyst::AnalysisResults &stats);

	void DiscardRegContentsIfCached(MIPSGPReg preg);
	void DiscardR(MIPSGPReg preg);
	void SetEmitter(Gen::XEmitter *emitter) {emit = emitter;}

	void FlushR(Gen::X64Reg reg); 
	void FlushLockX(Gen::X64Reg reg) {
		FlushR(reg);
		LockX(reg);
	}
	void FlushLockX(Gen::X64Reg reg1, Gen::X64Reg reg2) {
		FlushR(reg1); FlushR(reg2);
		LockX(reg1); LockX(reg2);
	}
	void Flush();
	void FlushBeforeCall();

	// Flushes one register and reuses the register for another one. Dirtyness is implied.
	void FlushRemap(MIPSGPReg oldreg, MIPSGPReg newreg);

	int SanityCheck() const;
	void KillImmediate(MIPSGPReg preg, bool doLoad, bool makeDirty);

	void MapReg(MIPSGPReg preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(MIPSGPReg preg);

	const Gen::OpArg &R(MIPSGPReg preg) const {return regs[preg].location;}
	Gen::X64Reg RX(MIPSGPReg preg) const
	{
		if (regs[preg].away && regs[preg].location.IsSimpleReg()) 
			return regs[preg].location.GetSimpleReg(); 
		_assert_msg_(false, "Not so simple - %d", preg); 
		return (Gen::X64Reg)-1;
	}
	Gen::OpArg GetDefaultLocation(MIPSGPReg reg) const;

	// Register locking.
	void Lock(MIPSGPReg p1, MIPSGPReg p2 = MIPS_REG_INVALID, MIPSGPReg p3 = MIPS_REG_INVALID, MIPSGPReg p4 = MIPS_REG_INVALID);
	void LockX(int x1, int x2=0xff, int x3=0xff, int x4=0xff);
	void UnlockAll();
	void UnlockAllX();

	void SetImm(MIPSGPReg preg, u32 immValue);
	bool IsImm(MIPSGPReg preg) const;
	u32 GetImm(MIPSGPReg preg) const;

	void GetState(GPRRegCacheState &state) const;
	void RestoreState(const GPRRegCacheState& state);

	MIPSState *mips_ = nullptr;

private:
	Gen::X64Reg GetFreeXReg();
	Gen::X64Reg FindBestToSpill(bool unusedOnly, bool *clobbered);
	const Gen::X64Reg *GetAllocationOrder(int &count);

	MIPSCachedReg regs[X64JitConstants::NUM_MIPS_GPRS]{};
	X64CachedReg xregs[X64JitConstants::NUM_X_REGS]{};

	Gen::XEmitter *emit = nullptr;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;
};
