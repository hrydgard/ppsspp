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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include "Core/MemMap.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/Reporting.h"
#include "Common/ArmEmitter.h"

#ifndef offsetof
#include "stddef.h"
#endif

using namespace ArmGen;
using namespace ArmJitConstants;

ArmRegCache::ArmRegCache(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo) : mips_(mipsState), js_(js), jo_(jo) {
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
	// R12 is also potentially usable.
	// R4-R7 are registers we could use for static allocation or downcount.
	// R8 is used to preserve flags in nasty branches.
	// R9 and upwards are reserved for jit basics.
	// R14 (LR) is used as a scratch reg (overwritten on calls/return.)
	if (jo_->downcountInRegister) {
		static const ARMReg allocationOrder[] = {
			R1, R2, R3, R4, R5, R6, R12,
		};
		count = sizeof(allocationOrder) / sizeof(const int);
		return allocationOrder;
	} else {
		static const ARMReg allocationOrder2[] = {
			R1, R2, R3, R4, R5, R6, R7, R12,
		};
		count = sizeof(allocationOrder2) / sizeof(const int);
		return allocationOrder2;
	}
}

void ArmRegCache::FlushBeforeCall() {
	// R4-R11 are preserved. Others need flushing.
	FlushArmReg(R1);
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
	emit_->ADD(armReg, MEMBASEREG, armReg);
	ar[armReg].isDirty = false;
	ar[armReg].mipsReg = mipsReg;
	mr[mipsReg].loc = ML_ARMREG_AS_PTR;
	return armReg;
}

bool ArmRegCache::IsMappedAsPointer(MIPSGPReg mipsReg) {
	return mr[mipsReg].loc == ML_ARMREG_AS_PTR;
}

bool ArmRegCache::IsMapped(MIPSGPReg mipsReg) {
	return mr[mipsReg].loc == ML_ARMREG;
}

void ArmRegCache::SetRegImm(ARMReg reg, u32 imm) {
	// If we can do it with a simple Operand2, let's do that.
	Operand2 op2;
	bool inverse;
	if (TryMakeOperand2_AllowInverse(imm, op2, &inverse)) {
		if (!inverse)
			emit_->MOV(reg, op2);
		else
			emit_->MVN(reg, op2);
		return;
	}

	// Okay, so it's a bit more complex.  Let's see if we have any useful regs with imm values.
	for (int i = 0; i < NUM_MIPSREG; i++) {
		const auto &mreg = mr[i];
		if (mreg.loc != ML_ARMREG_IMM)
			continue;

		if (mreg.imm - imm < 256) {
			emit_->SUB(reg, mreg.reg, mreg.imm - imm);
			return;
		}
		if (imm - mreg.imm < 256) {
			emit_->ADD(reg, mreg.reg, imm - mreg.imm);
			return;
		}
		// This could be common when using an address.
		if ((mreg.imm & 0x3FFFFFFF) == imm) {
			emit_->BIC(reg, mreg.reg, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
			return;
		}
		// TODO: All sorts of things are possible here, shifted adds, ands/ors, etc.
	}

	// No luck.  Let's go with a regular load.
	emit_->MOVI2R(reg, imm);
}

void ArmRegCache::MapRegTo(ARMReg reg, MIPSGPReg mipsReg, int mapFlags) {
	ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
	if ((mapFlags & MAP_NOINIT) != MAP_NOINIT) {
		if (mipsReg == MIPS_REG_ZERO) {
			// If we get a request to load the zero register, at least we won't spend
			// time on a memory access...
			// TODO: EOR?
			emit_->MOV(reg, 0);

			// This way, if we SetImm() it, we'll keep it.
			mr[mipsReg].loc = ML_ARMREG_IMM;
			mr[mipsReg].imm = 0;
		} else {
			switch (mr[mipsReg].loc) {
			case ML_MEM:
				emit_->LDR(reg, CTXREG, GetMipsRegOffset(mipsReg));
				mr[mipsReg].loc = ML_ARMREG;
				break;
			case ML_IMM:
				SetRegImm(reg, mr[mipsReg].imm);
				ar[reg].isDirty = true;  // IMM is always dirty.

				// If we are mapping dirty, it means we're gonna overwrite.
				// So the imm value is no longer valid.
				if (mapFlags & MAP_DIRTY)
					mr[mipsReg].loc = ML_ARMREG;
				else
					mr[mipsReg].loc = ML_ARMREG_IMM;
				break;
			default:
				mr[mipsReg].loc = ML_ARMREG;
				break;
			}
		}
	} else {
		mr[mipsReg].loc = ML_ARMREG;
	}
	ar[reg].mipsReg = mipsReg;
	mr[mipsReg].reg = reg;
}

ARMReg ArmRegCache::FindBestToSpill(bool unusedOnly, bool *clobbered) {
	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

	static const int UNUSED_LOOKAHEAD_OPS = 30;

	*clobbered = false;
	for (int i = 0; i < allocCount; i++) {
		ARMReg reg = allocOrder[i];
		if (ar[reg].mipsReg != MIPS_REG_INVALID && mr[ar[reg].mipsReg].spillLock)
			continue;

		// Awesome, a clobbered reg.  Let's use it.
		if (MIPSAnalyst::IsRegisterClobbered(ar[reg].mipsReg, compilerPC_, UNUSED_LOOKAHEAD_OPS)) {
			*clobbered = true;
			return reg;
		}

		// Not awesome.  A used reg.  Let's try to avoid spilling.
		if (unusedOnly && MIPSAnalyst::IsRegisterUsed(ar[reg].mipsReg, compilerPC_, UNUSED_LOOKAHEAD_OPS)) {
			continue;
		}

		return reg;
	}

	return INVALID_REG;
}

// TODO: Somewhat smarter spilling - currently simply spills the first available, should do
// round robin or FIFO or something.
ARMReg ArmRegCache::MapReg(MIPSGPReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_IMM) {
		ARMReg armReg = mr[mipsReg].reg;
		if (ar[armReg].mipsReg != mipsReg) {
			ERROR_LOG_REPORT(Log::JIT, "Register mapping out of sync! %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			// Mapping dirty means the old imm value is invalid.
			mr[mipsReg].loc = ML_ARMREG;
			ar[armReg].isDirty = true;
		}
		return (ARMReg)mr[mipsReg].reg;
	} else if (mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		// Was mapped as pointer, now we want it mapped as a value, presumably to
		// add or subtract stuff to it. Later we could allow such things but for now
		// let's just convert back to a register value by reloading from the backing storage.
		ARMReg armReg = mr[mipsReg].reg;
		if ((mapFlags & MAP_NOINIT) != MAP_NOINIT) {
			emit_->LDR(armReg, CTXREG, GetMipsRegOffset(mipsReg));
		}
		mr[mipsReg].loc = ML_ARMREG;
		if (mapFlags & MAP_DIRTY) {
			ar[armReg].isDirty = true;
		}
		return (ARMReg)mr[mipsReg].reg;
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

	ARMReg desiredReg = INVALID_REG;
	// Try to "statically" allocate the first 6 regs after v0.
	int desiredOrder = allocCount - (6 - (mipsReg - (int)MIPS_REG_V0));
	if (desiredOrder >= 0 && desiredOrder < allocCount)
		desiredReg = allocOrder[desiredOrder];

	if (desiredReg != INVALID_REG) {
		if (ar[desiredReg].mipsReg == MIPS_REG_INVALID) {
			// With this placement, we may be able to optimize flush.
			MapRegTo(desiredReg, mipsReg, mapFlags);
			return desiredReg;
		}
	}

allocate:
	for (int i = 0; i < allocCount; i++) {
		ARMReg reg = allocOrder[i];

		if (ar[reg].mipsReg == MIPS_REG_INVALID) {
			// That means it's free. Grab it, and load the value into it (if requested).
			MapRegTo(reg, mipsReg, mapFlags);
			return reg;
		}
	}

	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	bool clobbered;
	ARMReg bestToSpill = FindBestToSpill(true, &clobbered);
	if (bestToSpill == INVALID_REG) {
		bestToSpill = FindBestToSpill(false, &clobbered);
	}

	if (bestToSpill != INVALID_REG) {
		// ERROR_LOG(Log::JIT, "Out of registers at PC %08x - spills register %i.", mips_->pc, bestToSpill);
		// TODO: Broken somehow in Dante's Inferno, but most games work.  Bad flags in MIPSTables somewhere?
		if (clobbered) {
			DiscardR(ar[bestToSpill].mipsReg);
		} else {
			FlushArmReg(bestToSpill);
		}
		goto allocate;
	}

	// Uh oh, we have all of them spilllocked....
	ERROR_LOG_REPORT(Log::JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
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
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyDirtyIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, bool avoidLoad) {
	SpillLock(rd1, rd2, rs);
	bool load1 = !avoidLoad || rd1 == rs;
	bool load2 = !avoidLoad || rd2 == rs;
	MapReg(rd1, load1 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rd2, load2 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd1, rd2, rs, rt);
	bool load1 = !avoidLoad || (rd1 == rs || rd1 == rt);
	bool load2 = !avoidLoad || (rd2 == rs || rd2 == rt);
	MapReg(rd1, load1 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rd2, load2 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void ArmRegCache::FlushArmReg(ARMReg r) {
	if (ar[r].mipsReg == MIPS_REG_INVALID) {
		// Nothing to do, reg not mapped.
		if (ar[r].isDirty) {
			ERROR_LOG_REPORT(Log::JIT, "Dirty but no mipsreg?");
		}
		return;
	}
	if (ar[r].mipsReg != MIPS_REG_INVALID) {
		auto &mreg = mr[ar[r].mipsReg];
		if (mreg.loc == ML_ARMREG_IMM || ar[r].mipsReg == MIPS_REG_ZERO) {
			// We know its immedate value, no need to STR now.
			mreg.loc = ML_IMM;
			mreg.reg = INVALID_REG;
		} else {
			if (ar[r].isDirty && mreg.loc == ML_ARMREG)
				emit_->STR(r, CTXREG, GetMipsRegOffset(ar[r].mipsReg));
			mreg.loc = ML_MEM;
			mreg.reg = INVALID_REG;
			mreg.imm = 0;
		}
	}
	ar[r].isDirty = false;
	ar[r].mipsReg = MIPS_REG_INVALID;
}

void ArmRegCache::DiscardR(MIPSGPReg mipsReg) {
	const RegMIPSLoc prevLoc = mr[mipsReg].loc;
	if (prevLoc == ML_ARMREG || prevLoc == ML_ARMREG_AS_PTR || prevLoc == ML_ARMREG_IMM) {
		ARMReg armReg = mr[mipsReg].reg;
		ar[armReg].isDirty = false;
		ar[armReg].mipsReg = MIPS_REG_INVALID;
		mr[mipsReg].reg = INVALID_REG;
		if (mipsReg == MIPS_REG_ZERO) {
			mr[mipsReg].loc = ML_IMM;
		} else {
			mr[mipsReg].loc = ML_MEM;
		}
		mr[mipsReg].imm = 0;
	}
	if (prevLoc == ML_IMM && mipsReg != MIPS_REG_ZERO) {
		mr[mipsReg].loc = ML_MEM;
		mr[mipsReg].imm = 0;
	}
}

void ArmRegCache::FlushR(MIPSGPReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		if (r != MIPS_REG_ZERO) {
			SetRegImm(SCRATCHREG1, mr[r].imm);
			emit_->STR(SCRATCHREG1, CTXREG, GetMipsRegOffset(r));
		}
		break;

	case ML_ARMREG:
	case ML_ARMREG_IMM:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG_REPORT(Log::JIT, "FlushR: MipsReg %d had bad ArmReg", r);
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
			ERROR_LOG_REPORT(Log::JIT, "ARMREG_AS_PTR cannot be dirty (yet)");
		}
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		ERROR_LOG_REPORT(Log::JIT, "FlushR: MipsReg %d with invalid location %d", r, mr[r].loc);
		break;
	}
	if (r == MIPS_REG_ZERO) {
		mr[r].loc = ML_IMM;
	} else {
		mr[r].loc = ML_MEM;
	}
	mr[r].reg = INVALID_REG;
	mr[r].imm = 0;
}

// Note: if allowFlushImm is set, this also flushes imms while checking the sequence.
int ArmRegCache::FlushGetSequential(MIPSGPReg startMipsReg, bool allowFlushImm) {
	// Only start a sequence on a dirty armreg.
	// TODO: Could also start with an imm?
	const auto &startMipsInfo = mr[startMipsReg];
	if ((startMipsInfo.loc != ML_ARMREG && startMipsInfo.loc != ML_ARMREG_IMM) || startMipsInfo.reg == INVALID_REG || !ar[startMipsInfo.reg].isDirty) {
		return 0;
	}

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

	int c = 1;
	// The sequence needs to have ascending arm regs for STMIA.
	int lastArmReg = startMipsInfo.reg;
	// Can't use HI/LO, only regs in the main r[] array.
	for (int r = (int)startMipsReg + 1; r < 32; ++r) {
		if ((mr[r].loc == ML_ARMREG || mr[r].loc == ML_ARMREG_IMM) && mr[r].reg != INVALID_REG) {
			if ((int)mr[r].reg > lastArmReg && ar[mr[r].reg].isDirty) {
				++c;
				lastArmReg = mr[r].reg;
				continue;
			}
		// If we're not allowed to flush imms, don't even consider them.
		} else if (allowFlushImm && mr[r].loc == ML_IMM && MIPSGPReg(r) != MIPS_REG_ZERO) {
			// Okay, let's search for a free (and later) reg to put this imm into.
			bool found = false;
			for (int j = 0; j < allocCount; ++j) {
				ARMReg immReg = allocOrder[j];
				if ((int)immReg > lastArmReg && ar[immReg].mipsReg == MIPS_REG_INVALID) {
					++c;
					lastArmReg = immReg;

					// Even if the sequence fails, we'll need it in a reg anyway, might as well be this one.
					MapRegTo(immReg, MIPSGPReg(r), 0);
					found = true;
					break;
				}
			}
			if (found) {
				continue;
			}
		}

		// If it didn't hit a continue above, the chain is over.
		// There's no way to skip a slot with STMIA.
		break;
	}

	return c;
}

void ArmRegCache::FlushAll() {
	// ADD + STMIA is probably better than STR + STR, so let's merge 2 into a STMIA.
	const int minSequential = 2;

	// Let's try to put things in order and use STMIA.
	// First we have to save imms.  We have to use a separate loop because otherwise
	// we would overwrite existing regs, and other code assumes FlushAll() won't do that.
	for (int i = 0; i < NUM_MIPSREG; i++) {
		MIPSGPReg mipsReg = MIPSGPReg(i);

		// This happens to also flush imms to regs as much as possible.
		int c = FlushGetSequential(mipsReg, true);
		if (c >= minSequential) {
			// Skip the next c (adjust down 1 because the loop increments.)
			i += c - 1;
		}
	}

	// Okay, now the real deal: this time NOT flushing imms.
	for (int i = 0; i < NUM_MIPSREG; i++) {
		MIPSGPReg mipsReg = MIPSGPReg(i);

		int c = FlushGetSequential(mipsReg, false);
		if (c >= minSequential) {
			u16 regs = 0;
			for (int j = 0; j < c; ++j) {
				regs |= 1 << mr[i + j].reg;
			}

			emit_->ADD(SCRATCHREG1, CTXREG, GetMipsRegOffset(mipsReg));
			emit_->STMBitmask(SCRATCHREG1, true, false, false, regs);

			// Okay, those are all done now, discard them.
			for (int j = 0; j < c; ++j) {
				DiscardR(MIPSGPReg(i + j));
			}
			// Skip the next c (adjust down 1 because the loop increments.)
			i += c - 1;
		} else {
			FlushR(mipsReg);
		}
	}
	// Sanity check
	for (int i = 0; i < NUM_ARMREG; i++) {
		if (ar[i].mipsReg != MIPS_REG_INVALID) {
			ERROR_LOG_REPORT(Log::JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

void ArmRegCache::SetImm(MIPSGPReg r, u32 immVal) {
	if (r == MIPS_REG_ZERO && immVal != 0) {
		ERROR_LOG_REPORT(Log::JIT, "Trying to set immediate %08x to r0 at %08x", immVal, compilerPC_);
		return;
	}

	if (mr[r].loc == ML_ARMREG_IMM && mr[r].imm == immVal) {
		// Already have that value, let's keep it in the reg.
		return;
	}
	// Zap existing value if cached in a reg
	if (mr[r].reg != INVALID_REG) {
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		ar[mr[r].reg].isDirty = false;
	}
	mr[r].loc = ML_IMM;
	mr[r].imm = immVal;
	mr[r].reg = INVALID_REG;
}

bool ArmRegCache::IsImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return true;
	return mr[r].loc == ML_IMM || mr[r].loc == ML_ARMREG_IMM;
}

u32 ArmRegCache::GetImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return 0;
	if (mr[r].loc != ML_IMM && mr[r].loc != ML_ARMREG_IMM) {
		ERROR_LOG_REPORT(Log::JIT, "Trying to get imm from non-imm register %i", r);
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
	case MIPS_REG_FPCOND:
		return offsetof(MIPSState, fpcond);
	case MIPS_REG_VFPUCC:
		return offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_CC]);
	default:
		ERROR_LOG_REPORT(Log::JIT, "bad mips register %i", r);
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
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_IMM) {
		return (ARMReg)mr[mipsReg].reg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}

ARMReg ArmRegCache::RPtr(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG_AS_PTR) {
		return (ARMReg)mr[mipsReg].reg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm reg as pointer. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}

#endif // PPSSPP_ARCH(ARM)
