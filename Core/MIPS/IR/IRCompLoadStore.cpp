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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/IR/IRRegCache.h"

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
#define INVALIDOP { Comp_Generic(op); return; }

namespace MIPSComp {
	void IRFrontend::Comp_ITypeMemLR(MIPSOpcode op, bool load) {
		CONDITIONAL_DISABLE;

		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;

		if (!js.inDelaySlot && opts.unalignedLoadStore) {
			// Optimisation: Combine to single unaligned load/store.
			const bool isLeft = (o == 34 || o == 42);
			MIPSOpcode nextOp = GetOffsetInstruction(1);
			// Find a matching shifted load/store in opposite direction with opposite offset.
			if (nextOp == (isLeft ? (op.encoding + (4 << 26) - 3) : (op.encoding - (4 << 26) + 3))) {
				EatInstruction(nextOp);

				if (isLeft) {
					// Get the unaligned base offset from the lwr/swr instruction.
					offset = (signed short)(nextOp & 0xFFFF);
					// Already checked it if we're on the lwr.
					CheckMemoryBreakpoint(rs, offset);
				}

				if (load) {
					ir.Write(IROp::Load32, rt, rs, ir.AddConstant(offset));
				} else {
					ir.Write(IROp::Store32, rt, rs, ir.AddConstant(offset));
				}
				return;
			}
		}

		int addrReg = IRTEMP_0;
		int valueReg = IRTEMP_1;
		int maskReg = IRTEMP_2;
		int shiftReg = IRTEMP_3;

		// addrReg = rs + imm
		ir.Write(IROp::AddConst, addrReg, rs, ir.AddConstant(offset));
		// shiftReg = (addr & 3) * 8
		ir.Write(IROp::AndConst, shiftReg, addrReg, ir.AddConstant(3));
		ir.Write(IROp::ShlImm, shiftReg, shiftReg, 3);
		// addrReg = addr & 0xfffffffc (for stores, later)
		ir.Write(IROp::AndConst, addrReg, addrReg, ir.AddConstant(0xFFFFFFFC));
		// valueReg = RAM(addrReg)
		ir.Write(IROp::Load32, valueReg, addrReg, ir.AddConstant(0));

		switch (o) {
		case 34: //lwl
			// rt &= (0x00ffffff >> shift)
			// Alternatively, could shift to a wall and back (but would require two shifts each way.)
			ir.WriteSetConstant(maskReg, 0x00ffffff);
			ir.Write(IROp::Shr, maskReg, maskReg, shiftReg);
			ir.Write(IROp::And, rt, rt, maskReg);
			// valueReg <<= (24 - shift)
			ir.Write(IROp::Neg, shiftReg, shiftReg);
			ir.Write(IROp::AddConst, shiftReg, shiftReg, ir.AddConstant(24));
			ir.Write(IROp::Shl, valueReg, valueReg, shiftReg);
			// rt |= valueReg
			ir.Write(IROp::Or, rt, rt, valueReg);
			break;
		case 38: //lwr
			// valueReg >>= shift
			ir.Write(IROp::Shr, valueReg, valueReg, shiftReg);
			// shiftReg = 24 - shift
			ir.Write(IROp::Neg, shiftReg, shiftReg);
			ir.Write(IROp::AddConst, shiftReg, shiftReg, ir.AddConstant(24));
			// rt &= (0xffffff00 << (24 - shift))
			// Alternatively, could shift to a wall and back (but would require two shifts each way.)
			ir.WriteSetConstant(maskReg, 0xffffff00);
			ir.Write(IROp::Shl, maskReg, maskReg, shiftReg);
			ir.Write(IROp::And, rt, rt, maskReg);
			// rt |= valueReg
			ir.Write(IROp::Or, rt, rt, valueReg);
			break;
		case 42: //swl
			// valueReg &= 0xffffff00 << shift
			ir.WriteSetConstant(maskReg, 0xffffff00);
			ir.Write(IROp::Shl, maskReg, maskReg, shiftReg);
			ir.Write(IROp::And, valueReg, valueReg, maskReg);
			// shiftReg = 24 - shift
			ir.Write(IROp::Neg, shiftReg, shiftReg);
			ir.Write(IROp::AddConst, shiftReg, shiftReg, ir.AddConstant(24));
			// valueReg |= rt >> (24 - shift)
			ir.Write(IROp::Shr, maskReg, rt, shiftReg);
			ir.Write(IROp::Or, valueReg, valueReg, maskReg);
			break;
		case 46: //swr
			// valueReg &= 0x00ffffff << (24 - shift)
			ir.WriteSetConstant(maskReg, 0x00ffffff);
			ir.Write(IROp::Neg, shiftReg, shiftReg);
			ir.Write(IROp::AddConst, shiftReg, shiftReg, ir.AddConstant(24));
			ir.Write(IROp::Shr, maskReg, maskReg, shiftReg);
			ir.Write(IROp::And, valueReg, valueReg, maskReg);
			ir.Write(IROp::Neg, shiftReg, shiftReg);
			ir.Write(IROp::AddConst, shiftReg, shiftReg, ir.AddConstant(24));
			// valueReg |= rt << shift
			ir.Write(IROp::Shl, maskReg, rt, shiftReg);
			ir.Write(IROp::Or, valueReg, valueReg, maskReg);
			break;
		default:
			INVALIDOP;
			return;
		}

		if (!load) {
			// RAM(addrReg) = valueReg
			ir.Write(IROp::Store32, valueReg, addrReg, ir.AddConstant(0));
		}
	}

	void IRFrontend::Comp_ITypeMem(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;
		if (((op >> 29) & 1) == 0 && rt == MIPS_REG_ZERO) {
			// Don't load anything into $zr
			return;
		}

		CheckMemoryBreakpoint(rs, offset);

		switch (o) {
			// Load
		case 35:
			ir.Write(IROp::Load32, rt, rs, ir.AddConstant(offset));
			break;
		case 37:
			ir.Write(IROp::Load16, rt, rs, ir.AddConstant(offset));
			break;
		case 33:
			ir.Write(IROp::Load16Ext, rt, rs, ir.AddConstant(offset));
			break;
		case 36:
			ir.Write(IROp::Load8, rt, rs, ir.AddConstant(offset));
			break;
		case 32:
			ir.Write(IROp::Load8Ext, rt, rs, ir.AddConstant(offset));
			break;
			// Store
		case 43:
			ir.Write(IROp::Store32, rt, rs, ir.AddConstant(offset));
			break;
		case 41:
			ir.Write(IROp::Store16, rt, rs, ir.AddConstant(offset));
			break;
		case 40:
			ir.Write(IROp::Store8, rt, rs, ir.AddConstant(offset));
			break;

		case 34: //lwl
		case 38: //lwr
			Comp_ITypeMemLR(op, true);
			break;
		case 42: //swl
		case 46: //swr
			Comp_ITypeMemLR(op, false);
			break;

		default:
			INVALIDOP;
			return;
		}
	}

	void IRFrontend::Comp_Cache(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

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
