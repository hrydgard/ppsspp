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


#include "Common/Log.h"
#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/x86/RegCacheFPU.h"

u32 FPURegCache::tempValues[NUM_TEMPS];

FPURegCache::FPURegCache() : mips(0), emit(0) {
	memset(regs, 0, sizeof(regs));
	memset(xregs, 0, sizeof(xregs));
	vregs = regs + 32;
}

void FPURegCache::Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats) {
	this->mips = mips;
	for (int i = 0; i < NUM_X_FPREGS; i++) {
		xregs[i].mipsReg = -1;
		xregs[i].dirty = false;
	}
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		regs[i].location = GetDefaultLocation(i);
		regs[i].away = false;
		regs[i].locked = false;
		regs[i].tempLocked = false;
	}
}

void FPURegCache::SpillLock(int p1, int p2, int p3, int p4) {
	regs[p1].locked = true;
	if (p2 != 0xFF) regs[p2].locked = true;
	if (p3 != 0xFF) regs[p3].locked = true;
	if (p4 != 0xFF) regs[p4].locked = true;
}

void FPURegCache::SpillLockV(const u8 *v, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[v[i]].locked = true;
	}
}

void FPURegCache::SpillLockV(int vec, VectorSize sz) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
}

void FPURegCache::MapRegV(int vreg, int flags) {
	BindToRegister(vreg + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
}

void FPURegCache::MapRegsV(int vec, VectorSize sz, int flags) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		BindToRegister(v[i] + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
	}
}

void FPURegCache::MapRegsV(const u8 *v, VectorSize sz, int flags) {
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		BindToRegister(v[i] + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
	}
}

void FPURegCache::ReleaseSpillLock(int mipsreg)
{
	regs[mipsreg].locked = false;
}

void FPURegCache::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPS_FPRS; i++)
		regs[i].locked = false;
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i)
		DiscardR(i);
}

void FPURegCache::BindToRegister(const int i, bool doLoad, bool makeDirty) {
	_assert_msg_(DYNA_REC, !regs[i].location.IsImm(), "WTF - load - imm");
	if (!regs[i].away) {
		// Reg is at home in the memory register file. Let's pull it out.
		X64Reg xr = GetFreeXReg();
		_assert_msg_(DYNA_REC, xr < NUM_X_FPREGS, "WTF - load - invalid reg");
		xregs[xr].mipsReg = i;
		xregs[xr].dirty = makeDirty;
		OpArg newloc = ::Gen::R(xr);
		if (doLoad)	{
			if (!regs[i].location.IsImm() && (regs[i].location.offset & 0x3)) {
				PanicAlert("WARNING - misaligned fp register location %i", i);
			}
			emit->MOVSS(xr, regs[i].location);
		}
		regs[i].location = newloc;
		regs[i].away = true;
	} else {
		// There are no immediates in the FPR reg file, so we already had this in a register. Make dirty as necessary.
		xregs[RX(i)].dirty |= makeDirty;
		_assert_msg_(DYNA_REC, regs[i].location.IsSimpleReg(), "not loaded and not simple.");
	}
}

void FPURegCache::StoreFromRegister(int i) {
	_assert_msg_(DYNA_REC, !regs[i].location.IsImm(), "WTF - store - imm");
	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(DYNA_REC, xr < NUM_X_FPREGS, "WTF - store - invalid reg");
		xregs[xr].dirty = false;
		xregs[xr].mipsReg = -1;
		OpArg newLoc = GetDefaultLocation(i);
		emit->MOVSS(newLoc, xr);
		regs[i].location = newLoc;
		regs[i].away = false;
	} else {
		//	_assert_msg_(DYNA_REC,0,"already stored");
	}
}

void FPURegCache::DiscardR(int i) {
	_assert_msg_(DYNA_REC, !regs[i].location.IsImm(), "FPU can't handle imm yet.");
	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(DYNA_REC, xr < NUM_X_FPREGS, "DiscardR: MipsReg had bad X64Reg");
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		xregs[xr].dirty = false;
		xregs[xr].mipsReg = -1;
		regs[i].location = GetDefaultLocation(i);
		regs[i].away = false;
		regs[i].tempLocked = false;
	} else {
		//	_assert_msg_(DYNA_REC,0,"already stored");
		regs[i].tempLocked = false;
	}
}

bool FPURegCache::IsTempX(X64Reg xr) {
	return xregs[xr].mipsReg >= TEMP0;
}

int FPURegCache::GetTempR() {
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (!regs[r].away && !regs[r].tempLocked) {
			regs[r].tempLocked = true;
			return r;
		}
	}

	_assert_msg_(DYNA_REC, 0, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

void FPURegCache::Flush() {
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		if (regs[i].locked) {
			PanicAlert("Somebody forgot to unlock MIPS reg %i.", i);
		}
		if (regs[i].away) {
			if (regs[i].location.IsSimpleReg()) {
				X64Reg xr = RX(i);
				StoreFromRegister(i);
				xregs[xr].dirty = false;
			} else if (regs[i].location.IsImm()) {
				StoreFromRegister(i);
			} else {
				_assert_msg_(DYNA_REC,0,"Jit64 - Flush unhandled case, reg %i PC: %08x", i, mips->pc);
			}
		}
	}
}

OpArg FPURegCache::GetDefaultLocation(int reg) const {
	if (reg < 32) {
		return M(&mips->f[reg]);
	} else if (reg < 32 + 128) {
		return M(&mips->v[reg - 32]);
	} else {
		return M(&tempValues[reg - 32 - 128]);
	}
}

int FPURegCache::SanityCheck() const {
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		if (regs[i].away) {
			if (regs[i].location.IsSimpleReg()) {
				Gen::X64Reg simple = regs[i].location.GetSimpleReg();
				if (xregs[simple].mipsReg != i)
					return 2;
			}
			else if (regs[i].location.IsImm())
				return 3;
		}
	}
	return 0;
}

const int *FPURegCache::GetAllocationOrder(int &count) {
	static const int allocationOrder[] = {
#ifdef _M_X64
		XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, XMM2, XMM3, XMM4, XMM5
#elif _M_IX86
		XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
#endif
	};
	count = sizeof(allocationOrder) / sizeof(int);
	return allocationOrder;
}

X64Reg FPURegCache::GetFreeXReg() {
	int aCount;
	const int *aOrder = GetAllocationOrder(aCount);
	for (int i = 0; i < aCount; i++) {
		X64Reg xr = (X64Reg)aOrder[i];
		if (xregs[xr].mipsReg == -1) {
			return (X64Reg)xr;
		}
	}
	//Okay, not found :( Force grab one

	//TODO - add a pass to grab xregs whose mipsreg is not used in the next 3 instructions
	for (int i = 0; i < aCount; i++) {
		X64Reg xr = (X64Reg)aOrder[i];
		int preg = xregs[xr].mipsReg;
		if (!regs[preg].locked) {
			StoreFromRegister(preg);
			return xr;
		}
	}
	//Still no dice? Die!
	_assert_msg_(DYNA_REC, 0, "Regcache ran out of regs");
	return (X64Reg) -1;
}

void FPURegCache::FlushX(X64Reg reg) {
	if (reg >= NUM_X_FPREGS)
		PanicAlert("Flushing non existent reg");
	if (xregs[reg].mipsReg != -1) {
		StoreFromRegister(xregs[reg].mipsReg);
	}
}
