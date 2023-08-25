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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#ifndef offsetof
#include <cstddef>
#endif

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRAnalysis.h"
#include "Core/MIPS/x86/X64IRRegCache.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Reporting.h"

using namespace Gen;
using namespace X64IRJitConstants;

X64IRRegCache::X64IRRegCache(MIPSComp::JitOptions *jo)
	: IRNativeRegCacheBase(jo) {
	config_.totalNativeRegs = NUM_X_REGS + NUM_X_FREGS;
	config_.mapFPUSIMD = true;
	// XMM regs are used for both FPU and Vec, so we don't need VREGs.
	config_.mapUseVRegs = false;
}

void X64IRRegCache::Init(XEmitter *emitter) {
	emit_ = emitter;
}

const int *X64IRRegCache::GetAllocationOrder(MIPSLoc type, MIPSMap flags, int &count, int &base) const {
	if (type == MIPSLoc::REG) {
		base = RAX;

		static const int allocationOrder[] = {
#if PPSSPP_ARCH(AMD64)
#ifdef _WIN32
			RSI, RDI, R8, R9, R10, R11, R12, R13, RDX, RCX,
#else
			RBP, R8, R9, R10, R11, R12, R13, RDX, RCX,
#endif
			// Intentionally last.
			R15,
#elif PPSSPP_ARCH(X86)
			ESI, EDI, EDX, EBX, ECX,
#endif
		};

		if ((flags & X64Map::MASK) == X64Map::SHIFT) {
			// It's a single option for shifts.
			static const int shiftReg[] = { ECX };
			count = 1;
			return shiftReg;
		}
		if ((flags & X64Map::MASK) == X64Map::HIGH_DATA) {
			// It's a single option for shifts.
			static const int shiftReg[] = { EDX };
			count = 1;
			return shiftReg;
		}
#if PPSSPP_ARCH(X86)
		if ((flags & X64Map::MASK) == X64Map::LOW_SUBREG) {
			static const int lowSubRegAllocationOrder[] = {
				EDX, EBX, ECX,
			};
			count = ARRAY_SIZE(lowSubRegAllocationOrder);
			return lowSubRegAllocationOrder;
		}
#else
		if (jo_->reserveR15ForAsm) {
			count = ARRAY_SIZE(allocationOrder) - 1;
			return allocationOrder;
		}
#endif
		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else if (type == MIPSLoc::FREG) {
		base = -NUM_X_REGS;

		// TODO: Might have to change this if we can't live without dedicated temps.
		static const int allocationOrder[] = {
#if PPSSPP_ARCH(AMD64)
		XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, XMM1, XMM2, XMM3, XMM4, XMM5, XMM0,
#elif PPSSPP_ARCH(X86)
		XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, XMM0,
#endif
		};

		count = ARRAY_SIZE(allocationOrder);
		return allocationOrder;
	} else {
		_assert_msg_(false, "Allocation order not yet implemented");
		count = 0;
		return nullptr;
	}
}

void X64IRRegCache::FlushBeforeCall() {
	// These registers are not preserved by function calls.
#if PPSSPP_ARCH(AMD64)
#ifdef _WIN32
	FlushNativeReg(GPRToNativeReg(RCX));
	FlushNativeReg(GPRToNativeReg(RDX));
	FlushNativeReg(GPRToNativeReg(R8));
	FlushNativeReg(GPRToNativeReg(R9));
	FlushNativeReg(GPRToNativeReg(R10));
	FlushNativeReg(GPRToNativeReg(R11));
	for (int i = 0; i < 6; ++i)
		FlushNativeReg(NUM_X_REGS + i);
#else
	FlushNativeReg(GPRToNativeReg(R8));
	FlushNativeReg(GPRToNativeReg(R9));
	FlushNativeReg(GPRToNativeReg(R10));
	FlushNativeReg(GPRToNativeReg(R11));
	for (int i = 0; i < NUM_X_FREGS; ++i)
		FlushNativeReg(NUM_X_REGS + i);
#endif
#elif PPSSPP_ARCH(X86)
	FlushNativeReg(GPRToNativeReg(ECX));
	FlushNativeReg(GPRToNativeReg(EDX));
	for (int i = 0; i < NUM_X_FREGS; ++i)
		FlushNativeReg(NUM_X_REGS + i);
#endif
}

X64Reg X64IRRegCache::TryMapTempImm(IRReg r, X64Map flags) {
	_dbg_assert_(IsValidGPR(r));

	auto canUseReg = [flags](X64Reg r) {
		switch (flags & X64Map::MASK) {
		case X64Map::NONE:
			return true;
		case X64Map::LOW_SUBREG:
			return HasLowSubregister(r);
		case X64Map::SHIFT:
			return r == RCX;
		case X64Map::HIGH_DATA:
			return r == RCX;
		default:
			_assert_msg_(false, "Unexpected flags");
		}
		return false;
	};

	// If already mapped, no need for a temporary.
	if (IsGPRMapped(r)) {
		if (canUseReg(RX(r)))
			return RX(r);
	}

	if (mr[r].loc == MIPSLoc::IMM) {
		// Try our luck - check for an exact match in another xreg.
		for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
			if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == mr[r].imm) {
				// Awesome, let's just use this reg.
				if (canUseReg(FromNativeReg(mr[i].nReg)))
					return FromNativeReg(mr[i].nReg);
			}
		}
	}

	return INVALID_REG;
}

X64Reg X64IRRegCache::GetAndLockTempR() {
	X64Reg reg = FromNativeReg(AllocateReg(MIPSLoc::REG, MIPSMap::INIT));
	if (reg != INVALID_REG) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return reg;
}

void X64IRRegCache::ReserveAndLockXGPR(Gen::X64Reg r) {
	IRNativeReg nreg = GPRToNativeReg(r);
	if (nr[nreg].mipsReg != -1)
		FlushNativeReg(nreg);
	nr[r].tempLockIRIndex = irIndex_;
}

X64Reg X64IRRegCache::MapWithFPRTemp(IRInst &inst) {
	return FromNativeReg(MapWithTemp(inst, MIPSLoc::FREG));
}

void X64IRRegCache::MapWithFlags(IRInst inst, X64Map destFlags, X64Map src1Flags, X64Map src2Flags) {
	Mapping mapping[3];
	MappingFromInst(inst, mapping);

	mapping[0].flags = mapping[0].flags | destFlags;
	mapping[1].flags = mapping[1].flags | src1Flags;
	mapping[2].flags = mapping[2].flags | src2Flags;

	auto flushReg = [&](IRNativeReg nreg) {
		for (int i = 0; i < 3; ++i) {
			if (mapping[i].reg == nr[nreg].mipsReg && (mapping[i].flags & MIPSMap::NOINIT) == MIPSMap::NOINIT) {
				DiscardNativeReg(nreg);
				return;
			}
		}

		FlushNativeReg(nreg);
	};

	// If there are any special rules, we might need to spill.
	for (int i = 0; i < 3; ++i) {
		switch (mapping[i].flags & X64Map::MASK) {
		case X64Map::SHIFT:
			if (nr[RCX].mipsReg != mapping[i].reg)
				flushReg(RCX);
			break;

		case X64Map::HIGH_DATA:
			if (nr[RDX].mipsReg != mapping[i].reg)
				flushReg(RDX);
			break;

		default:
			break;
		}
	}

	ApplyMapping(mapping, 3);
	CleanupMapping(mapping, 3);
}

X64Reg X64IRRegCache::MapGPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg));

	// Okay, not mapped, so we need to allocate an RV register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return FromNativeReg(nreg);
}

X64Reg X64IRRegCache::MapGPRAsPointer(IRReg reg) {
	return FromNativeReg(MapNativeRegAsPointer(reg));
}

X64Reg X64IRRegCache::MapFPR(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::MEM || mr[mipsReg + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, mipsReg + 32, 1, mapFlags);
	if (nreg != -1)
		return FromNativeReg(nreg);
	return INVALID_REG;
}

X64Reg X64IRRegCache::MapVec4(IRReg first, MIPSMap mapFlags) {
	_dbg_assert_(IsValidFPR(first));
	_dbg_assert_((first & 3) == 0);
	_dbg_assert_(mr[first + 32].loc == MIPSLoc::MEM || mr[first + 32].loc == MIPSLoc::FREG);

	IRNativeReg nreg = MapNativeReg(MIPSLoc::FREG, first + 32, 4, mapFlags);
	if (nreg != -1)
		return FromNativeReg(nreg);
	return INVALID_REG;
}

void X64IRRegCache::AdjustNativeRegAsPtr(IRNativeReg nreg, bool state) {
	_assert_(nreg >= 0 && nreg < NUM_X_REGS);
	X64Reg r = FromNativeReg(nreg);
	if (state) {
#if defined(MASKED_PSP_MEMORY)
		// This destroys the value...
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->AND(PTRBITS, ::R(r), Imm32(Memory::MEMVIEW32_MASK));
		emit_->ADD(PTRBITS, ::R(r), ImmPtr(Memory::base));
#else
		// Clear the top bits to be safe.
		emit_->ADD(PTRBITS, ::R(r), ::R(MEMBASEREG));
#endif
	} else {
#if defined(MASKED_PSP_MEMORY)
		_dbg_assert_(!nr[nreg].isDirty);
		emit_->SUB(PTRBITS, ::R(r), ImmPtr(Memory::base));
#else
		emit_->SUB(PTRBITS, ::R(r), ::R(MEMBASEREG));
#endif
	}
}

void X64IRRegCache::LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	X64Reg r = FromNativeReg(nreg);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (nreg < NUM_X_REGS) {
		// Multilane not yet supported.
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		if (lanes == 1)
			emit_->MOV(32, ::R(r), MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
#if PPSSPP_ARCH(AMD64)
		else if (lanes == 2)
			emit_->MOV(64, ::R(r), MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
#endif
		else
			_assert_(false);
	} else {
		_dbg_assert_(nreg < NUM_X_REGS + NUM_X_FREGS);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot load this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			emit_->MOVSS(r, MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
		else if (lanes == 2)
			emit_->MOVLPS(r, MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
		else if (lanes == 4)
			emit_->MOVUPS(r, MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
		else
			_assert_(false);
	}
}

void X64IRRegCache::StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) {
	X64Reg r = FromNativeReg(nreg);
	_dbg_assert_(first != MIPS_REG_ZERO);
	if (nreg < NUM_X_REGS) {
		// Multilane not yet supported.
		_assert_(lanes == 1 || (lanes == 2 && first == IRREG_LO));
		_assert_(mr[first].loc == MIPSLoc::REG || mr[first].loc == MIPSLoc::REG_IMM);
		if (lanes == 1)
			emit_->MOV(32, MDisp(CTXREG, -128 + GetMipsRegOffset(first)), ::R(r));
#if PPSSPP_ARCH(AMD64)
		else if (lanes == 2)
			emit_->MOV(64, MDisp(CTXREG, -128 + GetMipsRegOffset(first)), ::R(r));
#endif
		else
			_assert_(false);
	} else {
		_dbg_assert_(nreg < NUM_X_REGS + NUM_X_FREGS);
		_assert_msg_(mr[first].loc == MIPSLoc::FREG, "Cannot store this type: %d", (int)mr[first].loc);
		if (lanes == 1)
			emit_->MOVSS(MDisp(CTXREG, -128 + GetMipsRegOffset(first)), r);
		else if (lanes == 2)
			emit_->MOVLPS(MDisp(CTXREG, -128 + GetMipsRegOffset(first)), r);
		else if (lanes == 4)
			emit_->MOVUPS(MDisp(CTXREG, -128 + GetMipsRegOffset(first)), r);
		else
			_assert_(false);
	}
}

void X64IRRegCache::SetNativeRegValue(IRNativeReg nreg, uint32_t imm) {
	X64Reg r = FromNativeReg(nreg);
	_dbg_assert_(nreg >= 0 && nreg < NUM_X_REGS);
	emit_->MOV(32, ::R(r), Imm32(imm));
}

void X64IRRegCache::StoreRegValue(IRReg mreg, uint32_t imm) {
	_assert_(IsValidGPRNoZero(mreg));
	// Try to optimize using a different reg.
	X64Reg storeReg = INVALID_REG;

	// Could we get lucky?  Check for an exact match in another xreg.
	for (int i = 0; i < TOTAL_MAPPABLE_IRREGS; ++i) {
		if (mr[i].loc == MIPSLoc::REG_IMM && mr[i].imm == imm) {
			// Awesome, let's just store this reg.
			storeReg = (X64Reg)mr[i].nReg;
			break;
		}
	}

	if (storeReg == INVALID_REG)
		emit_->MOV(32, MDisp(CTXREG, -128 + GetMipsRegOffset(mreg)), Imm32(imm));
	else
		emit_->MOV(32, MDisp(CTXREG, -128 + GetMipsRegOffset(mreg)), ::R(storeReg));
}

OpArg X64IRRegCache::R(IRReg mipsReg) {
	return ::R(RX(mipsReg));
}

OpArg X64IRRegCache::RPtr(IRReg mipsReg) {
	return ::R(RXPtr(mipsReg));
}

OpArg X64IRRegCache::F(IRReg mipsReg) {
	return ::R(FX(mipsReg));
}

X64Reg X64IRRegCache::RX(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM);
	if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		return FromNativeReg(mr[mipsReg].nReg);
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in x64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

X64Reg X64IRRegCache::RXPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return FromNativeReg(mr[mipsReg].nReg);
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int rv = mr[mipsReg].nReg;
		_dbg_assert_(nr[rv].pointerified);
		if (nr[rv].pointerified) {
			return FromNativeReg(mr[mipsReg].nReg);
		} else {
			ERROR_LOG(JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in x64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

X64Reg X64IRRegCache::FX(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return FromNativeReg(mr[mipsReg + 32].nReg);
	} else {
		ERROR_LOG_REPORT(JIT, "Reg %i not in x64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

bool X64IRRegCache::HasLowSubregister(Gen::X64Reg reg) {
#if !PPSSPP_ARCH(AMD64)
	// Can't use ESI or EDI (which we use), no 8-bit versions.  Only these.
	return reg == EAX || reg == EBX || reg == ECX || reg == EDX;
#else
	return true;
#endif
}

#endif
