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

#include "ppsspp_config.h"
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#ifndef offsetof
#include <cstddef>
#endif

#include "Common/CPUDetect.h"
#include "Common/LogReporting.h"
#include "Core/MemMap.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"
#include "Core/MIPS/JitCommon/JitState.h"

using namespace Arm64Gen;
using namespace Arm64IRJitConstants;

Arm64IRRegCache::Arm64IRRegCache(MIPSComp::JitOptions *jo)
	: IRNativeRegCacheBase(jo) {
	// The S/D/Q regs overlap, so we just use one slot.  The numbers don't match ARM64Reg.
	config_.totalNativeRegs = NUM_X_REGS + NUM_X_FREGS;
	config_.mapFPUSIMD = true;
	// XMM regs are used for both FPU and Vec, so we don't need VREGs.
	config_.mapUseVRegs = false;
}

void Arm64IRRegCache::Init(ARM64XEmitter *emitter, ARM64FloatEmitter *fp) {
	emit_ = emitter;
	fp_ = fp;
}

const int *Arm64IRRegCache::GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const {
	if (type == MIPSLoc::REG) {
		// See register alloc remarks in Arm64Asm.cpp.
		base = W0;

		// W19-W23 are most suitable for static allocation. Those that are chosen for static allocation
		// should be omitted here and added in GetStaticAllocations.
		static const int allocationOrder[] = {
			W19, W20, W21, W22, W23, W24, W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
		};
		static const int allocationOrderStaticAlloc[] = {
			W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
		};

		if (jo_->useStaticAlloc) {
			count = ARRAY_SIZE(allocationOrderStaticAlloc);
			return allocationOrderStaticAlloc;
		}
		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else if (type == MIPSLoc::FREG) {
		base = S0 - NUM_X_REGS;

		// We don't really need four temps, probably.
		// We start with S8 for call flushes.
		static const int allocationOrder[] = {
			// Reserve four full 128-bit temp registers, should be plenty.
			S8,  S9,  S10, S11, // Partially callee-save (bottom 64 bits)
			S12, S13, S14, S15, // Partially callee-save (bottom 64 bits)
			S16, S17, S18, S19,
			S20, S21, S22, S23,
			S24, S25, S26, S27,
			S28, S29, S30, S31,
			S4,  S5,  S6,  S7,
		};

		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else {
		_assert_msg_(false, "Allocation order not yet implemented");
		count = 0;
		return nullptr;
	}
}

const Arm64IRRegCache::StaticAllocation *Arm64IRRegCache::GetStaticAllocations(int &count) const {
	static const StaticAllocation allocs[] = {
		{ MIPS_REG_SP, W19, MIPSLoc::REG, true },
		{ MIPS_REG_V0, W20, MIPSLoc::REG },
		{ MIPS_REG_V1, W21, MIPSLoc::REG },
		{ MIPS_REG_A0, W22, MIPSLoc::REG },
		{ MIPS_REG_A1, W23, MIPSLoc::REG },
		{ MIPS_REG_RA, W24, MIPSLoc::REG },
	};

	if (jo_->useStaticAlloc) {
		count = ARRAY_SIZE(allocs);
		return allocs;
	}
	return IRNativeRegCacheBase::GetStaticAllocations(count);
}

void Arm64IRRegCache::EmitLoadStaticRegisters() {
	int count = 0;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	for (int i = 0; i < count; ++i) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		if (i + 1 < count && allocs[i].mr == allocs[i + 1].mr - 1) {
			_assert_(!allocs[i].pointerified && !allocs[i + 1].pointerified);
			emit_->LDP(INDEX_SIGNED, FromNativeReg(allocs[i].nr), FromNativeReg(allocs[i + 1].nr), CTXREG, offset);
			++i;
		} else {
			emit_->LDR(INDEX_UNSIGNED, FromNativeReg(allocs[i].nr), CTXREG, offset);
			if (allocs[i].pointerified && jo_->enablePointerify) {
				ARM64Reg r64 = FromNativeReg64(allocs[i].nr);
				uint32_t membaseHigh = (uint32_t)((uint64_t)Memory::base >> 32);
				emit_->MOVK(r64, membaseHigh & 0xFFFF, SHIFT_32);
				if (membaseHigh & 0xFFFF0000)
					emit_->MOVK(r64, membaseHigh >> 16, SHIFT_48);
			}
		}
	}
}

void Arm64IRRegCache::EmitSaveStaticRegisters() {
	int count = 0;
	const StaticAllocation *allocs = GetStaticAllocations(count);
	// This only needs to run once (by Asm) so checks don't need to be fast.
	for (int i = 0; i < count; ++i) {
		int offset = GetMipsRegOffset(allocs[i].mr);
		if (i + 1 < count && allocs[i].mr == allocs[i + 1].mr - 1) {
			emit_->STP(INDEX_SIGNED, FromNativeReg(allocs[i].nr), FromNativeReg(allocs[i + 1].nr), CTXREG, offset);
			++i;
		} else {
			emit_->STR(INDEX_UNSIGNED, FromNativeReg(allocs[i].nr), CTXREG, offset);
		}
	}
}

void Arm64IRRegCache::FlushBeforeCall() {
	// These registers are not preserved by function calls.
	auto isGPRSaved = [&](IRNativeReg nreg) {
		ARM64Reg ar = FromNativeReg(nreg);
		return ar >= W19 && ar <= W29;
	};
	auto isFPRSaved = [&](IRNativeReg nreg) {
		ARM64Reg ar = FromNativeReg(nreg);
		return ar >= S8 && ar <= S15;
	};

	// Go through by IR index first, to use STP where we can.
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS - 1; ++i) {
		if (mr[i].nReg == -1 || mr[i + 1].nReg == -1 || mr[i].isStatic || mr[i + 1].isStatic)
			continue;
		// Ignore multilane regs.
		if (mr[i].lane != -1 || mr[i + 1].lane != -1)
			continue;
		if (!nr[mr[i].nReg].isDirty || !nr[mr[i + 1].nReg].isDirty)
			continue;
		// Make sure not to try to pair a GPR and FPR.
		if (IsValidGPR(i) != IsValidGPR(i + 1))
			continue;

		int offset = GetMipsRegOffset(i);

		// Okay, it's a maybe.  Are we flushing both as GPRs?
		if (!isGPRSaved(mr[i].nReg) && !isGPRSaved(mr[i + 1].nReg) && IsValidGPR(i) && offset <= 252) {
			// If either is mapped as a pointer, fix it.
			if (mr[i].loc == MIPSLoc::REG_AS_PTR)
				AdjustNativeRegAsPtr(mr[i].nReg, false);
			if (mr[i + 1].loc == MIPSLoc::REG_AS_PTR)
				AdjustNativeRegAsPtr(mr[i + 1].nReg, false);

			// That means we should use STP.
			emit_->STP(INDEX_SIGNED, FromNativeReg(mr[i].nReg), FromNativeReg(mr[i + 1].nReg), CTXREG, offset);

			DiscardNativeReg(mr[i].nReg);
			DiscardNativeReg(mr[i + 1].nReg);

			++i;
			continue;
		}

		// Perhaps as FPRs?  Note: these must be single lane at this point.
		// TODO: Could use STP on quads etc. too, i.e. i & i + 4.
		if (!isFPRSaved(mr[i].nReg) && !isFPRSaved(mr[i + 1].nReg) && !IsValidGPR(i) && offset <= 252) {
			fp_->STP(32, INDEX_SIGNED, FromNativeReg(mr[i].nReg), FromNativeReg(mr[i + 1].nReg), CTXREG, offset);

			DiscardNativeReg(mr[i].nReg);
			DiscardNativeReg(mr[i + 1].nReg);

			++i;
			continue;
		}
	}
	
	// Alright, now go through any that didn't get flushed with STP.
	for (int i = 0; i < 19; ++i) {
		FlushNativeReg(GPRToNativeReg(ARM64Reg(W0 + i)));
	}
	FlushNativeReg(GPRToNativeReg(W30));

	for (int i = 0; i < 8; ++i) {
		FlushNativeReg(VFPToNativeReg(ARM64Reg(S0 + i)));
	}
	for (int i = 8; i < 16; ++i) {
		// These are preserved but only the low 64 bits.
		IRNativeReg nreg = VFPToNativeReg(ARM64Reg(S0 + i));
		if (nr[nreg].mipsReg != IRREG_INVALID && GetFPRLaneCount(nr[nreg].mipsReg - 32) > 2)
			FlushNativeReg(nreg);
	}
	for (int i = 16; i < 32; ++i) {
		FlushNativeReg(VFPToNativeReg(ARM64Reg(S0 + i)));
	}
}

ARM64Reg Arm64IRRegCache::TryMapTempImm(IRReg r) {
	_dbg_assert_(IsValidGPR(r));

	// If already mapped, no need for a temporary.
	if (IsGPRMapped(r)) {
		return R(r);
	}

	if (mr[r].loc == MIPSLoc::IMM) {
		// Can we just use zero?
		if (mr[r].imm == 0)
			return WZR;

		// Try our luck - check for an exact match in another xreg.
		for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just use this reg.
				return FromNativeReg(mr[i].nReg);
			}
		}
	}

	return INVALID_REG;
}

ARM64Reg Arm64IRRegCache::GetAndLockTempGPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::REG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return FromNativeReg(reg);
}

ARM64Reg Arm64IRRegCache::GetAndLockTempFPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::FREG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return FromNativeReg(reg);
}

ARM64Reg Arm64IRRegCache::MapWithFPRTemp(const IRInst &inst) {
	return FromNativeReg(MapWithTemp(inst, MIPSLoc::FREG));
}

ARM64Reg Arm64IRRegCache::MapGPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg));

	// Okay, not mapped, so we need to allocate an arm64 register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return FromNativeReg(nreg);
}

ARM64Reg Arm64IRRegCache::MapGPR2(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg) && IsValidGPR(mipsReg + 1));

	// Okay, not mapped, so we need to allocate an arm64 register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 2, mapFlags);
	return FromNativeReg64(nreg);
}

ARM64Reg Arm64IRRegCache::MapGPRAsPointer(IRReg reg) {
	return FromNativeReg64(MapNativeRegAsPointer(reg));
}

ARM64Reg Arm64IRRegCache::MapFPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return FromNativeReg(nreg);
	return INVALID_REG;
}

ARM64Reg Arm64IRRegCache::MapVec2(IRReg first, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(first));
	_dbg_assert_((first & 1) == 0);
	_dbg_assert_(mr[first + 32].loc == MIPSLoc::MEM || mr[first + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, first + 32, 2, mapFlags);
	if (nreg != -1)
		return EncodeRegToDouble(FromNativeReg(nreg));
	return INVALID_REG;
}

ARM64Reg Arm64IRRegCache::MapVec4(IRReg first, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(first));
	_dbg_assert_((first & 3) == 0);
	_dbg_assert_(mr[first + 32].loc == MIPSLoc::MEM || mr[first + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, first + 32, 4, mapFlags);
	if (nreg != -1)
		return EncodeRegToQuad(FromNativeReg(nreg));
	return INVALID_REG;
}

void Arm64IRRegCache::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	_assert_(nreg >= 0 && nreg < (IRNativeReg)WZR);
	ARM64Reg r = FromNativeReg64(nreg);
	if (state) {
		if (!jo_->enablePointerify) {
#if defined(MASKED_PSP_MEMORY)
			// This destroys the value...
			_dbg_assert_(!nr[nreg].isDirty);
			emit_->ANDI2R(r, r, Memory::MEMVIEW32_MASK);
#endif
			emit_->ADD(r, r, MEMBASEREG);
		} else {
			uint32_t membaseHigh = (uint32_t)((uint64_t)Memory::base >> 32);
			emit_->MOVK(r, membaseHigh & 0xFFFF, SHIFT_32);
			if (membaseHigh & 0xFFFF0000)
				emit_->MOVK(r, membaseHigh >> 16, SHIFT_48);
		}
	} else {
		if (!jo_->enablePointerify) {
#if defined(MASKED_PSP_MEMORY)
			_dbg_assert_(!nr[nreg].isDirty);
#endif
			emit_->SUB(r, r, MEMBASEREG);
		} else {
			// Nothing to do, just ignore the high 32 bits.
		}
	}
}

bool Arm64IRRegCache::IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) {
	// No special flags, skip the check for a little speed.
	return true;
}

void Arm64IRRegCache::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	ARM64Reg r = FromNativeReg(nreg);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (nreg < NUM_X_REGS) {
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		if (lanes == 1)
			emit_->LDR(INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->LDR(INDEX_UNSIGNED, EncodeRegTo64(r), CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	} else {
		_dbg_assert_(nreg < NUM_X_REGS + NUM_X_FREGS);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot load this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			fp_->LDR(32, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			fp_->LDR(64, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 4)
			fp_->LDR(128, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	}
}

void Arm64IRRegCache::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	ARM64Reg r = FromNativeReg(nreg);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (nreg < NUM_X_REGS) {
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		_assert_(mr[first].loc == MIPSLoc::REG || mr[first].loc == MIPSLoc::REG_IMM);
		if (lanes == 1)
			emit_->STR(INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			emit_->STR(INDEX_UNSIGNED, EncodeRegTo64(r), CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	} else {
		_dbg_assert_(nreg < NUM_X_REGS + NUM_X_FREGS);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			fp_->STR(32, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 2)
			fp_->STR(64, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else if (lanes == 4)
			fp_->STR(128, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(first));
		else
			_assert_(false);
	}
}

void Arm64IRRegCache::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	ARM64Reg r = FromNativeReg(nreg);
	_dbg_assert_(nreg >= 0 && nreg < (IRNativeReg)WZR);
	// On ARM64, MOVZ/MOVK is really fast.
	emit_->MOVI2R(r, imm);
}

void Arm64IRRegCache::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_(IsValidGPRNoZero(mreg));
	// Try to optimize using a different reg.
	ARM64Reg storeReg = INVALID_REG;
	if (imm == 0)
		storeReg = WZR;

	// Could we get lucky?  Check for an exact match in another xreg.
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; ++i) {
		if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == imm) {
			// Awesome, let's just store this reg.
			storeReg = (ARM64Reg)mr[i].nReg;
			break;
		}
	}

	if (storeReg == INVALID_REG) {
		emit_->MOVI2R(SCRATCH1, imm);
		storeReg = SCRATCH1;
	}
	emit_->STR(INDEX_UNSIGNED, storeReg, CTXREG, GetMipsRegOffset(mreg));
}

bool Arm64IRRegCache::TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) {
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

bool Arm64IRRegCache::TransferVecTo1(IRNativeReg nreg, IRNativeReg dest, IRReg first, int oldlanes) {
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
			fp_->DUP(32, FromNativeReg(freeReg), FromNativeReg(nreg), i);

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

	// Next, move the desired element into first place.
	if (mr[first].lane > 0) {
		fp_->DUP(32, FromNativeReg(dest), FromNativeReg(nreg), mr[first].lane);
	} else if (mr[first].lane <= 0 && dest != nreg) {
		fp_->DUP(32, FromNativeReg(dest), FromNativeReg(nreg), 0);
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

bool Arm64IRRegCache::Transfer1ToVec(IRNativeReg nreg, IRNativeReg dest, IRReg first, int lanes) {
	ARM64Reg destReg = FromNativeReg(dest);
	ARM64Reg cur[4]{};
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
			fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[3]));
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b0001) {
			// y = yw##, w = x###, w = xz##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[3]));
			fp_->LDR(32, INDEX_UNSIGNED, cur[3], CTXREG, GetMipsRegOffset(first + 0));
			fp_->ZIP1(32, EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[2]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b0010) {
			// x = xz##, z = y###, z = yw##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
			fp_->LDR(32, INDEX_UNSIGNED, cur[2], CTXREG, GetMipsRegOffset(first + 1));
			fp_->ZIP1(32, EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[3]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
		} else if (blendMask == 0b0011 && (first & 1) == 0) {
			// z = zw##, w = xy##, dest = xyzw.  Mixed lane sizes.
			fp_->ZIP1(32, EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[3]));
			fp_->LDR(64, INDEX_UNSIGNED, EncodeRegToDouble(cur[3]), CTXREG, GetMipsRegOffset(first + 0));
			fp_->ZIP1(64, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[2]));
		} else if (blendMask == 0b0100) {
			// y = yw##, w = z###, x = xz##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[3]));
			fp_->LDR(32, INDEX_UNSIGNED, cur[3], CTXREG, GetMipsRegOffset(first + 2));
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[3]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b0101 && (first & 3) == 0) {
			// y = yw##, w=x#z#, w = xz##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[3]));
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(cur[3]), CTXREG, GetMipsRegOffset(first));
			fp_->UZP1(32, EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[3]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b0110 && (first & 3) == 0) {
			if (destReg == cur[0]) {
				// w = wx##, dest = #yz#, dest = xyz#, dest = xyzw.
				fp_->ZIP1(32, EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[3]), EncodeRegToQuad(cur[0]));
				fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
				fp_->INS(32, EncodeRegToQuad(destReg), 0, EncodeRegToQuad(cur[3]), 1);
				fp_->INS(32, EncodeRegToQuad(destReg), 3, EncodeRegToQuad(cur[3]), 0);
			} else {
				// Assumes destReg may equal cur[3].
				// x = xw##, dest = #yz#, dest = xyz#, dest = xyzw.
				fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[3]));
				fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
				fp_->INS(32, EncodeRegToQuad(destReg), 0, EncodeRegToQuad(cur[0]), 0);
				fp_->INS(32, EncodeRegToQuad(destReg), 3, EncodeRegToQuad(cur[0]), 1);
			}
		} else if (blendMask == 0b0111 && (first & 3) == 0 && destReg != cur[3]) {
			// dest = xyz#, dest = xyzw.
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
			fp_->INS(32, EncodeRegToQuad(destReg), 3, EncodeRegToQuad(cur[3]), 0);
		} else if (blendMask == 0b1000) {
			// x = xz##, z = w###, y = yw##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
			fp_->LDR(32, INDEX_UNSIGNED, cur[2], CTXREG, GetMipsRegOffset(first + 3));
			fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[2]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b1001 && (first & 3) == 0) {
			if (destReg == cur[1]) {
				// w = zy##, dest = x##w, dest = xy#w, dest = xyzw.
				fp_->ZIP1(32, EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[1]));
				fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
				fp_->INS(32, EncodeRegToQuad(destReg), 1, EncodeRegToQuad(cur[2]), 1);
				fp_->INS(32, EncodeRegToQuad(destReg), 2, EncodeRegToQuad(cur[2]), 0);
			} else {
				// Assumes destReg may equal cur[2].
				// y = yz##, dest = x##w, dest = xy#w, dest = xyzw.
				fp_->ZIP1(32, EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[1]), EncodeRegToQuad(cur[2]));
				fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
				fp_->INS(32, EncodeRegToQuad(destReg), 1, EncodeRegToQuad(cur[1]), 0);
				fp_->INS(32, EncodeRegToQuad(destReg), 2, EncodeRegToQuad(cur[1]), 1);
			}
		} else if (blendMask == 0b1010 && (first & 3) == 0) {
			// x = xz##, z = #y#w, z=yw##, dest = xyzw.
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(cur[2]), CTXREG, GetMipsRegOffset(first));
			fp_->UZP2(32, EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[2]), EncodeRegToQuad(cur[2]));
			fp_->ZIP1(32, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[2]));
		} else if (blendMask == 0b1011 && (first & 3) == 0 && destReg != cur[2]) {
			// dest = xy#w, dest = xyzw.
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
			fp_->INS(32, EncodeRegToQuad(destReg), 2, EncodeRegToQuad(cur[2]), 0);
		} else if (blendMask == 0b1100 && (first & 1) == 0) {
			// x = xy##, y = zw##, dest = xyzw.  Mixed lane sizes.
			fp_->ZIP1(32, EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[1]));
			fp_->LDR(64, INDEX_UNSIGNED, EncodeRegToDouble(cur[1]), CTXREG, GetMipsRegOffset(first + 2));
			fp_->ZIP1(64, EncodeRegToQuad(destReg), EncodeRegToQuad(cur[0]), EncodeRegToQuad(cur[1]));
		} else if (blendMask == 0b1101 && (first & 3) == 0 && destReg != cur[1]) {
			// dest = x#zw, dest = xyzw.
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
			fp_->INS(32, EncodeRegToQuad(destReg), 1, EncodeRegToQuad(cur[1]), 0);
		} else if (blendMask == 0b1110 && (first & 3) == 0 && destReg != cur[0]) {
			// dest = #yzw, dest = xyzw.
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
			fp_->INS(32, EncodeRegToQuad(destReg), 0, EncodeRegToQuad(cur[0]), 0);
		} else if (blendMask == 0b1110 && (first & 3) == 0) {
			// If dest == cur[0] (which may be common), we need a temp...
			IRNativeReg freeReg = FindFreeReg(MIPSLoc::FREG, MIPSMap::INIT);
			// Very unfortunate.
			if (freeReg == INVALID_REG)
				return false;

			// free = x###, dest = #yzw, dest = xyzw.
			fp_->DUP(32, EncodeRegToQuad(FromNativeReg(freeReg)), EncodeRegToQuad(cur[0]), 0);
			fp_->LDR(128, INDEX_UNSIGNED, EncodeRegToQuad(destReg), CTXREG, GetMipsRegOffset(first));
			fp_->INS(32, EncodeRegToQuad(destReg), 0, EncodeRegToQuad(FromNativeReg(freeReg)), 0);
		} else {
			return false;
		}
	} else if (lanes == 2) {
		if (cur[0] != INVALID_REG && cur[1] != INVALID_REG) {
			fp_->ZIP1(32, EncodeRegToDouble(destReg), EncodeRegToDouble(cur[0]), EncodeRegToDouble(cur[1]));
		} else if (cur[0] == INVALID_REG && dest != nreg) {
			fp_->LDR(32, INDEX_UNSIGNED, destReg, CTXREG, GetMipsRegOffset(first + 0));
			fp_->INS(32, EncodeRegToDouble(destReg), 1, EncodeRegToDouble(cur[1]), 0);
		} else {
			IRNativeReg freeReg = FindFreeReg(MIPSLoc::FREG, MIPSMap::INIT);
			if (freeReg == INVALID_REG)
				return false;

			if (cur[0] == INVALID_REG) {
				fp_->LDR(32, INDEX_UNSIGNED, FromNativeReg(freeReg), CTXREG, GetMipsRegOffset(first + 0));
				fp_->ZIP1(32, EncodeRegToDouble(destReg), EncodeRegToDouble(FromNativeReg(freeReg)), EncodeRegToDouble(cur[1]));
			} else {
				fp_->LDR(32, INDEX_UNSIGNED, FromNativeReg(freeReg), CTXREG, GetMipsRegOffset(first + 1));
				fp_->ZIP1(32, EncodeRegToDouble(destReg), EncodeRegToDouble(cur[0]), EncodeRegToDouble(FromNativeReg(freeReg)));
			}
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

void Arm64IRRegCache::FlushAll(bool gprs, bool fprs) {
	// Note: make sure not to change the registers when flushing:
	// Branching code may expect the armreg to retain its value.

	auto needsFlush = [&](IRReg i) {
		if (mr[i].loc != MIPSLoc::MEM || mr[i].isStatic)
			return false;
		if (mr[i].nReg == -1 || !nr[mr[i].nReg].isDirty)
			return false;
		return true;
	};

	// Try to flush in pairs when possible.
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS - 1; ++i) {
		if (!needsFlush(i) || !needsFlush(i + 1))
			continue;
		// Ignore multilane regs.  Could handle with more smartness...
		if (mr[i].lane != -1 || mr[i + 1].lane != -1)
			continue;

		int offset = GetMipsRegOffset(i);

		// If both are imms, let's materialize a single reg and store.
		if (mr[i].loc == MIPSLoc::IMM && mr[i + 1].loc == MIPSLoc::IMM) {
			if ((i & 1) == 0) {
				uint64_t fullImm = ((uint64_t) mr[i + 1].imm << 32) | mr[i].imm;
				emit_->MOVI2R(SCRATCH1_64, fullImm);
				emit_->STR(INDEX_UNSIGNED, SCRATCH1_64, CTXREG, offset);
				DiscardReg(i);
				DiscardReg(i + 1);
				++i;
			}
			continue;
		}

		// Okay, two dirty regs in a row, in need of flushing.  Both GPRs?
		if (IsValidGPR(i) && IsValidGPR(i + 1) && offset <= 252) {
			auto setupForFlush = [&](ARM64Reg &ar, IRReg r) {
				if (mr[r].loc == MIPSLoc::IMM) {
					ar = TryMapTempImm(r);
					if (ar == INVALID_REG) {
						// Both cannot be imms, so this is safe.
						ar = SCRATCH1;
						emit_->MOVI2R(ar, mr[r].imm);
					}
				} else if (mr[r].loc == MIPSLoc::REG_AS_PTR) {
					AdjustNativeRegAsPtr(r, false);
					ar = FromNativeReg(mr[r].nReg);
				} else {
					_dbg_assert_(mr[r].loc == MIPSLoc::REG || mr[r].loc == MIPSLoc::REG_IMM);
					ar = FromNativeReg(mr[r].nReg);
				}
			};

			ARM64Reg armRegs[2]{ INVALID_REG, INVALID_REG };
			setupForFlush(armRegs[0], i);
			setupForFlush(armRegs[1], i + 1);

			emit_->STP(INDEX_SIGNED, armRegs[0], armRegs[1], CTXREG, offset);
			DiscardReg(i);
			DiscardReg(i + 1);
			++i;
			continue;
		}

		// Perhaps as FPRs?  Note: these must be single lane at this point.
		// TODO: Could use STP on quads etc. too, i.e. i & i + 4.
		if (i >= 32 && IsValidFPR(i - 32) && IsValidFPR(i + 1 - 32) && offset <= 252) {
			_dbg_assert_(mr[i].loc == MIPSLoc::FREG && mr[i + 1].loc == MIPSLoc::FREG);
			fp_->STP(32, INDEX_SIGNED, FromNativeReg(mr[i].nReg), FromNativeReg(mr[i + 1].nReg), CTXREG, offset);

			DiscardNativeReg(mr[i].nReg);
			DiscardNativeReg(mr[i + 1].nReg);

			++i;
			continue;
		}
	}

	// Flush all the rest that weren't done via STP.
	IRNativeRegCacheBase::FlushAll(gprs, fprs);
}

ARM64Reg Arm64IRRegCache::R(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return FromNativeReg(mr[mipsReg].nReg);
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

ARM64Reg Arm64IRRegCache::R64(IRReg mipsReg) {
	return EncodeRegTo64(R(mipsReg));
}

ARM64Reg Arm64IRRegCache::RPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return FromNativeReg64(mr[mipsReg].nReg);
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int r = mr[mipsReg].nReg;
		_dbg_assert_(nr[r].pointerified);
		if (nr[r].pointerified) {
			return FromNativeReg64(mr[mipsReg].nReg);
		} else {
			ERROR_LOG(Log::JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

ARM64Reg Arm64IRRegCache::F(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return FromNativeReg(mr[mipsReg + 32].nReg);
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

ARM64Reg Arm64IRRegCache::FD(IRReg mipsReg) {
	return EncodeRegToDouble(F(mipsReg));
}

ARM64Reg Arm64IRRegCache::FQ(IRReg mipsReg) {
	return EncodeRegToQuad(F(mipsReg));
}

IRNativeReg Arm64IRRegCache::GPRToNativeReg(ARM64Reg r) {
	_dbg_assert_msg_(r >= 0 && r < 0x40, "Not a GPR?");
	return (IRNativeReg)DecodeReg(r);
}

IRNativeReg Arm64IRRegCache::VFPToNativeReg(ARM64Reg r) {
	_dbg_assert_msg_(r >= 0x40 && r < 0xE0, "Not VFP?");
	return (IRNativeReg)(NUM_X_REGS + (int)DecodeReg(r));
}

ARM64Reg Arm64IRRegCache::FromNativeReg(IRNativeReg r) {
	if (r >= NUM_X_REGS)
		return EncodeRegToSingle((Arm64Gen::ARM64Reg)r);
	return (Arm64Gen::ARM64Reg)r;
}

ARM64Reg Arm64IRRegCache::FromNativeReg64(IRNativeReg r) {
	_dbg_assert_msg_(r >= 0 && r < NUM_X_REGS, "Not a GPR?");
	return EncodeRegTo64((Arm64Gen::ARM64Reg)r);
}

#endif
