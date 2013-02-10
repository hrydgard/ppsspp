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
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"


using namespace ArmGen;

#define CTXREG (R10)

ArmRegCacheFPU::ArmRegCacheFPU(MIPSState *mips) : mips_(mips) {
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
		mr[i].imm = -1;
		mr[i].spillLock = false;
	}
}

static const ARMReg *GetMIPSAllocationOrder(int &count) {
	// We conservatively reserve both S0 and S1 as scratch for now.
	// Will probably really only need one, if that.
	static const ARMReg allocationOrder[] = {
		S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
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
				if (mr[mipsReg].loc == ML_MEM) {
					emit->VLDR((ARMReg)(reg + S0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			return (ARMReg)(reg + S0);
		}
	}

	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	int bestToSpill = -1;
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - S0;
		if (ar[reg].mipsReg != -1 && mr[ar[reg].mipsReg].spillLock)
			continue;
		bestToSpill = reg;
		break;
	}

	if (bestToSpill != -1) {
		// ERROR_LOG(JIT, "Out of registers at PC %08x - spills register %i.", mips_->pc, bestToSpill);
		FlushArmReg((ARMReg)bestToSpill);
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

void ArmRegCacheFPU::FlushArmReg(ARMReg r) {
	int reg = r - S0;
	if (ar[reg].mipsReg == -1) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (ar[reg].mipsReg != -1) {
		if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_ARMREG)
			emit->VSTR(CTXREG, r, GetMipsRegOffset(ar[reg].mipsReg));
		// IMMs won't be in an ARM reg.
		mr[ar[reg].mipsReg].loc = ML_MEM;
		mr[ar[reg].mipsReg].reg = INVALID_REG;
		mr[ar[reg].mipsReg].imm = 0;
	} else {
		ERROR_LOG(HLE, "Dirty but no mipsreg?");
	}
	ar[reg].isDirty = false;
	ar[reg].mipsReg = -1;
}

void ArmRegCacheFPU::FlushMipsReg(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(HLE, "Imm in FP register?");
		break;

	case ML_ARMREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(HLE, "FlushMipsReg: MipsReg had bad ArmReg");
		}
		if (ar[mr[r].reg].isDirty) {
			emit->VSTR(CTXREG, (ARMReg)(mr[r].reg + S0), GetMipsRegOffset(r));
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
	mr[r].imm = 0;
}

void ArmRegCacheFPU::FlushAll() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		FlushMipsReg(i);
	}
	// Sanity check
	for (int i = 0; i < NUM_ARMFPUREG; i++) {
		if (ar[i].mipsReg != -1) {
			ERROR_LOG(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

void ArmRegCacheFPU::SetImm(MIPSReg r, u32 immVal) {
	// Zap existing value if cached in a reg
	if (mr[r].loc == ML_ARMREG) {
		ar[mr[r].reg].mipsReg = -1;
		ar[mr[r].reg].isDirty = false;
	}
	mr[r].loc = ML_IMM;
	mr[r].imm = immVal;
	mr[r].reg = INVALID_REG;
}

bool ArmRegCacheFPU::IsImm(MIPSReg r) const {
	return mr[r].loc == ML_IMM;
}

u32 ArmRegCacheFPU::GetImm(MIPSReg r) const {
	if (mr[r].loc != ML_IMM) {
		ERROR_LOG(JIT, "Trying to get imm from non-imm register %i", r);
	}
	return mr[r].imm;
}

int ArmRegCacheFPU::GetMipsRegOffset(MIPSReg r) {
	// These are offsets within the MIPSState structure. First there are the GPRS, then FPRS, then the "VFPURs".
	if (r < 32)
		return (r + 32) * 4;
	else if (r < 32 + 128)
		return (r + 64) * 4;
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
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mr[i].spillLock = false;
	}
}

ARMReg ArmRegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARMReg)(mr[mipsReg].reg + S0);
	} else {
		ERROR_LOG(JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}
