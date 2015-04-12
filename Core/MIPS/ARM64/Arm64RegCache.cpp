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

#include "Core/MemMap.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/Reporting.h"
#include "Common/Arm64Emitter.h"

#ifndef offsetof
#include "stddef.h"
#endif

using namespace Arm64Gen;
using namespace Arm64JitConstants;

Arm64RegCache::Arm64RegCache(MIPSState *mips, MIPSComp::JitState *js, MIPSComp::JitOptions *jo) : mips_(mips), js_(js), jo_(jo) {
}

void Arm64RegCache::Init(ARM64XEmitter *emitter) {
	emit_ = emitter;
}

void Arm64RegCache::Start(MIPSAnalyst::AnalysisResults &stats) {
	for (int i = 0; i < NUM_ARMREG; i++) {
		ar[i].mipsReg = MIPS_REG_INVALID;
		ar[i].isDirty = false;
		ar[i].pointerified = false;
	}
	for (int i = 0; i < NUM_MIPSREG; i++) {
		mr[i].loc = ML_MEM;
		mr[i].reg = INVALID_REG;
		mr[i].imm = -1;
		mr[i].spillLock = false;
	}
}

const ARM64Reg *Arm64RegCache::GetMIPSAllocationOrder(int &count) {
	// See register alloc remarks in Arm64Asm.cpp
	// TODO: Add static allocation of top MIPS registers like SP
	static const ARM64Reg allocationOrder[] = {
		W19, W20, W21, W22, W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

void Arm64RegCache::FlushBeforeCall() {
	// TODO: More optimal
	FlushAll();
}

bool Arm64RegCache::IsMapped(MIPSGPReg mipsReg) {
	return mr[mipsReg].loc == ML_ARMREG;
}

void Arm64RegCache::SetRegImm(ARM64Reg reg, u64 imm) {
	// On ARM64, at least Cortex A57, good old MOVT/MOVW  (MOVK in 64-bit) is really fast.
	emit_->MOVI2R(reg, imm);
}

void Arm64RegCache::MapRegTo(ARM64Reg reg, MIPSGPReg mipsReg, int mapFlags) {
	ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
	if ((mapFlags & MAP_NOINIT) != MAP_NOINIT) {
		if (mipsReg == MIPS_REG_ZERO) {
			// If we get a request to load the zero register, at least we won't spend
			// time on a memory access...
			emit_->MOVI2R(reg, 0);

			// This way, if we SetImm() it, we'll keep it.
			mr[mipsReg].loc = ML_ARMREG_IMM;
			mr[mipsReg].imm = 0;
		} else {
			switch (mr[mipsReg].loc) {
			case ML_MEM:
			{
				int offset = GetMipsRegOffset(mipsReg);
				// INFO_LOG(JIT, "MapRegTo %d mips: %d offset %d", (int)reg, mipsReg, offset);
				emit_->LDR(INDEX_UNSIGNED, reg, CTXREG, offset);
				mr[mipsReg].loc = ML_ARMREG;
				break;
			}
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
		if (mipsReg == MIPS_REG_ZERO) {
			// This way, if we SetImm() it, we'll keep it.
			mr[mipsReg].loc = ML_ARMREG_IMM;
			mr[mipsReg].imm = 0;
		} else {
			mr[mipsReg].loc = ML_ARMREG;
		}
	}
	ar[reg].mipsReg = mipsReg;
	ar[reg].pointerified = false;
	mr[mipsReg].reg = reg;
}

ARM64Reg Arm64RegCache::FindBestToSpill(bool unusedOnly, bool *clobbered) {
	int allocCount;
	const ARM64Reg *allocOrder = GetMIPSAllocationOrder(allocCount);

	static const int UNUSED_LOOKAHEAD_OPS = 30;

	*clobbered = false;
	for (int i = 0; i < allocCount; i++) {
		ARM64Reg reg = allocOrder[i];
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
ARM64Reg Arm64RegCache::MapReg(MIPSGPReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_IMM) {
		ARM64Reg armReg = mr[mipsReg].reg;
		if (ar[armReg].mipsReg != mipsReg) {
			ERROR_LOG_REPORT(JIT, "Register mapping out of sync! %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			// Mapping dirty means the old imm value is invalid.
			mr[mipsReg].loc = ML_ARMREG;
			ar[armReg].isDirty = true;
			// If reg is written to, pointerification is lost.
			ar[armReg].pointerified = false;
		}
		return (ARM64Reg)mr[mipsReg].reg;
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARM64Reg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		ARM64Reg reg = allocOrder[i];

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
	ARM64Reg bestToSpill = FindBestToSpill(true, &clobbered);
	if (bestToSpill == INVALID_REG) {
		bestToSpill = FindBestToSpill(false, &clobbered);
	}

	if (bestToSpill != INVALID_REG) {
		// ERROR_LOG(JIT, "Out of registers at PC %08x - spills register %i.", mips_->pc, bestToSpill);
		// TODO: Broken somehow in Dante's Inferno, but most games work.  Bad flags in MIPSTables somewhere?
		if (clobbered) {
			DiscardR(ar[bestToSpill].mipsReg);
		} else {
			FlushArmReg(bestToSpill);
		}
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG_REPORT(JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
	return INVALID_REG;
}

Arm64Gen::ARM64Reg Arm64RegCache::MapRegAsPointer(MIPSGPReg reg) {
	ARM64Reg retval = INVALID_REG;
	if (mr[reg].loc != ML_ARMREG) {
		retval = MapReg(reg);
	}

	if (mr[reg].loc == ML_ARMREG) {
		int a = DecodeReg(mr[reg].reg);
		if (!ar[a].pointerified) {
			emit_->MOVK(ARM64Reg(X0 + a), ((uint64_t)Memory::base) >> 32, SHIFT_32);
			ar[a].pointerified = true;
		}
	} else {
		ERROR_LOG(JIT, "MapRegAsPointer : MapReg failed to allocate a register?");
	}
	return retval;
}

void Arm64RegCache::MapIn(MIPSGPReg rs) {
	MapReg(rs);
}

void Arm64RegCache::MapInIn(MIPSGPReg rd, MIPSGPReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLocks();
}

void Arm64RegCache::MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLocks();
}

void Arm64RegCache::MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void Arm64RegCache::MapDirtyDirtyIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, bool avoidLoad) {
	SpillLock(rd1, rd2, rs);
	bool load1 = !avoidLoad || rd1 == rs;
	bool load2 = !avoidLoad || rd2 == rs;
	MapReg(rd1, load1 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rd2, load2 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLocks();
}

void Arm64RegCache::MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad) {
	SpillLock(rd1, rd2, rs, rt);
	bool load1 = !avoidLoad || (rd1 == rs || rd1 == rt);
	bool load2 = !avoidLoad || (rd2 == rs || rd2 == rt);
	MapReg(rd1, load1 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rd2, load2 ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLocks();
}

void Arm64RegCache::FlushArmReg(ARM64Reg r) {
	if (ar[r].mipsReg == MIPS_REG_INVALID) {
		// Nothing to do, reg not mapped.
		if (ar[r].isDirty) {
			ERROR_LOG_REPORT(JIT, "Dirty but no mipsreg?");
		}
		return;
	}
	if (ar[r].mipsReg != MIPS_REG_INVALID) {
		auto &mreg = mr[ar[r].mipsReg];
		if (mreg.loc == ML_ARMREG_IMM) {
			// We know its immedate value, no need to STR now.
			mreg.loc = ML_IMM;
			mreg.reg = INVALID_REG;
		} else {
			if (ar[r].isDirty && mreg.loc == ML_ARMREG)
				emit_->STR(INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(ar[r].mipsReg));
			mreg.loc = ML_MEM;
			mreg.reg = INVALID_REG;
			mreg.imm = 0;
		}
	}
	ar[r].isDirty = false;
	ar[r].mipsReg = MIPS_REG_INVALID;
	ar[r].pointerified = false;
}

void Arm64RegCache::DiscardR(MIPSGPReg mipsReg) {
	const RegMIPSLoc prevLoc = mr[mipsReg].loc;
	if (prevLoc == ML_ARMREG || prevLoc == ML_ARMREG_IMM) {
		ARM64Reg armReg = mr[mipsReg].reg;
		ar[armReg].isDirty = false;
		ar[armReg].mipsReg = MIPS_REG_INVALID;
		ar[armReg].pointerified = false;
		mr[mipsReg].reg = INVALID_REG;
		mr[mipsReg].loc = ML_MEM;
		mr[mipsReg].imm = 0;
	}
}

void Arm64RegCache::FlushR(MIPSGPReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		if (r != MIPS_REG_ZERO) {
			SetRegImm(SCRATCH1, mr[r].imm);
			emit_->STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, GetMipsRegOffset(r));
		}
		break;

	case ML_ARMREG:
	case ML_ARMREG_IMM:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG_REPORT(JIT, "FlushR: MipsReg %d had bad ArmReg", r);
		}
		if (ar[mr[r].reg].isDirty) {
			if (r != MIPS_REG_ZERO) {
				emit_->STR(INDEX_UNSIGNED, mr[r].reg, CTXREG, GetMipsRegOffset(r));
			}
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = MIPS_REG_INVALID;
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		ERROR_LOG_REPORT(JIT, "FlushR: MipsReg %d with invalid location %d", r, mr[r].loc);
		break;
	}
	mr[r].loc = ML_MEM;
	mr[r].reg = INVALID_REG;
	mr[r].imm = 0;
}

void Arm64RegCache::FlushAll() {
	// TODO: Flush in pairs
	for (int i = 0; i < NUM_MIPSREG; i++) {
		MIPSGPReg mipsReg = MIPSGPReg(i);
		FlushR(mipsReg);
	}

	// Sanity check
	for (int i = 0; i < NUM_ARMREG; i++) {
		if (ar[i].mipsReg != MIPS_REG_INVALID) {
			ERROR_LOG_REPORT(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

void Arm64RegCache::SetImm(MIPSGPReg r, u32 immVal) {
	if (r == MIPS_REG_ZERO && immVal != 0)
		ERROR_LOG(JIT, "Trying to set immediate %08x to r0", immVal);

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

bool Arm64RegCache::IsImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return true;
	return mr[r].loc == ML_IMM || mr[r].loc == ML_ARMREG_IMM;
}

u32 Arm64RegCache::GetImm(MIPSGPReg r) const {
	if (r == MIPS_REG_ZERO) return 0;
	if (mr[r].loc != ML_IMM && mr[r].loc != ML_ARMREG_IMM) {
		ERROR_LOG_REPORT(JIT, "Trying to get imm from non-imm register %i", r);
	}
	return mr[r].imm;
}

int Arm64RegCache::GetMipsRegOffset(MIPSGPReg r) {
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
		ERROR_LOG_REPORT(JIT, "bad mips register %i", r);
		return 0;  // or what?
	}
}

void Arm64RegCache::SpillLock(MIPSGPReg r1, MIPSGPReg r2, MIPSGPReg r3, MIPSGPReg r4) {
	mr[r1].spillLock = true;
	if (r2 != MIPS_REG_INVALID) mr[r2].spillLock = true;
	if (r3 != MIPS_REG_INVALID) mr[r3].spillLock = true;
	if (r4 != MIPS_REG_INVALID) mr[r4].spillLock = true;
}

void Arm64RegCache::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPSREG; i++) {
		mr[i].spillLock = false;
	}
}

void Arm64RegCache::ReleaseSpillLock(MIPSGPReg reg) {
	mr[reg].spillLock = false;
}

ARM64Reg Arm64RegCache::R(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_IMM) {
		return (ARM64Reg)mr[mipsReg].reg;
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}

ARM64Reg Arm64RegCache::RPtr(MIPSGPReg mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG || mr[mipsReg].loc == ML_ARMREG_IMM) {
		int a = mr[mipsReg].reg;
		if (ar[a].pointerified) {
			return (ARM64Reg)mr[mipsReg].reg;
		} else {
			ERROR_LOG(JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in arm reg. compilerPC = %08x", mipsReg, compilerPC_);
		return INVALID_REG;  // BAAAD
	}
}
