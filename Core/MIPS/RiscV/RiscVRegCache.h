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

#include "Common/RiscVEmitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"

namespace RiscVJitConstants {

// Note: we don't support 32-bit or 128-bit CPUs currently.
constexpr int XLEN = 64;

const RiscVGen::RiscVReg DOWNCOUNTREG = RiscVGen::X24;
const RiscVGen::RiscVReg JITBASEREG = RiscVGen::X25;
const RiscVGen::RiscVReg CTXREG = RiscVGen::X26;
const RiscVGen::RiscVReg MEMBASEREG = RiscVGen::X27;
// TODO: Experiment.  X7-X13 are compressed regs.  X8/X9 are saved so nice for static alloc, though.
const RiscVGen::RiscVReg SCRATCH1 = RiscVGen::X10;
const RiscVGen::RiscVReg SCRATCH2 = RiscVGen::X11;

} // namespace RiscVJitConstants

class RiscVRegCache : public IRNativeRegCacheBase {
public:
	RiscVRegCache(MIPSComp::JitOptions *jo);

	void Init(RiscVGen::RiscVEmitter *emitter);

	// May fail and return INVALID_REG if it needs flushing.
	RiscVGen::RiscVReg TryMapTempImm(IRReg reg);

	// Returns an RV register containing the requested MIPS register.
	RiscVGen::RiscVReg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	RiscVGen::RiscVReg MapGPRAsPointer(IRReg reg);
	RiscVGen::RiscVReg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);

	RiscVGen::RiscVReg MapWithFPRTemp(const IRInst &inst);

	bool IsNormalized32(IRReg reg);

	// Copies to another reg if specified, otherwise same reg.
	RiscVGen::RiscVReg Normalize32(IRReg reg, RiscVGen::RiscVReg destReg = RiscVGen::INVALID_REG);

	void FlushBeforeCall();

	RiscVGen::RiscVReg GetAndLockTempGPR();

	RiscVGen::RiscVReg R(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	RiscVGen::RiscVReg RPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer
	RiscVGen::RiscVReg F(IRReg preg);

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

private:
	RiscVGen::RiscVEmitter *emit_ = nullptr;

	enum {
		NUM_RVGPR = 32,
		NUM_RVFPR = 32,
		NUM_RVVPR = 32,
	};
};
