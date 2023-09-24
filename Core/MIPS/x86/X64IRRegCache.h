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
// Note: this is actually offset from the base.
const Gen::X64Reg JITBASEREG = Gen::R15;
const Gen::X64Reg DOWNCOUNTREG = Gen::R15;
#else
const Gen::X64Reg CTXREG = Gen::EBP;
const Gen::X64Reg DOWNCOUNTREG = Gen::INVALID_REG;
#endif
const Gen::X64Reg SCRATCH1 = Gen::EAX;

static constexpr auto downcountOffset = offsetof(MIPSState, downcount) - 128;
static constexpr auto tempOffset = offsetof(MIPSState, temp) - 128;
static constexpr auto fcr31Offset = offsetof(MIPSState, fcr31) - 128;
static constexpr auto pcOffset = offsetof(MIPSState, pc) - 128;
static constexpr auto mxcsrTempOffset = offsetof(MIPSState, mxcsrTemp) - 128;

enum class X64Map : uint8_t {
	NONE = 0,
	// On 32-bit: EAX, EBX, ECX, EDX
	LOW_SUBREG = 0x10,
	// EDX/RDX for DIV/MUL/similar.
	HIGH_DATA = 0x20,
	// ECX/RCX only, for shifts.
	SHIFT = 0x30,
	// XMM0 for BLENDVPS, funcs.
	XMM0 = 0x40,
	MASK = 0xF0,
};
static inline MIPSMap operator |(const MIPSMap &lhs, const X64Map &rhs) {
	return MIPSMap((uint8_t)lhs | (uint8_t)rhs);
}
static inline X64Map operator |(const X64Map &lhs, const X64Map &rhs) {
	return X64Map((uint8_t)lhs | (uint8_t)rhs);
}
static inline X64Map operator &(const MIPSMap &lhs, const X64Map &rhs) {
	return X64Map((uint8_t)lhs & (uint8_t)rhs);
}
static inline X64Map operator &(const X64Map &lhs, const X64Map &rhs) {
	return X64Map((uint8_t)lhs & (uint8_t)rhs);
}

} // namespace X64IRJitConstants

class X64IRRegCache : public IRNativeRegCacheBase {
public:
	X64IRRegCache(MIPSComp::JitOptions *jo);

	void Init(Gen::XEmitter *emitter);

	// May fail and return INVALID_REG if it needs flushing.
	Gen::X64Reg TryMapTempImm(IRReg reg, X64IRJitConstants::X64Map flags = X64IRJitConstants::X64Map::NONE);

	// Returns an X64 register containing the requested MIPS register.
	Gen::X64Reg MapGPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Gen::X64Reg MapGPR2(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Gen::X64Reg MapGPRAsPointer(IRReg reg);
	Gen::X64Reg MapFPR(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);
	Gen::X64Reg MapVec4(IRReg first, MIPSMap mapFlags = MIPSMap::INIT);

	Gen::X64Reg MapWithFPRTemp(const IRInst &inst);

	void MapWithFlags(IRInst inst, X64IRJitConstants::X64Map destFlags, X64IRJitConstants::X64Map src1Flags = X64IRJitConstants::X64Map::NONE, X64IRJitConstants::X64Map src2Flags = X64IRJitConstants::X64Map::NONE);

	// Note: may change the high lanes of single-register XMMs.
	void FlushAll(bool gprs = true, bool fprs = true) override;
	void FlushBeforeCall();

	Gen::X64Reg GetAndLockTempGPR();
	Gen::X64Reg GetAndLockTempFPR();
	void ReserveAndLockXGPR(Gen::X64Reg r);

	Gen::OpArg R(IRReg preg);
	Gen::OpArg RPtr(IRReg preg);
	Gen::OpArg F(IRReg preg);
	Gen::X64Reg RX(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	Gen::X64Reg RXPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer
	Gen::X64Reg FX(IRReg preg);

	static bool HasLowSubregister(Gen::X64Reg reg);

protected:
	const int *GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const override;
	void AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) override;

	void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) override;
	void StoreRegValue(IRReg mreg, uint32_t imm) override;
	bool TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) override;

private:
	bool TransferVecTo1(IRNativeReg nreg, IRNativeReg dest, IRReg first, int oldlanes);
	bool Transfer1ToVec(IRNativeReg nreg, IRNativeReg dest, IRReg first, int lanes);

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
