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

#include <cstring>
#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Core/MemMap.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/JitCommon/JitState.h"

void IRImmRegCache::Flush(IRReg rd) {
	if (rd == 0) {
		return;
	}
	if (reg_[rd].isImm) {
		_assert_((rd > 0 && rd < 32) || (rd >= IRTEMP_0 && rd < IRREG_VFPU_CTRL_BASE));
		ir_->WriteSetConstant(rd, reg_[rd].immVal);
		reg_[rd].isImm = false;
	}
}

void IRImmRegCache::Discard(IRReg rd) {
	if (rd == 0) {
		return;
	}
	reg_[rd].isImm = false;
}

IRImmRegCache::IRImmRegCache(IRWriter *ir) : ir_(ir) {
	memset(&reg_, 0, sizeof(reg_));
	reg_[0].isImm = true;
	ir_ = ir;
}

void IRImmRegCache::FlushAll() {
	for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; i++) {
		Flush(i);
	}
}

void IRImmRegCache::MapIn(IRReg rd) {
	Flush(rd);
}

void IRImmRegCache::MapDirty(IRReg rd) {
	Discard(rd);
}

void IRImmRegCache::MapInIn(IRReg rs, IRReg rt) {
	Flush(rs);
	Flush(rt);
}

void IRImmRegCache::MapInInIn(IRReg rd, IRReg rs, IRReg rt) {
	Flush(rd);
	Flush(rs);
	Flush(rt);
}

void IRImmRegCache::MapDirtyIn(IRReg rd, IRReg rs) {
	if (rs != rd) {
		Discard(rd);
	}
	Flush(rs);
}

void IRImmRegCache::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt) {
	if (rs != rd && rt != rd) {
		Discard(rd);
	}
	Flush(rs);
	Flush(rt);
}

IRNativeRegCacheBase::IRNativeRegCacheBase(MIPSComp::JitOptions *jo)
	: jo_(jo) {}

void IRNativeRegCacheBase::Start(MIPSComp::IRBlock *irBlock) {
	if (!initialReady_) {
		SetupInitialRegs();
		initialReady_ = true;
	}

	memcpy(nr, nrInitial_, sizeof(nr[0]) * totalNativeRegs_);
	memcpy(mr, mrInitial_, sizeof(mr));

	int numStatics;
	const StaticAllocation *statics = GetStaticAllocations(numStatics);
	for (int i = 0; i < numStatics; i++) {
		nr[statics[i].nr].mipsReg = statics[i].mr;
		nr[statics[i].nr].pointerified = statics[i].pointerified && jo_->enablePointerify;
		nr[statics[i].nr].normalized32 = statics[i].normalized32;
		mr[statics[i].mr].loc = statics[i].loc;
		mr[statics[i].mr].nReg = statics[i].nr;
		mr[statics[i].mr].isStatic = true;
		// Lock it until the very end.
		mr[statics[i].mr].spillLockIRIndex = irBlock->GetNumInstructions();
	}

	irBlock_ = irBlock;
	irIndex_ = 0;
}

void IRNativeRegCacheBase::SetupInitialRegs() {
	_assert_msg_(totalNativeRegs_ > 0, "totalNativeRegs_ was never set by backend");

	// Everything else is initialized in the struct.
	mrInitial_[MIPS_REG_ZERO].loc = MIPSLoc::IMM;
	mrInitial_[MIPS_REG_ZERO].imm = 0;
}

bool IRNativeRegCacheBase::IsGPRInRAM(IRReg gpr) {
	_dbg_assert_(IsValidGPR(gpr));
	return mr[gpr].loc == MIPSLoc::MEM;
}

bool IRNativeRegCacheBase::IsFPRInRAM(IRReg fpr) {
	_dbg_assert_(IsValidFPR(fpr));
	return mr[fpr + 32].loc == MIPSLoc::MEM;
}

bool IRNativeRegCacheBase::IsGPRMapped(IRReg gpr) {
	_dbg_assert_(IsValidGPR(gpr));
	return mr[gpr].loc == MIPSLoc::REG || mr[gpr].loc == MIPSLoc::REG_IMM;
}

bool IRNativeRegCacheBase::IsFPRMapped(IRReg fpr) {
	_dbg_assert_(IsValidFPR(fpr));
	return mr[fpr + 32].loc == MIPSLoc::FREG || mr[fpr + 32].loc == MIPSLoc::VREG;
}

bool IRNativeRegCacheBase::IsGPRMappedAsPointer(IRReg gpr) {
	_dbg_assert_(IsValidGPR(gpr));
	if (mr[gpr].loc == MIPSLoc::REG) {
		return nr[mr[gpr].nReg].pointerified;
	} else if (mr[gpr].loc == MIPSLoc::REG_IMM) {
		_assert_msg_(!nr[mr[gpr].nReg].pointerified, "Really shouldn't be pointerified here");
	} else if (mr[gpr].loc == MIPSLoc::REG_AS_PTR) {
		return true;
	}
	return false;
}

bool IRNativeRegCacheBase::IsGPRMappedAsStaticPointer(IRReg gpr) {
	if (IsGPRMappedAsPointer(gpr)) {
		return mr[gpr].isStatic;
	}
	return false;
}

bool IRNativeRegCacheBase::IsGPRImm(IRReg gpr) {
	_dbg_assert_(IsValidGPR(gpr));
	if (gpr == MIPS_REG_ZERO)
		return true;
	return mr[gpr].loc == MIPSLoc::IMM || mr[gpr].loc == MIPSLoc::REG_IMM;
}

bool IRNativeRegCacheBase::IsGPR2Imm(IRReg base) {
	return IsGPRImm(base) && IsGPRImm(base + 1);
}

uint32_t IRNativeRegCacheBase::GetGPRImm(IRReg gpr) {
	_dbg_assert_(IsValidGPR(gpr));
	if (gpr == MIPS_REG_ZERO)
		return 0;
	if (mr[gpr].loc != MIPSLoc::IMM && mr[gpr].loc != MIPSLoc::REG_IMM) {
		_assert_msg_(mr[gpr].loc == MIPSLoc::IMM || mr[gpr].loc == MIPSLoc::REG_IMM, "GPR %d not in an imm", gpr);
	}
	return mr[gpr].imm;
}

uint64_t IRNativeRegCacheBase::GetGPR2Imm(IRReg base) {
	return (uint64_t)GetGPRImm(base) | ((uint64_t)GetGPRImm(base + 1) << 32);
}

void IRNativeRegCacheBase::SetGPRImm(IRReg gpr, uint32_t immVal) {
	_dbg_assert_(IsValidGPR(gpr));
	if (gpr == MIPS_REG_ZERO && immVal != 0) {
		ERROR_LOG_REPORT(JIT, "Trying to set immediate %08x to r0", immVal);
		return;
	}

	if (mr[gpr].loc == MIPSLoc::REG_IMM && mr[gpr].imm == immVal) {
		// Already have that value, let's keep it in the reg.
		return;
	}

	if (mr[gpr].nReg != -1) {
		// Zap existing value if cached in a reg.
		_assert_msg_(mr[gpr].lane == -1, "Should not be a multilane reg");
		DiscardNativeReg(mr[gpr].nReg);
	}

	mr[gpr].loc = MIPSLoc::IMM;
	mr[gpr].imm = immVal;
}

void IRNativeRegCacheBase::SetGPR2Imm(IRReg base, uint64_t immVal) {
	_dbg_assert_(IsValidGPRNoZero(base));
	uint32_t imm0 = (uint32_t)(immVal & 0xFFFFFFFF);
	uint32_t imm1 = (uint32_t)(immVal >> 32);

	if (IsGPRImm(base) && IsGPRImm(base + 1) && GetGPRImm(base) == imm0 && GetGPRImm(base + 1) == imm1) {
		// Already set to this, don't bother.
		return;
	}

	if (mr[base].nReg != -1) {
		// Zap existing value if cached in a reg.
		DiscardNativeReg(mr[base].nReg);
		if (mr[base + 1].nReg != -1)
			DiscardNativeReg(mr[base + 1].nReg);
	}

	mr[base].loc = MIPSLoc::IMM;
	mr[base].imm = imm0;
	mr[base + 1].loc = MIPSLoc::IMM;
	mr[base + 1].imm = imm1;
}

void IRNativeRegCacheBase::SpillLockGPR(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidGPR(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidGPR(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidGPR(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidGPR(r4));
	SetSpillLockIRIndex(r1, r2, r3, r4, 0, irIndex_);
}

void IRNativeRegCacheBase::SpillLockFPR(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidFPR(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidFPR(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidFPR(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidFPR(r4));
	SetSpillLockIRIndex(r1, r2, r3, r4, 32, irIndex_);
}

void IRNativeRegCacheBase::ReleaseSpillLockGPR(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidGPR(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidGPR(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidGPR(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidGPR(r4));
	SetSpillLockIRIndex(r1, r2, r3, r4, 0, -1);
}

void IRNativeRegCacheBase::ReleaseSpillLockFPR(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidFPR(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidFPR(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidFPR(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidFPR(r4));
	SetSpillLockIRIndex(r1, r2, r3, r4, 32, -1);
}

void IRNativeRegCacheBase::SetSpillLockIRIndex(IRReg r1, IRReg r2, IRReg r3, IRReg r4, int offset, int index) {
	if (!mr[r1 + offset].isStatic)
		mr[r1 + offset].spillLockIRIndex = index;
	if (r2 != IRREG_INVALID && !mr[r2 + offset].isStatic)
		mr[r2 + offset].spillLockIRIndex = index;
	if (r3 != IRREG_INVALID && !mr[r3 + offset].isStatic)
		mr[r3 + offset].spillLockIRIndex = index;
	if (r4 != IRREG_INVALID && !mr[r4 + offset].isStatic)
		mr[r4 + offset].spillLockIRIndex = index;
}

void IRNativeRegCacheBase::MarkGPRDirty(IRReg gpr, bool andNormalized32) {
	_assert_(IsGPRMapped(gpr));
	if (!IsGPRMapped(gpr))
		return;

	IRNativeReg nreg = mr[gpr].nReg;
	nr[nreg].isDirty = true;
	nr[nreg].normalized32 = andNormalized32;
	// If reg is written to, pointerification is assumed lost.
	nr[nreg].pointerified = false;
	if (mr[gpr].loc == MIPSLoc::REG_AS_PTR || mr[gpr].loc == MIPSLoc::REG_IMM) {
		mr[gpr].loc = MIPSLoc::REG;
		mr[gpr].imm = -1;
	}
	_dbg_assert_(mr[gpr].loc == MIPSLoc::REG);
}

void IRNativeRegCacheBase::MarkGPRAsPointerDirty(IRReg gpr) {
	_assert_(IsGPRMappedAsPointer(gpr));
	if (!IsGPRMappedAsPointer(gpr))
		return;

#ifdef MASKED_PSP_MEMORY
	if (mr[gpr].loc == MIPSLoc::REG_AS_PTR) {
		_assert_msg_(false, "MarkGPRAsPointerDirty is not possible when using MASKED_PSP_MEMORY");
	}
#endif

	IRNativeReg nreg = mr[gpr].nReg;
	_dbg_assert_(!nr[nreg].normalized32);
	nr[nreg].isDirty = true;
	// Stays pointerified or REG_AS_PTR.
}

IRNativeReg IRNativeRegCacheBase::AllocateReg(MIPSLoc type) {
	_dbg_assert_(type == MIPSLoc::REG || type == MIPSLoc::FREG || type == MIPSLoc::VREG);

	IRNativeReg nreg = FindFreeReg(type);
	if (nreg != -1)
		return nreg;

	// Still nothing. Let's spill a reg and goto 10.
	bool clobbered;
	IRNativeReg bestToSpill = FindBestToSpill(type, true, &clobbered);
	if (bestToSpill == -1) {
		bestToSpill = FindBestToSpill(type, false, &clobbered);
	}

	if (bestToSpill != -1) {
		if (clobbered) {
			DiscardNativeReg(bestToSpill);
		} else {
			FlushNativeReg(bestToSpill);
		}
		// Now one must be free.
		return FindFreeReg(type);
	}

	// Uh oh, we have all of them spilllocked....
	ERROR_LOG_REPORT(JIT, "Out of spillable registers in block PC %08x, index %d", irBlock_->GetOriginalStart(), irIndex_);
	_assert_(bestToSpill != -1);
	return -1;
}

IRNativeReg IRNativeRegCacheBase::FindFreeReg(MIPSLoc type) const {
	int allocCount = 0, base = 0;
	const int *allocOrder = GetAllocationOrder(type, allocCount, base);

	for (int i = 0; i < allocCount; i++) {
		IRNativeReg nreg = IRNativeReg(allocOrder[i] - base);

		if (nr[nreg].mipsReg == IRREG_INVALID) {
			return nreg;
		}
	}

	return -1;
}

IRNativeReg IRNativeRegCacheBase::FindBestToSpill(MIPSLoc type, bool unusedOnly, bool *clobbered) const {
	int allocCount = 0, base = 0;
	const int *allocOrder = GetAllocationOrder(type, allocCount, base);

	static const int UNUSED_LOOKAHEAD_OPS = 30;

	IRSituation info;
	info.lookaheadCount = UNUSED_LOOKAHEAD_OPS;
	info.currentIndex = irIndex_;
	info.instructions = irBlock_->GetInstructions();
	info.numInstructions = irBlock_->GetNumInstructions();

	auto getUsage = [type, &info](IRReg mipsReg) {
		if (type == MIPSLoc::REG)
			return IRNextGPRUsage(mipsReg, info);
		else if (type == MIPSLoc::FREG || type == MIPSLoc::VREG)
			return IRNextFPRUsage(mipsReg - 32, info);
		_assert_msg_(false, "Unknown spill allocation type");
		return IRUsage::UNKNOWN;
	};

	*clobbered = false;
	for (int i = 0; i < allocCount; i++) {
		IRNativeReg nreg = IRNativeReg(allocOrder[i] - base);
		if (nr[nreg].mipsReg != IRREG_INVALID && mr[nr[nreg].mipsReg].spillLockIRIndex >= irIndex_)
			continue;
		if (nr[nreg].tempLockIRIndex >= irIndex_)
			continue;

		// As it's in alloc-order, we know it's not static so we don't need to check for that.
		IRReg mipsReg = nr[nreg].mipsReg;
		IRUsage usage = getUsage(mipsReg);

		// Awesome, a clobbered reg.  Let's use it?
		if (usage == IRUsage::CLOBBERED) {
			// If multiple mips regs use this native reg (i.e. vector, HI/LO), check each.
			// Note: mipsReg points to the lowest numbered IRReg.
			bool canClobber = true;
			for (IRReg m = mipsReg + 1; mr[m].nReg == nreg && m < IRREG_INVALID && canClobber; ++m)
				canClobber = getUsage(mipsReg) == IRUsage::CLOBBERED;

			// Okay, if all can be clobbered, we're good to go.
			if (canClobber) {
				*clobbered = true;
				return nreg;
			}
		}

		// Not awesome.  A used reg.  Let's try to avoid spilling.
		if (!unusedOnly || usage == IRUsage::UNUSED) {
			// TODO: Use age or something to choose which register to spill?
			// TODO: Spill dirty regs first? or opposite?
			return nreg;
		}
	}

	return -1;
}

void IRNativeRegCacheBase::DiscardNativeReg(IRNativeReg nreg) {
	_assert_msg_(nreg >= 0 && nreg < totalNativeRegs_, "DiscardNativeReg on invalid register %d", nreg);
	if (nr[nreg].mipsReg != IRREG_INVALID) {
		_assert_(nr[nreg].mipsReg != MIPS_REG_ZERO);
		int8_t lanes = 0;
		for (IRReg m = nr[nreg].mipsReg; mr[m].nReg == nreg && m < IRREG_INVALID; ++m)
			lanes++;

		if (mr[nr[nreg].mipsReg].isStatic) {
			int numStatics;
			const StaticAllocation *statics = GetStaticAllocations(numStatics);

			// If it's not currently marked as in a reg, throw it away.
			for (IRReg m = nr[nreg].mipsReg; m < nr[nreg].mipsReg + lanes; ++m) {
				_assert_msg_(mr[m].isStatic, "Reg in lane %d mismatched static status", m - nr[nreg].mipsReg);
				for (int i = 0; i < numStatics; i++) {
					if (m == statics[i].mr)
						mr[m].loc = statics[i].loc;
				}
			}
		} else {
			for (IRReg m = nr[nreg].mipsReg; m < nr[nreg].mipsReg + lanes; ++m) {
				mr[m].loc = MIPSLoc::MEM;
				mr[m].nReg = -1;
				mr[m].imm = 0;
				mr[m].lane = -1;
				_assert_msg_(!mr[m].isStatic, "Reg in lane %d mismatched static status", m - nr[nreg].mipsReg);
			}

			nr[nreg].mipsReg = IRREG_INVALID;
		}
	}

	// Even for a static reg, we assume this means it's not pointerified anymore.
	nr[nreg].pointerified = false;
	nr[nreg].isDirty = false;
	nr[nreg].normalized32 = false;
}

void IRNativeRegCacheBase::FlushNativeReg(IRNativeReg nreg) {
	_assert_msg_(nreg >= 0 && nreg < totalNativeRegs_, "FlushNativeReg on invalid register %d", nreg);
	if (nr[nreg].mipsReg == IRREG_INVALID || nr[nreg].mipsReg == MIPS_REG_ZERO) {
		// Nothing to do, reg not mapped or mapped to fixed zero.
		_dbg_assert_(!nr[nreg].isDirty);
		return;
	}
	_dbg_assert_(!mr[nr[nreg].mipsReg].isStatic);
	if (mr[nr[nreg].mipsReg].isStatic) {
		ERROR_LOG(JIT, "Cannot FlushNativeReg a statically mapped register");
		return;
	}

	// Multiple mipsRegs may match this if a vector or HI/LO, etc.
	bool isDirty = nr[nreg].isDirty;
	int8_t lanes = 0;
	for (IRReg m = nr[nreg].mipsReg; mr[m].nReg == nreg && m < IRREG_INVALID; ++m) {
		_assert_(!mr[m].isStatic);
		// If we're flushing a native reg, better not be partially in mem or an imm.
		_assert_(mr[m].loc != MIPSLoc::MEM && mr[m].loc != MIPSLoc::IMM);
		lanes++;
	}

	if (isDirty) {
		IRReg first = nr[nreg].mipsReg;
		if (mr[first].loc == MIPSLoc::REG_AS_PTR) {
			// We assume this can't be multiple lanes.  Maybe some gather craziness?
			_assert_(lanes == 1);
			AdjustNativeRegAsPtr(nreg, false);
			mr[first].loc = MIPSLoc::REG;
		}
		StoreNativeReg(nreg, first, lanes);
	}

	for (int8_t i = 0; i < lanes; ++i) {
		auto &mreg = mr[nr[nreg].mipsReg + i];
		mreg.nReg = -1;
		// Note that it loses its imm status, because imms are always dirty.
		mreg.loc = MIPSLoc::MEM;
		mreg.imm = 0;
		mreg.lane = -1;
	}

	nr[nreg].mipsReg = IRREG_INVALID;
	nr[nreg].isDirty = false;
	nr[nreg].pointerified = false;
	nr[nreg].normalized32 = false;
}

void IRNativeRegCacheBase::DiscardReg(IRReg mreg) {
	if (mr[mreg].isStatic) {
		DiscardNativeReg(mr[mreg].nReg);
		return;
	}
	switch (mr[mreg].loc) {
	case MIPSLoc::IMM:
		if (mreg != MIPS_REG_ZERO) {
			mr[mreg].loc = MIPSLoc::MEM;
			mr[mreg].imm = 0;
		}
		break;

	case MIPSLoc::REG:
	case MIPSLoc::REG_AS_PTR:
	case MIPSLoc::REG_IMM:
	case MIPSLoc::FREG:
	case MIPSLoc::VREG:
		DiscardNativeReg(mr[mreg].nReg);
		break;

	case MIPSLoc::MEM:
		// Already discarded.
		break;
	}
	mr[mreg].spillLockIRIndex = -1;
}

void IRNativeRegCacheBase::FlushReg(IRReg mreg) {
	_assert_msg_(!mr[mreg].isStatic, "Cannot flush static reg %d", mreg);

	switch (mr[mreg].loc) {
	case MIPSLoc::IMM:
		// IMM is always "dirty".
		StoreRegValue(mreg, mr[mreg].imm);
		mr[mreg].loc = MIPSLoc::MEM;
		mr[mreg].nReg = -1;
		mr[mreg].imm = 0;
		break;

	case MIPSLoc::REG:
	case MIPSLoc::REG_IMM:
	case MIPSLoc::REG_AS_PTR:
	case MIPSLoc::FREG:
	case MIPSLoc::VREG:
		// Might be in a native reg with multiple IR regs, flush together.
		FlushNativeReg(mr[mreg].nReg);
		break;

	case MIPSLoc::MEM:
		// Already there, nothing to do.
		break;
	}
}

void IRNativeRegCacheBase::FlushAll() {
	// Note: make sure not to change the registers when flushing.
	// Branching code may expect the native reg to retain its value.

	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; i++) {
		IRReg mipsReg = (IRReg)i;
		if (mr[i].isStatic) {
			IRNativeReg nreg = mr[i].nReg;
			// Cannot leave any IMMs in registers, not even MIPSLoc::REG_IMM.
			// Can confuse the regalloc later if this flush is mid-block
			// due to an interpreter fallback that changes the register.
			if (mr[i].loc == MIPSLoc::IMM) {
				SetNativeRegValue(mr[i].nReg, mr[i].imm);
				_assert_(IsValidGPR(mipsReg));
				mr[i].loc = MIPSLoc::REG;
				nr[nreg].pointerified = false;
			} else if (mr[i].loc == MIPSLoc::REG_IMM) {
				// The register already contains the immediate.
				if (nr[nreg].pointerified) {
					ERROR_LOG(JIT, "RVREG_IMM but pointerified. Wrong.");
					nr[nreg].pointerified = false;
				}
				mr[i].loc = MIPSLoc::REG;
			} else if (mr[i].loc == MIPSLoc::REG_AS_PTR) {
				AdjustNativeRegAsPtr(mr[i].nReg, false);
				mr[i].loc = MIPSLoc::REG;
			}
			_assert_(mr[i].nReg != -1);
		} else if (mr[i].loc != MIPSLoc::MEM) {
			FlushReg(mipsReg);
		}
	}

	int count = 0;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; i++) {
		if (allocs[i].pointerified && !nr[allocs[i].nr].pointerified && jo_->enablePointerify) {
			// Re-pointerify
			if (mr[allocs[i].mr].loc == MIPSLoc::REG_IMM)
				mr[allocs[i].mr].loc = MIPSLoc::REG;
			_dbg_assert_(mr[allocs[i].mr].loc == MIPSLoc::REG);
			AdjustNativeRegAsPtr(allocs[i].nr, true);
			nr[allocs[i].nr].pointerified = true;
		} else if (!allocs[i].pointerified) {
			// If this register got pointerified on the way, mark it as not.
			// This is so that after save/reload (like in an interpreter fallback),
			// it won't be regarded as such, as it may no longer be.
			nr[allocs[i].nr].pointerified = false;
		}
	}
	// Sanity check
	for (int i = 0; i < totalNativeRegs_; i++) {
		if (nr[i].mipsReg != IRREG_INVALID && !mr[nr[i].mipsReg].isStatic) {
			ERROR_LOG_REPORT(JIT, "Flush fail: nr[%i].mipsReg=%i", i, nr[i].mipsReg);
		}
	}
}

IRNativeReg IRNativeRegCacheBase::MapNativeReg(MIPSLoc type, IRReg first, int lanes, MIPSMap flags) {
	_assert_msg_(first != IRREG_INVALID, "Cannot map invalid register");
	_assert_msg_(lanes >= 1 && lanes <= 4, "Cannot map %d lanes", lanes);
	if (first == IRREG_INVALID || lanes < 0)
		return -1;

	// Let's see if it's already mapped or we need a new reg.
	IRNativeReg nreg = mr[first].nReg;
	if (mr[first].isStatic) {
		_assert_msg_(nreg != -1, "MapIRReg on static without an nReg?");
	} else {
		switch (mr[first].loc) {
		case MIPSLoc::REG_IMM:
		case MIPSLoc::REG_AS_PTR:
		case MIPSLoc::REG:
			if (type != MIPSLoc::REG)
				nreg = AllocateReg(type);
			break;

		case MIPSLoc::FREG:
			if (type != MIPSLoc::FREG)
				nreg = AllocateReg(type);
			break;

		case MIPSLoc::VREG:
			if (type != MIPSLoc::VREG)
				nreg = AllocateReg(type);
			break;

		case MIPSLoc::IMM:
		case MIPSLoc::MEM:
			nreg = AllocateReg(type);
			break;
		}
	}

	if (nreg != -1) {
		// This will handle already mapped and new mappings.
		MapNativeReg(type, nreg, first, lanes, flags);
	}

	return nreg;
}

void IRNativeRegCacheBase::MapNativeReg(MIPSLoc type, IRNativeReg nreg, IRReg first, int lanes, MIPSMap flags) {
	// First, try to clean up any lane mismatches.
	// It must either be in the same nreg and lane count, or not in an nreg.
	for (int i = 0; i < lanes; ++i) {
		auto &mreg = mr[first + i];
		if (mreg.nReg != -1) {
			// How many lanes is it currently in?
			int oldlanes = 0;
			for (IRReg m = nr[mreg.nReg].mipsReg; mr[m].nReg == mreg.nReg && m < IRREG_INVALID; ++m)
				oldlanes++;

			// We may need to flush if it goes outside or we're initing.
			bool mismatch = oldlanes != lanes || mreg.lane != (lanes == 1 ? -1 : i);
			if (mismatch) {
				_assert_msg_(!mreg.isStatic, "Cannot MapNativeReg a static reg mismatch");
				if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
					// If we need init, we have to flush mismatches.
					// TODO: Do a shuffle if interior only?
					// TODO: We may also be motivated to have multiple read-only "views" or an IRReg.
					// For example Vec4Scale v0..v3, v0..v3, v3
					FlushNativeReg(mreg.nReg);
				} else if (oldlanes != 1) {
					// Even if we don't care about the current contents, we can't discard outside.
					bool extendsBefore = mreg.lane > i;
					bool extendsAfter = i + oldlanes - mreg.lane > lanes;
					if (extendsBefore || extendsAfter)
						FlushNativeReg(mreg.nReg);
				}
			}

			// If it's still in a different reg, either discard or possibly transfer.
			if (mreg.nReg != -1 && mreg.nReg != nreg) {
				_assert_msg_(!mreg.isStatic, "Cannot MapNativeReg a static reg to a new reg");
				if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
					// We better not be trying to map to a different nreg if it's in one now.
					// This might happen on some sort of transfer...
					// TODO: Make a direct transfer, i.e. FREG -> VREG?
					FlushNativeReg(mreg.nReg);
				} else {
					DiscardNativeReg(mreg.nReg);
				}
			}
		}

		// If somehow this is an imm and mapping to a multilane native reg (HI/LO?), we store it.
		// TODO: Could check the others are imm and be smarter, but seems an unlikely case.
		if (mreg.loc == MIPSLoc::IMM && lanes > 1) {
			if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT)
				StoreRegValue(first + i, mreg.imm);
			mreg.loc = MIPSLoc::MEM;
			if (!mreg.isStatic)
				mreg.nReg = -1;
			mreg.imm = 0;
		}
	}

	// Double check: everything should be in the same loc for multilane now.
	for (int i = 1; i < lanes; ++i) {
		_assert_(mr[first + i].loc == mr[first].loc);
	}

	bool markDirty = (flags & MIPSMap::DIRTY) == MIPSMap::DIRTY;
	if (mr[first].nReg != nreg) {
		nr[nreg].isDirty = markDirty;
		nr[nreg].pointerified = false;
		nr[nreg].normalized32 = (flags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
	}

	// Alright, now to actually map.
	if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
		if (first == MIPS_REG_ZERO) {
			_assert_msg_(lanes == 1, "Cannot use MIPS_REG_ZERO in multilane");
			SetNativeRegValue(nreg, 0);
			mr[first].loc = MIPSLoc::REG_IMM;
			mr[first].imm = 0;
		} else {
			// Note: we checked above, everything is in the same loc if multilane.
			switch (mr[first].loc) {
			case MIPSLoc::IMM:
				_assert_msg_(lanes == 1, "Not handling multilane imm here");
				SetNativeRegValue(nreg, mr[first].imm);
				// IMM is always dirty unless static.
				if (!mr[first].isStatic)
					nr[nreg].isDirty = true;

				// If we are mapping dirty, it means we're gonna overwrite.
				// So the imm value is no longer valid.
				if ((flags & MIPSMap::DIRTY) == MIPSMap::DIRTY)
					mr[first].loc = MIPSLoc::REG;
				else
					mr[first].loc = MIPSLoc::REG_IMM;
				break;

			case MIPSLoc::REG_IMM:
				// If it's not dirty, we can keep it.
				_assert_msg_(type == MIPSLoc::REG, "Should have flushed this reg already");
				if ((flags & MIPSMap::DIRTY) == MIPSMap::DIRTY || lanes != 1)
					mr[first].loc = MIPSLoc::REG;
				for (int i = 1; i < lanes; ++i)
					mr[first + i].loc = type;
				break;

			case MIPSLoc::REG_AS_PTR:
				_assert_msg_(lanes == 1, "Should have flushed before getting here");
				_assert_msg_(type == MIPSLoc::REG, "Should have flushed this reg already");
				AdjustNativeRegAsPtr(nreg, false);
				for (int i = 0; i < lanes; ++i)
					mr[first + i].loc = type;
				break;

			case MIPSLoc::REG:
			case MIPSLoc::FREG:
			case MIPSLoc::VREG:
				// Might be flipping from FREG -> VREG or something.
				_assert_msg_(type == mr[first].loc, "Should have flushed this reg already");
				for (int i = 0; i < lanes; ++i)
					mr[first + i].loc = type;
				break;

			case MIPSLoc::MEM:
				for (int i = 0; i < lanes; ++i)
					mr[first + i].loc = type;
				LoadNativeReg(nreg, first, lanes);
				break;
			}
		}
	} else {
		for (int i = 0; i < lanes; ++i)
			mr[first + i].loc = type;
	}

	for (int i = 0; i < lanes; ++i) {
		mr[first + i].nReg = nreg;
		mr[first + i].lane = lanes == 1 ? -1 : i;
	}

	nr[nreg].mipsReg = first;

	if (markDirty) {
		nr[nreg].isDirty = true;
		nr[nreg].pointerified = false;
		nr[nreg].normalized32 = (flags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32;
		_assert_(first != MIPS_REG_ZERO);
	} else if ((flags & MIPSMap::MARK_NORM32) == MIPSMap::MARK_NORM32) {
		nr[nreg].normalized32 = true;
	}
}

IRNativeReg IRNativeRegCacheBase::MapNativeRegAsPointer(IRReg gpr) {
	_dbg_assert_(IsValidGPRNoZero(gpr));

	// Already mapped.
	if (mr[gpr].loc == MIPSLoc::REG_AS_PTR) {
		return mr[gpr].nReg;
	}

	IRNativeReg nreg = mr[gpr].nReg;
	if (mr[gpr].loc != MIPSLoc::REG && mr[gpr].loc != MIPSLoc::REG_IMM) {
		nreg = MapNativeReg(MIPSLoc::REG, gpr, 1, MIPSMap::INIT);
	}

	if (mr[gpr].loc == MIPSLoc::REG || mr[gpr].loc == MIPSLoc::REG_IMM) {
		// If there was an imm attached, discard it.
		mr[gpr].loc = MIPSLoc::REG;
		mr[gpr].imm = 0;

		if (!jo_->enablePointerify) {
			AdjustNativeRegAsPtr(nreg, true);
			mr[gpr].loc = MIPSLoc::REG_AS_PTR;
		} else if (!nr[nreg].pointerified) {
			AdjustNativeRegAsPtr(nreg, true);
			nr[nreg].pointerified = true;
		}
	} else {
		ERROR_LOG(JIT, "MapNativeRegAsPointer: MapNativeReg failed to allocate a register?");
	}
	return nreg;
}

void IRNativeRegCacheBase::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	// This isn't necessary to implement if REG_AS_PTR is unsupported entirely.
	_assert_msg_(false, "AdjustNativeRegAsPtr unimplemented");
}

int IRNativeRegCacheBase::GetMipsRegOffset(IRReg r) {
	_dbg_assert_(IsValidGPR(r) || (r >= 32 && IsValidFPR(r - 32)));
	return r * 4;
}

bool IRNativeRegCacheBase::IsValidGPR(IRReg r) const {
	// See MIPSState for these offsets.

	// Don't allow FPU regs, VFPU regs, or VFPU temps here.
	if (r >= 32 && IsValidFPR(r - 32))
		return false;
	// Don't allow nextPC, etc. since it's probably a mistake.
	if (r > IRREG_FPCOND && r != IRREG_LLBIT)
		return false;
	// Don't allow PC either.
	if (r == 241)
		return false;

	return true;
}

bool IRNativeRegCacheBase::IsValidGPRNoZero(IRReg r) const {
	return IsValidGPR(r) && r != MIPS_REG_ZERO;
}

bool IRNativeRegCacheBase::IsValidFPR(IRReg r) const {
	// FPR parameters are off by 32 within the MIPSState object.
	if (r >= TOTAL_MAPPABLE_IRREGS - 32)
		return false;

	// See MIPSState for these offsets.
	int index = r + 32;

	// Allow FPU or VFPU regs here.
	if (index >= 32 && index < 32 + 32 + 128)
		return true;
	// Also allow VFPU temps.
	if (index >= 224 && index < 224 + 16)
		return true;

	// Nothing else is allowed for the FPU side.
	return false;
}
