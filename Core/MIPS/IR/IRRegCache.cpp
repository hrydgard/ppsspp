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
