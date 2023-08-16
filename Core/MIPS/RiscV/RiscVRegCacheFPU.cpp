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
#include "Core/MIPS/RiscV/RiscVRegCacheFPU.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Reporting.h"

using namespace RiscVGen;
using namespace RiscVJitConstants;

RiscVRegCacheFPU::RiscVRegCacheFPU(MIPSComp::JitOptions *jo)
	: IRNativeRegCache(jo) {
	totalNativeRegs_ = NUM_RVFPUREG;
}

void RiscVRegCacheFPU::Init(RiscVEmitter *emitter) {
	emit_ = emitter;
}

const RiscVReg *RiscVRegCacheFPU::GetMIPSAllocationOrder(int &count) {
	// F8 through F15 are used for compression, so they are great.
	// TODO: Maybe we could remove some saved regs since we rarely need that many?  Or maybe worth it?
	static const RiscVReg allocationOrder[] = {
		F8, F9, F10, F11, F12, F13, F14, F15,
		F0, F1, F2, F3, F4, F5, F6, F7,
		F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31,
	};

	count = ARRAY_SIZE(allocationOrder);
	return allocationOrder;
}

bool RiscVRegCacheFPU::IsInRAM(IRReg reg) {
	_dbg_assert_(IsValidReg(reg));
	return mr[reg].loc == MIPSLoc::MEM;
}

bool RiscVRegCacheFPU::IsMapped(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	return mr[mipsReg].loc == MIPSLoc::FREG;
}

RiscVReg RiscVRegCacheFPU::MapReg(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::MEM || mr[mipsReg].loc == MIPSLoc::FREG);

	pendingFlush_ = true;

	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == MIPSLoc::FREG) {
		_assert_msg_(nr[mr[mipsReg].nReg].mipsReg == mipsReg, "GPU mapping out of sync, IR=%i", mipsReg);
		if ((mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY) {
			nr[mr[mipsReg].nReg].isDirty = true;
		}
		return (RiscVReg)(mr[mipsReg].nReg + F0);
	}

	// Okay, not mapped, so we need to allocate an RV register.
	RiscVReg reg = AllocateReg();
	if (reg != INVALID_REG) {
		// That means it's free. Grab it, and load the value into it (if requested).
		nr[reg - F0].isDirty = (mapFlags & MIPSMap::DIRTY) == MIPSMap::DIRTY;
		if ((mapFlags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
			if (mr[mipsReg].loc == MIPSLoc::MEM) {
				emit_->FL(32, reg, CTXREG, GetMipsRegOffset(mipsReg));
			}
		}
		nr[reg - F0].mipsReg = mipsReg;
		mr[mipsReg].loc = MIPSLoc::FREG;
		mr[mipsReg].nReg = reg - F0;
		return reg;
	}

	return reg;
}

RiscVReg RiscVRegCacheFPU::AllocateReg() {
	int allocCount = 0;
	const RiscVReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		RiscVReg reg = allocOrder[i];

		if (nr[reg - F0].mipsReg == IRREG_INVALID) {
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
			DiscardR(nr[bestToSpill - F0].mipsReg);
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

RiscVReg RiscVRegCacheFPU::FindBestToSpill(bool unusedOnly, bool *clobbered) {
	int allocCount = 0;
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
		if (nr[reg - F0].mipsReg != IRREG_INVALID && mr[nr[reg - F0].mipsReg].spillLockIRIndex >= irIndex_)
			continue;

		// As it's in alloc-order, we know it's not static so we don't need to check for that.
		IRUsage usage = IRNextFPRUsage(nr[reg - F0].mipsReg, info);

		// Awesome, a clobbered reg.  Let's use it.
		if (usage == IRUsage::CLOBBERED) {
			*clobbered = true;
			return reg;
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

void RiscVRegCacheFPU::MapInIn(IRReg rd, IRReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCacheFPU::MapDirtyIn(IRReg rd, IRReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rs);
	ReleaseSpillLock(rd, rs);
}

void RiscVRegCacheFPU::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd, rs, rt);
}

RiscVReg RiscVRegCacheFPU::MapDirtyInTemp(IRReg rd, IRReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rs);
	RiscVReg temp = AllocateReg();
	ReleaseSpillLock(rd, rs);
	return temp;
}

void RiscVRegCacheFPU::Map4DirtyIn(IRReg rdbase, IRReg rsbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLock(rdbase + i, rsbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLock(rdbase + i, rsbase + i);
}

void RiscVRegCacheFPU::Map4DirtyInIn(IRReg rdbase, IRReg rsbase, IRReg rtbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLock(rdbase + i, rsbase + i, rtbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase) || (rdbase < rtbase + 4 && rdbase + 4 > rtbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	for (int i = 0; i < 4; ++i)
		MapReg(rtbase + i);
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLock(rdbase + i, rsbase + i, rtbase + i);
}

RiscVReg RiscVRegCacheFPU::Map4DirtyInTemp(IRReg rdbase, IRReg rsbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLock(rdbase + i, rsbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	RiscVReg temp = AllocateReg();
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLock(rdbase + i, rsbase + i);
	return temp;
}

void RiscVRegCacheFPU::FlushRiscVReg(RiscVReg r) {
	_dbg_assert_(r >= F0 && r <= F31);
	int reg = r - F0;
	if (nr[reg].mipsReg == IRREG_INVALID) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (nr[reg].isDirty && mr[nr[reg].mipsReg].loc == MIPSLoc::FREG) {
		emit_->FS(32, r, CTXREG, GetMipsRegOffset(nr[reg].mipsReg));
	}
	mr[nr[reg].mipsReg].loc = MIPSLoc::MEM;
	mr[nr[reg].mipsReg].nReg = (int)INVALID_REG;
	nr[reg].mipsReg = IRREG_INVALID;
	nr[reg].isDirty = false;
}

void RiscVRegCacheFPU::FlushR(IRReg r) {
	_dbg_assert_(IsValidReg(r));
	RiscVReg reg = RiscVRegForFlush(r);
	if (reg != INVALID_REG)
		FlushRiscVReg(reg);
}

RiscVReg RiscVRegCacheFPU::RiscVRegForFlush(IRReg r) {
	_dbg_assert_(IsValidReg(r));
	switch (mr[r].loc) {
	case MIPSLoc::FREG:
		_assert_msg_(mr[r].nReg != INVALID_REG, "RiscVRegForFlush: IR %d had bad RiscVReg", r);
		if (mr[r].nReg == INVALID_REG) {
			return INVALID_REG;
		}
		return (RiscVReg)(F0 + mr[r].nReg);

	case MIPSLoc::MEM:
		return INVALID_REG;

	default:
		_assert_(false);
		return INVALID_REG;
	}
}

void RiscVRegCacheFPU::FlushBeforeCall() {
	// Note: don't set this false at the end, since we don't flush everything.
	if (!pendingFlush_) {
		return;
	}

	// These registers are not preserved by function calls.
	for (int i = 0; i <= 7; ++i) {
		FlushRiscVReg(RiscVReg(F0 + i));
	}
	for (int i = 10; i <= 17; ++i) {
		FlushRiscVReg(RiscVReg(F0 + i));
	}
	for (int i = 28; i <= 31; ++i) {
		FlushRiscVReg(RiscVReg(F0 + i));
	}
}

void RiscVRegCacheFPU::FlushAll() {
	if (!pendingFlush_) {
		// Nothing allocated.  FPU regs are not nearly as common as GPR.
		return;
	}

	int numRVRegs = 0;
	const RiscVReg *order = GetMIPSAllocationOrder(numRVRegs);

	for (int i = 0; i < numRVRegs; i++) {
		int a = order[i] - F0;
		int m = nr[a].mipsReg;

		if (nr[a].isDirty) {
			_assert_(m != MIPS_REG_INVALID);
			emit_->FS(32, order[i], CTXREG, GetMipsRegOffset(m));

			mr[m].loc = MIPSLoc::MEM;
			mr[m].nReg = (int)INVALID_REG;
			nr[a].mipsReg = IRREG_INVALID;
			nr[a].isDirty = false;
		} else {
			if (m != IRREG_INVALID) {
				mr[m].loc = MIPSLoc::MEM;
				mr[m].nReg = (int)INVALID_REG;
			}
			nr[a].mipsReg = IRREG_INVALID;
		}
	}

	pendingFlush_ = false;
}

void RiscVRegCacheFPU::DiscardR(IRReg r) {
	_dbg_assert_(IsValidReg(r));
	switch (mr[r].loc) {
	case MIPSLoc::FREG:
		_assert_(mr[r].nReg != INVALID_REG);
		if (mr[r].nReg != INVALID_REG) {
			// Note that we DO NOT write it back here. That's the whole point of Discard.
			nr[mr[r].nReg].isDirty = false;
			nr[mr[r].nReg].mipsReg = IRREG_INVALID;
		}
		break;

	case MIPSLoc::MEM:
		// Already there, nothing to do.
		break;

	default:
		_assert_(false);
		break;
	}
	mr[r].loc = MIPSLoc::MEM;
	mr[r].nReg = (int)INVALID_REG;
	mr[r].spillLockIRIndex = -1;
}

int RiscVRegCacheFPU::GetMipsRegOffset(IRReg r) {
	_assert_(IsValidReg(r));
	// These are offsets within the MIPSState structure.
	// IR gives us an index that is already 32 after the state index (skipping GPRs.)
	return (32 + r) * 4;
}

void RiscVRegCacheFPU::SpillLock(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	mr[r1].spillLockIRIndex = irIndex_;
	if (r2 != IRREG_INVALID)
		mr[r2].spillLockIRIndex = irIndex_;
	if (r3 != IRREG_INVALID)
		mr[r3].spillLockIRIndex = irIndex_;
	if (r4 != IRREG_INVALID)
		mr[r4].spillLockIRIndex = irIndex_;
}

void RiscVRegCacheFPU::ReleaseSpillLock(IRReg r1, IRReg r2, IRReg r3, IRReg r4) {
	_dbg_assert_(IsValidReg(r1));
	_dbg_assert_(r2 == IRREG_INVALID || IsValidReg(r2));
	_dbg_assert_(r3 == IRREG_INVALID || IsValidReg(r3));
	_dbg_assert_(r4 == IRREG_INVALID || IsValidReg(r4));
	mr[r1].spillLockIRIndex = -1;
	if (r2 != IRREG_INVALID)
		mr[r2].spillLockIRIndex = -1;
	if (r3 != IRREG_INVALID)
		mr[r3].spillLockIRIndex = -1;
	if (r4 != IRREG_INVALID)
		mr[r4].spillLockIRIndex = -1;
}

RiscVReg RiscVRegCacheFPU::R(IRReg mipsReg) {
	_dbg_assert_(IsValidReg(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::FREG);
	if (mr[mipsReg].loc == MIPSLoc::FREG) {
		return (RiscVReg)(mr[mipsReg].nReg + F0);
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

bool RiscVRegCacheFPU::IsValidReg(IRReg r) const {
	if (r < 0 || r >= NUM_MIPSFPUREG)
		return false;

	// See MIPSState for these offsets.
	int index = r + 32;

	// Allow FPU or VFPU regs here.
	if (index >= 32 && index < 32 + 32 + 128)
		return true;
	// Also allow VFPU temps.
	if (index >= 224 && index < 224 + 16)
		return true;

	// Nothing else is allowed for the FPU side cache.
	return false;
}

void RiscVRegCacheFPU::SetupInitialRegs() {
	IRNativeRegCache::SetupInitialRegs();

	// TODO: Move to a shared cache?
	mrInitial_[0].loc = MIPSLoc::MEM;
}
