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

IRNativeRegCache::IRNativeRegCache(MIPSComp::JitOptions *jo)
	: jo_(jo) {}

void IRNativeRegCache::Start(MIPSComp::IRBlock *irBlock) {
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
		mr[statics[i].mr].loc = MIPSLoc::REG;
		mr[statics[i].mr].nReg = statics[i].nr;
		mr[statics[i].mr].isStatic = true;
		// Lock it until the very end.
		mr[statics[i].mr].spillLockIRIndex = irBlock->GetNumInstructions();
	}

	irBlock_ = irBlock;
	irIndex_ = 0;
}

void IRNativeRegCache::SetupInitialRegs() {
	_assert_msg_(totalNativeRegs_ > 0, "totalNativeRegs_ was never set by backend");

	// Everything else is initialized in the struct.
	mrInitial_[MIPS_REG_ZERO].loc = MIPSLoc::IMM;
	mrInitial_[MIPS_REG_ZERO].imm = 0;
}
