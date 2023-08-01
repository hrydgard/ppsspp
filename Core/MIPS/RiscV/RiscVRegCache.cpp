// Copyright (c) 2023- PPSSPP Project.

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

#ifndef offsetof
#include <cstddef>
#endif

#include "Common/CPUDetect.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Reporting.h"

using namespace RiscVGen;
using namespace RiscVJitConstants;

RiscVRegCache::RiscVRegCache(MIPSState *mipsState, MIPSComp::JitOptions *jo)
	: mips_(mipsState), jo_(jo) {
}

void RiscVRegCache::Init(RiscVEmitter *emitter) {
	emit_ = emitter;
}

void RiscVRegCache::Start(MIPSComp::IRBlock *irBlock) {
	if (!initialReady_) {
		SetupInitialRegs();
		initialReady_ = true;
	}

	memcpy(ar, arInitial_, sizeof(ar));
	memcpy(mr, mrInitial_, sizeof(mr));

	int numStatics;
	const StaticAllocation *statics = GetStaticAllocations(numStatics);
	for (int i = 0; i < numStatics; i++) {
		ar[statics[i].ar].mipsReg = statics[i].mr;
		ar[statics[i].ar].pointerified = statics[i].pointerified && jo_->enablePointerify;
		ar[statics[i].ar].normalized32 = false;
		mr[statics[i].mr].loc = MIPSLoc::RVREG;
		mr[statics[i].mr].reg = statics[i].ar;
		mr[statics[i].mr].isStatic = true;
		mr[statics[i].mr].spillLock = true;
	}

	irBlock_ = irBlock;
	irIndex_ = 0;
}

void RiscVRegCache::SetupInitialRegs() {
	for (int i = 0; i < NUM_RVREG; i++) {
		arInitial_[i].mipsReg = IRREG_INVALID;
		arInitial_[i].isDirty = false;
		arInitial_[i].pointerified = false;
		arInitial_[i].tempLocked = false;
		arInitial_[i].normalized32 = false;
	}
	for (int i = 0; i < NUM_MIPSREG; i++) {
		mrInitial_[i].loc = MIPSLoc::MEM;
		mrInitial_[i].reg = INVALID_REG;
		mrInitial_[i].imm = -1;
		mrInitial_[i].spillLock = false;
		mrInitial_[i].isStatic = false;
	}

	// Treat R_ZERO a bit specially, but it's basically static alloc too.
	arInitial_[R_ZERO].mipsReg = MIPS_REG_ZERO;
	arInitial_[R_ZERO].normalized32 = true;
	mrInitial_[MIPS_REG_ZERO].loc = MIPSLoc::RVREG_IMM;
	mrInitial_[MIPS_REG_ZERO].reg = R_ZERO;
	mrInitial_[MIPS_REG_ZERO].imm = 0;
	mrInitial_[MIPS_REG_ZERO].isStatic = true;
}

const RiscVReg *RiscVRegCache::GetMIPSAllocationOrder(int &count) {
	// X8 and X9 are the most ideal for static alloc because they can be used with compression.
	// Otherwise we stick to saved regs - might not be necessary.
	static const RiscVReg allocationOrder[] = {
		X8, X9, X12, X13, X14, X15, X5, X6, X7, X16, X17, X18, X19, X20, X21, X22, X23, X28, X29, X30, X31,
	};
	static const RiscVReg allocationOrderStaticAlloc[] = {
		X12, X13, X14, X15, X5, X6, X7, X16, X17, X21, X22, X23, X28, X29, X30, X31,
	};

	if (jo_->useStaticAlloc) {
		count = ARRAY_SIZE(allocationOrderStaticAlloc);
		return allocationOrderStaticAlloc;
	} else {
		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	}
}

const RiscVRegCache::StaticAllocation *RiscVRegCache::GetStaticAllocations(int &count) {
	static const StaticAllocation allocs[] = {
		{ MIPS_REG_SP, X8, true },
		{ MIPS_REG_V0, X9 },
		{ MIPS_REG_V1, X18 },
		{ MIPS_REG_A0, X19 },
		{ MIPS_REG_RA, X20 },
	};

	if (jo_->useStaticAlloc) {
		count = ARRAY_SIZE(allocs);
		return allocs;
	} else {
		count = 0;
		return nullptr;
	}
}

void RiscVRegCache::EmitLoadStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		if (allocs[i].pointerified && jo_->enablePointerify) {
			emit_->LWU(allocs[i].ar, CTXREG, offset);
			emit_->ADD(allocs[i].ar, allocs[i].ar, MEMBASEREG);
		} else {
			emit_->LW(allocs[i].ar, CTXREG, offset);
		}
	}
}

void RiscVRegCache::EmitSaveStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	// This only needs to run once (by Asm) so checks don't need to be fast.
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		emit_->SW(allocs[i].ar, CTXREG, offset);
	}
}

void RiscVRegCache::FlushBeforeCall() {
	// These registers are not preserved by function calls.
	for (int i = 5; i <= 7; ++i) {
		FlushRiscVReg(RiscVReg(X0 + i));
	}
	for (int i = 10; i <= 17; ++i) {
		FlushRiscVReg(RiscVReg(X0 + i));
	}
	for (int i = 28; i <= 31; ++i) {
		FlushRiscVReg(RiscVReg(X0 + i));
	}
}

bool RiscVRegCache::IsInRAM(IRRegIndex reg) {
	_dbg_assert_(IsValidReg(reg));
	return mr[reg].loc == MIPSLoc::MEM;
}

bool RiscVRegCache::IsMapped(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	return mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM;
}

bool RiscVRegCache::IsMappedAsPointer(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	if (mr[mipsReg].loc == MIPSLoc::RVREG) {
		return ar[mr[mipsReg].reg].pointerified;
	} else if (mr[mipsReg].loc == MIPSLoc::RVREG_IMM) {
		if (ar[mr[mipsReg].reg].pointerified) {
			ERROR_LOG(JIT, "Really shouldn't be pointerified here");
		}
	} else if (mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR) {
		return true;
	}
	return false;
}

bool RiscVRegCache::IsMappedAsStaticPointer(IRRegIndex reg) {
	if (IsMappedAsPointer(reg)) {
		return mr[reg].isStatic;
	}
	return false;
}

bool RiscVRegCache::IsNormalized32(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	if (XLEN == 32)
		return true;
	if (mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM) {
		return ar[mr[mipsReg].reg].normalized32;
	}
	return false;
}

void RiscVRegCache::MarkDirty(RiscVReg reg, bool andNormalized32) {
	// Can't mark X0 dirty.
	_dbg_assert_(reg > X0 && reg <= X31);
	ar[reg].isDirty = true;
	ar[reg].normalized32 = andNormalized32;
	// If reg is written to, pointerification is lost.
	ar[reg].pointerified = false;
	if (ar[reg].mipsReg != IRREG_INVALID) {
		RegStatusMIPS &m = mr[ar[reg].mipsReg];
		if (m.loc == MIPSLoc::RVREG_AS_PTR || m.loc == MIPSLoc::RVREG_IMM) {
			m.loc = MIPSLoc::RVREG;
			m.imm = -1;
		}
		_dbg_assert_(m.loc == MIPSLoc::RVREG);
	}
}

void RiscVRegCache::MarkPtrDirty(RiscVReg reg) {
	// Can't mark X0 dirty.
	_dbg_assert_(reg > X0 && reg <= X31);
	_dbg_assert_(!ar[reg].normalized32);
	ar[reg].isDirty = true;
	if (ar[reg].mipsReg != IRREG_INVALID) {
		_dbg_assert_(mr[ar[reg].mipsReg].loc == MIPSLoc::RVREG_AS_PTR);
	} else {
		_dbg_assert_(ar[reg].pointerified);
	}
}

RiscVGen::RiscVReg RiscVRegCache::Normalize32(IRRegIndex mipsReg, RiscVGen::RiscVReg destReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(destReg == INVALID_REG || (destReg > X0 && destReg <= X31));

	RiscVReg reg = mr[mipsReg].reg;
	if (XLEN == 32)
		return reg;

	switch (mr[mipsReg].loc) {
	case MIPSLoc::IMM:
	case MIPSLoc::MEM:
		_assert_msg_(false, "Cannot normalize an imm or mem");
		return INVALID_REG;

	case MIPSLoc::RVREG:
	case MIPSLoc::RVREG_IMM:
		if (!ar[mr[mipsReg].reg].normalized32) {
			if (destReg == INVALID_REG) {
				emit_->SEXT_W(mr[mipsReg].reg, mr[mipsReg].reg);
				ar[mr[mipsReg].reg].normalized32 = true;
				ar[mr[mipsReg].reg].pointerified = false;
			} else {
				emit_->SEXT_W(destReg, mr[mipsReg].reg);
			}
		} else if (destReg != INVALID_REG) {
			emit_->SEXT_W(destReg, mr[mipsReg].reg);
		}
		break;

	case MIPSLoc::RVREG_AS_PTR:
		_dbg_assert_(ar[mr[mipsReg].reg].normalized32 == false);
		if (destReg == INVALID_REG) {
			// If we can pointerify, SEXT_W will be enough.
			if (!jo_->enablePointerify)
				emit_->SUB(mr[mipsReg].reg, mr[mipsReg].reg, MEMBASEREG);
			emit_->SEXT_W(mr[mipsReg].reg, mr[mipsReg].reg);
			mr[mipsReg].loc = MIPSLoc::RVREG;
			ar[mr[mipsReg].reg].normalized32 = true;
			ar[mr[mipsReg].reg].pointerified = false;
		} else if (!jo_->enablePointerify) {
			emit_->SUB(destReg, mr[mipsReg].reg, MEMBASEREG);
			emit_->SEXT_W(destReg, destReg);
		} else {
			emit_->SEXT_W(destReg, mr[mipsReg].reg);
		}
		break;
	}

	return destReg == INVALID_REG ? reg : destReg;
}

void RiscVRegCache::SetRegImm(RiscVReg reg, u64 imm) {
	_dbg_assert_(reg != R_ZERO || imm == 0);
	_dbg_assert_(reg >= X0 && reg <= X31);
	// TODO: Could optimize this more for > 32 bit constants.
	emit_->LI(reg, imm);
	_dbg_assert_(!ar[reg].pointerified);
	ar[reg].normalized32 = imm == (u64)(s64)(s32)imm;
}

void RiscVRegCache::MapRegTo(RiscVReg reg, IRRegIndex mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(reg > X0 && reg <= X31);
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(!mr[mipsReg].isStatic);
	if (mr[mipsReg].isStatic) {
		ERROR_LOG(JIT, "Cannot MapRegTo static register %d", mipsReg);
		return;
	}
	ar[reg].isDirty = (mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY;
	if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
		if (mipsReg == MIPS_REG_ZERO) {
			// If we get a request to load the zero register, at least we won't spend
			// time on a memory access...
			emit_->LI(reg, 0);

			// This way, if we SetImm() it, we'll keep it.
			mr[mipsReg].loc = MIPSLoc::RVREG_IMM;
			mr[mipsReg].imm = 0;
			ar[reg].normalized32 = true;
		} else {
			switch (mr[mipsReg].loc) {
			case MIPSLoc::MEM:
				emit_->LW(reg, CTXREG, GetMipsRegOffset(mipsReg));
				mr[mipsReg].loc = MIPSLoc::RVREG;
				ar[reg].normalized32 = true;
				break;
			case MIPSLoc::IMM:
				SetRegImm(reg, mr[mipsReg].imm);
				// IMM is always dirty.
				ar[reg].isDirty = true;

				// If we are mapping dirty, it means we're gonna overwrite.
				// So the imm value is no longer valid.
				if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY)
					mr[mipsReg].loc = MIPSLoc::RVREG;
				else
					mr[mipsReg].loc = MIPSLoc::RVREG_IMM;
				break;
			case MIPSLoc::RVREG_IMM:
				// If it's not dirty, we can keep it.
				if (ar[reg].isDirty)
					mr[mipsReg].loc = MIPSLoc::RVREG;
				break;
			default:
				_assert_msg_(mr[mipsReg].loc != MIPSLoc::RVREG_AS_PTR, "MapRegTo with a pointer?");
				mr[mipsReg].loc = MIPSLoc::RVREG;
				break;
			}
		}
	} else {
		_dbg_assert_(mipsReg != MIPS_REG_ZERO);
		_dbg_assert_(ar[reg].isDirty);
		mr[mipsReg].loc = MIPSLoc::RVREG;
	}
	ar[reg].mipsReg = mipsReg;
	ar[reg].pointerified = false;
	if (ar[reg].isDirty)
		ar[reg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
	mr[mipsReg].reg = reg;
}

RiscVReg RiscVRegCache::AllocateReg() {
	int allocCount;
	const RiscVReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		RiscVReg reg = allocOrder[i];

		if (ar[reg].mipsReg == IRREG_INVALID && !ar[reg].tempLocked) {
			return reg;
		}
	}

	// Still nothing. Let's spill a reg and goto 10.
	bool clobbered;
	RiscVReg bestToSpill = FindBestToSpill(true, &clobbered);
	if (bestToSpill == INVALID_REG) {
		bestToSpill = FindBestToSpill(false, &clobbered);
	}

	if (bestToSpill != INVALID_REG) {
		if (clobbered) {
			DiscardR(ar[bestToSpill].mipsReg);
		} else {
			FlushRiscVReg(bestToSpill);
		}
		// Now one must be free.
		goto allocate;
	}

	// Uh oh, we have all of them spilllocked....
	ERROR_LOG_REPORT(JIT, "Out of spillable registers near PC %08x", mips_->pc);
	_assert_(bestToSpill != INVALID_REG);
	return INVALID_REG;
}

RiscVReg RiscVRegCache::FindBestToSpill(bool unusedOnly, bool *clobbered) {
	int allocCount;
	const RiscVReg *allocOrder = GetMIPSAllocationOrder(allocCount);

	static const int UNUSED_LOOKAHEAD_OPS = 30;

	IRSituation info;
	info.lookaheadCount = UNUSED_LOOKAHEAD_OPS;
	info.currentIndex = irIndex_;
	info.instructions = irBlock_->GetInstructions();
	info.numInstructions = irBlock_->GetNumInstructions();

	*clobbered = false;
	for (int i = 0; i < allocCount; i++) {
		RiscVReg reg = allocOrder[i];
		if (ar[reg].mipsReg != IRREG_INVALID && mr[ar[reg].mipsReg].spillLock)
			continue;
		if (ar[reg].tempLocked)
			continue;

		// As it's in alloc-order, we know it's not static so we don't need to check for that.
		IRUsage usage = IRNextGPRUsage(ar[reg].mipsReg, info);

		// Awesome, a clobbered reg.  Let's use it.
		if (usage == IRUsage::CLOBBERED) {
			// TODO: Check HI/LO clobber together if we combine.
			bool canClobber = true;
			if (canClobber) {
				*clobbered = true;
				return reg;
			}
		}

		// Not awesome.  A used reg.  Let's try to avoid spilling.
		if (!unusedOnly || usage == IRUsage::UNUSED) {
			// TODO: Use age or something to choose which register to spill?
			// TODO: Spill dirty regs first? or opposite?
			return reg;
		}
	}

	return INVALID_REG;
}

RiscVReg RiscVRegCache::TryMapTempImm(IRRegIndex r) {
	_dbg_assert_(IsValidReg(r));
	// If already mapped, no need for a temporary.
	if (IsMapped(r)) {
		return R(r);
	}

	if (mr[r].loc == MIPSLoc::IMM) {
		if (mr[r].imm == 0) {
			return R_ZERO;
		}

		// Try our luck - check for an exact match in another rvreg.
		for (int i = 0; i < NUM_MIPSREG; ++i) {
			if (mr[i].loc == MIPSLoc::RVREG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just use this reg.
				return mr[i].reg;
			}
		}
	}

	return INVALID_REG;
}

RiscVReg RiscVRegCache::GetAndLockTempR() {
	RiscVReg reg = AllocateReg();
	if (reg != INVALID_REG) {
		ar[reg].tempLocked = true;
		pendingUnlock_ = true;
	}
	return reg;
}

RiscVReg RiscVRegCache::MapReg(IRRegIndex mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidReg(mipsReg));

	// TODO: Optimization to force HI/LO to be combined?

	if (mipsReg == IRREG_INVALID) {
		ERROR_LOG(JIT, "Cannot map invalid register");
		return INVALID_REG;
	}

	RiscVReg riscvReg = mr[mipsReg].reg;

	if (mr[mipsReg].isStatic) {
		_dbg_assert_(riscvReg != INVALID_REG);
		if (riscvReg == INVALID_REG) {
			ERROR_LOG(JIT, "MapReg on statically mapped reg %d failed - riscvReg got lost", mipsReg);
		}
		if (mr[mipsReg].loc == MIPSLoc::IMM) {
			// Back into the register, with or without the imm value.
			// If noinit, the MAP_DIRTY check below will take care of the rest.
			if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
				// This may set normalized32 to true.
				SetRegImm(riscvReg, mr[mipsReg].imm);
				mr[mipsReg].loc = MIPSLoc::RVREG_IMM;
				ar[riscvReg].pointerified = false;
			}
			if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32)
				ar[riscvReg].normalized32 = true;
		} else if (mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR) {
			// Was mapped as pointer, now we want it mapped as a value, presumably to
			// add or subtract stuff to it.
			if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!ar[riscvReg].isDirty && (mapFlags & MIPSMap::DIRTY) != MIPSMap::DIRTY);
#endif
				emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
			}
			mr[mipsReg].loc = MIPSLoc::RVREG;
			ar[riscvReg].normalized32 = false;
		}
		// Erasing the imm on dirty (necessary since otherwise we will still think it's ML_RVREG_IMM and return
		// true for IsImm and calculate crazily wrong things).  /unknown
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			// As we are dirty, can't keep RVREG_IMM, we will quickly drift out of sync
			mr[mipsReg].loc = MIPSLoc::RVREG;
			ar[riscvReg].pointerified = false;
			ar[riscvReg].isDirty = true;
			ar[riscvReg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
		} else if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32) {
			ar[riscvReg].normalized32 = true;
		}
		return mr[mipsReg].reg;
	}

	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM) {
		_dbg_assert_(riscvReg != INVALID_REG && ar[riscvReg].mipsReg == mipsReg);
		if (ar[riscvReg].mipsReg != mipsReg) {
			ERROR_LOG_REPORT(JIT, "Register mapping out of sync! %i", mipsReg);
		}
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			// Mapping dirty means the old imm value is invalid.
			mr[mipsReg].loc = MIPSLoc::RVREG;
			ar[riscvReg].isDirty = true;
			// If reg is written to, pointerification is lost.
			ar[riscvReg].pointerified = false;
			ar[riscvReg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
		} else if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32) {
			ar[riscvReg].normalized32 = true;
		}

		return mr[mipsReg].reg;
	} else if (mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR) {
		// Was mapped as pointer, now we want it mapped as a value, presumably to
		// add or subtract stuff to it.
		if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
#ifdef MASKED_PSP_MEMORY
			_dbg_assert_(!ar[riscvReg].isDirty && (mapFlags & MAP_DIRTY) == 0);
#endif
			emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
		}
		mr[mipsReg].loc = MIPSLoc::RVREG;
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			ar[riscvReg].isDirty = true;
		}
		// Let's always set this false, the SUB won't normalize.
		ar[riscvReg].normalized32 = false;
		return mr[mipsReg].reg;
	}

	// Okay, not mapped, so we need to allocate an RV register.
	RiscVReg reg = AllocateReg();
	if (reg != INVALID_REG) {
		// Grab it, and load the value into it (if requested).
		MapRegTo(reg, mipsReg, mapFlags);
	}

	return reg;
}

RiscVReg RiscVRegCache::MapRegAsPointer(IRRegIndex reg) {
	_dbg_assert_(IsValidRegNoZero(reg));

	// Already mapped.
	if (mr[reg].loc == MIPSLoc::RVREG_AS_PTR) {
		return mr[reg].reg;
	}

	RiscVReg riscvReg = INVALID_REG;
	if (mr[reg].loc != MIPSLoc::RVREG && mr[reg].loc != MIPSLoc::RVREG_IMM) {
		riscvReg = MapReg(reg);
	} else {
		riscvReg = mr[reg].reg;
	}

	if (mr[reg].loc == MIPSLoc::RVREG || mr[reg].loc == MIPSLoc::RVREG_IMM) {
		// If there was an imm attached, discard it.
		mr[reg].loc = MIPSLoc::RVREG;
		if (!jo_->enablePointerify) {
			// Convert to a pointer by adding the base and clearing off the top bits.
			// If SP, we can probably avoid the top bit clear, let's play with that later.
			AddMemBase(riscvReg);
			mr[reg].loc = MIPSLoc::RVREG_AS_PTR;
		} else if (!ar[riscvReg].pointerified) {
			AddMemBase(riscvReg);
			ar[riscvReg].pointerified = true;
		}
		ar[riscvReg].normalized32 = false;
	} else {
		ERROR_LOG(JIT, "MapRegAsPointer : MapReg failed to allocate a register?");
	}
	return riscvReg;
}

void RiscVRegCache::AddMemBase(RiscVGen::RiscVReg reg) {
	_assert_(reg >= X0 && reg <= X31);
#ifdef MASKED_PSP_MEMORY
	// This destroys the value...
	_dbg_assert_(!ar[reg].isDirty);
	emit_->SLLIW(reg, reg, 2);
	emit_->SRLIW(reg, reg, 2);
	emit_->ADD(reg, reg, MEMBASEREG);
#else
	// Clear the top bits to be safe.
	if (cpu_info.RiscV_Zba) {
		emit_->ADD_UW(reg, reg, MEMBASEREG);
	} else {
		_assert_(XLEN == 64);
		emit_->SLLI(reg, reg, 32);
		emit_->SRLI(reg, reg, 32);
		emit_->ADD(reg, reg, MEMBASEREG);
	}
#endif
	ar[reg].normalized32 = false;
}

void RiscVRegCache::MapIn(IRRegIndex rs) {
	MapReg(rs);
}

void RiscVRegCache::MapInIn(IRRegIndex rd, IRRegIndex rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCache::MapDirtyIn(IRRegIndex rd, IRRegIndex rs, MapType type) {
	SpillLock(rd, rs);
	bool load = type == MapType::ALWAYS_LOAD || rd == rs;
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd, (load ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCache::MapDirtyInIn(IRRegIndex rd, IRRegIndex rs, IRRegIndex rt, MapType type) {
	SpillLock(rd, rs, rt);
	bool load = type == MapType::ALWAYS_LOAD || (rd == rs || rd == rt);
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd, (load ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd, rs, rt);
}

void RiscVRegCache::MapDirtyDirtyIn(IRRegIndex rd1, IRRegIndex rd2, IRRegIndex rs, MapType type) {
	SpillLock(rd1, rd2, rs);
	bool load1 = type == MapType::ALWAYS_LOAD || rd1 == rs;
	bool load2 = type == MapType::ALWAYS_LOAD || rd2 == rs;
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd1, (load1 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rd2, (load2 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rs);
	ReleaseSpillLock(rd1, rd2, rs);
}

void RiscVRegCache::MapDirtyDirtyInIn(IRRegIndex rd1, IRRegIndex rd2, IRRegIndex rs, IRRegIndex rt, MapType type) {
	SpillLock(rd1, rd2, rs, rt);
	bool load1 = type == MapType::ALWAYS_LOAD || (rd1 == rs || rd1 == rt);
	bool load2 = type == MapType::ALWAYS_LOAD || (rd2 == rs || rd2 == rt);
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd1, (load1 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rd2, (load2 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd1, rd2, rs, rt);
}

void RiscVRegCache::FlushRiscVReg(RiscVReg r) {
	_dbg_assert_(r > X0 && r <= X31);
	_dbg_assert_(ar[r].mipsReg != MIPS_REG_ZERO);
	if (r == INVALID_REG) {
		ERROR_LOG(JIT, "FlushRiscVReg called on invalid register %d", r);
		return;
	}
	if (ar[r].mipsReg == IRREG_INVALID) {
		// Nothing to do, reg not mapped.
		_dbg_assert_(!ar[r].isDirty);
		return;
	}
	_dbg_assert_(!mr[ar[r].mipsReg].isStatic);
	if (mr[ar[r].mipsReg].isStatic) {
		ERROR_LOG(JIT, "Cannot FlushRiscVReg a statically mapped register");
		return;
	}
	auto &mreg = mr[ar[r].mipsReg];
	if (mreg.loc == MIPSLoc::RVREG_IMM || ar[r].mipsReg == MIPS_REG_ZERO) {
		// We know its immediate value, no need to STR now.
		mreg.loc = MIPSLoc::IMM;
		mreg.reg = INVALID_REG;
	} else {
		if (mreg.loc == MIPSLoc::IMM || ar[r].isDirty) {
			if (mreg.loc == MIPSLoc::RVREG_AS_PTR) {
				// Unpointerify, in case dirty.
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!ar[r].isDirty);
#endif
				emit_->SUB(r, r, MEMBASEREG);
				mreg.loc = MIPSLoc::RVREG;
				ar[r].normalized32 = false;
			}
			RiscVReg storeReg = RiscVRegForFlush(ar[r].mipsReg);
			if (storeReg != INVALID_REG)
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(ar[r].mipsReg));
		}
		mreg.loc = MIPSLoc::MEM;
		mreg.reg = INVALID_REG;
		mreg.imm = -1;
	}
	ar[r].isDirty = false;
	ar[r].mipsReg = IRREG_INVALID;
	ar[r].pointerified = false;
}

void RiscVRegCache::DiscardR(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidRegNoZero(mipsReg));
	if (mr[mipsReg].isStatic) {
		// Simply do nothing unless it's an IMM/RVREG_IMM/RVREG_AS_PTR, in case we just switch it over to RVREG, losing the value.
		RiscVReg riscvReg = mr[mipsReg].reg;
		_dbg_assert_(riscvReg != INVALID_REG);
		if (mipsReg == MIPS_REG_ZERO) {
			// Shouldn't happen, but in case it does.
			mr[mipsReg].loc = MIPSLoc::RVREG_IMM;
			mr[mipsReg].reg = R_ZERO;
			mr[mipsReg].imm = 0;
		} else if (mr[mipsReg].loc == MIPSLoc::RVREG_IMM || mr[mipsReg].loc == MIPSLoc::IMM || mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR) {
			// Ignore the imm value, restore sanity
			mr[mipsReg].loc = MIPSLoc::RVREG;
			ar[riscvReg].pointerified = false;
			ar[riscvReg].isDirty = false;
			ar[riscvReg].normalized32 = false;
		}
		return;
	}
	const MIPSLoc prevLoc = mr[mipsReg].loc;
	if (prevLoc == MIPSLoc::RVREG || prevLoc == MIPSLoc::RVREG_IMM || prevLoc == MIPSLoc::RVREG_AS_PTR) {
		RiscVReg riscvReg = mr[mipsReg].reg;
		_dbg_assert_(riscvReg != INVALID_REG);
		ar[riscvReg].mipsReg = IRREG_INVALID;
		ar[riscvReg].pointerified = false;
		ar[riscvReg].isDirty = false;
		ar[riscvReg].normalized32 = false;
		mr[mipsReg].reg = INVALID_REG;
		mr[mipsReg].loc = MIPSLoc::MEM;
		mr[mipsReg].imm = -1;
	}
	if (prevLoc == MIPSLoc::IMM && mipsReg != MIPS_REG_ZERO) {
		mr[mipsReg].loc = MIPSLoc::MEM;
		mr[mipsReg].imm = -1;
	}
}

RiscVReg RiscVRegCache::RiscVRegForFlush(IRRegIndex r) {
	_dbg_assert_(IsValidReg(r));
	if (mr[r].isStatic)
		return INVALID_REG;  // No flushing needed

	switch (mr[r].loc) {
	case MIPSLoc::IMM:
		if (r == MIPS_REG_ZERO) {
			return INVALID_REG;
		}
		// Zero is super easy.
		if (mr[r].imm == 0) {
			return R_ZERO;
		}
		// Could we get lucky?  Check for an exact match in another rvreg.
		for (int i = 0; i < NUM_MIPSREG; ++i) {
			if (mr[i].loc == MIPSLoc::RVREG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just store this reg.
				return mr[i].reg;
			}
		}
		return INVALID_REG;

	case MIPSLoc::RVREG:
	case MIPSLoc::RVREG_IMM:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG_REPORT(JIT, "RiscVRegForFlush: MipsReg %d had bad riscvReg", r);
			return INVALID_REG;
		}
		// No need to flush if it's zero or not dirty.
		if (r == MIPS_REG_ZERO || !ar[mr[r].reg].isDirty) {
			return INVALID_REG;
		}
		// TODO: Lo/hi optimization?
		return mr[r].reg;

	case MIPSLoc::RVREG_AS_PTR:
		return INVALID_REG;

	case MIPSLoc::MEM:
		return INVALID_REG;

	default:
		ERROR_LOG_REPORT(JIT, "RiscVRegForFlush: MipsReg %d with invalid location %d", r, (int)mr[r].loc);
		return INVALID_REG;
	}
}

void RiscVRegCache::FlushR(IRRegIndex r) {
	_dbg_assert_(IsValidRegNoZero(r));
	if (mr[r].isStatic) {
		ERROR_LOG(JIT, "Cannot flush static reg %d", r);
		return;
	}

	switch (mr[r].loc) {
	case MIPSLoc::IMM:
		// IMM is always "dirty".
		// TODO: HI/LO optimization?
		if (r != MIPS_REG_ZERO) {
			// Try to optimize using a different reg.
			RiscVReg storeReg = RiscVRegForFlush(r);
			if (storeReg == INVALID_REG) {
				SetRegImm(SCRATCH1, mr[r].imm);
				storeReg = SCRATCH1;
			}
			emit_->SW(storeReg, CTXREG, GetMipsRegOffset(r));
		}
		break;

	case MIPSLoc::RVREG:
	case MIPSLoc::RVREG_IMM:
		if (ar[mr[r].reg].isDirty) {
			RiscVReg storeReg = RiscVRegForFlush(r);
			if (storeReg != INVALID_REG) {
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(r));
			}
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = IRREG_INVALID;
		ar[mr[r].reg].pointerified = false;
		break;

	case MIPSLoc::RVREG_AS_PTR:
		if (ar[mr[r].reg].isDirty) {
#ifdef MASKED_PSP_MEMORY
			// This is kinda bad, because we've cleared bits in it.
			_dbg_assert_(!ar[mr[r].reg].isDirty);
#endif
			emit_->SUB(mr[r].reg, mr[r].reg, MEMBASEREG);
			// We set this so RiscVRegForFlush knows it's no longer a pointer.
			mr[r].loc = MIPSLoc::RVREG;
			RiscVReg storeReg = RiscVRegForFlush(r);
			if (storeReg != INVALID_REG) {
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(r));
			}
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = IRREG_INVALID;
		break;

	case MIPSLoc::MEM:
		// Already there, nothing to do.
		break;

	default:
		ERROR_LOG_REPORT(JIT, "FlushR: MipsReg %d with invalid location %d", r, (int)mr[r].loc);
		break;
	}
	if (r == MIPS_REG_ZERO) {
		mr[r].loc = MIPSLoc::RVREG_IMM;
		mr[r].reg = R_ZERO;
		mr[r].imm = 0;
	} else {
		mr[r].loc = MIPSLoc::MEM;
		mr[r].reg = INVALID_REG;
		mr[r].imm = -1;
	}
}

void RiscVRegCache::FlushAll() {
	// Note: make sure not to change the registers when flushing:
	// Branching code expects the armreg to retain its value.

	// TODO: HI/LO optimization?

	// Final pass to grab any that were left behind.
	for (int i = 1; i < NUM_MIPSREG; i++) {
		IRRegIndex mipsReg = IRRegIndex(i);
		if (mr[i].isStatic) {
			RiscVReg riscvReg = mr[i].reg;
			// Cannot leave any IMMs in registers, not even ML_ARMREG_IMM, can confuse the regalloc later if this flush is mid-block
			// due to an interpreter fallback that changes the register.
			if (mr[i].loc == MIPSLoc::IMM) {
				SetRegImm(mr[i].reg, mr[i].imm);
				mr[i].loc = MIPSLoc::RVREG;
				ar[riscvReg].pointerified = false;
			} else if (mr[i].loc == MIPSLoc::RVREG_IMM) {
				// The register already contains the immediate.
				if (ar[riscvReg].pointerified) {
					ERROR_LOG(JIT, "RVREG_IMM but pointerified. Wrong.");
					ar[riscvReg].pointerified = false;
				}
				mr[i].loc = MIPSLoc::RVREG;
			} else if (mr[i].loc == MIPSLoc::RVREG_AS_PTR) {
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!ar[riscvReg].isDirty);
#endif
				emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
				mr[i].loc = MIPSLoc::RVREG;
			}
			if (i != MIPS_REG_ZERO && mr[i].reg == INVALID_REG) {
				ERROR_LOG(JIT, "RV reg of static %i is invalid", i);
				continue;
			}
		} else if (IsValidRegNoZero(mipsReg)) {
			FlushR(mipsReg);
		}
	}

	int count = 0;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; i++) {
		if (allocs[i].pointerified && !ar[allocs[i].ar].pointerified && jo_->enablePointerify) {
			// Re-pointerify
			_dbg_assert_(mr[allocs[i].mr].loc == MIPSLoc::RVREG);
			AddMemBase(allocs[i].ar);
			ar[allocs[i].ar].pointerified = true;
		} else if (!allocs[i].pointerified) {
			// If this register got pointerified on the way, mark it as not.
			// This is so that after save/reload (like in an interpreter fallback),
			// it won't be regarded as such, as it may no longer be.
			ar[allocs[i].ar].pointerified = false;
		}
	}
	// Sanity check
	for (int i = 0; i < NUM_RVREG; i++) {
		if (ar[i].mipsReg != IRREG_INVALID && mr[ar[i].mipsReg].isStatic == false) {
			ERROR_LOG_REPORT(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

void RiscVRegCache::SetImm(IRRegIndex r, u64 immVal) {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO && immVal != 0) {
		ERROR_LOG_REPORT(JIT, "Trying to set immediate %08x to r0", (u32)immVal);
		return;
	}

	if (mr[r].loc == MIPSLoc::RVREG_IMM && mr[r].imm == immVal) {
		// Already have that value, let's keep it in the reg.
		return;
	}

	// TODO: HI/LO optimization?
	// All regs on the PSP are 32 bit, but LO we treat as HI:LO so is 64 full bits.
	immVal = immVal & 0xFFFFFFFF;

	if (mr[r].isStatic) {
		mr[r].loc = MIPSLoc::IMM;
		mr[r].imm = immVal;
		ar[mr[r].reg].pointerified = false;
		ar[mr[r].reg].normalized32 = false;
		// We do not change reg to INVALID_REG for obvious reasons..
	} else {
		// Zap existing value if cached in a reg
		if (mr[r].reg != INVALID_REG) {
			ar[mr[r].reg].mipsReg = IRREG_INVALID;
			ar[mr[r].reg].isDirty = false;
			ar[mr[r].reg].pointerified = false;
			ar[mr[r].reg].normalized32 = false;
		}
		mr[r].loc = MIPSLoc::IMM;
		mr[r].imm = immVal;
		mr[r].reg = INVALID_REG;
	}
}

bool RiscVRegCache::IsImm(IRRegIndex r) const {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO)
		return true;
	else
		return mr[r].loc == MIPSLoc::IMM || mr[r].loc == MIPSLoc::RVREG_IMM;
}

u64 RiscVRegCache::GetImm(IRRegIndex r) const {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO)
		return 0;
	if (mr[r].loc != MIPSLoc::IMM && mr[r].loc != MIPSLoc::RVREG_IMM) {
		ERROR_LOG_REPORT(JIT, "Trying to get imm from non-imm register %i", r);
	}
	return mr[r].imm;
}

int RiscVRegCache::GetMipsRegOffset(IRRegIndex r) {
	_dbg_assert_(IsValidReg(r));
	return r * 4;
}

bool RiscVRegCache::IsValidReg(IRRegIndex r) const {
	if (r < 0 || r >= NUM_MIPSREG)
		return false;

	// See MIPSState for these offsets.

	// Don't allow FPU or VFPU regs here.
	if (r >= 32 && r < 32 + 32 + 128)
		return false;
	// Also disallow VFPU temps.
	if (r >= 224 && r < 224 + 16)
		return false;
	// Don't allow nextPC, etc. since it's probably a mistake.
	if (r > IRREG_FPCOND && r != IRREG_LLBIT)
		return false;
	// Don't allow PC either.
	if (r == 241)
		return false;

	return true;
}

bool RiscVRegCache::IsValidRegNoZero(IRRegIndex r) const {
	return IsValidReg(r) && r != MIPS_REG_ZERO;
}

void RiscVRegCache::SpillLock(IRRegIndex r1, IRRegIndex r2, IRRegIndex r3, IRRegIndex r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	mr[r1].spillLock = true;
	if (r2 != IRREG_INVALID) mr[r2].spillLock = true;
	if (r3 != IRREG_INVALID) mr[r3].spillLock = true;
	if (r4 != IRREG_INVALID) mr[r4].spillLock = true;
	pendingUnlock_ = true;
}

void RiscVRegCache::ReleaseSpillLocksAndDiscardTemps() {
	if (!pendingUnlock_)
		return;

	for (int i = 0; i < NUM_MIPSREG; i++) {
		if (!mr[i].isStatic)
			mr[i].spillLock = false;
	}
	for (int i = 0; i < NUM_RVREG; i++) {
		ar[i].tempLocked = false;
	}

	pendingUnlock_ = false;
}

void RiscVRegCache::ReleaseSpillLock(IRRegIndex r1, IRRegIndex r2, IRRegIndex r3, IRRegIndex r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	if (!mr[r1].isStatic)
		mr[r1].spillLock = false;
	if (r2 != IRREG_INVALID && !mr[r2].isStatic)
		mr[r2].spillLock = false;
	if (r3 != IRREG_INVALID && !mr[r3].isStatic)
		mr[r3].spillLock = false;
	if (r4 != IRREG_INVALID && !mr[r4].isStatic)
		mr[r4].spillLock = false;
}

RiscVReg RiscVRegCache::R(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM) {
		return mr[mipsReg].reg;
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

RiscVReg RiscVRegCache::RPtr(IRRegIndex mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM || mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::RVREG_AS_PTR) {
		return mr[mipsReg].reg;
	} else if (mr[mipsReg].loc == MIPSLoc::RVREG || mr[mipsReg].loc == MIPSLoc::RVREG_IMM) {
		int rv = mr[mipsReg].reg;
		_dbg_assert_(ar[rv].pointerified);
		if (ar[rv].pointerified) {
			return mr[mipsReg].reg;
		} else {
			ERROR_LOG(JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}
