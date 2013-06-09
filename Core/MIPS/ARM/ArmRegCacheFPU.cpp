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

#include "Common/ArmEmitter.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"


using namespace ArmGen;


ArmRegCacheFPU::ArmRegCacheFPU(MIPSState *mips) : mips_(mips), vr(mr + 32) {
}

void ArmRegCacheFPU::Init(ARMXEmitter *emitter) {
	emit = emitter;
}

void ArmRegCacheFPU::Start(MIPSAnalyst::AnalysisResults &stats) {
	for (int i = 0; i < NUM_ARMFPUREG; i++) {
		ar[i].mipsReg = -1;
		ar[i].isDirty = false;
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mr[i].loc = ML_MEM;
		mr[i].reg = INVALID_REG;
		mr[i].spillLock = false;
		mr[i].tempLock = false;
	}
}

static const ARMReg *GetMIPSAllocationOrder(int &count) {
	// We conservatively reserve both S0 and S1 as scratch for now.
	// Will probably really only need one, if that.
	static const ARMReg allocationOrder[] = {
		S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15
	};
	// With NEON, we have many more.
	static const ARMReg allocationOrderNEON[] = {
		S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15,
		S16, S17, S18, S19, S20, S21, S22, S23, S24, S25, S26, S27, S28, S29, S30, S31
	};
	if (false && cpu_info.bNEON) {
		count = sizeof(allocationOrderNEON) / sizeof(const int);
		return allocationOrderNEON;
	} else {
		count = sizeof(allocationOrder) / sizeof(const int);
		return allocationOrder;
	}
}

ARMReg ArmRegCacheFPU::MapReg(MIPSReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG) {
		if (ar[mr[mipsReg].reg].mipsReg != mipsReg) {
			ERROR_LOG(HLE, "Register mapping out of sync! %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			ar[mr[mipsReg].reg].isDirty = true;
		}
		//INFO_LOG(HLE, "Already mapped %i to %i", mipsReg, mr[mipsReg].reg);
		return (ARMReg)(mr[mipsReg].reg + S0);
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - S0;

		if (ar[reg].mipsReg == -1) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if (!(mapFlags & MAP_NOINIT)) {
				if (mr[mipsReg].loc == ML_MEM && mipsReg < TEMP0) {
					emit->VLDR((ARMReg)(reg + S0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			//INFO_LOG(HLE, "Mapped %i to %i", mipsReg, mr[mipsReg].reg);
			return (ARMReg)(reg + S0);
		}
	}


	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	int bestToSpill = -1;
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - S0;
		if (ar[reg].mipsReg != -1 && (mr[ar[reg].mipsReg].spillLock || mr[ar[reg].mipsReg].tempLock))
			continue;
		bestToSpill = reg;
		break;
	}

	if (bestToSpill != -1) {
		FlushArmReg((ARMReg)(S0 + bestToSpill));
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG(JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
	return INVALID_REG;
}

void ArmRegCacheFPU::MapInIn(MIPSReg rd, MIPSReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCacheFPU::MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool overlap = avoidLoad && rd == rs;
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCacheFPU::MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool overlap = avoidLoad && (rd == rs || rd == rt);
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCacheFPU::SpillLockV(const u8 *v, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vr[v[i]].spillLock = true;
	}
}

void ArmRegCacheFPU::SpillLockV(int vec, VectorSize sz) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
}

void ArmRegCacheFPU::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, flags);
}

void ArmRegCacheFPU::MapRegsV(int vec, VectorSize sz, int flags) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void ArmRegCacheFPU::MapRegsV(const u8 *v, VectorSize sz, int flags) {
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void ArmRegCacheFPU::FlushArmReg(ARMReg r) {
	int reg = r - S0;
	if (ar[reg].mipsReg == -1) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (ar[reg].mipsReg != -1) {
		if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_ARMREG)
		{
			//INFO_LOG(HLE, "Flushing ARM reg %i", reg);
			emit->VSTR(r, CTXREG, GetMipsRegOffset(ar[reg].mipsReg));
		}
		// IMMs won't be in an ARM reg.
		mr[ar[reg].mipsReg].loc = ML_MEM;
		mr[ar[reg].mipsReg].reg = INVALID_REG;
	} else {
		ERROR_LOG(HLE, "Dirty but no mipsreg?");
	}
	ar[reg].isDirty = false;
	ar[reg].mipsReg = -1;
}

void ArmRegCacheFPU::FlushR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(HLE, "Imm in FP register?");
		break;

	case ML_ARMREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(HLE, "FlushR: MipsReg had bad ArmReg");
		}
		if (ar[mr[r].reg].isDirty) {
			//INFO_LOG(HLE, "Flushing dirty reg %i", mr[r].reg);
			emit->VSTR((ARMReg)(mr[r].reg + S0), CTXREG, GetMipsRegOffset(r));
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = -1;
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		//BAD
		break;
	}
	mr[r].loc = ML_MEM;
	mr[r].reg = (int)INVALID_REG;
}

void ArmRegCacheFPU::DiscardR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(HLE, "Imm in FP register?");
		break;
		 
	case ML_ARMREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(HLE, "DiscardR: MipsReg had bad ArmReg");
		}
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		ar[mr[r].reg].isDirty = false;
		ar[mr[r].reg].mipsReg = -1;
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		//BAD
		break;
	}
	mr[r].loc = ML_MEM;
	mr[r].reg = (int)INVALID_REG;
	mr[r].tempLock = false;
	// spill lock?
}


bool ArmRegCacheFPU::IsTempX(ARMReg r) const {
	return ar[r - S0].mipsReg >= TEMP0;
}

int ArmRegCacheFPU::GetTempR() {
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (mr[r].loc == ML_MEM && !mr[r].tempLock) {
			mr[r].tempLock = true;
			return r;
		}
	}

	_assert_msg_(DYNA_REC, 0, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}


void ArmRegCacheFPU::FlushAll() {
	// Discard temps!
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; i++) {
		DiscardR(i);
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		FlushR(i);
	} 
	// Sanity check
	for (int i = 0; i < NUM_ARMFPUREG; i++) {
		if (ar[i].mipsReg != -1) {
			ERROR_LOG(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

int ArmRegCacheFPU::GetMipsRegOffset(MIPSReg r) {
	// These are offsets within the MIPSState structure. First there are the GPRS, then FPRS, then the "VFPURs".
	if (r < 32 + 128 + NUM_TEMPS)
		return (r + 32) << 2;
	ERROR_LOG(JIT, "bad mips register %i", r);
	return 0;  // or what?
}

void ArmRegCacheFPU::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3, MIPSReg r4) {
	mr[r1].spillLock = true;
	if (r2 != -1) mr[r2].spillLock = true;
	if (r3 != -1) mr[r3].spillLock = true;
	if (r4 != -1) mr[r4].spillLock = true;
}

// This is actually pretty slow with all the 160 regs...
void ArmRegCacheFPU::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++)
		mr[i].spillLock = false;
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i)
		DiscardR(i);
}

ARMReg ArmRegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARMReg)(mr[mipsReg].reg + S0);
	} else {
		ERROR_LOG(JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}
