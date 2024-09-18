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

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Reporting.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

#include "Common/Arm64Emitter.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM26 (op & 0x03FFFFFF)
#define TARGET16 ((int)(SignExtend16ToU32(op) << 2))
#define TARGET26 (_IMM26 << 2)

#define LOOPOPTIMIZATION 0

// We can disable nice delay slots.
// #define CONDITIONAL_NICE_DELAYSLOT branchInfo.delaySlotIsNice = false;
#define CONDITIONAL_NICE_DELAYSLOT ;

using namespace MIPSAnalyst;

namespace MIPSComp
{
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;

void Arm64Jit::BranchRSRTComp(MIPSOpcode op, CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in RSRTComp delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), false, likely);
	branchInfo.delaySlotIsNice = IsDelaySlotNiceReg(op, branchInfo.delaySlotOp, rt, rs);
	CONDITIONAL_NICE_DELAYSLOT;

	bool immBranch = false;
	bool immBranchTaken = false;
	if (gpr.IsImm(rs) && gpr.IsImm(rt) && !branchInfo.delaySlotIsBranch) {
		// The cc flags are opposites: when NOT to take the branch.
		bool immBranchNotTaken;
		s32 rsImm = (s32)gpr.GetImm(rs);
		s32 rtImm = (s32)gpr.GetImm(rt);

		switch (cc)
		{
		case CC_EQ: immBranchNotTaken = rsImm == rtImm; break;
		case CC_NEQ: immBranchNotTaken = rsImm != rtImm; break;
		default: immBranchNotTaken = false; _dbg_assert_msg_(false, "Bad cc flag in BranchRSRTComp().");
		}
		immBranch = true;
		immBranchTaken = !immBranchNotTaken;
	}

	if (jo.immBranches && immBranch && js.numInstructions < jo.continueMaxInstructions) {
		if (!immBranchTaken) {
			// Skip the delay slot if likely, otherwise it'll be the next instruction.
			if (likely)
				js.compilerPC += 4;
			return;
		}

		// Branch taken.  Always compile the delay slot, and then go to dest.
		CompileDelaySlot(DELAYSLOT_NICE);
		AddContinuedBlock(targetAddr);
		// Account for the increment in the loop.
		js.compilerPC = targetAddr - 4;
		// In case the delay slot was a break or something.
		js.compiling = true;
		return;
	}

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);

	u32 notTakenTarget = ResolveNotTakenTarget(branchInfo);
	if (immBranch) {
		// Continuing is handled above, this is just static jumping.
		if (immBranchTaken || !likely)
			CompileDelaySlot(DELAYSLOT_FLUSH);
		else
			FlushAll();

		const u32 destAddr = immBranchTaken ? targetAddr : notTakenTarget;
		WriteExit(destAddr, js.nextExit++);
	} else {
		if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_NICE);

		// We might be able to flip the condition (EQ/NEQ are easy.)
		const bool canFlip = cc == CC_EQ || cc == CC_NEQ;
		const bool rsIsZero = gpr.IsImm(rs) && gpr.GetImm(rs) == 0;
		const bool rtIsZero = gpr.IsImm(rt) && gpr.GetImm(rt) == 0;

		Arm64Gen::FixupBranch ptr;
		if ((likely || branchInfo.delaySlotIsNice) && (rsIsZero || rtIsZero) && canFlip) {
			// Special case, we can just use CBZ/CBNZ directly.
			MIPSGPReg r = rsIsZero ? rt : rs;
			gpr.MapReg(r);
			// Flush should keep r in the same armreg.
			ARM64Reg ar = gpr.R(r);
			FlushAll();
			if (cc == CC_EQ) {
				ptr = CBZ(ar);
			} else {
				ptr = CBNZ(ar);
			}
		} else {
			u32 val;
			bool shift;
			if (gpr.IsImm(rt) && IsImmArithmetic(gpr.GetImm(rt), &val, &shift)) {
				gpr.MapReg(rs);
				CMP(gpr.R(rs), val, shift);
			} else if (gpr.IsImm(rt) && IsImmArithmetic((u64)(s64)-(s32)gpr.GetImm(rt), &val, &shift)) {
				gpr.MapReg(rs);
				CMN(gpr.R(rs), val, shift);
			} else if (gpr.IsImm(rs) && IsImmArithmetic(gpr.GetImm(rs), &val, &shift) && canFlip) {
				gpr.MapReg(rt);
				CMP(gpr.R(rt), val, shift);
			} else if (gpr.IsImm(rs) && IsImmArithmetic((u64)(s64)-(s32)gpr.GetImm(rs), &val, &shift) && canFlip) {
				gpr.MapReg(rt);
				CMN(gpr.R(rt), val, shift);
			} else {
				gpr.MapInIn(rs, rt);
				CMP(gpr.R(rs), gpr.R(rt));
			}

			if (!likely) {
				if (!branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
					CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
				else
					FlushAll();
				ptr = B(cc);
			} else {
				FlushAll();
				ptr = B(cc);
			}
		}

		if (likely && !branchInfo.delaySlotIsBranch) {
			// Only executed when taking the branch.
			CompileDelaySlot(DELAYSLOT_FLUSH);
		}

		if (branchInfo.delaySlotIsBranch) {
			// We still link when the branch is taken (targetAddr case.)
			// Remember, it's from the perspective of the delay slot, so +12.
			if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
				gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 12);
			if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
				gpr.SetImm(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
			FlushAll();
		}

		// Take the branch
		WriteExit(targetAddr, js.nextExit++);

		SetJumpTarget(ptr);
		// Not taken
		WriteExit(notTakenTarget, js.nextExit++);
	}

	js.compiling = false;
}


void Arm64Jit::BranchRSZeroComp(MIPSOpcode op, CCFlags cc, bool andLink, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in RSZeroComp delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	MIPSGPReg rs = _RS;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), andLink, likely);
	branchInfo.delaySlotIsNice = IsDelaySlotNiceReg(op, branchInfo.delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;

	bool immBranch = false;
	bool immBranchTaken = false;
	if (gpr.IsImm(rs) && !branchInfo.delaySlotIsBranch) {
		// The cc flags are opposites: when NOT to take the branch.
		bool immBranchNotTaken;
		s32 imm = (s32)gpr.GetImm(rs);

		switch (cc)
		{
		case CC_GT: immBranchNotTaken = imm > 0; break;
		case CC_GE: immBranchNotTaken = imm >= 0; break;
		case CC_LT: immBranchNotTaken = imm < 0; break;
		case CC_LE: immBranchNotTaken = imm <= 0; break;
		default: immBranchNotTaken = false; _dbg_assert_msg_(false, "Bad cc flag in BranchRSZeroComp().");
		}
		immBranch = true;
		immBranchTaken = !immBranchNotTaken;
	}

	if (jo.immBranches && immBranch && js.numInstructions < jo.continueMaxInstructions) {
		if (!immBranchTaken) {
			// Skip the delay slot if likely, otherwise it'll be the next instruction.
			if (andLink)
				gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
			if (likely)
				js.compilerPC += 4;
			return;
		}

		// Branch taken.  Always compile the delay slot, and then go to dest.
		if (andLink)
			gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);

		AddContinuedBlock(targetAddr);
		// Account for the increment in the loop.
		js.compilerPC = targetAddr - 4;
		// In case the delay slot was a break or something.
		js.compiling = true;
		return;
	}

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);

	u32 notTakenTarget = ResolveNotTakenTarget(branchInfo);
	if (immBranch) {
		// Continuing is handled above, this is just static jumping.
		if (andLink)
			gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
		if (immBranchTaken || !likely)
			CompileDelaySlot(DELAYSLOT_FLUSH);
		else
			FlushAll();

		const u32 destAddr = immBranchTaken ? targetAddr : notTakenTarget;
		WriteExit(destAddr, js.nextExit++);
	} else {
		if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_NICE);

		gpr.MapReg(rs);
		CMP(gpr.R(rs), 0);

		if (andLink)
			gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);

		Arm64Gen::FixupBranch ptr;
		if (!likely)
		{
			if (!branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
				CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
			else
				FlushAll();
			ptr = B(cc);
		}
		else
		{
			FlushAll();
			ptr = B(cc);
			if (!branchInfo.delaySlotIsBranch)
				CompileDelaySlot(DELAYSLOT_FLUSH);
		}

		if (branchInfo.delaySlotIsBranch) {
			// We still link when the branch is taken (targetAddr case.)
			// Remember, it's from the perspective of the delay slot, so +12.
			if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
				gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 12);
			if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
				gpr.SetImm(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
			FlushAll();
		}

		// Take the branch
		WriteExit(targetAddr, js.nextExit++);

		SetJumpTarget(ptr);
		// Not taken
		WriteExit(notTakenTarget, js.nextExit++);
	}
	js.compiling = false;
}


void Arm64Jit::Comp_RelBranch(MIPSOpcode op)
{
	// The CC flags here should be opposite of the actual branch becuase they skip the branching action.
	switch (op >> 26)
	{
	case 4: BranchRSRTComp(op, CC_NEQ, false); break;//beq
	case 5: BranchRSRTComp(op, CC_EQ,  false); break;//bne

	case 6: BranchRSZeroComp(op, CC_GT, false, false); break;//blez
	case 7: BranchRSZeroComp(op, CC_LE, false, false); break;//bgtz

	case 20: BranchRSRTComp(op, CC_NEQ, true); break;//beql
	case 21: BranchRSRTComp(op, CC_EQ,  true); break;//bnel

	case 22: BranchRSZeroComp(op, CC_GT, false, true); break;//blezl
	case 23: BranchRSZeroComp(op, CC_LE, false, true); break;//bgtzl

	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}

void Arm64Jit::Comp_RelBranchRI(MIPSOpcode op)
{
	switch ((op >> 16) & 0x1F)
	{
	case 0: BranchRSZeroComp(op, CC_GE, false, false); break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltz
	case 1: BranchRSZeroComp(op, CC_LT, false, false); break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, CC_GE, false, true);  break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, CC_LT, false, true);  break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	case 16: BranchRSZeroComp(op, CC_GE, true, false); break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltzal
	case 17: BranchRSZeroComp(op, CC_LT, true, false);  break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgezal
	case 18: BranchRSZeroComp(op, CC_GE, true, true);  break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else SkipLikely(); break;//bltzall
	case 19: BranchRSZeroComp(op, CC_LT, true, true);   break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezall
	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void Arm64Jit::BranchFPFlag(MIPSOpcode op, CCFlags cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in FPFlag delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), false, likely);
	branchInfo.delaySlotIsNice = IsDelaySlotNiceFPU(op, branchInfo.delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);
	if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot(DELAYSLOT_NICE);

	gpr.MapReg(MIPS_REG_FPCOND);
	Arm64Gen::FixupBranch ptr;
	if (likely || branchInfo.delaySlotIsNice) {
		// FlushAll() won't actually change the reg.
		ARM64Reg ar = gpr.R(MIPS_REG_FPCOND);
		FlushAll();
		if (cc == CC_EQ) {
			ptr = TBZ(ar, 0);
		} else {
			ptr = TBNZ(ar, 0);
		}
	} else {
		TSTI2R(gpr.R(MIPS_REG_FPCOND), 1, SCRATCH1);
		if (!branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B(cc);
	}

	if (likely && !branchInfo.delaySlotIsBranch) {
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			gpr.SetImm(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
		FlushAll();
	}

	// Take the branch
	WriteExit(targetAddr, js.nextExit++);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(ResolveNotTakenTarget(branchInfo), js.nextExit++);
	js.compiling = false;
}

void Arm64Jit::Comp_FPUBranch(MIPSOpcode op) {
	switch((op >> 16) & 0x1f) {
	case 0:	BranchFPFlag(op, CC_NEQ, false); break;  // bc1f
	case 1: BranchFPFlag(op, CC_EQ, false); break;  // bc1t
	case 2: BranchFPFlag(op, CC_NEQ, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, CC_EQ, true);  break;  // bc1tl
	default:
		_dbg_assert_msg_( 0, "Trying to interpret instruction that can't be interpreted");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void Arm64Jit::BranchVFPUFlag(MIPSOpcode op, CCFlags cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in VFPU delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), false, likely);
	// Sometimes there's a VFPU branch in a delay slot (Disgaea 2: Dark Hero Days, Zettai Hero Project, La Pucelle)
	// The behavior is undefined - the CPU may take the second branch even if the first one passes.
	// However, it does consistently try each branch, which these games seem to expect.
	branchInfo.delaySlotIsNice = IsDelaySlotNiceVFPU(op, branchInfo.delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);
	if (!likely && branchInfo.delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	int imm3 = (op >> 18) & 7;

	gpr.MapReg(MIPS_REG_VFPUCC);
	Arm64Gen::FixupBranch ptr;
	if (likely || branchInfo.delaySlotIsNice || branchInfo.delaySlotIsBranch) {
		// FlushAll() won't actually change the reg.
		ARM64Reg ar = gpr.R(MIPS_REG_VFPUCC);
		FlushAll();
		if (cc == CC_EQ) {
			ptr = TBZ(ar, imm3);
		} else {
			ptr = TBNZ(ar, imm3);
		}
	} else {
		TSTI2R(gpr.R(MIPS_REG_VFPUCC), 1ULL << imm3, SCRATCH1);
		if (!branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B(cc);
	}

	if (likely && !branchInfo.delaySlotIsBranch) {
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			gpr.SetImm(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
		FlushAll();
	}

	// Take the branch
	WriteExit(targetAddr, js.nextExit++);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(ResolveNotTakenTarget(branchInfo), js.nextExit++);
	js.compiling = false;
}

void Arm64Jit::Comp_VBranch(MIPSOpcode op)
{
	switch ((op >> 16) & 3)
	{
	case 0:	BranchVFPUFlag(op, CC_NEQ, false); break;  // bvf
	case 1: BranchVFPUFlag(op, CC_EQ,  false); break;  // bvt
	case 2: BranchVFPUFlag(op, CC_NEQ, true);  break;  // bvfl
	case 3: BranchVFPUFlag(op, CC_EQ,  true);  break;  // bvtl
	}
}

static void HitInvalidJump(uint32_t dest) {
	Core_ExecException(dest, currentMIPS->pc - 8, ExecExceptionType::JUMP);
}

void Arm64Jit::Comp_Jump(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in Jump delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	u32 off = TARGET26;
	u32 targetAddr = (GetCompilerPC() & 0xF0000000) | off;

	// Might be a stubbed address or something?
	if (!Memory::IsValidAddress(targetAddr) || (targetAddr & 3) != 0) {
		if (js.nextExit == 0) {
			ERROR_LOG_REPORT(Log::JIT, "Jump to invalid address: %08x", targetAddr);
		} else {
			js.compiling = false;
		}
		// TODO: Mark this block dirty or something?  May be indication it will be changed by imports.
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		gpr.SetRegImm(SCRATCH1, GetCompilerPC() + 8);
		MovToPC(SCRATCH1);
		MOVI2R(W0, targetAddr);
		QuickCallFunction(SCRATCH1, (const void *)&HitInvalidJump);
		WriteSyscallExit();
		return;
	}

	switch (op >> 26) {
	case 2: //j
		CompileDelaySlot(DELAYSLOT_NICE);
		if (jo.continueJumps && js.numInstructions < jo.continueMaxInstructions) {
			AddContinuedBlock(targetAddr);
			// Account for the increment in the loop.
			js.compilerPC = targetAddr - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}
		FlushAll();
		WriteExit(targetAddr, js.nextExit++);
		break;

	case 3: //jal
		if (ReplaceJalTo(targetAddr))
			return;

		gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		if (jo.continueJumps && js.numInstructions < jo.continueMaxInstructions) {
			AddContinuedBlock(targetAddr);
			// Account for the increment in the loop.
			js.compilerPC = targetAddr - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}
		FlushAll();
		WriteExit(targetAddr, js.nextExit++);
		break;

	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}

void Arm64Jit::Comp_JumpReg(MIPSOpcode op)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in JumpReg delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;
	bool andLink = (op & 0x3f) == 9 && rd != MIPS_REG_ZERO;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);
	js.downcountAmount += MIPSGetInstructionCycleEstimate(delaySlotOp);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	if (andLink && rs == rd)
		delaySlotIsNice = false;
	CONDITIONAL_NICE_DELAYSLOT;

	ARM64Reg destReg = INVALID_REG;
	if (IsSyscall(delaySlotOp)) {
		gpr.MapReg(rs);
		MovToPC(gpr.R(rs));  // For syscall to be able to return.
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_FLUSH);
		return;  // Syscall (delay slot) wrote exit code.
	} else if (delaySlotIsNice) {
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);

		if (!andLink && rs == MIPS_REG_RA && g_Config.bDiscardRegsOnJRRA) {
			// According to the MIPS ABI, there are some regs we don't need to preserve.
			// Let's discard them so we don't need to write them back.
			// NOTE: Not all games follow the MIPS ABI! Tekken 6, for example, will crash
			// with this enabled.
			gpr.DiscardR(MIPS_REG_COMPILER_SCRATCH);
			for (int i = MIPS_REG_A0; i <= MIPS_REG_T7; i++)
				gpr.DiscardR((MIPSGPReg)i);
			gpr.DiscardR(MIPS_REG_T8);
			gpr.DiscardR(MIPS_REG_T9);
		}

		if (jo.continueJumps && gpr.IsImm(rs) && js.numInstructions < jo.continueMaxInstructions) {
			AddContinuedBlock(gpr.GetImm(rs));
			// Account for the increment in the loop.
			js.compilerPC = gpr.GetImm(rs) - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}

		gpr.MapReg(rs);
		destReg = gpr.R(rs);  // Safe because FlushAll doesn't change any regs
		FlushAll();
	} else {
		// Since we can't be in a delay slot, should be safe to steal FLAGTEMPREG for a temp reg.
		// It will be saved, even if a function is called.
		destReg = DecodeReg(FLAGTEMPREG);
		gpr.MapReg(rs);
		MOV(destReg, gpr.R(rs));
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
	}

	switch (op & 0x3f) 
	{
	case 8: //jr
		break;
	case 9: //jalr
		break;
	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}

	WriteExitDestInR(destReg);
	js.compiling = false;
}

	
void Arm64Jit::Comp_Syscall(MIPSOpcode op)
{
	if (op.encoding == 0x03FFFFcc) {
		WARN_LOG(Log::JIT, "Encountered bad syscall instruction at %08x (%08x)", js.compilerPC, op.encoding);
	}
	if (!g_Config.bSkipDeadbeefFilling)
	{
		// All of these will be overwritten with DEADBEEF anyway.
		gpr.DiscardR(MIPS_REG_COMPILER_SCRATCH);
		// We need to keep A0 - T3, which are used for args.
		gpr.DiscardR(MIPS_REG_T4);
		gpr.DiscardR(MIPS_REG_T5);
		gpr.DiscardR(MIPS_REG_T6);
		gpr.DiscardR(MIPS_REG_T7);
		gpr.DiscardR(MIPS_REG_T8);
		gpr.DiscardR(MIPS_REG_T9);

		gpr.DiscardR(MIPS_REG_HI);
		gpr.DiscardR(MIPS_REG_LO);
	}

	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	WriteDownCount(offset, false);
	RestoreRoundingMode();
	js.downcountAmount = -offset;

	if (!js.inDelaySlot) {
		gpr.SetRegImm(SCRATCH1, GetCompilerPC() + 4);
		MovToPC(SCRATCH1);
	}

	FlushAll();

	SaveStaticRegisters();
#ifdef USE_PROFILER
	// When profiling, we can't skip CallSyscall, since it times syscalls.
	MOVI2R(W0, op.encoding);
	QuickCallFunction(X1, (void *)&CallSyscall);
#else
	// Skip the CallSyscall where possible.
	void *quickFunc = GetQuickSyscallFunc(op);
	if (quickFunc) {
		MOVI2R(X0, (uintptr_t)GetSyscallFuncPointer(op));
		// Already flushed, so X1 is safe.
		QuickCallFunction(X1, quickFunc);
	} else {
		MOVI2R(W0, op.encoding);
		QuickCallFunction(X1, (void *)&CallSyscall);
	}
#endif
	LoadStaticRegisters();
	ApplyRoundingMode();

	WriteSyscallExit();
	js.compiling = false;
}

void Arm64Jit::Comp_Break(MIPSOpcode op)
{
	Comp_Generic(op);
	WriteSyscallExit();
	js.compiling = false;
}

}   // namespace Mipscomp

#endif // PPSSPP_ARCH(ARM64)
