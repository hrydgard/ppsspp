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

		if ((flags & X64Map::MASK) == X64Map::XMM0) {
			// Certain cases require this reg.
			static const int blendReg[] = { XMM0 };
			count = 1;
			return blendReg;
		}

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

void X64IRRegCache::FlushAll(bool gprs, bool fprs) {
	// Note: make sure not to change the registers when flushing:
	// Branching code may expect the x64reg to retain its value.

	auto needsFlush = [&](IRReg i) {
		if (mr[i].loc != MIPSLoc::MEM || mr[i].isStatic)
			return false;
		if (mr[i].nReg == -1 || !nr[mr[i].nReg].isDirty)
			return false;
		return true;
	};

	auto isSingleFloat = [&](IRReg i) {
		if (mr[i].lane != -1 || mr[i].loc != MIPSLoc::FREG)
			return false;
		return true;
	};

	// Sometimes, float/vector regs may be in separate regs in a sequence.
	// It's worth combining and flushing together.
	for (int i = 1; i < TOTAL_MAPPABLE_IRREGS - 1; ++i) {
		if (!needsFlush(i) || !needsFlush(i + 1))
			continue;
		// GPRs are probably not worth it.  Merging Vec2s might be, but pretty uncommon.
		if (!isSingleFloat(i) || !isSingleFloat(i + 1))
			continue;

		X64Reg regs[4]{ INVALID_REG, INVALID_REG, INVALID_REG, INVALID_REG };
		regs[0] = FromNativeReg(mr[i + 0].nReg);
		regs[1] = FromNativeReg(mr[i + 1].nReg);

		bool flushVec4 = i + 3 < TOTAL_MAPPABLE_IRREGS && needsFlush(i + 2) && needsFlush(i + 3);
		if (flushVec4 && isSingleFloat(i + 2) && isSingleFloat(i + 3) && (i & 3) == 0) {
			regs[2] = FromNativeReg(mr[i + 2].nReg);
			regs[3] = FromNativeReg(mr[i + 3].nReg);

			// Note that this doesn't change the low lane of any of these regs.
			emit_->UNPCKLPS(regs[1], ::R(regs[3]));
			emit_->UNPCKLPS(regs[0], ::R(regs[2]));
			emit_->UNPCKLPS(regs[0], ::R(regs[1]));
			emit_->MOVAPS(MDisp(CTXREG, -128 + GetMipsRegOffset(i)), regs[0]);

			for (int j = 0; j < 4; ++j)
				DiscardReg(i + j);
			i += 3;
			continue;
		}

		// TODO: Maybe this isn't always worth doing.
		emit_->UNPCKLPS(regs[0], ::R(regs[1]));
		emit_->MOVLPS(MDisp(CTXREG, -128 + GetMipsRegOffset(i)), regs[0]);

		DiscardReg(i);
		DiscardReg(i + 1);
		++i;
		continue;
	}

	IRNativeRegCacheBase::FlushAll(gprs, fprs);
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

X64Reg X64IRRegCache::GetAndLockTempGPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::REG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return FromNativeReg(reg);
}

X64Reg X64IRRegCache::GetAndLockTempFPR() {
	IRNativeReg reg = AllocateReg(MIPSLoc::FREG, MIPSMap::INIT);
	if (reg != -1) {
		nr[reg].tempLockIRIndex = irIndex_;
	}
	return FromNativeReg(reg);
}

void X64IRRegCache::ReserveAndLockXGPR(Gen::X64Reg r) {
	IRNativeReg nreg = GPRToNativeReg(r);
	if (nr[nreg].mipsReg != IRREG_INVALID)
		FlushNativeReg(nreg);
	nr[r].tempLockIRIndex = irIndex_;
}

X64Reg X64IRRegCache::MapWithFPRTemp(const IRInst &inst) {
	return FromNativeReg(MapWithTemp(inst, MIPSLoc::FREG));
}

void X64IRRegCache::MapWithFlags(IRInst inst, X64Map destFlags, X64Map src1Flags, X64Map src2Flags) {
	Mapping mapping[3];
	MappingFromInst(inst, mapping);

	mapping[0].flags = mapping[0].flags | destFlags;
	mapping[1].flags = mapping[1].flags | src1Flags;
	mapping[2].flags = mapping[2].flags | src2Flags;

	auto flushReg = [&](IRNativeReg nreg) {
		bool mustKeep = false;
		bool canDiscard = false;
		for (int i = 0; i < 3; ++i) {
			if (mapping[i].reg != nr[nreg].mipsReg)
				continue;

			if ((mapping[i].flags & MIPSMap::NOINIT) != MIPSMap::NOINIT) {
				mustKeep = true;
				break;
			} else {
				canDiscard = true;
			}
		}

		if (mustKeep || !canDiscard) {
			FlushNativeReg(nreg);
		} else {
			DiscardNativeReg(nreg);
		}
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

		case X64Map::XMM0:
			if (nr[XMMToNativeReg(XMM0)].mipsReg != mapping[i].reg)
				flushReg(XMMToNativeReg(XMM0));
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

	// Okay, not mapped, so we need to allocate an x64 register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 1, mapFlags);
	return FromNativeReg(nreg);
}

X64Reg X64IRRegCache::MapGPR2(IRReg mipsReg, MIPSMap mapFlags) {
	_dbg_assert_(IsValidGPR(mipsReg) && IsValidGPR(mipsReg + 1));

	// Okay, not mapped, so we need to allocate an x64 register.
	IRNativeReg nreg = MapNativeReg(MIPSLoc::REG, mipsReg, 2, mapFlags);
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
		else if (lanes == 4 && (first & 3) == 0)
			emit_->MOVAPS(r, MDisp(CTXREG, -128 + GetMipsRegOffset(first)));
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
		else if (lanes == 4 && (first & 3) == 0)
			emit_->MOVAPS(MDisp(CTXREG, -128 + GetMipsRegOffset(first)), r);
		else if (lanes == 4)
			emit_->MOVUPS(MDisp(CTXREG, -128 + GetMipsRegOffset(first)), r);
		else
			_assert_(false);
	}
}

bool X64IRRegCache::TransferNativeReg(IRNativeReg nreg, IRNativeReg dest, MIPSLoc type, IRReg first, int lanes, MIPSMap flags) {
	bool allowed = !mr[nr[nreg].mipsReg].isStatic;
	// There's currently no support for non-XMMs here.
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

bool X64IRRegCache::TransferVecTo1(IRNativeReg nreg, IRNativeReg dest, IRReg first, int oldlanes) {
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
			u8 shuf = VFPU_SWIZZLE(i, i, i, i);
			if (i == 0) {
				emit_->MOVAPS(FromNativeReg(freeReg), ::R(FromNativeReg(nreg)));
			} else if (cpu_info.bAVX) {
				emit_->VPERMILPS(128, FromNativeReg(freeReg), ::R(FromNativeReg(nreg)), shuf);
			} else if (i == 2) {
				emit_->MOVHLPS(FromNativeReg(freeReg), FromNativeReg(nreg));
			} else {
				emit_->MOVAPS(FromNativeReg(freeReg), ::R(FromNativeReg(nreg)));
				emit_->SHUFPS(FromNativeReg(freeReg), ::R(FromNativeReg(freeReg)), shuf);
			}

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
	u8 shuf = VFPU_SWIZZLE(mr[first].lane, mr[first].lane, mr[first].lane, mr[first].lane);
	if (mr[first].lane > 0 && cpu_info.bAVX && dest != nreg) {
		emit_->VPERMILPS(128, FromNativeReg(dest), ::R(FromNativeReg(nreg)), shuf);
	} else if (mr[first].lane <= 0 && dest != nreg) {
		emit_->MOVAPS(FromNativeReg(dest), ::R(FromNativeReg(nreg)));
	} else if (mr[first].lane == 2) {
		emit_->MOVHLPS(FromNativeReg(dest), FromNativeReg(nreg));
	} else if (mr[first].lane > 0) {
		if (dest != nreg)
			emit_->MOVAPS(FromNativeReg(dest), ::R(FromNativeReg(nreg)));
		emit_->SHUFPS(FromNativeReg(dest), ::R(FromNativeReg(dest)), shuf);
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

bool X64IRRegCache::Transfer1ToVec(IRNativeReg nreg, IRNativeReg dest, IRReg first, int lanes) {
	X64Reg cur[4]{};
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

	// Move things together into a reg.
	if (lanes == 4 && cpu_info.bSSE4_1 && numInRegs == 1 && (first & 3) == 0) {
		// Use a blend to grab the rest.  BLENDPS is pretty good.
		if (cpu_info.bAVX && nreg != dest) {
			if (cur[0] == INVALID_REG) {
				// Broadcast to all lanes, then blend from memory to replace.
				emit_->VPERMILPS(128, FromNativeReg(dest), ::R(FromNativeReg(nreg)), 0);
				emit_->BLENDPS(FromNativeReg(dest), MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
			} else {
				emit_->VBLENDPS(128, FromNativeReg(dest), FromNativeReg(nreg), MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
			}
			cur[0] = FromNativeReg(dest);
		} else {
			if (cur[0] == INVALID_REG)
				emit_->SHUFPS(FromNativeReg(nreg), ::R(FromNativeReg(nreg)), 0);
			emit_->BLENDPS(FromNativeReg(nreg), MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
			// If this is not dest, it'll get moved there later.
			cur[0] = FromNativeReg(nreg);
		}
	} else if (lanes == 4) {
		if (blendMask == 0) {
			// y = yw##, x = xz##, x = xyzw.
			emit_->UNPCKLPS(cur[1], ::R(cur[3]));
			emit_->UNPCKLPS(cur[0], ::R(cur[2]));
			emit_->UNPCKLPS(cur[0], ::R(cur[1]));
		} else if (blendMask == 0b1100) {
			// x = xy##, then load zw.
			emit_->UNPCKLPS(cur[0], ::R(cur[1]));
			emit_->MOVHPS(cur[0], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 2)));
		} else if (blendMask == 0b1010 && cpu_info.bSSE4_1 && (first & 3) == 0) {
			// x = x#z#, x = xyzw.
			emit_->SHUFPS(cur[0], ::R(cur[2]), VFPU_SWIZZLE(0, 0, 0, 0));
			emit_->BLENDPS(cur[0], MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
		} else if (blendMask == 0b0110 && cpu_info.bSSE4_1 && (first & 3) == 0) {
			// x = x##w, x = xyzw.
			emit_->SHUFPS(cur[0], ::R(cur[3]), VFPU_SWIZZLE(0, 0, 0, 0));
			emit_->BLENDPS(cur[0], MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
		} else if (blendMask == 0b1001 && cpu_info.bSSE4_1 && (first & 3) == 0) {
			// y = #yz#, y = xyzw.
			emit_->SHUFPS(cur[1], ::R(cur[2]), VFPU_SWIZZLE(0, 0, 0, 0));
			emit_->BLENDPS(cur[1], MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
			// Will be moved to dest as needed.
			cur[0] = cur[1];
		} else if (blendMask == 0b0101 && cpu_info.bSSE4_1 && (first & 3) == 0) {
			// y = #y#w, y = xyzw.
			emit_->SHUFPS(cur[1], ::R(cur[3]), VFPU_SWIZZLE(0, 0, 0, 0));
			emit_->BLENDPS(cur[1], MDisp(CTXREG, -128 + GetMipsRegOffset(first)), blendMask);
			// Will be moved to dest as needed.
			cur[0] = cur[1];
		} else if (blendMask == 0b1000) {
			// x = xz##, z = w###, y = yw##, x = xyzw.
			emit_->UNPCKLPS(cur[0], ::R(cur[2]));
			emit_->MOVSS(cur[2], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 3)));
			emit_->UNPCKLPS(cur[1], ::R(cur[2]));
			emit_->UNPCKLPS(cur[0], ::R(cur[1]));
		} else if (blendMask == 0b0100) {
			// y = yw##, w = z###, x = xz##, x = xyzw.
			emit_->UNPCKLPS(cur[1], ::R(cur[3]));
			emit_->MOVSS(cur[3], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 2)));
			emit_->UNPCKLPS(cur[0], ::R(cur[3]));
			emit_->UNPCKLPS(cur[0], ::R(cur[1]));
		} else if (blendMask == 0b0010) {
			// z = zw##, w = y###, x = xy##, x = xyzw.
			emit_->UNPCKLPS(cur[2], ::R(cur[3]));
			emit_->MOVSS(cur[3], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 1)));
			emit_->UNPCKLPS(cur[0], ::R(cur[3]));
			emit_->MOVLHPS(cur[0], cur[2]);
		} else if (blendMask == 0b0001) {
			// y = yw##, w = x###, w = xz##, w = xyzw.
			emit_->UNPCKLPS(cur[1], ::R(cur[3]));
			emit_->MOVSS(cur[3], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 0)));
			emit_->UNPCKLPS(cur[3], ::R(cur[2]));
			emit_->UNPCKLPS(cur[3], ::R(cur[1]));
			// Will be moved to dest as needed.
			cur[0] = cur[3];
		} else if (blendMask == 0b0011) {
			// z = zw##, w = xy##, w = xyzw.
			emit_->UNPCKLPS(cur[2], ::R(cur[3]));
			emit_->MOVLPS(cur[3], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 0)));
			emit_->MOVLHPS(cur[3], cur[2]);
			// Will be moved to dest as needed.
			cur[0] = cur[3];
		} else {
			// This must mean no SSE4, and numInRegs <= 2 in trickier cases.
			return false;
		}
	} else if (lanes == 2) {
		if (cur[0] != INVALID_REG && cur[1] != INVALID_REG) {
			emit_->UNPCKLPS(cur[0], ::R(cur[1]));
		} else if (cur[0] != INVALID_REG && cpu_info.bSSE4_1) {
			emit_->INSERTPS(cur[0], MDisp(CTXREG, -128 + GetMipsRegOffset(first + 1)), 1);
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

	if (cur[0] != FromNativeReg(dest))
		emit_->MOVAPS(FromNativeReg(dest), ::R(cur[0]));

	if (dest != nreg) {
		nr[dest].mipsReg = first;
		nr[nreg].mipsReg = -1;
		nr[nreg].isDirty = false;
	}

	return true;
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
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in x64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

X64Reg X64IRRegCache::RXPtr(IRReg mipsReg) {
	_dbg_assert_(IsValidGPR(mipsReg));
	_dbg_assert_(mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM || mr[mipsReg].loc == MIPSLoc::REG_AS_PTR);
	if (mr[mipsReg].loc == MIPSLoc::REG_AS_PTR) {
		return FromNativeReg(mr[mipsReg].nReg);
	} else if (mr[mipsReg].loc == MIPSLoc::REG || mr[mipsReg].loc == MIPSLoc::REG_IMM) {
		int r = mr[mipsReg].nReg;
		_dbg_assert_(nr[r].pointerified);
		if (nr[r].pointerified) {
			return FromNativeReg(mr[mipsReg].nReg);
		} else {
			ERROR_LOG(Log::JIT, "Tried to use a non-pointer register as a pointer");
			return INVALID_REG;
		}
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in x64 reg", mipsReg);
		return INVALID_REG;  // BAAAD
	}
}

X64Reg X64IRRegCache::FX(IRReg mipsReg) {
	_dbg_assert_(IsValidFPR(mipsReg));
	_dbg_assert_(mr[mipsReg + 32].loc == MIPSLoc::FREG);
	if (mr[mipsReg + 32].loc == MIPSLoc::FREG) {
		return FromNativeReg(mr[mipsReg + 32].nReg);
	} else {
		ERROR_LOG_REPORT(Log::JIT, "Reg %i not in x64 reg", mipsReg);
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
