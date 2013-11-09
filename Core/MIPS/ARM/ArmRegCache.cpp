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

#include "ArmRegCache.h"
#include "ArmEmitter.h"
#include "ArmJit.h"

#if defined(MAEMO)
#include "stddef.h"
#endif

using namespace ArmGen;

ArmRegCache::ArmRegCache(MIPSState *mips, MIPSComp::ArmJitOptions *options) : mips_(mips), options_(options) {
}

void ArmRegCache::Init(ARMXEmitter *emitter) {
	emit_ = emitter;
}

void ArmRegCache::Start(MIPSAnalyst::AnalysisResults &stats) {
	for (int i = 0; i < NUM_ARMREG; i++) {
		ar[i].mipsReg = MIPS_REG_INVALID;
		ar[i].isDirty = false;
	}
	for (int i = 0; i < NUM_MIPSREG; i++) {
		mr[i].loc = ML_MEM;
		mr[i].reg = INVALID_REG;
		mr[i].imm = -1;
		mr[i].spillLock = false;
	}
}

const ARMReg *ArmRegCache::GetMIPSAllocationOrder(int &count) {
	// Note that R0 is reserved as scratch for now.
	// R1 could be used as it's only used for scratch outside "regalloc space" now.
	// R12 is also potentially usable.
	// R4-R7 are registers we could use for static allocation or downcount.
	// R8 is used to preserve flags in nasty branches.
	// R9 and upwards are reserved for jit basics.
	if (options_->downcountInRegister) {
		static const ARMReg allocationOrder[] = {
			R2, R3, R4, R5, R6, R12,
		};
		count = sizeof(allocationOrder) / sizeof(const int);
		return allocationOrder;
	} else {
		static const ARMReg allocationOrder2[] = {
			R2, R3, R4, R5, R6, R7, R12,
		};
		count = sizeof(allocationOrder2) / sizeof(const int);
		return allocationOrder2;
	}
}

void ArmRegCache::FlushBeforeCall() {
	// R4-R11 are preserved. Others need flushing.
	FlushArmReg(R2);
	FlushArmReg(R3);
	FlushArmReg(R12);
}

ARMReg ArmRegCache::MapRegAsPointer(MIPSGPReg mipsReg) {  // read-only, non-dirty.
	// If already mapped as a pointer, bail.
	if (mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		return mr[mipsReg].reg;
	}
	// First, make sure the register is already mapped.
	MapReg(mipsReg, 0);
	// If it's dirty, flush it.
	ARMReg armReg = mr[mipsReg].reg;
	if (ar[armReg].isDirty) {
		emit_->STR(armReg, CTXREG, GetMipsRegOffset(ar[armReg].mipsReg));
	}
	// Convert to a pointer by adding the base and clearing off the top bits.
	// If SP, we can probably avoid the top bit clear, let's play with that later.
	emit_->BIC(armReg, armReg, Operand2(0xC0, 4));    // &= 0x3FFFFFFF
	emit_->ADD(armReg, R11, armReg);
	ar[armReg].isDirty = false;
	ar[armReg].mipsReg = mipsReg;
	mr[mipsReg].loc = ML_ARMREG_AS_PTR;
	return armReg;
}

bool ArmRegCache::IsMappedAsPointer(MIPSGPReg mipsReg) {
	return mr[mipsReg].loc == ML_ARMREG_AS_PTR;
}

// TODO: Somewhat smarter spilling - currently simply spills the first available, should do
// round robin or FIFO or something.
ARMReg ArmRegCache::MapReg(MIPSGPReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG) {
		ARMReg armReg = mr[mipsReg].reg;
		if (ar[armReg].mipsReg != mipsReg) {
			ERROR_LOG(JIT, "Register mapping out of sync! %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			ar[armReg].isDirty = true;
		}
		return (ARMReg)mr[mipsReg].reg;
	} else if (mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		// Was mapped as pointer, now we want it mapped as a value, presumably to
		// add or subtract stuff to it. Later we could allow such things but for now
		// let's just convert back to a register value by reloading from the backing storage.
		ARMReg armReg = mr[mipsReg].reg;
		emit_->LDR(armReg, CTXREG, GetMipsRegOffset(mipsReg));
		mr[mipsReg].loc = ML_ARMREG;
		if (mapFlags & MAP_DIRTY) {
			ar[armReg].isDirty = true;
		}
		return (ARMReg)mr[mipsReg].reg;
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		ARMReg reg = allocOrder[i];

		if (ar[reg].mipsReg == MIPS_REG_INVALID) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if (!(mapFlags & MAP_NOINIT)) {
				if (mipsReg == 0) {
					// If we get a request to load the zero register, at least we won't spend
					// time on a memory access...
					emit_->MOV(reg, 0);
				} else {
					if (mr[mipsReg].loc == ML_MEM) {
						emit_->LDR(reg, CTXREG, GetMipsRegOffset(mipsReg));
					} else if (mr[mipsReg].loc == ML_IMM) {
						emit_->MOVI2R(reg, mr[mipsReg].imm);
						ar[reg].isDirty = true;  // IMM is always dirty.
					}
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			return reg;
		}
	}

	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	ARMReg bestToSpill = INVALID_REG;
	for (int i = 0; i < allocCount; i++) {
		ARMReg reg = allocOrder[i];
		if (ar[reg].mipsReg != MIPS_REG_INVALID && mr[ar[reg].mipsReg].spillLock)
			continue;
		bestToSpill = reg;
		break;
	}

	if (bestToSpill != INVALID_REG) {
		// ERROR_LOG(JIT, "Out of registers at PC %08x - spills register %i.", mips_->pc, bestToSpill);
		FlushArmReg(bestToSpill);
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG(JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
	return INVALID_REG;
}

void ArmRegCache::MapInIn(MIPSGPReg rd, MIPSGPReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, MAP_DIRTY | (load ? 0 : MAP_NOINIT));
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, MAP_DIRTY | (load ? 0 : MAP_NOINIT));
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd1, rd2, rs, rt);
	bool load1 = !avoidLoad || (rd1 == rs || rd1 == rt);
	bool load2 = !avoidLoad || (rd2 == rs || rd2 == rt);
	MapReg(rd1, MAP_DIRTY | (load1 ? 0 : MAP_NOINIT));
	MapReg(rd2, MAP_DIRTY | (load2 ? 0 : MAP_NOINIT));
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::FlushArmReg(ARMReg r) {
	if (ar[r].mipsReg == MIPS_REG_INVALID) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (ar[r].mipsReg != MIPS_REG_INVALID) {
		if (ar[r].isDirty && mr[ar[r].mipsReg].loc == ML_ARMREG)
			emit_->STR(r, CTXREG, GetMipsRegOffset(ar[r].mipsReg));
		// IMMs won't be in an ARM reg.
		mr[ar[r].mipsReg].loc = ML_MEM;
		mr[ar[r].mipsReg].reg = INVALID_REG;
		mr[ar[r].mipsReg].imm = 0;
	} else {
		ERROR_LOG(JIT, "Dirty but no mipsreg?");
	}
	ar[r].isDirty = false;
	ar[r].mipsReg = MIPS_REG_INVALID;
}

void ArmRegCache::DiscardR(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		ARMReg armReg = mr[mipsReg].reg;
		ar[armReg].isDirty = false;
		ar[armReg].mipsReg = MIPS_REG_INVALID;
		mr[mipsReg].reg = INVALID_REG;
		mr[mipsReg].loc = ML_MEM;
		mr[mipsReg].imm = 0;
	}
}

void ArmRegCache::FlushR(MIPSGPReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		if (r != MIPS_REG_ZERO) {
			emit_->MOVI2R(R0, mr[r].imm);
			emit_->STR(R0, CTXREG, GetMipsRegOffset(r));
		}
		break;

	case ML_ARMREG:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG(JIT, "FlushMipsReg: MipsReg had bad ArmReg");
		}
		if (ar[mr[r].reg].isDirty) {
			if (r != MIPS_REG_ZERO) {
				emit_->STR((ARMReg)mr[r].reg, CTXREG, GetMipsRegOffset(r));
			}
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		break;

	case ML_ARMREG_AS_PTR:
		// Never dirty.
		if (ar[mr[r].reg].isDirty) {
			ERROR_LOG(JIT, "ARMREG_AS_PTR cannot be dirty (yet)");
		}
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		//BAD
		break;
	}
	mr[r].loc = ML_MEM;
	mr[r].reg = INVALID_REG;
	mr[r].imm = 0;
}

void ArmRegCache::FlushAll() {
	for (int i = 0; i < NUM_MIPSREG; i++) {
		FlushR(MIPSGPReg(i));
	}
	// Sanity check
	for (int i = 0; i < NUM_ARMREG; i++) {
		if (ar[i].mipsReg != MIPS_REG_INVALID) {
			ERROR_LOG(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

void ArmRegCache::SetImm(MIPSGPReg r, u32 immVal) {
	if (r == MIPS_REG_ZERO)
		ERROR_LOG(JIT, "Trying to set immediate %08x to r0", immVal);

	// Zap existing value if cached in a reg
	if (mr[r].loc == ML_ARMREG || mr[r].loc == ML_ARMREG_AS_PTR) {
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		ar[mr[r].reg].isDirty = false;
	}
	mr[r].loc = ML_IMM;
	mr[r].imm = immVal;
	mr[r].reg = INVALID_REG;
}

bool ArmRegCache::IsImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return true;
	return mr[r].loc == ML_IMM;
}

u32 ArmRegCache::GetImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return 0;
	if (mr[r].loc != ML_IMM) {
		ERROR_LOG(JIT, "Trying to get imm from non-imm register %i", r);
	}
	return mr[r].imm;
}

int ArmRegCache::GetMipsRegOffset(MIPSGPReg r) {
	if (r < 32)
		return r * 4;
	switch (r) {
	case MIPS_REG_HI:
		return offsetof(MIPSState, hi);
	case MIPS_REG_LO:
		return offsetof(MIPSState, lo);
	default:
		ERROR_LOG(JIT, "bad mips register %i", r);
		return 0;  // or what?
	}
}

void ArmRegCache::SpillLock(MIPSGPReg r1, MIPSGPReg r2, MIPSGPReg r3, MIPSGPReg r4) {
	mr[r1].spillLock = true;
	if (r2 != MIPS_REG_INVALID) mr[r2].spillLock = true;
	if (r3 != MIPS_REG_INVALID) mr[r3].spillLock = true;
	if (r4 != MIPS_REG_INVALID) mr[r4].spillLock = true;
}

void ArmRegCache::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPSREG; i++) {
		mr[i].spillLock = false;
	}
}

void ArmRegCache::ReleaseSpillLock(MIPSGPReg reg) {
	mr[reg].spillLock = false;
}

ARMReg ArmRegCache::R(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARMReg)mr[mipsReg].reg;
	} else {
		ERROR_LOG(JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}

ARMReg ArmRegCache::RPtr(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		return (ARMReg)mr[mipsReg].reg;
	} else {
		ERROR_LOG(JIT, "Reg %i not in arm reg as pointer. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}

