// Copyright (c) 2012- PPSSPP Project.

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
#if PPSSPP_ARCH(ARM)

#ifndef offsetof
#include <cstddef>
#endif

#include "Common/LogReporting.h"
#include "Core/MemMap.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"
#include "Core/MIPS/JitCommon/JitState.h"

using namespace ArmGen;
using namespace ArmIRJitConstants;

ArmIRRegCache::ArmIRRegCache(MIPSComp::JitOptions *jo)
	: IRNativeRegCacheBase(jo) {
	config_.totalNativeRegs = NUM_GPRS + NUM_FPRS;
	config_.mapFPUSIMD = false;
	config_.mapUseVRegs = false;
}

void ArmIRRegCache::Init(ARMXEmitter *emitter) {
	emit_ = emitter;
}

const int *ArmIRRegCache::GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const {
	// ARMReg values are used directly as native reg numbers.
	base = 0;
	if (type == MIPSLoc::REG) {
		// R0/R1 are scratch, R9-R11 are fixed (see ArmIRJitConstants), R13/R15 are SP/PC.
		// The callee-saved ones come first, since they survive calls.
		static const int allocationOrder[] = {
			R4, R5, R6, R7, R8, R2, R3, R12, R14,
		};
		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else if (type == MIPSLoc::FREG) {
		// S16-S31 (D8-D15) are callee-saved. S0-S3 are scratch.
		static const int allocationOrder[] = {
			S16, S17, S18, S19, S20, S21, S22, S23,
			S24, S25, S26, S27, S28, S29, S30, S31,
			S4, S5, S6, S7, S8, S9, S10, S11,
			S12, S13, S14, S15,
		};
		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else {
		_assert_msg_(false, "Allocation order not yet implemented");
		count = 0;
		return nullptr;
	}
}

void ArmIRRegCache::FlushHostRegs(std::initializer_list<ARMReg> regs) {
	for (ARMReg r : regs)
		FlushNativeReg((IRNativeReg)r);
}

void ArmIRRegCache::FlushBeforeCall() {
	// These are not preserved by calls.
	for (int r : { R2, R3, R12, R14 })
		FlushNativeReg((IRNativeReg)r);
	for (int r = S4; r <= S15; ++r)
		FlushNativeReg((IRNativeReg)r);
}

ARMReg ArmIRRegCache::TryMapTempImm(IRReg r) {
	_dbg_assert_(IsValidGPR(r));

	// If already mapped, no need for a temporary.
	if (IsGPRMapped(r)) {
		return R(r);
	}

	if (mr[r].loc == MIPSLoc::IMM) {
		// Try our luck - check for an exact match in another reg.
		for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				return (ARMReg)mr[i].nReg;
			}
		}
	}

	return INVALID_REG;
}

ARMReg ArmIRRegCache::GetAndLockTempGPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::REG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return (ARMReg)reg;
}

ARMReg ArmIRRegCache::GetAndLockTempFPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::FREG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return (ARMReg)reg;
}

ARMReg ArmIRRegCache::MapWithFPRTemp(const IRInst &inst) {
	return (ARMReg)MapWithTemp(inst, MIPSLoc::FREG);
}

ARMReg ArmIRRegCache::MapGPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg));
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return (ARMReg)nreg;
}

ARMReg ArmIRRegCache::MapFPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return (ARMReg)nreg;
	return INVALID_REG;
}

void ArmIRRegCache::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	_assert_(nreg >= 0 && nreg < NUM_GPRS);
	ARMReg r = (ARMReg)nreg;
	// 32-bit builds always mask PSP addresses, which destroys the value.
	if (state) {
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->BIC(r, r, Operand2(0xC0, 4));
		emit_->ADD(r, r, MEMBASEREG);
	} else {
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->SUB(r, r, MEMBASEREG);
	}
}

bool ArmIRRegCache::IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) {
	// Every reg holds one lane.
	return lanes == 1 && IRNativeRegCacheBase::IsNativeRegCompatible(nreg, type, flags, lanes);
}

void ArmIRRegCache::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	_dbg_assert_(first != MIPS_REG_ZERO);
	_assert_msg_(lanes == 1, "Can't load %d lanes", lanes);
	ARMReg r = (ARMReg)nreg;
	if (nreg < NUM_GPRS) {
		emit_->LDR(r, CTXREG, GetMipsRegOffset(first));
	} else {
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot load this type: %d", (int)mr[first].loc);
		emit_->VLDR(r, CTXREG, GetMipsRegOffset(first));
	}
}

void ArmIRRegCache::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	_dbg_assert_(first != MIPS_REG_ZERO);
	_assert_msg_(lanes == 1, "Can't store %d lanes", lanes);
	ARMReg r = (ARMReg)nreg;
	if (nreg < NUM_GPRS) {
		_assert_(mr[first].loc == MIPSLoc::REG || mr[first].loc == MIPSLoc::REG_IMM);
		emit_->STR(r, CTXREG, GetMipsRegOffset(first));
	} else {
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		emit_->VSTR(r, CTXREG, GetMipsRegOffset(first));
	}
}

void ArmIRRegCache::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	_dbg_assert_(nreg >= 0 && nreg < NUM_GPRS);
	emit_->MOVI2R((ARMReg)nreg, imm);
}

void ArmIRRegCache::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_(IsValidGPRNoZero(mreg));
	// Could we get lucky?  Check for an exact match in another reg.
	ARMReg storeReg = INVALID_REG;
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS; ++i) {
		if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == imm) {
			storeReg = (ARMReg)mr[i].nReg;
			break;
		}
	}

	if (storeReg == INVALID_REG) {
		emit_->MOVI2R(SCRATCH1, imm);
		storeReg = SCRATCH1;
	}
	emit_->STR(storeReg, CTXREG, GetMipsRegOffset(mreg));
}

void ArmIRRegCache::FlushAll(bool gprs, bool fprs) {
	// Note: make sure not to change the registers when flushing:
	// Branching code may expect the armreg to retain its value.
	IRNativeRegCacheBase::FlushAll(gprs, fprs);
}

ARMReg ArmIRRegCache::R(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return (ARMReg)mr[mipsReg].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

ARMReg ArmIRRegCache::F(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return (ARMReg)mr[mipsReg + 32].nReg;
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in arm reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

#endif
