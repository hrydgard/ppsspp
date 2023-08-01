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

//#define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
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
#ifdef MASKED_PSP_MEMORY
		ANDI2R(SCRATCH1, SCRATCH1, 0x3FFFFFFF);
#endif
	}

	std::vector<FixupBranch> Arm64Jit::SetScratch1ForSafeAddress(MIPSGPReg rs, s16 offset, ARM64Reg tempReg) {
		std::vector<FixupBranch> skips;

		SetScratch1ToEffectiveAddress(rs, offset);

		// We can do this a little smarter by shifting out the lower 8 bits, since blocks are 0x100 aligned.
		// PSP_GetUserMemoryEnd() is dynamic, but the others encode to imms just fine.
		// So we only need to safety check the one value.
		// This is because ARM64 immediates for many instructions like CMP can only encode
		// immediates up to 12 bits, shifted by 12 or not.

		if ((PSP_GetUserMemoryEnd() & 0x000FFFFF) == 0) {
			// In other words, shift right 8, and kill off the top 4 bits as we don't want them involved in the ocmpares.
			UBFX(tempReg, SCRATCH1, 8, 24 - 4);
			// Now check if we're higher than that.
			CMPI2R(tempReg, PSP_GetUserMemoryEnd() >> 8);
		} else {
			// Compare first using the tempReg (need it because we have a full 28-bit value), then shift into it.
			ANDI2R(SCRATCH1, SCRATCH1, 0x0FFFFFFF);
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
		CONDITIONAL_DISABLE(LSU);
		CheckMemoryBreakpoint();
		int offset = SignExtend16ToS32(op & 0xFFFF);
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;

		if (!js.inDelaySlot && !jo.Disabled(JitDisable::LSU_UNALIGNED)) {
			// Optimisation: Combine to single unaligned load/store
			bool isLeft = (o == 34 || o == 42);
			CheckMemoryBreakpoint(1);
			MIPSOpcode nextOp = GetOffsetInstruction(1);
			// Find a matching shift in opposite direction with opposite offset.
			if (nextOp == (isLeft ? (op.encoding + (4 << 26) - 3) : (op.encoding - (4 << 26) + 3))) {
				EatInstruction(nextOp);
				nextOp = MIPSOpcode(((load ? 35 : 43) << 26) | ((isLeft ? nextOp : op) & 0x03FFFFFF)); //lw, sw
				Comp_ITypeMem(nextOp);
				return;
			}
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		std::vector<FixupBranch> skips;

		if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
#ifdef MASKED_PSP_MEMORY
			u32 addr = iaddr & 0x3FFFFFFF;
#else
			u32 addr = iaddr;
#endif
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
				ORR(SCRATCH2, SCRATCH2, gpr.R(rt), ArithOption(gpr.R(rt), ST_LSR, 24 - shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;

			case 46: // swr
				LDR(SCRATCH2, MEMBASEREG, SCRATCH1);
				ANDI2R(SCRATCH2, SCRATCH2, 0x00ffffff >> (24 - shift), INVALID_REG);
				ORR(SCRATCH2, SCRATCH2, gpr.R(rt), ArithOption(gpr.R(rt), ST_LSL, shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;
			}
			return;
		}

		_dbg_assert_msg_(!gpr.IsImm(rs), "Invalid immediate address %08x?  CPU bug?", iaddr);
		if (load) {
			gpr.MapDirtyIn(rt, rs, false);
		} else {
			gpr.MapInIn(rt, rs);
		}
		gpr.SpillLock(rt);
		gpr.SpillLock(rs);
		// Need to get temps before skipping safe mem.
		ARM64Reg LR_SCRATCH3 = gpr.GetAndLockTempR();
		ARM64Reg LR_SCRATCH4 = o == 42 || o == 46 ? gpr.GetAndLockTempR() : INVALID_REG;

		if (!g_Config.bFastMemory && rs != MIPS_REG_SP) {
			skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
		} else {
			SetScratch1ToEffectiveAddress(rs, offset);
		}

		// Here's our shift amount.
		ANDI2R(SCRATCH2, SCRATCH1, 3);
		LSL(SCRATCH2, SCRATCH2, 3);

		// Now align the address for the actual read.
		ANDI2R(SCRATCH1, SCRATCH1, ~3U);

		switch (o) {
		case 34: // lwl
			MOVI2R(LR_SCRATCH3, 0x00ffffff);
			LDR(SCRATCH1, MEMBASEREG, ArithOption(SCRATCH1));
			LSRV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(gpr.R(rt), gpr.R(rt), LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(SCRATCH1, SCRATCH1, SCRATCH2);
			ORR(gpr.R(rt), gpr.R(rt), SCRATCH1);
			break;

		case 38: // lwr
			MOVI2R(LR_SCRATCH3, 0xffffff00);
			LDR(SCRATCH1, MEMBASEREG, ArithOption(SCRATCH1));
			LSRV(SCRATCH1, SCRATCH1, SCRATCH2);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(gpr.R(rt), gpr.R(rt), LR_SCRATCH3);
			ORR(gpr.R(rt), gpr.R(rt), SCRATCH1);
			break;

		case 42: // swl
			MOVI2R(LR_SCRATCH3, 0xffffff00);
			LDR(LR_SCRATCH4, MEMBASEREG, ArithOption(SCRATCH1));
			LSLV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);

			LSRV(LR_SCRATCH3, gpr.R(rt), SCRATCH2);
			ORR(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			STR(LR_SCRATCH4, MEMBASEREG, ArithOption(SCRATCH1));
			break;

		case 46: // swr
			MOVI2R(LR_SCRATCH3, 0x00ffffff);
			LDR(LR_SCRATCH4, MEMBASEREG, ArithOption(SCRATCH1));
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSRV(LR_SCRATCH3, LR_SCRATCH3, SCRATCH2);
			AND(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			NEG(SCRATCH2, SCRATCH2);
			ADDI2R(SCRATCH2, SCRATCH2, 24);
			LSLV(LR_SCRATCH3, gpr.R(rt), SCRATCH2);
			ORR(LR_SCRATCH4, LR_SCRATCH4, LR_SCRATCH3);
			STR(LR_SCRATCH4, MEMBASEREG, ArithOption(SCRATCH1));
			break;
		}

		for (auto skip : skips) {
			SetJumpTarget(skip);
		}

		gpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_ITypeMem(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);
		CheckMemoryBreakpoint();

		int offset = SignExtend16ToS32(op & 0xFFFF);
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
		ARM64Reg targetReg = INVALID_REG;
		ARM64Reg addrReg = INVALID_REG;

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
#ifndef MASKED_PSP_MEMORY
			if (jo.cachePointers && g_Config.bFastMemory) {
				// ARM has smaller load/store immediate displacements than MIPS, 12 bits - and some memory ops only have 8 bits.
				int offsetRange = 0x3ff;
				if (o == 41 || o == 33 || o == 37 || o == 32)
					offsetRange = 0xff;  // 8 bit offset only
				if (!gpr.IsImm(rs) && rs != rt && (offset <= offsetRange) && offset >= 0 &&
					  (dataSize == 1 || (offset & (dataSize - 1)) == 0)) {  // Check that the offset is aligned to the access size as that's required for INDEX_UNSIGNED encodings. we can get here through fallback from lwl/lwr
					gpr.SpillLock(rs, rt);
					gpr.MapRegAsPointer(rs);

					// For a store, try to avoid mapping a reg if not needed.
					targetReg = load ? INVALID_REG : gpr.TryMapTempImm(rt);
					if (targetReg == INVALID_REG) {
						gpr.MapReg(rt, load ? MAP_NOINIT : 0);
						targetReg = gpr.R(rt);
					}

					switch (o) {
					case 35: LDR(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 37: LDRH(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 33: LDRSH(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 36: LDRB(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 32: LDRSB(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
						// Store
					case 43: STR(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 41: STRH(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					case 40: STRB(INDEX_UNSIGNED, targetReg, gpr.RPtr(rs), offset); break;
					}
					gpr.ReleaseSpillLocksAndDiscardTemps();
					break;
				}
			}
#endif

			if (!load && gpr.IsImm(rt) && gpr.TryMapTempImm(rt) != INVALID_REG) {
				// We're storing an immediate value, let's see if we can optimize rt.
				if (!gpr.IsImm(rs) || !Memory::IsValidAddress(iaddr) || offset == 0) {
					// In this case, we're always going to need rs mapped, which may flush the temp imm.
					// We handle that in the cases below since targetReg is INVALID_REG.
					gpr.MapIn(rs);
				}

				targetReg = gpr.TryMapTempImm(rt);
			}

			if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
#ifdef MASKED_PSP_MEMORY
				u32 addr = iaddr & 0x3FFFFFFF;
#else
				u32 addr = iaddr;
#endif
				if (addr == iaddr && offset == 0) {
					// It was already safe.  Let's shove it into a reg and use it directly.
					if (targetReg == INVALID_REG) {
						load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);
						targetReg = gpr.R(rt);
					}
					addrReg = gpr.R(rs);
				} else {
					// In this case, only map rt. rs+offset will be in SCRATCH1.
					if (targetReg == INVALID_REG) {
						gpr.MapReg(rt, load ? MAP_NOINIT : 0);
						targetReg = gpr.R(rt);
					}
					gpr.SetRegImm(SCRATCH1, addr);
					addrReg = SCRATCH1;
				}
			} else {
				// This gets hit in a few games, as a result of never-taken delay slots (some branch types
				// conditionally execute the delay slot instructions). Ignore in those cases.
				if (!js.inDelaySlot) {
					_dbg_assert_msg_(!gpr.IsImm(rs), "Invalid immediate address %08x?  CPU bug?", iaddr);
				}

				// If we already have a targetReg, we optimized an imm, and rs is already mapped.
				if (targetReg == INVALID_REG) {
					if (load) {
						gpr.MapDirtyIn(rt, rs);
					} else {
						gpr.MapInIn(rt, rs);
					}
					targetReg = gpr.R(rt);
				}

				if (!g_Config.bFastMemory && rs != MIPS_REG_SP) {
					skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
				} else {
					SetScratch1ToEffectiveAddress(rs, offset);
				}
				addrReg = SCRATCH1;
			}

			switch (o) {
				// Load
			case 35: LDR(targetReg, MEMBASEREG, addrReg); break;
			case 37: LDRH(targetReg, MEMBASEREG, addrReg); break;
			case 33: LDRSH(targetReg, MEMBASEREG, addrReg); break;
			case 36: LDRB(targetReg, MEMBASEREG, addrReg); break;
			case 32: LDRSB(targetReg, MEMBASEREG, addrReg); break;
				// Store
			case 43: STR(targetReg, MEMBASEREG, addrReg); break;
			case 41: STRH(targetReg, MEMBASEREG, addrReg); break;
			case 40: STRB(targetReg, MEMBASEREG, addrReg); break;
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

	void Arm64Jit::Comp_StoreSync(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		DISABLE;
	}

	void Arm64Jit::Comp_Cache(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		int func = (op >> 16) & 0x1F;

		// See Int_Cache for the definitions.
		switch (func) {
		case 24: break;
		case 25: break;
		case 27: break;
		case 30: break;
		default:
			// Fall back to the interpreter.
			DISABLE;
		}
	}
}

#endif // PPSSPP_ARCH(ARM64)
