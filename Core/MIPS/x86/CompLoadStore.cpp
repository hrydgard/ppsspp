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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/RegCache.h"


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

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }
#define INVALIDOP { Comp_Generic(op); return; }

namespace MIPSComp {
	using namespace Gen;

	void Jit::CompITypeMemRead(MIPSOpcode op, u32 bits, void (XEmitter::*mov)(int, int, X64Reg, OpArg), const void *safeFunc)
	{
		CONDITIONAL_DISABLE(LSU);
		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;

		gpr.Lock(rt, rs);
		gpr.MapReg(rt, rt == rs, true);

		JitSafeMem safe(this, rs, offset);
		OpArg src;
		if (safe.PrepareRead(src, bits / 8))
			(this->*mov)(32, bits, gpr.RX(rt), src);
		if (safe.PrepareSlowRead(safeFunc))
			(this->*mov)(32, bits, gpr.RX(rt), R(EAX));
		safe.Finish();

		gpr.UnlockAll();
	}

	static OpArg DowncastImm(OpArg in, int bits) {
		if (!in.IsImm())
			return in;
		if (in.GetImmBits() > bits) {
			in.SetImmBits(bits);
			return in;
		}
		return in;
	}

	void Jit::CompITypeMemWrite(MIPSOpcode op, u32 bits, const void *safeFunc, bool makeRTWritable)
	{
		CONDITIONAL_DISABLE(LSU);
		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;

		gpr.Lock(rt, rs);
		
		if (rt == MIPS_REG_ZERO || gpr.R(rt).IsImm()) {
			if (makeRTWritable) {
				gpr.MapReg(rt, true, true);
			}
			// NOTICE_LOG(Log::JIT, "%d-bit Imm at %08x : %08x", bits, js.blockStart, (u32)gpr.R(rt).GetImmValue());
		} else {
			gpr.MapReg(rt, true, false);
		}

#if PPSSPP_ARCH(X86)
		// We use EDX so we can have DL for 8-bit ops.
		const bool needSwap = bits == 8 && !gpr.R(rt).IsSimpleReg(EDX) && !gpr.R(rt).IsSimpleReg(ECX);
		if (needSwap)
			gpr.FlushLockX(EDX);
#else
		const bool needSwap = false;
#endif

		JitSafeMem safe(this, rs, offset);
		OpArg dest;
		if (safe.PrepareWrite(dest, bits / 8))
		{
			if (needSwap)
			{
				MOV(32, R(EDX), gpr.R(rt));
				MOV(bits, dest, R(EDX));
			}
			else {
				if (rt == MIPS_REG_ZERO) {
					switch (bits) {
					case 8: MOV(8, dest, Imm8(0)); break;
					case 16: MOV(16, dest, Imm16(0)); break;
					case 32: MOV(32, dest, Imm32(0)); break;
					}
				} else {
					// The downcast is needed so we don't try to generate a 8-bit write with a 32-bit imm
					// (that might have been generated from an li instruction) which is illegal.
					MOV(bits, dest, DowncastImm(gpr.R(rt), bits));
				}
			}
		}
		if (safe.PrepareSlowWrite())
			safe.DoSlowWrite(safeFunc, gpr.R(rt));
		safe.Finish();

		if (needSwap)
			gpr.UnlockAllX();
		gpr.UnlockAll();
	}

	void Jit::CompITypeMemUnpairedLR(MIPSOpcode op, bool isStore)
	{
		CONDITIONAL_DISABLE(LSU);
		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;

		X64Reg shiftReg = ECX;
		gpr.FlushLockX(ECX, EDX);
#if PPSSPP_ARCH(AMD64)
		// On x64, we need ECX for CL, but it's also the first arg and gets lost.  Annoying.
		gpr.FlushLockX(R9);
		shiftReg = R9;
#endif

		gpr.Lock(rt, rs);
		gpr.MapReg(rt, true, !isStore);

		// Grab the offset from alignment for shifting (<< 3 for bytes -> bits.)
		MOV(32, R(shiftReg), gpr.R(rs));
		ADD(32, R(shiftReg), Imm32(offset));
		AND(32, R(shiftReg), Imm32(3));
		SHL(32, R(shiftReg), Imm8(3));

		{
			JitSafeMem safe(this, rs, offset, ~3);
			OpArg src;
			if (safe.PrepareRead(src, 4))
			{
				if (!src.IsSimpleReg(EAX))
					MOV(32, R(EAX), src);

				CompITypeMemUnpairedLRInner(op, shiftReg);
			}
			if (safe.PrepareSlowRead(safeMemFuncs.readU32))
				CompITypeMemUnpairedLRInner(op, shiftReg);
			safe.Finish();
		}

		// For store ops, write EDX back to memory.
		if (isStore)
		{
			JitSafeMem safe(this, rs, offset, ~3);
			OpArg dest;
			if (safe.PrepareWrite(dest, 4))
				MOV(32, dest, R(EDX));
			if (safe.PrepareSlowWrite())
				safe.DoSlowWrite(safeMemFuncs.writeU32, R(EDX));
			safe.Finish();
		}

		gpr.UnlockAll();
		gpr.UnlockAllX();
	}

	void Jit::CompITypeMemUnpairedLRInner(MIPSOpcode op, X64Reg shiftReg)
	{
		CONDITIONAL_DISABLE(LSU);
		int o = op>>26;
		MIPSGPReg rt = _RT;

		// Make sure we have the shift for the target in ECX.
		if (shiftReg != ECX)
			MOV(32, R(ECX), R(shiftReg));

		// Now use that shift (left on target, right on source.)
		switch (o)
		{
		case 34: //lwl
			MOV(32, R(EDX), Imm32(0x00ffffff));
			SHR(32, R(EDX), R(CL));
			AND(32, gpr.R(rt), R(EDX));
			break;

		case 38: //lwr
			SHR(32, R(EAX), R(CL));
			break;

		case 42: //swl
			MOV(32, R(EDX), Imm32(0xffffff00));
			SHL(32, R(EDX), R(CL));
			AND(32, R(EAX), R(EDX));
			break;

		case 46: //swr
			MOV(32, R(EDX), gpr.R(rt));
			SHL(32, R(EDX), R(CL));
			// EDX is already the target value to write, but may be overwritten below.  Save it.
			PUSH(EDX);
			break;

		default:
			_dbg_assert_msg_(false, "Unsupported left/right load/store instruction.");
		}

		// Flip ECX around from 3 bytes / 24 bits.
		if (shiftReg == ECX)
		{
			MOV(32, R(EDX), Imm32(24));
			SUB(32, R(EDX), R(ECX));
			MOV(32, R(ECX), R(EDX));
		}
		else
		{
			MOV(32, R(ECX), Imm32(24));
			SUB(32, R(ECX), R(shiftReg));
		}

		// Use the flipped shift (left on source, right on target) and write target.
		switch (o)
		{
		case 34: //lwl
			SHL(32, R(EAX), R(CL));

			OR(32, gpr.R(rt), R(EAX));
			break;

		case 38: //lwr
			MOV(32, R(EDX), Imm32(0xffffff00));
			SHL(32, R(EDX), R(CL));
			AND(32, gpr.R(rt), R(EDX));

			OR(32, gpr.R(rt), R(EAX));
			break;

		case 42: //swl
			MOV(32, R(EDX), gpr.R(rt));
			SHR(32, R(EDX), R(CL));

			OR(32, R(EDX), R(EAX));
			break;

		case 46: //swr
			MOV(32, R(EDX), Imm32(0x00ffffff));
			SHR(32, R(EDX), R(CL));
			AND(32, R(EAX), R(EDX));

			// This is the target value we saved earlier.
			POP(EDX);
			OR(32, R(EDX), R(EAX));
			break;

		default:
			_dbg_assert_msg_(false, "Unsupported left/right load/store instruction.");
		}
	}

	void Jit::Comp_ITypeMem(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(LSU);
		int offset = _IMM16;
		MIPSGPReg rs = _RS;
		MIPSGPReg rt = _RT;
		int o = op>>26;
		if (((op >> 29) & 1) == 0 && rt == MIPS_REG_ZERO) {
			// Don't load anything into $zr
			return;
		}

		CheckMemoryBreakpoint(0, rs, offset);

		switch (o)
		{
		case 37: //R(rt) = ReadMem16(addr); break; //lhu
			CompITypeMemRead(op, 16, &XEmitter::MOVZX, safeMemFuncs.readU16);
			break;

		case 36: //R(rt) = ReadMem8 (addr); break; //lbu
			CompITypeMemRead(op, 8, &XEmitter::MOVZX,  safeMemFuncs.readU8);
			break;

		case 35: //R(rt) = ReadMem32(addr); break; //lw
			CompITypeMemRead(op, 32, &XEmitter::MOVZX, safeMemFuncs.readU32);
			break;

		case 32: //R(rt) = SignExtend8ToU32 (ReadMem8 (addr)); break; //lb
			CompITypeMemRead(op, 8, &XEmitter::MOVSX, safeMemFuncs.readU8);
			break;

		case 33: //R(rt) = SignExtend16ToU32(ReadMem16(addr)); break; //lh
			CompITypeMemRead(op, 16, &XEmitter::MOVSX, safeMemFuncs.readU16);
			break;

		case 40: //WriteMem8 (addr, R(rt)); break; //sb
			CompITypeMemWrite(op, 8, safeMemFuncs.writeU8);
			break;

		case 41: //WriteMem16(addr, R(rt)); break; //sh
			CompITypeMemWrite(op, 16, safeMemFuncs.writeU16);
			break;

		case 43: //WriteMem32(addr, R(rt)); break; //sw
			CompITypeMemWrite(op, 32, safeMemFuncs.writeU32);
			break;

		case 34: //lwl
			{
				MIPSOpcode nextOp = GetOffsetInstruction(1);
				// Looking for lwr rd, offset-3(rs) which makes a pair.
				u32 desiredOp = ((op & 0xFFFF0000) + (4 << 26)) + (offset - 3);
				if (!js.inDelaySlot && nextOp == desiredOp && !jo.Disabled(JitDisable::LSU_UNALIGNED))
				{
					CheckMemoryBreakpoint(1, rs, offset - 3);
					EatInstruction(nextOp);
					// nextOp has the correct address.
					CompITypeMemRead(nextOp, 32, &XEmitter::MOVZX, safeMemFuncs.readU32);
				}
				else
					CompITypeMemUnpairedLR(op, false);
			}
			break;

		case 38: //lwr
			{
				MIPSOpcode nextOp = GetOffsetInstruction(1);
				// Looking for lwl rd, offset+3(rs) which makes a pair.
				u32 desiredOp = ((op & 0xFFFF0000) - (4 << 26)) + (offset + 3);
				if (!js.inDelaySlot && nextOp == desiredOp && !jo.Disabled(JitDisable::LSU_UNALIGNED))
				{
					CheckMemoryBreakpoint(1, rs, offset + 3);
					EatInstruction(nextOp);
					// op has the correct address.
					CompITypeMemRead(op, 32, &XEmitter::MOVZX, safeMemFuncs.readU32);
				}
				else
					CompITypeMemUnpairedLR(op, false);
			}
			break;

		case 42: //swl
			{
				MIPSOpcode nextOp = GetOffsetInstruction(1);
				// Looking for swr rd, offset-3(rs) which makes a pair.
				u32 desiredOp = ((op & 0xFFFF0000) + (4 << 26)) + (offset - 3);
				if (!js.inDelaySlot && nextOp == desiredOp && !jo.Disabled(JitDisable::LSU_UNALIGNED))
				{
					CheckMemoryBreakpoint(1, rs, offset - 3);
					EatInstruction(nextOp);
					// nextOp has the correct address.
					CompITypeMemWrite(nextOp, 32, safeMemFuncs.writeU32);
				}
				else
					CompITypeMemUnpairedLR(op, true);
			}
			break;

		case 46: //swr
			{
				MIPSOpcode nextOp = GetOffsetInstruction(1);
				// Looking for swl rd, offset+3(rs) which makes a pair.
				u32 desiredOp = ((op & 0xFFFF0000) - (4 << 26)) + (offset + 3);
				if (!js.inDelaySlot && nextOp == desiredOp && !jo.Disabled(JitDisable::LSU_UNALIGNED))
				{
					CheckMemoryBreakpoint(1, rs, offset + 3);
					EatInstruction(nextOp);
					// op has the correct address.
					CompITypeMemWrite(op, 32, safeMemFuncs.writeU32);
				}
				else
					CompITypeMemUnpairedLR(op, true);
			}
			break;

		default:
			Comp_Generic(op);
			return;
		}

	}

	void Jit::Comp_StoreSync(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		// Note: still does something even if loading to zero.

		CheckMemoryBreakpoint(0, rs, offset);

		FixupBranch skipStore;
		FixupBranch finish;
		switch (op >> 26) {
		case 48: // ll
			CompITypeMemRead(op, 32, &XEmitter::MOVZX, safeMemFuncs.readU32);
			MOV(8, MDisp(X64JitConstants::CTXREG, -128 + offsetof(MIPSState, llBit)), Imm8(1));
			break;

		case 56: // sc
			// Map before the jump in case any regs spill.  Unlock happens inside CompITypeMemWrite().
			// This is not a very common op, but it's in jit so memory breakpoints can trip.
			gpr.Lock(rt, rs);
			gpr.MapReg(rt, true, true);
			gpr.MapReg(rs, true, false);

			CMP(8, MDisp(X64JitConstants::CTXREG, -128 + offsetof(MIPSState, llBit)), Imm8(1));
			skipStore = J_CC(CC_NE);

			CompITypeMemWrite(op, 32, safeMemFuncs.writeU32, true);
			MOV(32, gpr.R(rt), Imm32(1));
			finish = J();

			SetJumpTarget(skipStore);
			MOV(32, gpr.R(rt), Imm32(0));
			SetJumpTarget(finish);
			break;

		default:
			INVALIDOP;
		}
	}

	void Jit::Comp_Cache(MIPSOpcode op) {
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

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
