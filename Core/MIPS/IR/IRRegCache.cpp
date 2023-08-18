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
	pendingFlush_ = false;

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
				_assert_msg_(!mr[m].isStatic, "Reg in lane %d mismatched static status", m - nr[nreg].mipsReg);
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
		_dbg_assert_(!mr[m].isStatic);
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
	}

	nr[nreg].mipsReg = IRREG_INVALID;
	nr[nreg].isDirty = false;
	nr[nreg].pointerified = false;
	nr[nreg].normalized32 = false;
}

void IRNativeRegCacheBase::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	// This isn't necessary to implement if REG_AS_PTR is unsupported entirely.
	_assert_msg_(false, "AdjustNativeRegAsPtr unimplemented");
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
