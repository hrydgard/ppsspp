// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "ArmABI.h"
#include "ArmRegCache.h"
#include "ArmEmitter.h"

using namespace ArmGen;

#define CTXREG ((ARMReg)14)

ArmRegCache::ArmRegCache(MIPSState *mips) : mips_(mips) {
}

void ArmRegCache::Init(ARMXEmitter *emitter) {
	emit = emitter;
}

void ArmRegCache::Start(MIPSAnalyst::AnalysisResults &stats) {
	for (int i = 0; i < 16; i++) {
		ar[i].mipsReg = -1;
		ar[i].spillLock = false;
		ar[i].allocLock = false;
		ar[i].isDirty = false;
	}
	for (int i = 0; i < 32; i++) {
		mr[i].loc = ML_MEM;
	}
}

static const ARMReg *GetMIPSAllocationOrder(int &count) {
	// Note that R0 and R1 are reserved as scratch for now. We can probably free up R1 eventually.
	static const ARMReg allocationOrder[] = {
		R2, R3, R4, R5, R6, R7, R8, R9
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

ARMReg ArmRegCache::MapReg(MIPSReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped.
	for (int i = 0; i < NUM_ARMREG; i++) {
		if (ar[i].mipsReg == mipsReg) {
			// Already mapped, no need to do anything more.
			return (ARMReg)i;
		}
	}

	// Okay, so we need to allocate one.

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i];

		if (ar[reg].mipsReg == -1 && !ar[reg].allocLock) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].mipsReg = mipsReg;
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if (mapFlags & MAP_INITVAL) {
				if (mr[mipsReg].loc == ML_MEM)
					emit->LDR((ARMReg)reg, CTXREG, 4 * mipsReg);
				else if (mr[mipsReg].loc == ML_IMM)
					emit->ARMABI_MOVI2R((ARMReg)reg, mr[mipsReg].imm);
			}
			return (ARMReg)reg;
		}
	}

	// Still nothing. Let's spill a reg and goto 10

	for (int i = 0; i < allocCount; i++) {
		if (ar[i].spillLock || ar[i].allocLock)
			continue;
		FlushArmReg((ARMReg)i);
		goto allocate;
	}

	// Uh oh, we have all them alloclocked and spilllocked....
	_assert_msg_(JIT, false, "All available registers are locked dumb dumb");
	return INVALID_REG;
}

void ArmRegCache::FlushArmReg(ARMReg r) {
	if (ar[r].mipsReg == -1) {
		// Nothing to do
		return;
	}
	if (ar[r].isDirty) {
		if (mr[ar[r].mipsReg].loc == ML_MEM)
			emit->STR(r, CTXREG, 4 * ar[r].mipsReg);
	}
}

void ArmRegCache::FlushMipsReg(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		emit->ARMABI_MOVI2R(R0, mr[r].imm);
		emit->STR(R0, CTXREG, GetMipsRegOffset(r));
		break;

	case ML_ARMREG:
		if (ar[mr[r].reg].isDirty)
			emit->STR(mr[r].reg, CTXREG, GetMipsRegOffset(r));
		ar[mr[r].reg].mipsReg = -1;
		ar[mr[r].reg].isDirty = false;
		break;

	default:
		//BAD
		break;
	}
	mr[r].loc = ML_MEM;
}

void ArmRegCache::FlushAll() {
	for (int i = 0; i < NUM_MIPSREG; i++) {
		FlushMipsReg(i);
	}
}

void ArmRegCache::SetImm(MIPSReg r, u32 immVal) {
	// Zap existing value
	if (mr[r].loc == ML_ARMREG)
		ar[mr[r].reg].mipsReg = -1;
	mr[r].loc = ML_IMM;
	mr[r].imm = immVal;
}

bool ArmRegCache::IsImm(MIPSReg r) const {
	return mr[r].loc == ML_IMM;
}

u32 ArmRegCache::GetImm(MIPSReg r) const {
	// TODO: Check.
	return mr[r].imm;
}

int ArmRegCache::GetMipsRegOffset(MIPSReg r) {
	if (r < 32)
		return r * 4;
	switch (r) {
	case MIPSREG_HI:
		return offsetof(MIPSState, hi);
	case MIPSREG_LO:
		return offsetof(MIPSState, lo);
	}
	_dbg_assert_msg_(JIT, false, "bad mips register %i", (int)r);
	return -999;  // boom!
}

void ArmRegCache::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3) {
	if (mr[r1].loc == ML_ARMREG) ar[mr[r1].reg].spillLock = true;
	if (r2 != -1 && mr[r2].loc == ML_ARMREG) ar[mr[r2].reg].spillLock = true;
	if (r3 != -1 && mr[r3].loc == ML_ARMREG) ar[mr[r3].reg].spillLock = true;
}

void ArmRegCache::ReleaseSpillLocks() {
	for (int i = 0; i < 16; i++) {
		ar[i].spillLock = false;
	}
}

ARMReg ArmRegCache::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return mr[mipsReg].reg;
	} else {
		_dbg_assert_msg_(JIT, false, "R: not mapped");
		return INVALID_REG;  // BAAAD
	}
}

