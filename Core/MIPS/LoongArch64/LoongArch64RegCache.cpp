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
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Reporting.h"

using namespace LoongArch64Gen;
using namespace LoongArch64JitConstants;

LoongArch64RegCache::LoongArch64RegCache(MIPSComp::JitOptions *jo)
	: IRNativeRegCacheBase(jo) {
	// The V(LSX) regs overlap F regs, so we just use one slot.
	config_.totalNativeRegs = NUM_LAGPR + NUM_LAFPR;
	// F regs are used for both FPU and Vec, so we don't need VREGs.
	config_.mapUseVRegs = false;
	config_.mapFPUSIMD = true;
}

void LoongArch64RegCache::Init(LoongArch64Emitter *emitter) {
	emit_ = emitter;
}

void LoongArch64RegCache::SetupInitialRegs() {
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

const int *LoongArch64RegCache::GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const {
	base = R0;

	if (type == MIPSLoc::REG) {
		// R22-R26 (Also R27) are most suitable for static allocation. Those that are chosen for static allocation
		static const int allocationOrder[] = {
            R22, R23, R24, R25, R26, R27, R4, R5, R6, R7, R8, R9, R10, R11, R14, R15, R16, R17, R18, R19, R20,
		};
		static const int allocationOrderStaticAlloc[] = {
            R4, R5, R6, R7, R8, R9, R10, R11, R14, R15, R16, R17, R18, R19, R20,
		};

		if (jo_->useStaticAlloc) {
			count = ARRAY_SIZE(allocationOrderStaticAlloc);
			return allocationOrderStaticAlloc;
		} else {
			count = ARRAY_SIZE(allocationOrder);
			return allocationOrder;
		}
	} else if (type == MIPSLoc::FREG) {
		static const int allocationOrder[] = {
            F24, F25, F26, F27, F28, F29, F30, F31,
			F0, F1, F2, F3, F4, F5, F6, F7,
            F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23,
		};

		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else {
		_assert_msg_(false, "Allocation order not yet implemented");
		count = 0;
		return nullptr;
	}
}

const LoongArch64RegCache::StaticAllocation *LoongArch64RegCache::GetStaticAllocations(int &count) const {
	static const StaticAllocation allocs[] = {
		{ MIPS_REG_SP, R22, MIPSLoc::REG, true },
		{ MIPS_REG_V0, R23, MIPSLoc::REG },
		{ MIPS_REG_V1, R24, MIPSLoc::REG },
		{ MIPS_REG_A0, R25, MIPSLoc::REG },
		{ MIPS_REG_A1, R26, MIPSLoc::REG },
		{ MIPS_REG_RA, R27, MIPSLoc::REG },
	};

	if (jo_->useStaticAlloc) {
		count = ARRAY_SIZE(allocs);
		return allocs;
	}
	return IRNativeRegCacheBase::GetStaticAllocations(count);
}

void LoongArch64RegCache::EmitLoadStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		if (allocs[i].pointerified && jo_->enablePointerify) {
			emit_->LD_WU((LoongArch64Reg)allocs[i].nr, CTXREG, offset);
			emit_->ADD_D((LoongArch64Reg)allocs[i].nr, (LoongArch64Reg)allocs[i].nr, MEMBASEREG);
		} else {
			emit_->LD_W((LoongArch64Reg)allocs[i].nr, CTXREG, offset);
		}
	}
}

void LoongArch64RegCache::EmitSaveStaticRegisters() {
	int count;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	// This only needs to run once (by Asm) so checks don't need to be fast.
	for (int i = 0; i < count; i++) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		emit_->ST_W((LoongArch64Reg)allocs[i].nr, CTXREG, offset);
	}
}

void LoongArch64RegCache::FlushBeforeCall() {
	// These registers are not preserved by function calls.
	// They match between R0 and F0, conveniently.
	for (int i = 4; i <= 20; ++i) {
		FlushNativeReg(R0 + i);
	}
	for (int i = 0; i <= 23; ++i) {
		FlushNativeReg(F0 + i);
	}
}

bool LoongArch64RegCache::IsNormalized32(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return nr[mr[mipsReg].nReg].normalized32;
	}
	return false;
}

LoongArch64Gen::LoongArch64Reg LoongArch64RegCache::Normalize32(IRReg mipsReg, LoongArch64Gen::LoongArch64Reg destReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(destReg == INVALID_REG || (destReg > R0 && destReg <= R31));

	LoongArch64Reg reg = (LoongArch64Reg)mr[mipsReg].nReg;

	switch (mr[mipsReg].loc) {
	case MIPSLoc::IMM:
	case MIPSLoc::MEM:
		_assert_msg_(false, "Cannot normalize an imm or mem");
		return INVALID_REG;

	case MIPSLoc::REG:
	case MIPSLoc::REG_IMM:
		if (!nr[mr[mipsReg].nReg].normalized32) {
			if (destReg == INVALID_REG) {
				emit_->ADDI_W((LoongArch64Reg)mr[mipsReg].nReg, (LoongArch64Reg)mr[mipsReg].nReg, 0);
				nr[mr[mipsReg].nReg].normalized32 = true;
				nr[mr[mipsReg].nReg].pointerified = false;
			} else {
				emit_->ADDI_W(destReg, (LoongArch64Reg)mr[mipsReg].nReg, 0);
			}
		} else if (destReg != INVALID_REG) {
			emit_->ADDI_W(destReg, (LoongArch64Reg)mr[mipsReg].nReg, 0);
		}
		break;

	case MIPSLoc::REG_AS_PTR:
		_dbg_assert_(nr[mr[mipsReg].nReg].normalized32 == false);
		if (destReg == INVALID_REG) {
			// If we can pointerify, ADDI_W will be enough.
			if (!jo_->enablePointerify)
				AdjustNativeRegAsPtr(mr[mipsReg].nReg, false);
			emit_->ADDI_W((LoongArch64Reg)mr[mipsReg].nReg, (LoongArch64Reg)mr[mipsReg].nReg, 0);
			mr[mipsReg].loc = MIPSLoc::REG;
			nr[mr[mipsReg].nReg].normalized32 = true;
			nr[mr[mipsReg].nReg].pointerified = false;
		} else if (!jo_->enablePointerify) {
			emit_->SUB_D(destReg, (LoongArch64Reg)mr[mipsReg].nReg, MEMBASEREG);
			emit_->ADDI_W(destReg, destReg, 0);
		} else {
			emit_->ADDI_W(destReg, (LoongArch64Reg)mr[mipsReg].nReg, 0);
		}
		break;

	default:
		_assert_msg_(false, "Should not normalize32 floats");
		break;
	}

	return destReg == INVALID_REG ? reg : destReg;
}

LoongArch64Reg LoongArch64RegCache::TryMapTempImm(IRReg r) {
	_dbg_assert_(IsValidGPR(r));
	// If already mapped, no need for a temporary.
	if (IsGPRMapped(r)) {
		return R(r);
	}

	if (mr[r].loc == MIPSLoc::IMM) {
		if (mr[r].imm == 0) {
			return R_ZERO;
		}

		// Try our luck - check for an exact match in another LoongArch reg.
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just use this reg.
				return (LoongArch64Reg)mr[i].nReg;
			}
		}
	}

	return INVALID_REG;
}

LoongArch64Reg LoongArch64RegCache::GetAndLockTempGPR() {
	LoongArch64Reg reg = (LoongArch64Reg)AllocateReg(MIPSLoc::REG, MIPSMap::INIT);
	if (reg != INVALID_REG) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return reg;
}

LoongArch64Reg LoongArch64RegCache::MapWithFPRTemp(const IRInst &inst) {
	return (LoongArch64Reg)MapWithTemp(inst, MIPSLoc::FREG);
}

LoongArch64Reg LoongArch64RegCache::MapGPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg));

	// Okay, not mapped, so we need to allocate an LA register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return (LoongArch64Reg)nreg;
}

LoongArch64Reg LoongArch64RegCache::MapGPRAsPointer(IRReg reg) {
	return (LoongArch64Reg)MapNativeRegAsPointer(reg);
}

LoongArch64Reg LoongArch64RegCache::MapFPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return (LoongArch64Reg)nreg;
	return INVALID_REG;
}

LoongArch64Reg LoongArch64RegCache::MapVec4(IRReg first, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(first));
	_dbg_assert_((first & 3) == 0);
	_dbg_assert_(mr[first + 32].loc == MIPSLoc::MEM || mr[first + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, first + 32, 4, mapFlags);
	if (nreg != -1)
		return EncodeRegToV((LoongArch64Reg)nreg);
	return INVALID_REG;
}

void LoongArch64RegCache::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	LoongArch64Reg r = (LoongArch64Reg)(R0 + nreg);
	_assert_(r >= R0 && r <= R31);
	if (state) {
#ifdef MASKED_PSP_MEMORY
		// This destroys the value...
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->SLLI_W(r, r, 2);
		emit_->SRLI_W(r, r, 2);
		emit_->ADD_D(r, r, MEMBASEREG);
#else
		// Clear the top bits to be safe.
        emit_->SLLI_D(r, r, 32);
        emit_->SRLI_D(r, r, 32);
        emit_->ADD_D(r, r, MEMBASEREG);
#endif
		nr[nreg].normalized32 = false;
	} else {
#ifdef MASKED_PSP_MEMORY
		_dbg_assert_(!nr[nreg].isDirty);
#endif
		emit_->SUB_D(r, r, MEMBASEREG);
		nr[nreg].normalized32 = false;
	}
}

bool LoongArch64RegCache::IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) {
	// No special flags, skip the check for a little speed.
	return IRNativeRegCacheBase::IsNativeRegCompatible(nreg, type, flags, lanes);
}

void LoongArch64RegCache::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	LoongArch64Reg r = (LoongArch64Reg)(R0 + nreg);
	_dbg_assert_(r > R0);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (r <= R31) {
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		if (lanes == 1)
			emit_->LD_W(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->LD_D(r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
		nr[nreg].normalized32 = true;
	} else {
		_dbg_assert_(r >= F0 && r <= F31);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			emit_->FLD_S(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->FLD_D(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 4)
			emit_->VLD(EncodeRegToV(r), CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	}
}

void LoongArch64RegCache::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	LoongArch64Reg r = (LoongArch64Reg)(R0 + nreg);
	_dbg_assert_(r > R0);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (r <= R31) {
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		_assert_(mr[first].loc == MIPSLoc::REG || mr[first].loc == MIPSLoc::REG_IMM);
		if (lanes == 1)
			emit_->ST_W(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->ST_D(r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	} else {
		_dbg_assert_(r >= F0 && r <= F31);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			emit_->FST_S(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->FST_D(r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 4)
			emit_->VST(EncodeRegToV(r), CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	}
}

void LoongArch64RegCache::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	LoongArch64Reg r = (LoongArch64Reg)(R0 + nreg);
	if (r == R_ZERO && imm == 0)
		return;
	_dbg_assert_(r > R0 && r <= R31);
	emit_->LI(r, (int32_t)imm);

	// We always use 32-bit immediates, so this is normalized now.
	nr[nreg].normalized32 = true;
}

void LoongArch64RegCache::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_(IsValidGPRNoZero(mreg));
	// Try to optimize using a different reg.
	LoongArch64Reg storeReg = INVALID_REG;

	// Zero is super easy.
	if (imm == 0) {
		storeReg = R_ZERO;
	} else {
		// Could we get lucky?  Check for an exact match in another lareg.
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == imm) {
				// Awesome, let's just store this reg.
				storeReg = (LoongArch64Reg)mr[i].nReg;
				break;
			}
		}

		if (storeReg == INVALID_REG) {
			emit_->LI(SCRATCH1, imm);
			storeReg = SCRATCH1;
		}
	}

	emit_->ST_W(storeReg, CTXREG, GetMipsRegOffset(mreg));
}

bool LoongArch64RegCache::TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) {
	bool allowed = !mr[nr[nreg].mipsReg].isStatic;
	// There's currently no support for non-FREGs here.
	allowed = allowed && type == MIPSLoc::FREG;

	if (dest == -1)
		dest = nreg;

	if (allowed && (flags == MIPSMap::INIT || flags == MIPSMap::DIRTY)) {
		// Alright, changing lane count (possibly including lane position.)
		IRReg oldfirst = nr[nreg].mipsReg;
		int oldlanes = 0;
		while (mr[oldfirst + oldlanes].nReg == nreg)
			oldlanes++;
		_assert_msg_(oldlanes != 0, "TransferNativeReg encountered nreg mismatch");
		_assert_msg_(oldlanes != lanes, "TransferNativeReg transfer to same lanecount, misaligned?");

		if (lanes == 1 && TransferVecTo1(nreg, dest, first, oldlanes))
			return true;
		if (oldlanes == 1 && Transfer1ToVec(nreg, dest, first, lanes))
			return true;
	}

	return IRNativeRegCacheBase::TransferNativeReg(nreg, dest, type, first, lanes, flags);
}

bool LoongArch64RegCache::TransferVecTo1(IRNativeReg nreg, IRNativeReg dest, IRReg first, int oldlanes) {
	IRReg oldfirst = nr[nreg].mipsReg;

	// Is it worth preserving any of the old regs?
	int numKept = 0;
	for (int i = 0; i < oldlanes; ++i) {
		// Skip whichever one this is extracting.
		if (oldfirst + i == first)
			continue;
		// If 0 isn't being transfered, easy to keep in its original reg.
		if (i == 0 && dest != nreg) {
			numKept++;
			continue;
		}

		IRNativeReg freeReg = FindFreeReg(MIPSLoc::FREG, MIPSMap::INIT);
		if (freeReg != -1 && IsRegRead(MIPSLoc::FREG, oldfirst + i)) {
			// If there's one free, use it.  Don't modify nreg, though.
			emit_->VREPLVEI_W(FromNativeReg(freeReg), FromNativeReg(nreg), i);

			// Update accounting.
			nr[freeReg].isDirty = nr[nreg].isDirty;
			nr[freeReg].mipsReg = oldfirst + i;
			mr[oldfirst + i].lane = -1;
			mr[oldfirst + i].nReg = freeReg;
			numKept++;
		}
	}

	// Unless all other lanes were kept, store.
	if (nr[nreg].isDirty && numKept < oldlanes - 1) {
		StoreNativeReg(nreg, oldfirst, oldlanes);
		// Set false even for regs that were split out, since they were flushed too.
		for (int i = 0; i < oldlanes; ++i) {
			if (mr[oldfirst + i].nReg != -1)
				nr[mr[oldfirst + i].nReg].isDirty = false;
		}
	}

	// Next, shuffle the desired element into first place.
	if (mr[first].lane > 0) {
		emit_->VREPLVEI_W(FromNativeReg(dest), FromNativeReg(nreg), mr[first].lane);
	} else if (mr[first].lane <= 0 && dest != nreg) {
		emit_->VREPLVEI_W(FromNativeReg(dest), FromNativeReg(nreg), 0);
	}

	// Now update accounting.
	for (int i = 0; i < oldlanes; ++i) {
		auto &mreg = mr[oldfirst + i];
		if (oldfirst + i == first) {
			mreg.lane = -1;
			mreg.nReg = dest;
		} else if (mreg.nReg == nreg && i == 0 && nreg != dest) {
			// Still in the same register, but no longer a vec.
			mreg.lane = -1;
		} else if (mreg.nReg == nreg) {
			// No longer in a register.
			mreg.nReg = -1;
			mreg.lane = -1;
			mreg.loc = MIPSLoc::MEM;
		}
	}

	if (dest != nreg) {
		nr[dest].isDirty = nr[nreg].isDirty;
		if (oldfirst == first) {
			nr[nreg].mipsReg = -1;
			nr[nreg].isDirty = false;
		}
	}
	nr[dest].mipsReg = first;

	return true;
}

bool LoongArch64RegCache::Transfer1ToVec(IRNativeReg nreg, IRNativeReg dest, IRReg first, int lanes) {
	LoongArch64Reg destReg = FromNativeReg(dest);
	LoongArch64Reg cur[4]{};
	int numInRegs = 0;
	u8 blendMask = 0;
	for (int i = 0; i < lanes; ++i) {
		if (mr[first + i].lane != -1 || (i != 0 && mr[first + i].spillLockIRIndex >= irIndex_)) {
			// Can't do it, either double mapped or overlapping vec.
			return false;
		}

		if (mr[first + i].nReg == -1) {
			cur[i] = INVALID_REG;
			blendMask |= 1 << i;
		} else {
			cur[i] = FromNativeReg(mr[first + i].nReg);
			numInRegs++;
		}
	}

	// Shouldn't happen, this should only get called to transfer one in a reg.
	if (numInRegs == 0)
		return false;

	// If everything's currently in a reg, move it into this reg.
	if (lanes == 4) {
		// Go with an exhaustive approach, only 15 possibilities...
		if (blendMask == 0) {
			// y = yw##, x = xz##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[3]), EncodeRegToV(cur[1]));
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b0001) {
			// y = yw##, w = x###, w = xz##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[3]), EncodeRegToV(cur[1]));
			emit_->FLD_S( SCRATCHF1, CTXREG, GetMipsRegOffset(first + 0));
			emit_->VEXTRINS_W(EncodeRegToV(cur[3]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_W(EncodeRegToV(cur[3]), EncodeRegToV(cur[2]), EncodeRegToV(cur[3]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[3]));
		} else if (blendMask == 0b0010) {
			// x = xz##, z = y###, z = yw##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
			emit_->FLD_S( SCRATCHF1, CTXREG, GetMipsRegOffset(first + 1));
			emit_->VEXTRINS_W(EncodeRegToV(cur[2]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_W(EncodeRegToV(cur[2]), EncodeRegToV(cur[3]), EncodeRegToV(cur[2]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b0011 && (first & 1) == 0) {
			// z = zw##, w = xy##, dest = xyzw.  Mixed lane sizes.
			emit_->VILVL_W(EncodeRegToV(cur[2]), EncodeRegToV(cur[3]), EncodeRegToV(cur[2]));
			emit_->FLD_D( SCRATCHF1, CTXREG, GetMipsRegOffset(first + 0));
			emit_->VEXTRINS_D(EncodeRegToV(cur[3]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_D(EncodeRegToV(destReg), EncodeRegToV(cur[2]), EncodeRegToV(cur[3]));
		} else if (blendMask == 0b0100) {
			// y = yw##, w = z###, x = xz##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[3]), EncodeRegToV(cur[1]));
			emit_->FLD_S( SCRATCHF1, CTXREG, GetMipsRegOffset(first + 2));
			emit_->VEXTRINS_W(EncodeRegToV(cur[3]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[3]), EncodeRegToV(cur[0]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b0101 && (first & 3) == 0) {
			// y = yw##, w=x#z#, w = xz##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[3]), EncodeRegToV(cur[1]));
			emit_->VLD(EncodeRegToV(cur[3]), CTXREG, GetMipsRegOffset(first));
			emit_->VPICKEV_W(EncodeRegToV(cur[3]), EncodeRegToV(cur[3]), EncodeRegToV(cur[3]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[3]));
		} else if (blendMask == 0b0110 && (first & 3) == 0) {
			if (destReg == cur[0]) {
				// w = wx##, dest = #yz#, dest = xyz#, dest = xyzw.
				emit_->VILVL_W(EncodeRegToV(cur[3]), EncodeRegToV(cur[0]), EncodeRegToV(cur[3]));
				emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[3]), 1);
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[3]), (3 << 4));
			} else {
				// Assumes destReg may equal cur[3].
				// x = xw##, dest = #yz#, dest = xyz#, dest = xyzw.
				emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[3]), EncodeRegToV(cur[0]));
				emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[0]), 0);
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[0]), (3 << 4 | 1));
			}
		} else if (blendMask == 0b0111 && (first & 3) == 0 && destReg != cur[3]) {
			// dest = xyz#, dest = xyzw.
			emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
			emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[3]), (3 << 4));
		} else if (blendMask == 0b1000) {
			// x = xz##, z = w###, y = yw##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
			emit_->FLD_S(SCRATCHF1, CTXREG, GetMipsRegOffset(first + 3));
			emit_->VEXTRINS_W(EncodeRegToV(cur[2]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[2]), EncodeRegToV(cur[1]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b1001 && (first & 3) == 0) {
			if (destReg == cur[1]) {
				// w = zy##, dest = x##w, dest = xy#w, dest = xyzw.
				emit_->VILVL_W(EncodeRegToV(cur[2]), EncodeRegToV(cur[1]), EncodeRegToV(cur[2]));
				emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[2]), (1 << 4 | 1));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[2]), (2 << 4));
			} else {
				// Assumes destReg may equal cur[2].
				// y = yz##, dest = x##w, dest = xy#w, dest = xyzw.
				emit_->VILVL_W(EncodeRegToV(cur[1]), EncodeRegToV(cur[2]), EncodeRegToV(cur[1]));
				emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), (1 << 4));
				emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), (2 << 4 | 1));
			}
		} else if (blendMask == 0b1010 && (first & 3) == 0) {
			// x = xz##, z = #y#w, z=yw##, dest = xyzw.
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
			emit_->VLD(EncodeRegToV(cur[2]), CTXREG, GetMipsRegOffset(first));
			emit_->VPICKOD_W(EncodeRegToV(cur[2]), EncodeRegToV(cur[2]), EncodeRegToV(cur[2]));
			emit_->VILVL_W(EncodeRegToV(destReg), EncodeRegToV(cur[2]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b1011 && (first & 3) == 0 && destReg != cur[2]) {
			// dest = xy#w, dest = xyzw.
			emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
			emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[2]), (2 << 4));
		} else if (blendMask == 0b1100 && (first & 1) == 0) {
			// x = xy##, y = zw##, dest = xyzw.  Mixed lane sizes.
			emit_->VILVL_W(EncodeRegToV(cur[0]), EncodeRegToV(cur[1]), EncodeRegToV(cur[0]));
			emit_->FLD_D(SCRATCHF1, CTXREG, GetMipsRegOffset(first + 2));
			emit_->VEXTRINS_D(EncodeRegToV(cur[1]), EncodeRegToV(SCRATCHF1), 0);
			emit_->VILVL_D(EncodeRegToV(destReg), EncodeRegToV(cur[1]), EncodeRegToV(cur[0]));
		} else if (blendMask == 0b1101 && (first & 3) == 0 && destReg != cur[1]) {
			// dest = x#zw, dest = xyzw.
			emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
			emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[1]), (1 << 4));
		} else if (blendMask == 0b1110 && (first & 3) == 0 && destReg != cur[0]) {
			// dest = #yzw, dest = xyzw.
			emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
			emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(cur[0]), 0);
		} else if (blendMask == 0b1110 && (first & 3) == 0) {
			// If dest == cur[0] (which may be common), we need a temp...
			IRNativeReg freeReg = FindFreeReg(MIPSLoc::FREG, MIPSMap::INIT);
			// Very unfortunate.
			if (freeReg == INVALID_REG)
				return false;

			// free = x###, dest = #yzw, dest = xyzw.
			emit_->VREPLVEI_W(EncodeRegToV(FromNativeReg(freeReg)), EncodeRegToV(cur[0]), 0);
			emit_->VLD(EncodeRegToV(destReg), CTXREG, GetMipsRegOffset(first));
			emit_->VEXTRINS_W(EncodeRegToV(destReg), EncodeRegToV(FromNativeReg(freeReg)), 0);
		} else {
			return false;
		}
	} else {
		return false;
	}

	mr[first].lane = 0;
	for (int i = 0; i < lanes; ++i) {
		if (mr[first + i].nReg != -1) {
			// If this was dirty, the combined reg is now dirty.
			if (nr[mr[first + i].nReg].isDirty)
				nr[dest].isDirty = true;

			// Throw away the other register we're no longer using.
			if (i != 0)
				DiscardNativeReg(mr[first + i].nReg);
		}

		// And set it as using the new one.
		mr[first + i].lane = i;
		mr[first + i].loc = MIPSLoc::FREG;
		mr[first + i].nReg = dest;
	}

	if (dest != nreg) {
		nr[dest].mipsReg = first;
		nr[nreg].mipsReg = -1;
		nr[nreg].isDirty = false;
	}

	return true;
}

LoongArch64Reg LoongArch64RegCache::R(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return (LoongArch64Reg)mr[mipsReg].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in LoongArch64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

LoongArch64Reg LoongArch64RegCache::RPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return (LoongArch64Reg)mr[mipsReg].nReg;
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int la = mr[mipsReg].nReg;
		_dbg_assert_(nr[la].pointerified);
		if (nr[la].pointerified) {
			return (LoongArch64Reg)mr[mipsReg].nReg;
		} else {
			ERROR_LOG(Log::JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in LoongArch64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

LoongArch64Reg LoongArch64RegCache::F(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return (LoongArch64Reg)mr[mipsReg + 32].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in LoongArch64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

LoongArch64Reg LoongArch64RegCache::V(IRReg mipsReg) {
	return EncodeRegToV(F(mipsReg));
}
