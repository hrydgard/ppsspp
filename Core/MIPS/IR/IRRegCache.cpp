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
	if (isImm_[rd]) {
		if (rd == 0) {
			return;
		}
		_assert_((rd > 0 && rd < 32) || (rd >= IRTEMP_0 && rd < IRREG_VFPU_CTRL_BASE));
		ir_->WriteSetConstant(rd, immVal_[rd]);
		isImm_[rd] = false;
	}
}

void IRImmRegCache::Discard(IRReg rd) {
	if (rd == 0) {
		return;
	}
	isImm_[rd] = false;
}

IRImmRegCache::IRImmRegCache(IRWriter *ir) : ir_(ir) {
	memset(&isImm_, 0, sizeof(isImm_));
	memset(&immVal_, 0, sizeof(immVal_));
	isImm_[0] = true;
	ir_ = ir;
}

void IRImmRegCache::FlushAll() {
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; ) {
		if (isImm_[i]) {
			Flush(i);
		}

		// Most of the time, lots are not.  This speeds it up a lot.
		bool *next = (bool *)memchr(&isImm_[i], 1, TOTAL_MAPPABLE_IRREGS - i);
		if (!next)
			break;
		i = (int)(next - &isImm_[0]);
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

void IRNativeRegCacheBase::Start(MIPSComp::IRBlockCache *irBlockCache, int blockNum) {
	if (!initialReady_) {
		SetupInitialRegs();
		initialReady_ = true;
	}

	memcpy(nr, nrInitial_, sizeof(nr[0]) * config_.totalNativeRegs);
	memcpy(mr, mrInitial_, sizeof(mr));

	irBlock_ = irBlockCache->GetBlock(blockNum);

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
		mr[statics[i].mr].spillLockIRIndex = irBlock_->GetNumIRInstructions();
	}

	irBlockNum_ = blockNum;
	irBlockCache_ = irBlockCache;
	irIndex_ = 0;
}

void IRNativeRegCacheBase::SetupInitialRegs() {
	_assert_msg_(config_.totalNativeRegs > 0, "totalNativeRegs was never set by backend");

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

int IRNativeRegCacheBase::GetFPRLaneCount(IRReg fpr) {
	if (!IsFPRMapped(fpr))
		return 0;
	if (mr[fpr + 32].lane == -1)
		return 1;

	IRReg base = fpr + 32 - mr[fpr + 32].lane;
	int c = 1;
	for (int i = 1; i < 4; ++i) {
		if (mr[base + i].nReg != mr[base].nReg || mr[base + i].loc != mr[base].loc)
			return c;
		if (mr[base + i].lane != i)
			return c;

		c++;
	}

	return c;
}

int IRNativeRegCacheBase::GetFPRLane(IRReg fpr) {
	_dbg_assert_(IsValidFPR(fpr));
	if (mr[fpr + 32].loc == MIPSLoc::FREG || mr[fpr + 32].loc == MIPSLoc::VREG) {
		int l = mr[fpr + 32].lane;
		return l == -1 ? 0 : l;
	}
	return -1;
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
		ERROR_LOG_REPORT(Log::JIT, "Trying to set immediate %08x to r0", immVal);
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

void IRNativeRegCacheBase::SetSpillLockIRIndex(IRReg r1, int index) {
	if (!mr[r1].isStatic)
		mr[r1].spillLockIRIndex = index;
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

IRNativeReg IRNativeRegCacheBase::AllocateReg(MIPSLoc type, MIPSMap flags) {
	_dbg_assert_(type == MIPSLoc::REG || type == MIPSLoc::FREG || type == MIPSLoc::VREG);

	IRNativeReg nreg = FindFreeReg(type, flags);
	if (nreg != -1)
		return nreg;

	// Still nothing. Let's spill a reg and goto 10.
	bool clobbered;
	IRNativeReg bestToSpill = FindBestToSpill(type, flags, true, &clobbered);
	if (bestToSpill == -1) {
		bestToSpill = FindBestToSpill(type, flags, false, &clobbered);
	}

	if (bestToSpill != -1) {
		if (clobbered) {
			DiscardNativeReg(bestToSpill);
		} else {
			FlushNativeReg(bestToSpill);
		}
		// Now one must be free.
		return FindFreeReg(type, flags);
	}

	// Uh oh, we have all of them spilllocked....
	ERROR_LOG_REPORT(Log::JIT, "Out of spillable registers in block PC %08x, index %d", irBlock_->GetOriginalStart(), irIndex_);
	_assert_(bestToSpill != -1);
	return -1;
}

IRNativeReg IRNativeRegCacheBase::FindFreeReg(MIPSLoc type, MIPSMap flags) const {
	int allocCount = 0, base = 0;
	const int *allocOrder = GetAllocationOrder(type, flags, allocCount, base);

	for (int i = 0; i < allocCount; i++) {
		IRNativeReg nreg = IRNativeReg(allocOrder[i] - base);

		if (nr[nreg].mipsReg == IRREG_INVALID && nr[nreg].tempLockIRIndex < irIndex_) {
			return nreg;
		}
	}

	return -1;
}

bool IRNativeRegCacheBase::IsGPRClobbered(IRReg gpr) const {
	_dbg_assert_(IsValidGPR(gpr));
	return IsRegClobbered(MIPSLoc::REG, gpr);
}

bool IRNativeRegCacheBase::IsFPRClobbered(IRReg fpr) const {
	_dbg_assert_(IsValidFPR(fpr));
	return IsRegClobbered(MIPSLoc::FREG, fpr + 32);
}

IRUsage IRNativeRegCacheBase::GetNextRegUsage(const IRSituation &info, MIPSLoc type, IRReg r) const {
	if (type == MIPSLoc::REG)
		return IRNextGPRUsage(r, info);
	else if (type == MIPSLoc::FREG || type == MIPSLoc::VREG)
		return IRNextFPRUsage(r - 32, info);
	_assert_msg_(false, "Unknown spill allocation type");
	return IRUsage::UNKNOWN;
}

bool IRNativeRegCacheBase::IsRegClobbered(MIPSLoc type, IRReg r) const {
	static const int UNUSED_LOOKAHEAD_OPS = 30;

	IRSituation info;
	info.lookaheadCount = UNUSED_LOOKAHEAD_OPS;
	// We look starting one ahead, unlike spilling.  We want to know if it clobbers later.
	info.currentIndex = irIndex_ + 1;
	info.instructions = irBlockCache_->GetBlockInstructionPtr(irBlockNum_);
	info.numInstructions = irBlock_->GetNumIRInstructions();

	// Make sure we're on the first one if this is multi-lane.
	IRReg first = r;
	if (mr[r].lane != -1)
		first -= mr[r].lane;

	IRUsage usage = GetNextRegUsage(info, type, first);
	if (usage == IRUsage::CLOBBERED) {
		// If multiple mips regs use this native reg (i.e. vector, HI/LO), check each.
		bool canClobber = true;
		for (IRReg m = first + 1; mr[m].nReg == mr[first].nReg && m < IRREG_INVALID && canClobber; ++m)
			canClobber = GetNextRegUsage(info, type, m) == IRUsage::CLOBBERED;

		return canClobber;
	}
	return false;
}

bool IRNativeRegCacheBase::IsRegRead(MIPSLoc type, IRReg first) const {
	static const int UNUSED_LOOKAHEAD_OPS = 30;

	IRSituation info;
	info.lookaheadCount = UNUSED_LOOKAHEAD_OPS;
	// We look starting one ahead, unlike spilling.
	info.currentIndex = irIndex_ + 1;
	info.instructions = irBlockCache_->GetBlockInstructionPtr(irBlockNum_);
	info.numInstructions = irBlock_->GetNumIRInstructions();

	// Note: this intentionally doesn't look at the full reg, only the lane.
	IRUsage usage = GetNextRegUsage(info, type, first);
	return usage == IRUsage::READ;
}

IRNativeReg IRNativeRegCacheBase::FindBestToSpill(MIPSLoc type, MIPSMap flags, bool unusedOnly, bool *clobbered) const {
	int allocCount = 0, base = 0;
	const int *allocOrder = GetAllocationOrder(type, flags, allocCount, base);

	static const int UNUSED_LOOKAHEAD_OPS = 30;

	IRSituation info;
	info.lookaheadCount = UNUSED_LOOKAHEAD_OPS;
	info.currentIndex = irIndex_;
	info.instructions = irBlockCache_->GetBlockInstructionPtr(irBlockNum_);
	info.numInstructions = irBlock_->GetNumIRInstructions();

	*clobbered = false;
	for (int i = 0; i < allocCount; i++) {
		IRNativeReg nreg = IRNativeReg(allocOrder[i] - base);
		if (nr[nreg].mipsReg != IRREG_INVALID && mr[nr[nreg].mipsReg].spillLockIRIndex >= irIndex_)
			continue;
		if (nr[nreg].tempLockIRIndex >= irIndex_)
			continue;

		// As it's in alloc-order, we know it's not static so we don't need to check for that.
		IRReg mipsReg = nr[nreg].mipsReg;
		IRUsage usage = GetNextRegUsage(info, type, mipsReg);

		// Awesome, a clobbered reg.  Let's use it?
		if (usage == IRUsage::CLOBBERED) {
			// If multiple mips regs use this native reg (i.e. vector, HI/LO), check each.
			// Note: mipsReg points to the lowest numbered IRReg.
			bool canClobber = true;
			for (IRReg m = mipsReg + 1; mr[m].nReg == nreg && m < IRREG_INVALID && canClobber; ++m)
				canClobber = GetNextRegUsage(info, type, m) == IRUsage::CLOBBERED;

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
			*clobbered = mipsReg == MIPS_REG_ZERO;
			return nreg;
		}
	}

	return -1;
}

bool IRNativeRegCacheBase::IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) {
	int allocCount = 0, base = 0;
	const int *allocOrder = GetAllocationOrder(type, flags, allocCount, base);

	for (int i = 0; i < allocCount; i++) {
		IRNativeReg allocReg = IRNativeReg(allocOrder[i] - base);
		if (allocReg == nreg)
			return true;
	}

	return false;
}

bool IRNativeRegCacheBase::TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) {
	// To be overridden if the backend supports transfers.
	return false;
}

void IRNativeRegCacheBase::DiscardNativeReg(IRNativeReg nreg) {
	_assert_msg_(nreg >= 0 && nreg < config_.totalNativeRegs, "DiscardNativeReg on invalid register %d", nreg);
	if (nr[nreg].mipsReg != IRREG_INVALID) {
		int8_t lanes = 0;
		for (IRReg m = nr[nreg].mipsReg; mr[m].nReg == nreg && m < IRREG_INVALID; ++m)
			lanes++;

		if (mr[nr[nreg].mipsReg].isStatic) {
			_assert_(nr[nreg].mipsReg != MIPS_REG_ZERO);

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
	_assert_msg_(nreg >= 0 && nreg < config_.totalNativeRegs, "FlushNativeReg on invalid register %d", nreg);
	if (nr[nreg].mipsReg == IRREG_INVALID || nr[nreg].mipsReg == MIPS_REG_ZERO) {
		// Nothing to do, reg not mapped or mapped to fixed zero.
		_dbg_assert_(!nr[nreg].isDirty);
		return;
	}
	_dbg_assert_(!mr[nr[nreg].mipsReg].isStatic);
	if (mr[nr[nreg].mipsReg].isStatic) {
		ERROR_LOG(Log::JIT, "Cannot FlushNativeReg a statically mapped register");
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

void IRNativeRegCacheBase::FlushAll(bool gprs, bool fprs) {
	// Note: make sure not to change the registers when flushing.
	// Branching code may expect the native reg to retain its value.

	if (!mr[MIPS_REG_ZERO].isStatic && mr[MIPS_REG_ZERO].nReg != -1)
		DiscardNativeReg(mr[MIPS_REG_ZERO].nReg);

	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; i++) {
		IRReg mipsReg = (IRReg)i;
		if (!fprs && i >= 32 && IsValidFPR(mipsReg - 32))
			continue;
		if (!gprs && IsValidGPR(mipsReg))
			continue;

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
					ERROR_LOG(Log::JIT, "RVREG_IMM but pointerified. Wrong.");
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
		if (!fprs && allocs[i].loc != MIPSLoc::FREG && allocs[i].loc != MIPSLoc::VREG)
			continue;
		if (!gprs && allocs[i].loc != MIPSLoc::REG)
			continue;
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
	for (int i = 0; i < config_.totalNativeRegs; i++) {
		if (nr[i].mipsReg != IRREG_INVALID && !mr[nr[i].mipsReg].isStatic) {
			ERROR_LOG_REPORT(Log::JIT, "Flush fail: nr[%i].mipsReg=%i", i, nr[i].mipsReg);
		}
	}
}

void IRNativeRegCacheBase::Map(const IRInst &inst) {
	Mapping mapping[3];
	MappingFromInst(inst, mapping);

	ApplyMapping(mapping, 3);
	CleanupMapping(mapping, 3);
}

void IRNativeRegCacheBase::MapWithExtra(const IRInst &inst, std::vector<Mapping> extra) {
	extra.resize(extra.size() + 3);
	MappingFromInst(inst, &extra[extra.size() - 3]);

	ApplyMapping(extra.data(), (int)extra.size());
	CleanupMapping(extra.data(), (int)extra.size());
}

IRNativeReg IRNativeRegCacheBase::MapWithTemp(const IRInst &inst, MIPSLoc type) {
	Mapping mapping[3];
	MappingFromInst(inst, mapping);

	ApplyMapping(mapping, 3);
	// Grab a temp while things are spill locked.
	IRNativeReg temp = AllocateReg(type, MIPSMap::INIT);
	CleanupMapping(mapping, 3);
	return temp;
}

void IRNativeRegCacheBase::ApplyMapping(const Mapping *mapping, int count) {
	for (int i = 0; i < count; ++i) {
		SetSpillLockIRIndex(mapping[i].reg, irIndex_);
		if (!config_.mapFPUSIMD && mapping[i].type != 'G') {
			for (int j = 1; j < mapping[i].lanes; ++j)
				SetSpillLockIRIndex(mapping[i].reg + j, irIndex_);
		}
	}

	auto isNoinit = [](MIPSMap f) {
		return (f & MIPSMap::NOINIT) == MIPSMap::NOINIT;
	};

	auto mapRegs = [&](int i) {
		MIPSLoc type = MIPSLoc::MEM;
		switch (mapping[i].type) {
		case 'G': type = MIPSLoc::REG; break;
		case 'F': type = MIPSLoc::FREG; break;
		case 'V': type = MIPSLoc::VREG; break;

		case '_':
			// Ignored intentionally.
			return;

		default:
			_assert_msg_(false, "Unexpected type: %c", mapping[i].type);
			return;
		}

		bool mapSIMD = config_.mapFPUSIMD || mapping[i].type == 'G';
		MIPSMap flags = mapping[i].flags;
		for (int j = 0; j < count; ++j) {
			if (mapping[j].type == mapping[i].type && mapping[j].reg == mapping[i].reg && i != j) {
				_assert_msg_(!mapSIMD || mapping[j].lanes == mapping[i].lanes, "Lane aliasing not supported yet");

				if (!isNoinit(mapping[j].flags) && isNoinit(flags)) {
					flags = (flags & MIPSMap::BACKEND_MASK) | MIPSMap::DIRTY;
				}
			}
		}

		if (mapSIMD) {
			MapNativeReg(type, mapping[i].reg, mapping[i].lanes, flags);
			return;
		}

		for (int j = 0; j < mapping[i].lanes; ++j)
			MapNativeReg(type, mapping[i].reg + j, 1, flags);
	};
	auto mapFilteredRegs = [&](auto pred) {
		for (int i = 0; i < count; ++i) {
			if (pred(mapping[i].flags))
				mapRegs(i);
		}
	};

	// Do two passes: with backend special flags, and without.
	mapFilteredRegs([](MIPSMap flags) {
		return (flags & MIPSMap::BACKEND_MASK) != MIPSMap::INIT;
	});
	mapFilteredRegs([](MIPSMap flags) {
		return (flags & MIPSMap::BACKEND_MASK) == MIPSMap::INIT;
	});
}

void IRNativeRegCacheBase::CleanupMapping(const Mapping *mapping, int count) {
	for (int i = 0; i < count; ++i) {
		SetSpillLockIRIndex(mapping[i].reg, -1);
		if (!config_.mapFPUSIMD && mapping[i].type != 'G') {
			for (int j = 1; j < mapping[i].lanes; ++j)
				SetSpillLockIRIndex(mapping[i].reg + j, -1);
		}
	}

	// Sanity check.  If these don't pass, we may have Vec overlap issues or etc.
	for (int i = 0; i < count; ++i) {
		if (mapping[i].reg != IRREG_INVALID) {
			auto &mreg = mr[mapping[i].reg];
			_dbg_assert_(mreg.nReg != -1);
			if (mapping[i].type == 'G') {
				_dbg_assert_(mreg.loc == MIPSLoc::REG || mreg.loc == MIPSLoc::REG_AS_PTR || mreg.loc == MIPSLoc::REG_IMM);
			} else if (mapping[i].type == 'F') {
				_dbg_assert_(mreg.loc == MIPSLoc::FREG);
			} else if (mapping[i].type == 'V') {
				_dbg_assert_(mreg.loc == MIPSLoc::VREG);
			}
			if (mapping[i].lanes != 1 && (config_.mapFPUSIMD || mapping[i].type == 'G')) {
				_dbg_assert_(mreg.lane == 0);
				_dbg_assert_(mr[mapping[i].reg + mapping[i].lanes - 1].lane == mapping[i].lanes - 1);
				_dbg_assert_(mreg.nReg == mr[mapping[i].reg + mapping[i].lanes - 1].nReg);
			} else {
				_dbg_assert_(mreg.lane == -1);
			}
		}
	}
}

void IRNativeRegCacheBase::MappingFromInst(const IRInst &inst, Mapping mapping[3]) {
	mapping[0].reg = inst.dest;
	mapping[1].reg = inst.src1;
	mapping[2].reg = inst.src2;

	const IRMeta *m = GetIRMeta(inst.op);
	for (int i = 0; i < 3; ++i) {
		switch (m->types[i]) {
		case 'G':
			mapping[i].type = 'G';
			_assert_msg_(IsValidGPR(mapping[i].reg), "G was not valid GPR?");
			break;

		case 'F':
			mapping[i].reg += 32;
			mapping[i].type = 'F';
			_assert_msg_(IsValidFPR(mapping[i].reg - 32), "F was not valid FPR?");
			break;

		case 'V':
		case '2':
			mapping[i].reg += 32;
			mapping[i].type = config_.mapUseVRegs ? 'V' : 'F';
			mapping[i].lanes = m->types[i] == 'V' ? 4 : (m->types[i] == '2' ? 2 : 1);
			_assert_msg_(IsValidFPR(mapping[i].reg - 32), "%c was not valid FPR?", m->types[i]);
			break;

		case 'T':
			mapping[i].type = 'G';
			_assert_msg_(mapping[i].reg < VFPU_CTRL_MAX, "T was not valid VFPU CTRL?");
			mapping[i].reg += IRREG_VFPU_CTRL_BASE;
			break;

		case '\0':
		case '_':
		case 'C':
		case 'r':
		case 'I':
		case 'v':
		case 's':
		case 'm':
			mapping[i].type = '_';
			mapping[i].reg = IRREG_INVALID;
			mapping[i].lanes = 0;
			break;

		default:
			_assert_msg_(mapping[i].reg == IRREG_INVALID, "Unexpected register type %c", m->types[i]);
			break;
		}
	}

	if (mapping[0].type != '_') {
		if ((m->flags & IRFLAG_SRC3DST) != 0)
			mapping[0].flags = MIPSMap::DIRTY;
		else if ((m->flags & IRFLAG_SRC3) != 0)
			mapping[0].flags = MIPSMap::INIT;
		else
			mapping[0].flags = MIPSMap::NOINIT;
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
			if (type != MIPSLoc::REG) {
				nreg = AllocateReg(type, flags);
			} else if (!IsNativeRegCompatible(nreg, type, flags, lanes)) {
				// If it's not compatible, we'll need to reallocate.
				if (TransferNativeReg(nreg, -1, type, first, lanes, flags)) {
					nreg = mr[first].nReg;
				} else {
					FlushNativeReg(nreg);
					nreg = AllocateReg(type, flags);
				}
			}
			break;

		case MIPSLoc::FREG:
		case MIPSLoc::VREG:
			if (type != mr[first].loc) {
				nreg = AllocateReg(type, flags);
			} else if (!IsNativeRegCompatible(nreg, type, flags, lanes)) {
				if (TransferNativeReg(nreg, -1, type, first, lanes, flags)) {
					nreg = mr[first].nReg;
				} else {
					FlushNativeReg(nreg);
					nreg = AllocateReg(type, flags);
				}
			}
			break;

		case MIPSLoc::IMM:
		case MIPSLoc::MEM:
			nreg = AllocateReg(type, flags);
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
			int oldlane = mreg.lane == -1 ? 0 : mreg.lane;
			bool mismatch = oldlanes != lanes || oldlane != i;
			if (mismatch) {
				_assert_msg_(!mreg.isStatic, "Cannot MapNativeReg a static reg mismatch");
				if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
					// If we need init, we have to flush mismatches.
					if (!TransferNativeReg(mreg.nReg, nreg, type, first, lanes, flags)) {
						// TODO: We may also be motivated to have multiple read-only "views" or an IRReg.
						// For example Vec4Scale v0..v3, v0..v3, v3
						FlushNativeReg(mreg.nReg);
					}
					// The mismatch has been "resolved" now.
					mismatch = false;
				} else if (oldlanes != 1) {
					// Even if we don't care about the current contents, we can't discard outside.
					bool extendsBefore = oldlane > i;
					bool extendsAfter = i + oldlanes - oldlane > lanes;
					if (extendsBefore || extendsAfter) {
						// Usually, this is 4->1.  Check for clobber.
						bool clobbered = false;
						if (lanes == 1) {
							IRSituation info;
							info.lookaheadCount = 16;
							info.currentIndex = irIndex_;
							info.instructions = irBlockCache_->GetBlockInstructionPtr(irBlockNum_);
							info.numInstructions = irBlock_->GetNumIRInstructions();

							IRReg basefpr = first - oldlane - 32;
							clobbered = true;
							for (int l = 0; l < oldlanes; ++l) {
								// Ignore the one we're modifying.
								if (l == oldlane)
									continue;

								if (IRNextFPRUsage(basefpr + l, info) != IRUsage::CLOBBERED) {
									clobbered = false;
									break;
								}
							}
						}

						if (clobbered)
							DiscardNativeReg(mreg.nReg);
						else
							FlushNativeReg(mreg.nReg);

						// That took care of the mismatch, either by clobber or flush.
						mismatch = false;
					}
				}
			}

			// If it's still in a different reg, either discard or possibly transfer.
			if (mreg.nReg != -1 && (mreg.nReg != nreg || mismatch)) {
				_assert_msg_(!mreg.isStatic, "Cannot MapNativeReg a static reg to a new reg");
				if ((flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
					// We better not be trying to map to a different nreg if it's in one now.
					// This might happen on some sort of transfer...
					if (!TransferNativeReg(mreg.nReg, nreg, type, first, lanes, flags))
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
		nr[nreg].normalized32 = false;
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
#ifndef MASKED_PSP_MEMORY
				AdjustNativeRegAsPtr(nreg, false);
#endif
				for (int i = 0; i < lanes; ++i)
					mr[first + i].loc = type;
#ifdef MASKED_PSP_MEMORY
				LoadNativeReg(nreg, first, lanes);
#endif
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
		nr[nreg].normalized32 = false;
		_assert_(first != MIPS_REG_ZERO);
	}
}

IRNativeReg IRNativeRegCacheBase::MapNativeRegAsPointer(IRReg gpr) {
	_dbg_assert_(IsValidGPRNoZero(gpr));

	// Already mapped.
	if (mr[gpr].loc == MIPSLoc::REG_AS_PTR) {
		return mr[gpr].nReg;
	}

	// Cannot use if somehow multilane.
	if (mr[gpr].nReg != -1 && mr[gpr].lane != -1) {
		FlushNativeReg(mr[gpr].nReg);
	}

	IRNativeReg nreg = mr[gpr].nReg;
	if (mr[gpr].loc != MIPSLoc::REG && mr[gpr].loc != MIPSLoc::REG_IMM) {
		nreg = MapNativeReg(MIPSLoc::REG, gpr, 1, MIPSMap::INIT);
	}

	if (mr[gpr].loc == MIPSLoc::REG || mr[gpr].loc == MIPSLoc::REG_IMM) {
		// If there was an imm attached, discard it.
		mr[gpr].loc = MIPSLoc::REG;
		mr[gpr].imm = 0;

#ifdef MASKED_PSP_MEMORY
		if (nr[mr[gpr].nReg].isDirty) {
			StoreNativeReg(mr[gpr].nReg, gpr, 1);
			nr[mr[gpr].nReg].isDirty = false;
		}
#endif

		if (!jo_->enablePointerify) {
			AdjustNativeRegAsPtr(nreg, true);
			mr[gpr].loc = MIPSLoc::REG_AS_PTR;
		} else if (!nr[nreg].pointerified) {
			AdjustNativeRegAsPtr(nreg, true);
			nr[nreg].pointerified = true;
		}
	} else {
		ERROR_LOG(Log::JIT, "MapNativeRegAsPointer: MapNativeReg failed to allocate a register?");
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
