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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"

namespace X64IRJitConstants {

#if PPSSPP_ARCH(AMD64)
const Gen::X64Reg MEMBASEREG = Gen::RBX;
const Gen::X64Reg CTXREG = Gen::R14;
const Gen::X64Reg JITBASEREG = Gen::R15;
#else
const Gen::X64Reg CTXREG = Gen::EBP;
#endif
const Gen::X64Reg SCRATCH1 = Gen::EAX;

} // namespace X64IRJitConstants

class X64IRRegCache : public IRNativeRegCacheBase {
public:
	X64IRRegCache(MIPSComp::JitOptions *jo);

	void Init(Gen::XEmitter *emitter);

	// May fail and return INVALID_REG if it needs flushing.
	Gen::X64Reg TryMapTempImm(IRReg);

	// Returns an RV register containing the requested MIPS register.
	Gen::X64Reg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Gen::X64Reg MapGPRAsPointer(IRReg reg);
	Gen::X64Reg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);

	Gen::X64Reg MapWithFPRTemp(IRInst &inst);

	void FlushBeforeCall();

	Gen::X64Reg GetAndLockTempR();

	Gen::X64Reg R(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	Gen::X64Reg RPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer
	Gen::X64Reg F(IRReg preg);

protected:
	const int *GetAllocationOrder(MIPSLoc type, int &count, int &base) const override;
	void AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) override;

	void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) override;
	void StoreRegValue(IRReg mreg, uint32_t imm) override;

private:
	IRNativeReg GPRToNativeReg(Gen::X64Reg r) {
		return (IRNativeReg)r;
	}
	IRNativeReg XMMToNativeReg(Gen::X64Reg r) {
		return (IRNativeReg)(r + NUM_X_REGS);
	}
	Gen::X64Reg FromNativeReg(IRNativeReg r) {
		if (r >= NUM_X_REGS)
			return (Gen::X64Reg)(Gen::XMM0 + (r - NUM_X_REGS));
		return (Gen::X64Reg)(Gen::RAX + r);
	}

	Gen::XEmitter *emit_ = nullptr;

	enum {
#if PPSSPP_ARCH(AMD64)
		NUM_X_REGS = 16,
		NUM_X_FREGS = 16,
#elif PPSSPP_ARCH(X86)
		NUM_X_REGS = 8,
		NUM_X_FREGS = 8,
#endif
	};
};

#endif
