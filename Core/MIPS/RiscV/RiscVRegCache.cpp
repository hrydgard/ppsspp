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

RiscVRegCache::RiscVRegCache(MIPSComp::JitOptions *jo)
	: IRNativeRegCache(jo) {
	// TODO: Move to using for FPRs and VPRs too?
	totalNativeRegs_ = NUM_RVREG;
}

void RiscVRegCache::Init(RiscVEmitter *emitter) {
	emit_ = emitter;
}

void RiscVRegCache::SetupInitialRegs() {
	IRNativeRegCache::SetupInitialRegs();

	// Treat R_ZERO a bit specially, but it's basically static alloc too.
	nrInitial_[R_ZERO].mipsReg = MIPS_REG_ZERO;
	nrInitial_[R_ZERO].normalized32 = true;

	// Since we also have a fixed zero, mark it as a static allocation.
	mrInitial_[MIPS_REG_ZERO].loc = MIPSLoc::REG_IMM;
	mrInitial_[MIPS_REG_ZERO].nReg = R_ZERO;
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
	}
	return IRNativeRegCache::GetStaticAllocations(count);
}

void RiscVRegCache::EmitLoadStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		if (allocs[i].pointerified && jo_->enablePointerify) {
			emit_->LWU((RiscVReg)allocs[i].nr, CTXREG, offset);
			emit_->ADD((RiscVReg)allocs[i].nr, (RiscVReg)allocs[i].nr, MEMBASEREG);
		} else {
			emit_->LW((RiscVReg)allocs[i].nr, CTXREG, offset);
		}
	}
}

void RiscVRegCache::EmitSaveStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	// This only needs to run once (by Asm) so checks don't need to be fast.
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		emit_->SW((RiscVReg)allocs[i].nr, CTXREG, offset);
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

bool RiscVRegCache::IsInRAM(IRReg reg) {
	_dbg_assert_(IsValidReg(reg));
	return mr[reg].loc == MIPSLoc::MEM;
}

bool RiscVRegCache::IsMapped(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	return mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM;
}

bool RiscVRegCache::IsMappedAsPointer(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	if (mr[mipsReg].loc == MIPSLoc::REG) {
		return nr[mr[mipsReg].nReg].pointerified;
	} else if (mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		if (nr[mr[mipsReg].nReg].pointerified) {
			ERROR_LOG(JIT, "Really shouldn't be pointerified here");
		}
	} else if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return true;
	}
	return false;
}

bool RiscVRegCache::IsMappedAsStaticPointer(IRReg reg) {
	if (IsMappedAsPointer(reg)) {
		return mr[reg].isStatic;
	}
	return false;
}

bool RiscVRegCache::IsNormalized32(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	if (XLEN == 32)
		return true;
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return nr[mr[mipsReg].nReg].normalized32;
	}
	return false;
}

void RiscVRegCache::MarkDirty(RiscVReg reg, bool andNormalized32) {
	// Can't mark X0 dirty.
	_dbg_assert_(reg > X0 && reg <= X31);
	nr[reg].isDirty = true;
	nr[reg].normalized32 = andNormalized32;
	// If reg is written to, pointerification is lost.
	nr[reg].pointerified = false;
	if (nr[reg].mipsReg != IRREG_INVALID) {
		RegStatusMIPS &m = mr[nr[reg].mipsReg];
		if (m.loc == MIPSLoc::REG_AS_PTR || m.loc == MIPSLoc::REG_IMM) {
			m.loc = MIPSLoc::REG;
			m.imm = -1;
		}
		_dbg_assert_(m.loc == MIPSLoc::REG);
	}
}

void RiscVRegCache::MarkPtrDirty(RiscVReg reg) {
	// Can't mark X0 dirty.
	_dbg_assert_(reg > X0 && reg <= X31);
	_dbg_assert_(!nr[reg].normalized32);
	nr[reg].isDirty = true;
	if (nr[reg].mipsReg != IRREG_INVALID) {
		_dbg_assert_(mr[nr[reg].mipsReg].loc == MIPSLoc::REG_AS_PTR);
	} else {
		_dbg_assert_(nr[reg].pointerified);
	}
}

RiscVGen::RiscVReg RiscVRegCache::Normalize32(IRReg mipsReg, RiscVGen::RiscVReg destReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(destReg == INVALID_REG || (destReg > X0 && destReg <= X31));

	RiscVReg reg = (RiscVReg)mr[mipsReg].nReg;
	if (XLEN == 32)
		return reg;

	switch (mr[mipsReg].loc) {
	case MIPSLoc::IMM:
	case MIPSLoc::MEM:
		_assert_msg_(false, "Cannot normalize an imm or mem");
		return INVALID_REG;

	case MIPSLoc::REG:
	case MIPSLoc::REG_IMM:
		if (!nr[mr[mipsReg].nReg].normalized32) {
			if (destReg == INVALID_REG) {
				emit_->SEXT_W((RiscVReg)mr[mipsReg].nReg, (RiscVReg)mr[mipsReg].nReg);
				nr[mr[mipsReg].nReg].normalized32 = true;
				nr[mr[mipsReg].nReg].pointerified = false;
			} else {
				emit_->SEXT_W(destReg, (RiscVReg)mr[mipsReg].nReg);
			}
		} else if (destReg != INVALID_REG) {
			emit_->SEXT_W(destReg, (RiscVReg)mr[mipsReg].nReg);
		}
		break;

	case MIPSLoc::REG_AS_PTR:
		_dbg_assert_(nr[mr[mipsReg].nReg].normalized32 == false);
		if (destReg == INVALID_REG) {
			// If we can pointerify, SEXT_W will be enough.
			if (!jo_->enablePointerify)
				emit_->SUB((RiscVReg)mr[mipsReg].nReg, (RiscVReg)mr[mipsReg].nReg, MEMBASEREG);
			emit_->SEXT_W((RiscVReg)mr[mipsReg].nReg, (RiscVReg)mr[mipsReg].nReg);
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[mr[mipsReg].nReg].normalized32 = true;
			nr[mr[mipsReg].nReg].pointerified = false;
		} else if (!jo_->enablePointerify) {
			emit_->SUB(destReg, (RiscVReg)mr[mipsReg].nReg, MEMBASEREG);
			emit_->SEXT_W(destReg, destReg);
		} else {
			emit_->SEXT_W(destReg, (RiscVReg)mr[mipsReg].nReg);
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
	_dbg_assert_(!nr[reg].pointerified);
	nr[reg].normalized32 = imm == (u64)(s64)(s32)imm;
}

void RiscVRegCache::MapRegTo(RiscVReg reg, IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(reg > X0 && reg <= X31);
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(!mr[mipsReg].isStatic);
	if (mr[mipsReg].isStatic) {
		ERROR_LOG(JIT, "Cannot MapRegTo static register %d", mipsReg);
		return;
	}
	nr[reg].isDirty = (mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY;
	if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
		if (mipsReg == MIPS_REG_ZERO) {
			// If we get a request to load the zero register, at least we won't spend
			// time on a memory access...
			emit_->LI(reg, 0);

			// This way, if we SetImm() it, we'll keep it.
			mr[mipsReg].loc = MIPSLoc::REG_IMM;
			mr[mipsReg].imm = 0;
			nr[reg].normalized32 = true;
		} else {
			switch (mr[mipsReg].loc) {
			case MIPSLoc::MEM:
				emit_->LW(reg, CTXREG, GetMipsRegOffset(mipsReg));
				mr[mipsReg].loc = MIPSLoc::REG;
				nr[reg].normalized32 = true;
				break;
			case MIPSLoc::IMM:
				SetRegImm(reg, mr[mipsReg].imm);
				// IMM is always dirty.
				nr[reg].isDirty = true;

				// If we are mapping dirty, it means we're gonna overwrite.
				// So the imm value is no longer valid.
				if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY)
					mr[mipsReg].loc = MIPSLoc::REG;
				else
					mr[mipsReg].loc = MIPSLoc::REG_IMM;
				break;
			case MIPSLoc::REG_IMM:
				// If it's not dirty, we can keep it.
				if (nr[reg].isDirty)
					mr[mipsReg].loc = MIPSLoc::REG;
				break;
			default:
				_assert_msg_(mr[mipsReg].loc != MIPSLoc::REG_AS_PTR, "MapRegTo with a pointer?");
				mr[mipsReg].loc = MIPSLoc::REG;
				break;
			}
		}
	} else {
		_dbg_assert_(mipsReg != MIPS_REG_ZERO);
		_dbg_assert_(nr[reg].isDirty);
		mr[mipsReg].loc = MIPSLoc::REG;
	}
	nr[reg].mipsReg = mipsReg;
	nr[reg].pointerified = false;
	if (nr[reg].isDirty)
		nr[reg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
	mr[mipsReg].nReg = reg;
}

RiscVReg RiscVRegCache::AllocateReg() {
	int allocCount;
	const RiscVReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		RiscVReg reg = allocOrder[i];

		if (nr[reg].mipsReg == IRREG_INVALID && nr[reg].tempLockIRIndex < irIndex_) {
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
			DiscardR(nr[bestToSpill].mipsReg);
		} else {
			FlushRiscVReg(bestToSpill);
		}
		// Now one must be free.
		goto allocate;
	}

	// Uh oh, we have all of them spilllocked....
	ERROR_LOG_REPORT(JIT, "Out of spillable registers in block PC %08x, index %d", irBlock_->GetOriginalStart(), irIndex_);
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
		if (nr[reg].mipsReg != IRREG_INVALID && mr[nr[reg].mipsReg].spillLockIRIndex >= irIndex_)
			continue;
		if (nr[reg].tempLockIRIndex >= irIndex_)
			continue;

		// As it's in alloc-order, we know it's not static so we don't need to check for that.
		IRUsage usage = IRNextGPRUsage(nr[reg].mipsReg, info);

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

RiscVReg RiscVRegCache::TryMapTempImm(IRReg r) {
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
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just use this reg.
				return (RiscVReg)mr[i].nReg;
			}
		}
	}

	return INVALID_REG;
}

RiscVReg RiscVRegCache::GetAndLockTempR() {
	RiscVReg reg = AllocateReg();
	if (reg != INVALID_REG) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return reg;
}

RiscVReg RiscVRegCache::MapReg(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidReg(mipsReg));

	// TODO: Optimization to force HI/LO to be combined?

	if (mipsReg == IRREG_INVALID) {
		ERROR_LOG(JIT, "Cannot map invalid register");
		return INVALID_REG;
	}

	RiscVReg riscvReg = (RiscVReg)mr[mipsReg].nReg;

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
				mr[mipsReg].loc = MIPSLoc::REG_IMM;
				nr[riscvReg].pointerified = false;
			}
			if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32)
				nr[riscvReg].normalized32 = true;
		} else if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
			// Was mapped as pointer, now we want it mapped as a value, presumably to
			// add or subtract stuff to it.
			if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!nr[riscvReg].isDirty && (mapFlags & MIPSMap::DIRTY) != MIPSMap::DIRTY);
#endif
				emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
			}
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[riscvReg].normalized32 = false;
		}
		// Erasing the imm on dirty (necessary since otherwise we will still think it's ML_RVREG_IMM and return
		// true for IsImm and calculate crazily wrong things).  /unknown
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			// As we are dirty, can't keep RVREG_IMM, we will quickly drift out of sync
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[riscvReg].pointerified = false;
			nr[riscvReg].isDirty = true;
			nr[riscvReg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
		} else if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32) {
			nr[riscvReg].normalized32 = true;
		}
		return (RiscVReg)mr[mipsReg].nReg;
	}

	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		_dbg_assert_(riscvReg != INVALID_REG && nr[riscvReg].mipsReg == mipsReg);
		if (nr[riscvReg].mipsReg != mipsReg) {
			ERROR_LOG_REPORT(JIT, "Register mapping out of sync! %i", mipsReg);
		}
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			// Mapping dirty means the old imm value is invalid.
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[riscvReg].isDirty = true;
			// If reg is written to, pointerification is lost.
			nr[riscvReg].pointerified = false;
			nr[riscvReg].normalized32 = (mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
		} else if ((mapFlags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32) {
			nr[riscvReg].normalized32 = true;
		}

		return (RiscVReg)mr[mipsReg].nReg;
	} else if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		// Was mapped as pointer, now we want it mapped as a value, presumably to
		// add or subtract stuff to it.
		if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
#ifdef MASKED_PSP_MEMORY
			_dbg_assert_(!nr[riscvReg].isDirty && (mapFlags & MAP_DIRTY) == 0);
#endif
			emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
		}
		mr[mipsReg].loc = MIPSLoc::REG;
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			nr[riscvReg].isDirty = true;
		}
		// Let's always set this false, the SUB won't normalize.
		nr[riscvReg].normalized32 = false;
		return (RiscVReg)mr[mipsReg].nReg;
	}

	// Okay, not mapped, so we need to allocate an RV register.
	RiscVReg reg = AllocateReg();
	if (reg != INVALID_REG) {
		// Grab it, and load the value into it (if requested).
		MapRegTo(reg, mipsReg, mapFlags);
	}

	return reg;
}

RiscVReg RiscVRegCache::MapRegAsPointer(IRReg reg) {
	_dbg_assert_(IsValidRegNoZero(reg));

	// Already mapped.
	if (mr[reg].loc == MIPSLoc::REG_AS_PTR) {
		return (RiscVReg)mr[reg].nReg;
	}

	RiscVReg riscvReg = INVALID_REG;
	if (mr[reg].loc != MIPSLoc::REG && mr[reg].loc != MIPSLoc::REG_IMM) {
		riscvReg = MapReg(reg);
	} else {
		riscvReg = (RiscVReg)mr[reg].nReg;
	}

	if (mr[reg].loc == MIPSLoc::REG || mr[reg].loc == MIPSLoc::REG_IMM) {
		// If there was an imm attached, discard it.
		mr[reg].loc = MIPSLoc::REG;
		if (!jo_->enablePointerify) {
			// Convert to a pointer by adding the base and clearing off the top bits.
			// If SP, we can probably avoid the top bit clear, let's play with that later.
			AddMemBase(riscvReg);
			mr[reg].loc = MIPSLoc::REG_AS_PTR;
		} else if (!nr[riscvReg].pointerified) {
			AddMemBase(riscvReg);
			nr[riscvReg].pointerified = true;
		}
		nr[riscvReg].normalized32 = false;
	} else {
		ERROR_LOG(JIT, "MapRegAsPointer : MapReg failed to allocate a register?");
	}
	return riscvReg;
}

void RiscVRegCache::AddMemBase(RiscVGen::RiscVReg reg) {
	_assert_(reg >= X0 && reg <= X31);
#ifdef MASKED_PSP_MEMORY
	// This destroys the value...
	_dbg_assert_(!nr[reg].isDirty);
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
	nr[reg].normalized32 = false;
}

void RiscVRegCache::MapIn(IRReg rs) {
	MapReg(rs);
}

void RiscVRegCache::MapInIn(IRReg rd, IRReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCache::MapDirtyIn(IRReg rd, IRReg rs, MapType type) {
	SpillLock(rd, rs);
	bool load = type == MapType::ALWAYS_LOAD || rd == rs;
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd, (load ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCache::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt, MapType type) {
	SpillLock(rd, rs, rt);
	bool load = type == MapType::ALWAYS_LOAD || (rd == rs || rd == rt);
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd, (load ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd, rs, rt);
}

void RiscVRegCache::MapDirtyDirtyIn(IRReg rd1, IRReg rd2, IRReg rs, MapType type) {
	SpillLock(rd1, rd2, rs);
	bool load1 = type == MapType::ALWAYS_LOAD || rd1 == rs;
	bool load2 = type == MapType::ALWAYS_LOAD || rd2 == rs;
	MIPSMap norm32 = type == MapType::AVOID_LOAD_MARK_NORM32 ? MIPSMap::MARK_NORM32 : MIPSMap::INIT;
	MapReg(rd1, (load1 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rd2, (load2 ? MIPSMap::DIRTY : MIPSMap::NOINIT) | norm32);
	MapReg(rs);
	ReleaseSpillLock(rd1, rd2, rs);
}

void RiscVRegCache::MapDirtyDirtyInIn(IRReg rd1, IRReg rd2, IRReg rs, IRReg rt, MapType type) {
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
	_dbg_assert_(nr[r].mipsReg != MIPS_REG_ZERO);
	if (r == INVALID_REG) {
		ERROR_LOG(JIT, "FlushRiscVReg called on invalid register %d", r);
		return;
	}
	if (nr[r].mipsReg == IRREG_INVALID) {
		// Nothing to do, reg not mapped.
		_dbg_assert_(!nr[r].isDirty);
		return;
	}
	_dbg_assert_(!mr[nr[r].mipsReg].isStatic);
	if (mr[nr[r].mipsReg].isStatic) {
		ERROR_LOG(JIT, "Cannot FlushRiscVReg a statically mapped register");
		return;
	}
	auto &mreg = mr[nr[r].mipsReg];
	if (mreg.loc == MIPSLoc::REG_IMM || nr[r].mipsReg == MIPS_REG_ZERO) {
		// We know its immediate value, no need to STR now.
		mreg.loc = MIPSLoc::IMM;
		mreg.nReg = (int)INVALID_REG;
	} else {
		if (mreg.loc == MIPSLoc::IMM || nr[r].isDirty) {
			if (mreg.loc == MIPSLoc::REG_AS_PTR) {
				// Unpointerify, in case dirty.
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!nr[r].isDirty);
#endif
				emit_->SUB(r, r, MEMBASEREG);
				mreg.loc = MIPSLoc::REG;
				nr[r].normalized32 = false;
			}
			RiscVReg storeReg = RiscVRegForFlush(nr[r].mipsReg);
			if (storeReg != INVALID_REG)
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(nr[r].mipsReg));
		}
		mreg.loc = MIPSLoc::MEM;
		mreg.nReg = (int)INVALID_REG;
		mreg.imm = -1;
	}
	nr[r].isDirty = false;
	nr[r].mipsReg = IRREG_INVALID;
	nr[r].pointerified = false;
}

void RiscVRegCache::DiscardR(IRReg mipsReg) {
	_dbg_assert_(IsValidRegNoZero(mipsReg));
	if (mr[mipsReg].isStatic) {
		// Simply do nothing unless it's an IMM/RVREG_IMM/RVREG_AS_PTR, in case we just switch it over to RVREG, losing the value.
		RiscVReg riscvReg = (RiscVReg)mr[mipsReg].nReg;
		_dbg_assert_(riscvReg != INVALID_REG);
		if (mipsReg == MIPS_REG_ZERO) {
			// Shouldn't happen, but in case it does.
			mr[mipsReg].loc = MIPSLoc::REG_IMM;
			mr[mipsReg].nReg = R_ZERO;
			mr[mipsReg].imm = 0;
		} else if (mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
			// Ignore the imm value, restore sanity
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[riscvReg].pointerified = false;
			nr[riscvReg].isDirty = false;
			nr[riscvReg].normalized32 = false;
		}
		return;
	}
	const MIPSLoc prevLoc = mr[mipsReg].loc;
	if (prevLoc == MIPSLoc::REG || prevLoc == MIPSLoc::REG_IMM || prevLoc == MIPSLoc::REG_AS_PTR) {
		RiscVReg riscvReg = (RiscVReg)mr[mipsReg].nReg;
		_dbg_assert_(riscvReg != INVALID_REG);
		nr[riscvReg].mipsReg = IRREG_INVALID;
		nr[riscvReg].pointerified = false;
		nr[riscvReg].isDirty = false;
		nr[riscvReg].normalized32 = false;
		mr[mipsReg].nReg = (int)INVALID_REG;
		mr[mipsReg].loc = MIPSLoc::MEM;
		mr[mipsReg].imm = -1;
	}
	if (prevLoc == MIPSLoc::IMM && mipsReg != MIPS_REG_ZERO) {
		mr[mipsReg].loc = MIPSLoc::MEM;
		mr[mipsReg].imm = -1;
	}
}

RiscVReg RiscVRegCache::RiscVRegForFlush(IRReg r) {
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
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just store this reg.
				return (RiscVReg)mr[i].nReg;
			}
		}
		return INVALID_REG;

	case MIPSLoc::REG:
	case MIPSLoc::REG_IMM:
		if (mr[r].nReg == INVALID_REG) {
			ERROR_LOG_REPORT(JIT, "RiscVRegForFlush: MipsReg %d had bad riscvReg", r);
			return INVALID_REG;
		}
		// No need to flush if it's zero or not dirty.
		if (r == MIPS_REG_ZERO || !nr[mr[r].nReg].isDirty) {
			return INVALID_REG;
		}
		// TODO: Lo/hi optimization?
		return (RiscVReg)mr[r].nReg;

	case MIPSLoc::REG_AS_PTR:
		return INVALID_REG;

	case MIPSLoc::MEM:
		return INVALID_REG;

	default:
		ERROR_LOG_REPORT(JIT, "RiscVRegForFlush: MipsReg %d with invalid location %d", r, (int)mr[r].loc);
		return INVALID_REG;
	}
}

void RiscVRegCache::FlushR(IRReg r) {
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

	case MIPSLoc::REG:
	case MIPSLoc::REG_IMM:
		if (nr[mr[r].nReg].isDirty) {
			RiscVReg storeReg = RiscVRegForFlush(r);
			if (storeReg != INVALID_REG) {
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(r));
			}
			nr[mr[r].nReg].isDirty = false;
		}
		nr[mr[r].nReg].mipsReg = IRREG_INVALID;
		nr[mr[r].nReg].pointerified = false;
		break;

	case MIPSLoc::REG_AS_PTR:
		if (nr[mr[r].nReg].isDirty) {
#ifdef MASKED_PSP_MEMORY
			// This is kinda bad, because we've cleared bits in it.
			_dbg_assert_(!nr[mr[r].nReg].isDirty);
#endif
			emit_->SUB((RiscVReg)mr[r].nReg, (RiscVReg)mr[r].nReg, MEMBASEREG);
			// We set this so RiscVRegForFlush knows it's no longer a pointer.
			mr[r].loc = MIPSLoc::REG;
			RiscVReg storeReg = RiscVRegForFlush(r);
			if (storeReg != INVALID_REG) {
				emit_->SW(storeReg, CTXREG, GetMipsRegOffset(r));
			}
			nr[mr[r].nReg].isDirty = false;
		}
		nr[mr[r].nReg].mipsReg = IRREG_INVALID;
		break;

	case MIPSLoc::MEM:
		// Already there, nothing to do.
		break;

	default:
		ERROR_LOG_REPORT(JIT, "FlushR: MipsReg %d with invalid location %d", r, (int)mr[r].loc);
		break;
	}
	if (r == MIPS_REG_ZERO) {
		mr[r].loc = MIPSLoc::REG_IMM;
		mr[r].nReg = R_ZERO;
		mr[r].imm = 0;
	} else {
		mr[r].loc = MIPSLoc::MEM;
		mr[r].nReg = (int)INVALID_REG;
		mr[r].imm = -1;
	}
}

void RiscVRegCache::FlushAll() {
	// Note: make sure not to change the registers when flushing:
	// Branching code expects the armreg to retain its value.

	// TODO: HI/LO optimization?

	// Final pass to grab any that were left behind.
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; i++) {
		IRReg mipsReg = IRReg(i);
		if (mr[i].isStatic) {
			RiscVReg riscvReg = (RiscVReg)mr[i].nReg;
			// Cannot leave any IMMs in registers, not even ML_ARMREG_IMM, can confuse the regalloc later if this flush is mid-block
			// due to an interpreter fallback that changes the register.
			if (mr[i].loc == MIPSLoc::IMM) {
				SetRegImm((RiscVReg)mr[i].nReg, mr[i].imm);
				mr[i].loc = MIPSLoc::REG;
				nr[riscvReg].pointerified = false;
			} else if (mr[i].loc == MIPSLoc::REG_IMM) {
				// The register already contains the immediate.
				if (nr[riscvReg].pointerified) {
					ERROR_LOG(JIT, "RVREG_IMM but pointerified. Wrong.");
					nr[riscvReg].pointerified = false;
				}
				mr[i].loc = MIPSLoc::REG;
			} else if (mr[i].loc == MIPSLoc::REG_AS_PTR) {
#ifdef MASKED_PSP_MEMORY
				_dbg_assert_(!nr[riscvReg].isDirty);
#endif
				emit_->SUB(riscvReg, riscvReg, MEMBASEREG);
				mr[i].loc = MIPSLoc::REG;
			}
			if (i != MIPS_REG_ZERO && mr[i].nReg == INVALID_REG) {
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
		if (allocs[i].pointerified && !nr[allocs[i].nr].pointerified && jo_->enablePointerify) {
			// Re-pointerify
			_dbg_assert_(mr[allocs[i].mr].loc == MIPSLoc::REG);
			AddMemBase((RiscVReg)allocs[i].nr);
			nr[allocs[i].nr].pointerified = true;
		} else if (!allocs[i].pointerified) {
			// If this register got pointerified on the way, mark it as not.
			// This is so that after save/reload (like in an interpreter fallback),
			// it won't be regarded as such, as it may no longer be.
			nr[allocs[i].nr].pointerified = false;
		}
	}
	// Sanity check
	for (int i = 0; i < NUM_RVREG; i++) {
		if (nr[i].mipsReg != IRREG_INVALID && mr[nr[i].mipsReg].isStatic == false) {
			ERROR_LOG_REPORT(JIT, "Flush fail: nr[%i].mipsReg=%i", i, nr[i].mipsReg);
		}
	}
}

void RiscVRegCache::SetImm(IRReg r, u64 immVal) {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO && immVal != 0) {
		ERROR_LOG_REPORT(JIT, "Trying to set immediate %08x to r0", (u32)immVal);
		return;
	}

	if (mr[r].loc == MIPSLoc::REG_IMM && mr[r].imm == immVal) {
		// Already have that value, let's keep it in the reg.
		return;
	}

	// TODO: HI/LO optimization?
	// All regs on the PSP are 32 bit, but LO we treat as HI:LO so is 64 full bits.
	immVal = immVal & 0xFFFFFFFF;

	if (mr[r].isStatic) {
		mr[r].loc = MIPSLoc::IMM;
		mr[r].imm = immVal;
		nr[mr[r].nReg].pointerified = false;
		nr[mr[r].nReg].normalized32 = false;
		// We do not change reg to INVALID_REG for obvious reasons..
	} else {
		// Zap existing value if cached in a reg
		if (mr[r].nReg != INVALID_REG) {
			nr[mr[r].nReg].mipsReg = IRREG_INVALID;
			nr[mr[r].nReg].isDirty = false;
			nr[mr[r].nReg].pointerified = false;
			nr[mr[r].nReg].normalized32 = false;
		}
		mr[r].loc = MIPSLoc::IMM;
		mr[r].imm = immVal;
		mr[r].nReg = (int)INVALID_REG;
	}
}

bool RiscVRegCache::IsImm(IRReg r) const {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO)
		return true;
	else
		return mr[r].loc == MIPSLoc::IMM || mr[r].loc == MIPSLoc::REG_IMM;
}

u64 RiscVRegCache::GetImm(IRReg r) const {
	_dbg_assert_(IsValidReg(r));
	if (r == MIPS_REG_ZERO)
		return 0;
	if (mr[r].loc != MIPSLoc::IMM && mr[r].loc != MIPSLoc::REG_IMM) {
		ERROR_LOG_REPORT(JIT, "Trying to get imm from non-imm register %i", r);
	}
	return mr[r].imm;
}

int RiscVRegCache::GetMipsRegOffset(IRReg r) {
	_dbg_assert_(IsValidReg(r));
	return r * 4;
}

bool RiscVRegCache::IsValidReg(IRReg r) const {
	if (r < 0 || r >= TOTAL_MAPPABLE_IRREGS)
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

bool RiscVRegCache::IsValidRegNoZero(IRReg r) const {
	return IsValidReg(r) && r != MIPS_REG_ZERO;
}

void RiscVRegCache::SpillLock(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	mr[r1].spillLockIRIndex = irIndex_;
	if (r2 != IRREG_INVALID) mr[r2].spillLockIRIndex = irIndex_;
	if (r3 != IRREG_INVALID) mr[r3].spillLockIRIndex = irIndex_;
	if (r4 != IRREG_INVALID) mr[r4].spillLockIRIndex = irIndex_;
}

void RiscVRegCache::ReleaseSpillLock(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	if (!mr[r1].isStatic)
		mr[r1].spillLockIRIndex = -1;
	if (r2 != IRREG_INVALID && !mr[r2].isStatic)
		mr[r2].spillLockIRIndex = -1;
	if (r3 != IRREG_INVALID && !mr[r3].isStatic)
		mr[r3].spillLockIRIndex = -1;
	if (r4 != IRREG_INVALID && !mr[r4].isStatic)
		mr[r4].spillLockIRIndex = -1;
}

RiscVReg RiscVRegCache::R(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return (RiscVReg)mr[mipsReg].nReg;
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

RiscVReg RiscVRegCache::RPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return (RiscVReg)mr[mipsReg].nReg;
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int rv = mr[mipsReg].nReg;
		_dbg_assert_(nr[rv].pointerified);
		if (nr[rv].pointerified) {
			return (RiscVReg)mr[mipsReg].nReg;
		} else {
			ERROR_LOG(JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}
