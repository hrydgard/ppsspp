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

#pragma once

#include "ppsspp_config.h"

#include <initializer_list>
#if PPSSPP_ARCH(ARM)

#include "Common/ArmEmitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"

namespace ArmIRJitConstants {

const ArmGen::ARMReg DOWNCOUNTREG = ArmGen::R9;
const ArmGen::ARMReg CTXREG = ArmGen::R10;
const ArmGen::ARMReg MEMBASEREG = ArmGen::R11;
const ArmGen::ARMReg SCRATCH1 = ArmGen::R0;
const ArmGen::ARMReg SCRATCH2 = ArmGen::R1;
// S0-S3 (Q0) are never allocated.
const ArmGen::ARMReg SCRATCHF1 = ArmGen::S0;
const ArmGen::ARMReg SCRATCHF2 = ArmGen::S1;
const ArmGen::ARMReg SCRATCHF3 = ArmGen::S2;
const ArmGen::ARMReg SCRATCHF4 = ArmGen::S3;

} // namespace ArmIRJitConstants

// Native regs are numbered as ARMReg: R0-R15, then S0-S31. Floats are only mapped as single lanes
// (mapFPUSIMD is off), so a vector op sees its lanes in separate S regs.
class ArmIRRegCache : public IRNativeRegCacheBase {
public:
	ArmIRRegCache(MIPSComp::JitOptions *jo);

	void Init(ArmGen::ARMXEmitter *emitter);

	// May fail and return INVALID_REG if it needs flushing.
	ArmGen::ARMReg TryMapTempImm(IRReg reg);

	// Returns an ARM register containing the requested MIPS register.
	ArmGen::ARMReg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	ArmGen::ARMReg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);

	ArmGen::ARMReg MapWithFPRTemp(const IRInst &inst);

	void FlushBeforeCall();
	// Flushes whatever guest state these registers hold, for code that clobbers them.
	void FlushHostRegs(std::initializer_list<ArmGen::ARMReg> regs);
	void FlushAll(bool gprs = true, bool fprs = true) override;

	ArmGen::ARMReg GetAndLockTempGPR();
	ArmGen::ARMReg GetAndLockTempFPR();

	ArmGen::ARMReg R(IRReg preg);
	ArmGen::ARMReg F(IRReg preg);

protected:
	const int *GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const override;
	void AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) override;

	bool IsNativeRegCompatible(IRNativeReg nreg, MIPSLoc type, MIPSMap flags, int lanes) override;
	void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) override;
	void StoreRegValue(IRReg mreg, uint32_t imm) override;

private:
	ArmGen::ARMXEmitter *emit_ = nullptr;

	enum {
		NUM_GPRS = 16,
		NUM_FPRS = 32,
	};
};

#endif
