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

#pragma once

#include "Common/LoongArch64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"

namespace LoongArch64JitConstants {

const LoongArch64Gen::LoongArch64Reg DOWNCOUNTREG = LoongArch64Gen::R28;
const LoongArch64Gen::LoongArch64Reg JITBASEREG = LoongArch64Gen::R29;
const LoongArch64Gen::LoongArch64Reg CTXREG = LoongArch64Gen::R30;
const LoongArch64Gen::LoongArch64Reg MEMBASEREG = LoongArch64Gen::R31;
const LoongArch64Gen::LoongArch64Reg SCRATCH1 = LoongArch64Gen::R12;
const LoongArch64Gen::LoongArch64Reg SCRATCH2 = LoongArch64Gen::R13;
const LoongArch64Gen::LoongArch64Reg SCRATCHF1 = LoongArch64Gen::F8;
const LoongArch64Gen::LoongArch64Reg SCRATCHF2 = LoongArch64Gen::F9;

} // namespace LoongArch64JitConstants

class LoongArch64RegCache : public IRNativeRegCacheBase {
public:
	LoongArch64RegCache(MIPSComp::JitOptions *jo);

	void Init(LoongArch64Gen::LoongArch64Emitter *emitter);

	// May fail and return INVALID_REG if it needs flushing.
	LoongArch64Gen::LoongArch64Reg TryMapTempImm(IRReg reg);

	// Returns an LA register containing the requested MIPS register.
	LoongArch64Gen::LoongArch64Reg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	LoongArch64Gen::LoongArch64Reg MapGPRAsPointer(IRReg reg);
	LoongArch64Gen::LoongArch64Reg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	LoongArch64Gen::LoongArch64Reg MapVec4(IRReg first, MIPSMap mapFlags = MIPSMap::INIT);

	LoongArch64Gen::LoongArch64Reg MapWithFPRTemp(const IRInst &inst);

	bool IsNormalized32(IRReg reg);

	// Copies to another reg if specified, otherwise same reg.
	LoongArch64Gen::LoongArch64Reg Normalize32(IRReg reg, LoongArch64Gen::LoongArch64Reg destReg = LoongArch64Gen::INVALID_REG);

	void FlushBeforeCall();

	LoongArch64Gen::LoongArch64Reg GetAndLockTempGPR();

	LoongArch64Gen::LoongArch64Reg R(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	LoongArch64Gen::LoongArch64Reg RPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer
	LoongArch64Gen::LoongArch64Reg F(IRReg preg);
	LoongArch64Gen::LoongArch64Reg V(IRReg preg);

	// These are called once on startup to generate functions, that you should then call.
	void EmitLoadStaticRegisters();
	void EmitSaveStaticRegisters();

protected:
	void SetupInitialRegs() override;
	const StaticAllocation *GetStaticAllocations(int &count) const override;
	const int *GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const override;
	void AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) override;

	bool IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) override;
	void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) override;
	void StoreRegValue(IRReg mreg, uint32_t imm) override;
	bool TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) override;

private:
	bool TransferVecTo1(IRNativeReg nreg, IRNativeReg dest, IRReg first, int oldlanes);
	bool Transfer1ToVec(IRNativeReg nreg, IRNativeReg dest, IRReg first, int lanes);

	LoongArch64Gen::LoongArch64Reg FromNativeReg(IRNativeReg r) {
		if (r >= NUM_LAGPR)
			return (LoongArch64Gen::LoongArch64Reg)(LoongArch64Gen::V0 + (r - NUM_LAGPR));
		return (LoongArch64Gen::LoongArch64Reg)(LoongArch64Gen::R0 + r);
	}

	LoongArch64Gen::LoongArch64Emitter *emit_ = nullptr;

	enum {
		NUM_LAGPR = 32,
		NUM_LAFPR = 32,
	};
};
