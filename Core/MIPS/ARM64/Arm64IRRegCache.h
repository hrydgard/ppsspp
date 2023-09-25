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

#include "ppsspp_config.h"
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include "Common/Arm64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"

namespace Arm64IRJitConstants {

const Arm64Gen::ARM64Reg DOWNCOUNTREG = Arm64Gen::W25;
// Note: this is actually offset from the base.
const Arm64Gen::ARM64Reg JITBASEREG = Arm64Gen::X26;
const Arm64Gen::ARM64Reg CTXREG = Arm64Gen::X27;
const Arm64Gen::ARM64Reg MEMBASEREG = Arm64Gen::X28;
const Arm64Gen::ARM64Reg SCRATCH1_64 = Arm64Gen::X16;
const Arm64Gen::ARM64Reg SCRATCH2_64 = Arm64Gen::X17;
const Arm64Gen::ARM64Reg SCRATCH1 = Arm64Gen::W16;
const Arm64Gen::ARM64Reg SCRATCH2 = Arm64Gen::W17;
// TODO: How many do we actually need?
const Arm64Gen::ARM64Reg SCRATCHF1 = Arm64Gen::S0;
const Arm64Gen::ARM64Reg SCRATCHF2 = Arm64Gen::S1;
const Arm64Gen::ARM64Reg SCRATCHF3 = Arm64Gen::S2;
const Arm64Gen::ARM64Reg SCRATCHF4 = Arm64Gen::S3;

} // namespace X64IRJitConstants

class Arm64IRRegCache : public IRNativeRegCacheBase {
public:
	Arm64IRRegCache(MIPSComp::JitOptions *jo);

	void Init(Arm64Gen::ARM64XEmitter *emitter, Arm64Gen::ARM64FloatEmitter *fp);

	// May fail and return INVALID_REG if it needs flushing.
	Arm64Gen::ARM64Reg TryMapTempImm(IRReg reg);

	// Returns an arm64 register containing the requested MIPS register.
	Arm64Gen::ARM64Reg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Arm64Gen::ARM64Reg MapGPR2(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Arm64Gen::ARM64Reg MapGPRAsPointer(IRReg reg);
	Arm64Gen::ARM64Reg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Arm64Gen::ARM64Reg MapVec2(IRReg first, MIPSMap mapFlags = MIPSMap::INIT);
	Arm64Gen::ARM64Reg MapVec4(IRReg first, MIPSMap mapFlags = MIPSMap::INIT);

	Arm64Gen::ARM64Reg MapWithFPRTemp(const IRInst &inst);

	void FlushBeforeCall();
	void FlushAll(bool gprs = true, bool fprs = true) override;

	Arm64Gen::ARM64Reg GetAndLockTempGPR();
	Arm64Gen::ARM64Reg GetAndLockTempFPR();

	Arm64Gen::ARM64Reg R(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	Arm64Gen::ARM64Reg R64(IRReg preg);
	Arm64Gen::ARM64Reg RPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer
	Arm64Gen::ARM64Reg F(IRReg preg);
	Arm64Gen::ARM64Reg FD(IRReg preg);
	Arm64Gen::ARM64Reg FQ(IRReg preg);

	// These are called once on startup to generate functions, that you should then call.
	void EmitLoadStaticRegisters();
	void EmitSaveStaticRegisters();

protected:
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

	IRNativeReg GPRToNativeReg(Arm64Gen::ARM64Reg r);
	IRNativeReg VFPToNativeReg(Arm64Gen::ARM64Reg r);
	Arm64Gen::ARM64Reg FromNativeReg(IRNativeReg r);
	Arm64Gen::ARM64Reg FromNativeReg64(IRNativeReg r);

	Arm64Gen::ARM64XEmitter *emit_ = nullptr;
	Arm64Gen::ARM64FloatEmitter *fp_ = nullptr;

	enum {
		NUM_X_REGS = 32,
		NUM_X_FREGS = 32,
	};
};

#endif
