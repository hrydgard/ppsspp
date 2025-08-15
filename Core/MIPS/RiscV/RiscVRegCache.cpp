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
	: IRNativeRegCacheBase(jo) {
	// TODO: Update these when using RISC-V V.
	config_.totalNativeRegs = NUM_RVGPR + NUM_RVFPR;
	config_.mapUseVRegs = false;
	config_.mapFPUSIMD = false;
}

void RiscVRegCache::Init(RiscVEmitter *emitter) {
	emit_ = emitter;
}

void RiscVRegCache::SetupInitialRegs() {
	IRNativeRegCacheBase::SetupInitialRegs();

	// Treat R_ZERO a bit specially, but it's basically static alloc too.
	nrInitial_[R_ZERO].mipsReg = MIPS_REG_ZERO;
	nrInitial_[R_ZERO].normalized32 = true;

	// Since we also have a fixed zero, mark it as a static allocation.
	mrInitial_[MIPS_REG_ZERO].loc = MIPSLoc::REG_IMM;
	mrInitial_[MIPS_REG_ZERO].nReg = R_ZERO;
	mrInitial_[MIPS_REG_ZERO].imm = 0;
	mrInitial_[MIPS_REG_ZERO].isStatic = true;
}

const int *RiscVRegCache::GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const {
	base = X0;

	if (type == MIPSLoc::REG) {
		// X8 and X9 are the most ideal for static alloc because they can be used with compression.
		// Otherwise we stick to saved regs - might not be necessary.
		static const int allocationOrder[] = {
			X8, X9, X12, X13, X14, X15, X5, X6, X7, X16, X17, X18, X19, X20, X21, X22, X23, X28, X29, X30, X31,
		};
		static const int allocationOrderStaticAlloc[] = {
			X12, X13, X14, X15, X5, X6, X7, X16, X17, X21, X22, X23, X28, X29, X30, X31,
		};

		if (jo_->useStaticAlloc) {
			count = ARRAY_SIZE(allocationOrderStaticAlloc);
			return allocationOrderStaticAlloc;
		} else {
			count = ARRAY_SIZE(allocationOrder);
			return allocationOrder;
		}
	} else if (type == MIPSLoc::FREG) {
		// F8 through F15 are used for compression, so they are great.
		static const int allocationOrder[] = {
			F8, F9, F10, F11, F12, F13, F14, F15,
			F0, F1, F2, F3, F4, F5, F6, F7,
			F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31,
		};

		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else {
		_assert_msg_(false, "Allocation order not yet implemented");
		count = 0;
		return nullptr;
	}
}

const RiscVRegCache::StaticAllocation *RiscVRegCache::GetStaticAllocations(int &count) const {
	static const StaticAllocation allocs[] = {
		{ MIPS_REG_SP, X8, MIPSLoc::REG, true },
		{ MIPS_REG_V0, X9, MIPSLoc::REG },
		{ MIPS_REG_V1, X18, MIPSLoc::REG },
		{ MIPS_REG_A0, X19, MIPSLoc::REG },
		{ MIPS_REG_RA, X20, MIPSLoc::REG },
	};

	if (jo_->useStaticAlloc) {
		count = ARRAY_SIZE(allocs);
		return allocs;
	}
	return IRNativeRegCacheBase::GetStaticAllocations(count);
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
	// They match between X0 and F0, conveniently.
	for (int i = 5; i <= 7; ++i) {
		FlushNativeReg(X0 + i);
		FlushNativeReg(F0 + i);
	}
	for (int i = 10; i <= 17; ++i) {
		FlushNativeReg(X0 + i);
		FlushNativeReg(F0 + i);
	}
	for (int i = 28; i <= 31; ++i) {
		FlushNativeReg(X0 + i);
		FlushNativeReg(F0 + i);
	}
}

bool RiscVRegCache::IsNormalized32(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	if (XLEN == 32)
		return true;
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return nr[mr[mipsReg].nReg].normalized32;
	}
	return false;
}

RiscVGen::RiscVReg RiscVRegCache::Normalize32(IRReg mipsReg, RiscVGen::RiscVReg destReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
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
				AdjustNativeRegAsPtr(mr[mipsReg].nReg, false);
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

	default:
		_assert_msg_(false, "Should not normalize32 floats");
		break;
	}

	return destReg == INVALID_REG ? reg : destReg;
}

RiscVReg RiscVRegCache::TryMapTempImm(IRReg r) {
	_dbg_assert_(IsValidGPR(r));
	// If already mapped, no need for a temporary.
	if (IsGPRMapped(r)) {
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

RiscVReg RiscVRegCache::GetAndLockTempGPR() {
	RiscVReg reg = (RiscVReg)AllocateReg(MIPSLoc::REG, MIPSMap::INIT);
	if (reg != INVALID_REG) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return reg;
}

RiscVReg RiscVRegCache::MapWithFPRTemp(const IRInst &inst) {
	return (RiscVReg)MapWithTemp(inst, MIPSLoc::FREG);
}

RiscVReg RiscVRegCache::MapGPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg));

	// Okay, not mapped, so we need to allocate an RV register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return (RiscVReg)nreg;
}

RiscVReg RiscVRegCache::MapGPRAsPointer(IRReg reg) {
	return (RiscVReg)MapNativeRegAsPointer(reg);
}

RiscVReg RiscVRegCache::MapFPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return (RiscVReg)nreg;
	return INVALID_REG;
}

void RiscVRegCache::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	RiscVReg r = (RiscVReg)(X0 + nreg);
	_assert_(r >= X0 && r <= X31);
	if (state) {
#ifdef MASKED_PSP_MEMORY
		// This destroys the value...
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->SLLIW(r, r, 2);
		emit_->SRLIW(r, r, 2);
		emit_->ADD(r, r, MEMBASEREG);
#else
		// Clear the top bits to be safe.
		if (cpu_info.RiscV_Zba) {
			emit_->ADD_UW(r, r, MEMBASEREG);
		} else {
			_assert_(XLEN == 64);
			emit_->SLLI(r, r, 32);
			emit_->SRLI(r, r, 32);
			emit_->ADD(r, r, MEMBASEREG);
		}
#endif
		nr[nreg].normalized32 = false;
	} else {
#ifdef MASKED_PSP_MEMORY
		_dbg_assert_(!nr[nreg].isDirty);
#endif
		emit_->SUB(r, r, MEMBASEREG);
		nr[nreg].normalized32 = false;
	}
}

bool RiscVRegCache::IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) {
	// No special flags except VREG, skip the check for a little speed.
	if (type != MIPSLoc::VREG)
		return true;
	return IRNativeRegCacheBase::IsNativeRegCompatible(nreg, type, flags, lanes);
}

void RiscVRegCache::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	RiscVReg r = (RiscVReg)(X0 + nreg);
	_dbg_assert_(r > X0);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (r <= X31) {
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		if (lanes == 1)
			emit_->LW(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->LD(r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
		nr[nreg].normalized32 = true;
	} else {
		_dbg_assert_(r >= F0 && r <= F31);
		// Multilane not yet supported.
		_assert_(lanes == 1);
		if (mr[first].loc == MIPSLoc::FREG) {
			emit_->FL(32, r, CTXREG, GetMipsRegOffset(first));
		} else {
			_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		}
	}
}

void RiscVRegCache::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	RiscVReg r = (RiscVReg)(X0 + nreg);
	_dbg_assert_(r > X0);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (r <= X31) {
		// Multilane not yet supported.
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		_assert_(mr[first].loc == MIPSLoc::REG || mr[first].loc == MIPSLoc::REG_IMM);
		if (lanes == 1)
			emit_->SW(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->SD(r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	} else {
		_dbg_assert_(r >= F0 && r <= F31);
		// Multilane not yet supported.
		_assert_(lanes == 1);
		if (mr[first].loc == MIPSLoc::FREG) {
			emit_->FS(32, r, CTXREG, GetMipsRegOffset(first));
		} else {
			_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		}
	}
}

void RiscVRegCache::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	RiscVReg r = (RiscVReg)(X0 + nreg);
	if (r == R_ZERO && imm == 0)
		return;
	_dbg_assert_(r > X0 && r <= X31);
	emit_->LI(r, (int32_t)imm);

	// We always use 32-bit immediates, so this is normalized now.
	nr[nreg].normalized32 = true;
}

void RiscVRegCache::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_(IsValidGPRNoZero(mreg));
	// Try to optimize using a different reg.
	RiscVReg storeReg = INVALID_REG;

	// Zero is super easy.
	if (imm == 0) {
		storeReg = R_ZERO;
	} else {
		// Could we get lucky?  Check for an exact match in another rvreg.
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == imm) {
				// Awesome, let's just store this reg.
				storeReg = (RiscVReg)mr[i].nReg;
				break;
			}
		}

		if (storeReg == INVALID_REG) {
			emit_->LI(SCRATCH1, imm);
			storeReg = SCRATCH1;
		}
	}

	emit_->SW(storeReg, CTXREG, GetMipsRegOffset(mreg));
}

RiscVReg RiscVRegCache::R(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return (RiscVReg)mr[mipsReg].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

RiscVReg RiscVRegCache::RPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return (RiscVReg)mr[mipsReg].nReg;
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int rv = mr[mipsReg].nReg;
		_dbg_assert_(nr[rv].pointerified);
		if (nr[rv].pointerified) {
			return (RiscVReg)mr[mipsReg].nReg;
		} else {
			ERROR_LOG(Log::JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

RiscVReg RiscVRegCache::F(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return (RiscVReg)mr[mipsReg + 32].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in riscv reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}
