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
	: IRNativeRegCacheBase(jo) {
	totalNativeRegs_ = NUM_RVFPUREG;
}

void RiscVRegCacheFPU::Init(RiscVEmitter *emitter) {
	emit_ = emitter;
}

const int *RiscVRegCacheFPU::GetAllocationOrder(MIPSLoc type, int &count, int &base) const {
	_assert_(type == MIPSLoc::FREG);
	// F8 through F15 are used for compression, so they are great.
	// TODO: Maybe we could remove some saved regs since we rarely need that many?  Or maybe worth it?
	static const int allocationOrder[] = {
		F8, F9, F10, F11, F12, F13, F14, F15,
		F0, F1, F2, F3, F4, F5, F6, F7,
		F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31,
	};

	count = ARRAY_SIZE(allocationOrder);
	base = F0;
	return allocationOrder;
}

RiscVReg RiscVRegCacheFPU::MapReg(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return (RiscVReg)(F0 + nreg);
	return INVALID_REG;
}

void RiscVRegCacheFPU::MapInIn(IRReg rd, IRReg rs) {
	SpillLockFPR(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLockFPR(rd, rs);
}

void RiscVRegCacheFPU::MapDirtyIn(IRReg rd, IRReg rs, bool avoidLoad) {
	SpillLockFPR(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rs);
	ReleaseSpillLockFPR(rd, rs);
}

void RiscVRegCacheFPU::MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt, bool avoidLoad) {
	SpillLockFPR(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLockFPR(rd, rs, rt);
}

RiscVReg RiscVRegCacheFPU::MapDirtyInTemp(IRReg rd, IRReg rs, bool avoidLoad) {
	SpillLockFPR(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	MapReg(rs);
	RiscVReg temp = (RiscVReg)(F0 + AllocateReg(MIPSLoc::FREG));
	ReleaseSpillLockFPR(rd, rs);
	return temp;
}

void RiscVRegCacheFPU::Map4DirtyIn(IRReg rdbase, IRReg rsbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLockFPR(rdbase + i, rsbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLockFPR(rdbase + i, rsbase + i);
}

void RiscVRegCacheFPU::Map4DirtyInIn(IRReg rdbase, IRReg rsbase, IRReg rtbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLockFPR(rdbase + i, rsbase + i, rtbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase) || (rdbase < rtbase + 4 && rdbase + 4 > rtbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	for (int i = 0; i < 4; ++i)
		MapReg(rtbase + i);
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLockFPR(rdbase + i, rsbase + i, rtbase + i);
}

RiscVReg RiscVRegCacheFPU::Map4DirtyInTemp(IRReg rdbase, IRReg rsbase, bool avoidLoad) {
	for (int i = 0; i < 4; ++i)
		SpillLockFPR(rdbase + i, rsbase + i);
	bool load = !avoidLoad || (rdbase < rsbase + 4 && rdbase + 4 > rsbase);
	for (int i = 0; i < 4; ++i)
		MapReg(rdbase + i, load ? MIPSMap::DIRTY : MIPSMap::NOINIT);
	for (int i = 0; i < 4; ++i)
		MapReg(rsbase + i);
	RiscVReg temp = (RiscVReg)(F0 + AllocateReg(MIPSLoc::FREG));
	for (int i = 0; i < 4; ++i)
		ReleaseSpillLockFPR(rdbase + i, rsbase + i);
	return temp;
}

void RiscVRegCacheFPU::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	RiscVReg r = (RiscVReg)(F0 + nreg);
	_dbg_assert_(r >= F0 && r <= F31);
	// Multilane not yet supported.
	_assert_(lanes == 1);
	if (mr[first].loc == MIPSLoc::FREG) {
		emit_->FL(32, r, CTXREG, GetMipsRegOffset(first));
	} else {
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
	}
}

void RiscVRegCacheFPU::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	RiscVReg r = (RiscVReg)(F0 + nreg);
	_dbg_assert_(r >= F0 && r <= F31);
	// Multilane not yet supported.
	_assert_(lanes == 1);
	if (mr[first].loc == MIPSLoc::FREG) {
		emit_->FS(32, r, CTXREG, GetMipsRegOffset(first));
	} else {
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
	}
}

void RiscVRegCacheFPU::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	_assert_msg_(false, "Set float to imm is unsupported");
}

void RiscVRegCacheFPU::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_msg_(false, "Storing imms to floats is unsupported");
}

void RiscVRegCacheFPU::FlushR(IRReg r) {
	_dbg_assert_(IsValidFPR(r));
	FlushReg(r + 32);
}

void RiscVRegCacheFPU::FlushBeforeCall() {
	// These registers are not preserved by function calls.
	for (int i = 0; i <= 7; ++i) {
		FlushNativeReg(i);
	}
	for (int i = 10; i <= 17; ++i) {
		FlushNativeReg(i);
	}
	for (int i = 28; i <= 31; ++i) {
		FlushNativeReg(i);
	}
}

void RiscVRegCacheFPU::DiscardR(IRReg r) {
	_dbg_assert_(IsValidFPR(r));
	DiscardReg(r + 32);
}

RiscVReg RiscVRegCacheFPU::R(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return (RiscVReg)(mr[mipsReg + 32].nReg + F0);
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

void RiscVRegCacheFPU::SetupInitialRegs() {
	IRNativeRegCacheBase::SetupInitialRegs();

	// TODO: Move to a shared cache?
	mrInitial_[0].loc = MIPSLoc::MEM;
}
