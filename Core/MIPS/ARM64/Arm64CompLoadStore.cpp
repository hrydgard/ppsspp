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


// Optimization ideas:
//
// It's common to see sequences of stores writing or reading to a contiguous set of
// addresses in function prologues/epilogues:
//  sw s5, 104(sp)
//  sw s4, 100(sp)
//  sw s3, 96(sp)
//  sw s2, 92(sp)
//  sw s1, 88(sp)
//  sw s0, 84(sp)
//  sw ra, 108(sp)
//  mov s4, a0
//  mov s3, a1
//  ...
// Such sequences could easily be detected and turned into nice contiguous
// sequences of ARM stores instead of the current 3 instructions per sw/lw.
//
// Also, if we kept track of the likely register content of a cached register,
// (pointer or data), we could avoid many BIC instructions.

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM64)

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp {
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;
	
	// Destroys SCRATCH2
	void Arm64Jit::SetScratch1ToEffectiveAddress(MIPSGPReg rs, s16 offset) {
		if (offset) {
			ADDI2R(SCRATCH1, gpr.R(rs), offset, SCRATCH2);
		} else {
			MOV(SCRATCH1, gpr.R(rs));
		}
	}

	std::vector<FixupBranch> Arm64Jit::SetScratch1ForSafeAddress(MIPSGPReg rs, s16 offset, ARM64Reg tempReg) {
		std::vector<FixupBranch> skips;

		SetScratch1ToEffectiveAddress(rs, offset);

		// We can do this a little smarter by shifting out the lower 8 bits, since blocks are 0x100 aligned.
		// PSP_GetUserMemoryEnd() is dynamic, but the others encode to imms just fine.
		// So we only need to safety check the one value.

		if ((PSP_GetUserMemoryEnd() & 0x000FFFFF) == 0) {
			// In other words, shift right 8.
			UBFX(tempReg, SCRATCH1, 8, 24);
			// Now check if we're higher than that.
			CMPI2R(tempReg, PSP_GetUserMemoryEnd() >> 8);
		} else {
			// Compare first using the tempReg, then shift into it.
			CMPI2R(SCRATCH1, PSP_GetUserMemoryEnd(), tempReg);
			UBFX(tempReg, SCRATCH1, 8, 24);
		}
		skips.push_back(B(CC_HS));

		// If its higher than memory start and we didn't skip yet, it must be good.  Hurray.
		CMPI2R(tempReg, PSP_GetKernelMemoryBase() >> 8);
		FixupBranch inRAM = B(CC_HS);

		// If we got here and it's higher, then it's between VRAM and RAM - skip.
		CMPI2R(tempReg, PSP_GetVidMemEnd() >> 8);
		skips.push_back(B(CC_HS));

		// And if it's higher the VRAM and we're still here again, it's in VRAM.
		CMPI2R(tempReg, PSP_GetVidMemBase() >> 8);
		FixupBranch inVRAM = B(CC_HS);

		// Last gap, this is between SRAM and VRAM.  Skip it.
		CMPI2R(tempReg, PSP_GetScratchpadMemoryEnd() >> 8);
		skips.push_back(B(CC_HS));

		// And for lower than SRAM, we just skip again.
		CMPI2R(tempReg, PSP_GetScratchpadMemoryBase() >> 8);
		skips.push_back(B(CC_LO));

		// At this point, we're either in SRAM (above) or in RAM/VRAM.
		SetJumpTarget(inRAM);
		SetJumpTarget(inVRAM);

		return skips;
	}

	void Arm64Jit::Comp_ITypeMemLR(MIPSOpcode op, bool load) {
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op & 0xFFFF);
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;

		if (!js.inDelaySlot) {
			// Optimisation: Combine to single unaligned load/store
			bool isLeft = (o == 34 || o == 42);
			MIPSOpcode nextOp = GetOffsetInstruction(1);
			// Find a matching shift in opposite direction with opposite offset.
			if (nextOp == (isLeft ? (op.encoding + (4 << 26) - 3) : (op.encoding - (4 << 26) + 3))) {
				EatInstruction(nextOp);
				nextOp = MIPSOpcode(((load ? 35 : 43) << 26) | ((isLeft ? nextOp : op) & 0x03FFFFFF)); //lw, sw
				Comp_ITypeMem(nextOp);
				return;
			}
		}

		DISABLE;

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		std::vector<FixupBranch> skips;

		if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
			u32 addr = iaddr;
			// Need to initialize since this only loads part of the register.
			// But rs no longer matters (even if rs == rt) since we have the address.
			gpr.MapReg(rt, load ? MAP_DIRTY : 0);
			gpr.SetRegImm(SCRATCH1, addr & ~3);

			u8 shift = (addr & 3) * 8;

			switch (o) {
			case 34: // lwl
				LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
				ANDI2R(gpr.R(rt), gpr.R(rt), 0x00ffffff >> shift, INVALID_REG);
				ORR(gpr.R(rt), gpr.R(rt), SCRATCH1, ArithOption(gpr.R(rt), ST_LSL, 24 - shift));
				break;

			case 38: // lwr
				LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
				ANDI2R(gpr.R(rt), gpr.R(rt), 0xffffff00 << (24 - shift), INVALID_REG);
				ORR(gpr.R(rt), gpr.R(rt), SCRATCH1, ArithOption(gpr.R(rt), ST_LSR, shift));
				break;

			case 42: // swl
				LDR(SCRATCH2, MEMBASEREG, SCRATCH1);
				ANDI2R(SCRATCH2, SCRATCH2, 0xffffff00 << shift, INVALID_REG);
				ORR(SCRATCH2, SCRATCH2, SCRATCH2, ArithOption(gpr.R(rt), ST_LSR, 24 - shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;

			case 46: // swr
				LDR(SCRATCH2, MEMBASEREG, SCRATCH1);
				ANDI2R(SCRATCH2, SCRATCH2, 0x00ffffff >> (24 - shift), INVALID_REG);
				ORR(SCRATCH2, SCRATCH2, SCRATCH2, ArithOption(gpr.R(rt), ST_LSL, shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;
			}
			return;
		}

		switch (o) {
		case 34: // lwl
			DISABLE;
			break;

		case 38: // lwr
			DISABLE;
			break;

		case 42: // swl
			break;

		case 46: // swr
			break;
		}

		_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
		if (load) {
			gpr.MapDirtyIn(rt, rs, false);
		} else {
			gpr.MapInIn(rt, rs);
		}

		if (false && !g_Config.bFastMemory && rs != MIPS_REG_SP) {
			skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
		} else {
			SetScratch1ToEffectiveAddress(rs, offset);
		}

		// Need temp regs.  TODO: Get from the regcache?
		static const ARM64Reg LR_SCRATCH3 = W9;
		static const ARM64Reg LR_SCRATCH4 = W10;
		if (false && load) {
			PUSH(EncodeRegTo64(LR_SCRATCH3));
		} else {
			PUSH2(EncodeRegTo64(LR_SCRATCH3), EncodeRegTo64(LR_SCRATCH4));
		}

		// Here's our shift amount.
		ANDI2R(SCRATCH2, SCRATCH1, 3);
		LSL(SCRATCH2, SCRATCH2, 3);

		// Now align the address for the actual read.
		ANDI2R(SCRATCH1, SCRATCH1, ~3U);

		switch (o) {
		case 34: // lwl
			MOVI2R(LR_SCRATCH3, 0x00ffffff);
			LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
			LSRV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(gpr.R(rt), gpr.R(rt), LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(SCRATCH1, SCRATCH1, SCRATCH2);
			ORR(gpr.R(rt), gpr.R(rt), SCRATCH1);
			break;

		case 38: // lwr
			MOVI2R(LR_SCRATCH3, 0xffffff00);
			LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
			LSRV(SCRATCH1, SCRATCH1, SCRATCH2);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(gpr.R(rt), gpr.R(rt), LR_SCRATCH3);
			ORR(gpr.R(rt), gpr.R(rt), SCRATCH1);
			break;

		case 42: // swl
			MOVI2R(LR_SCRATCH3, 0xffffff00);
			LDR(LR_SCRATCH4, MEMBASEREG, SCRATCH1);
			LSLV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSRV(LR_SCRATCH3, gpr.R(rt), SCRATCH2);
			ORR(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			STR(LR_SCRATCH4, MEMBASEREG, SCRATCH1);
			break;

		case 46: // swr
			MOVI2R(LR_SCRATCH3, 0x00ffffff);
			LDR(LR_SCRATCH4, MEMBASEREG, SCRATCH1);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSRV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(LR_SCRATCH3, gpr.R(rt), SCRATCH2);
			ORR(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			STR(LR_SCRATCH4, MEMBASEREG, SCRATCH1);
			break;
		}

		if (false && load) {
			POP(EncodeRegTo64(LR_SCRATCH3));
		} else {
			POP2(EncodeRegTo64(LR_SCRATCH3), EncodeRegTo64(LR_SCRATCH4));
		}

		for (auto skip : skips) {
			SetJumpTarget(skip);
		}
	}

	void Arm64Jit::Comp_ITypeMem(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		int offset = (signed short)(op & 0xFFFF);
		bool load = false;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;
		if (((op >> 29) & 1) == 0 && rt == MIPS_REG_ZERO) {
			// Don't load anything into $zr
			return;
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		std::vector<FixupBranch> skips;
		ARM64Reg addrReg = SCRATCH1;

		int dataSize = 4;
		switch (o) {
		case 37:
		case 33:
			dataSize = 2;
			break;
		case 36:
		case 32:
			dataSize = 1;
			break;
			// Store
		case 41:
			dataSize = 2;
			break;
		case 40:
			dataSize = 1;
			break;
		}

		switch (o) {
		case 32: //lb
		case 33: //lh
		case 35: //lw
		case 36: //lbu
		case 37: //lhu
			load = true;
		case 40: //sb
		case 41: //sh
		case 43: //sw
			if (jo.cachePointers && g_Config.bFastMemory) {
				// ARM has smaller load/store immediate displacements than MIPS, 12 bits - and some memory ops only have 8 bits.
				int offsetRange = 0x3ff;
				if (o == 41 || o == 33 || o == 37 || o == 32)
					offsetRange = 0xff;  // 8 bit offset only
				if (!gpr.IsImm(rs) && rs != rt && (offset <= offsetRange) && offset >= 0 &&
					  (dataSize == 1 || (offset & (dataSize - 1)) == 0)) {  // Check that the offset is aligned to the access size as that's required for INDEX_UNSIGNED encodings. we can get here through fallback from lwl/lwr
					gpr.SpillLock(rs, rt);
					gpr.MapRegAsPointer(rs);

					Arm64Gen::ARM64Reg ar;
					if (!load && gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
						// Can just store from the zero register directly.
						ar = WZR;
					} else {
						gpr.MapReg(rt, load ? MAP_NOINIT : 0);
						ar = gpr.R(rt);
					}
					switch (o) {
					case 35: LDR(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 37: LDRH(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 33: LDRSH(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 36: LDRB(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 32: LDRSB(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
						// Store
					case 43: STR(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 41: STRH(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					case 40: STRB(INDEX_UNSIGNED, ar, gpr.RPtr(rs), offset); break;
					}
					gpr.ReleaseSpillLocks();
					break;
				}
			}

			if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
				// TODO: Avoid mapping a register for the "zero" register, use R0 instead.

				// We can compute the full address at compile time. Kickass.
				u32 addr = iaddr & 0x3FFFFFFF;

				if (addr == iaddr && offset == 0) {
					// It was already safe.  Let's shove it into a reg and use it directly.
					load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);
					addrReg = gpr.R(rs);
				} else {
					// In this case, only map rt. rs+offset will be in R0.
					gpr.MapReg(rt, load ? MAP_NOINIT : 0);
					gpr.SetRegImm(SCRATCH1, addr);
					addrReg = SCRATCH1;
				}
			} else {
				_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
				load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);

				if (!g_Config.bFastMemory && rs != MIPS_REG_SP) {
					skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
				} else {
					SetScratch1ToEffectiveAddress(rs, offset);
				}
				addrReg = SCRATCH1;
			}

			switch (o) {
				// Load
			case 35: LDR(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 37: LDRH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 33: LDRSH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 36: LDRB(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 32: LDRSB(gpr.R(rt), MEMBASEREG, addrReg); break;
				// Store
			case 43: STR(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 41: STRH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 40: STRB(gpr.R(rt), MEMBASEREG, addrReg); break;
			}
			for (auto skip : skips) {
				SetJumpTarget(skip);
				// TODO: Could clear to zero here on load, if skipping this for good reads.
			}
			break;
		case 34: //lwl
		case 38: //lwr
			load = true;
		case 42: //swl
		case 46: //swr
			Comp_ITypeMemLR(op, load);
			break;
		default:
			Comp_Generic(op);
			return;
		}
	}

	void Arm64Jit::Comp_Cache(MIPSOpcode op) {
//		int imm = (s16)(op & 0xFFFF);
//		int rs = _RS;
//		int addr = R(rs) + imm;
		int func = (op >> 16) & 0x1F;

		// It appears that a cache line is 0x40 (64) bytes, loops in games
		// issue the cache instruction at that interval.

		// These codes might be PSP-specific, they don't match regular MIPS cache codes very well
		switch (func) {
			// Icache
		case 8:
			// Invalidate the instruction cache at this address
			DISABLE;
			break;
			// Dcache
		case 24:
			// "Create Dirty Exclusive" - for avoiding a cacheline fill before writing to it.
			// Will cause garbage on the real machine so we just ignore it, the app will overwrite the cacheline.
			break;
		case 25:  // Hit Invalidate - zaps the line if present in cache. Should not writeback???? scary.
			// No need to do anything.
			break;
		case 27:  // D-cube. Hit Writeback Invalidate.  Tony Hawk Underground 2
			break;
		case 30:  // GTA LCS, a lot. Fill (prefetch).   Tony Hawk Underground 2
			break;

		default:
			DISABLE;
			break;
		}
	}
}

#endif // PPSSPP_ARCH(ARM64)
