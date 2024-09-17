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

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"

#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

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

#define MIPS_IS_BREAK(op) (((op) & 0xFC00003F) == 13)

using namespace MIPSAnalyst;

namespace MIPSComp
{

void IRFrontend::BranchRSRTComp(MIPSOpcode op, IRComparison cc, bool likely) {
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

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);

	// Often, div/divu are followed by a likely "break" if the divisor was zero.
	// Stalling is not really useful for us, so we optimize this out.
	if (likely && offset == 4 && MIPS_IS_BREAK(branchInfo.delaySlotOp)) {
		// Okay, let's not actually branch at all.  We're done here.
		EatInstruction(branchInfo.delaySlotOp);
		// Let's not double-count the downcount, though.
		js.downcountAmount--;
		return;
	}

	MIPSGPReg lhs = rs;
	MIPSGPReg rhs = rt;
	if (!branchInfo.delaySlotIsNice && !likely) {  // if likely, we don't need this
		if (rs != 0) {
			ir.Write(IROp::Mov, IRTEMP_LHS, rs);
			lhs = (MIPSGPReg)IRTEMP_LHS;
		}
		if (rt != 0) {
			ir.Write(IROp::Mov, IRTEMP_RHS, rt);
			rhs = (MIPSGPReg)IRTEMP_RHS;
		}
	}

	if (!likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(ResolveNotTakenTarget(branchInfo)), lhs, rhs);
	// This makes the block "impure" :(
	if (likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();
	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			ir.WriteSetConstant(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
	}

	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::BranchRSZeroComp(MIPSOpcode op, IRComparison cc, bool andLink, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in RSZeroComp delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	MIPSGPReg rs = _RS;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), andLink, likely);
	branchInfo.delaySlotIsNice = IsDelaySlotNiceReg(op, branchInfo.delaySlotOp, rs);

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);

	MIPSGPReg lhs = rs;
	if (!branchInfo.delaySlotIsNice) {  // if likely, we don't need this
		ir.Write(IROp::Mov, IRTEMP_LHS, rs);
		lhs = (MIPSGPReg)IRTEMP_LHS;
	}
	if (andLink)
		ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 8);

	if (!likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(ResolveNotTakenTarget(branchInfo)), lhs);
	if (likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();
	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			ir.WriteSetConstant(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
	}

	// Taken
	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::Comp_RelBranch(MIPSOpcode op) {
	// The CC flags here should be opposite of the actual branch because they skip the branching action.
	switch (op >> 26) {
	case 4: BranchRSRTComp(op, IRComparison::NotEqual, false); break;//beq
	case 5: BranchRSRTComp(op, IRComparison::Equal, false); break;//bne

	case 6: BranchRSZeroComp(op, IRComparison::Greater, false, false); break;//blez
	case 7: BranchRSZeroComp(op, IRComparison::LessEqual, false, false); break;//bgtz

	case 20: BranchRSRTComp(op, IRComparison::NotEqual, true); break;//beql
	case 21: BranchRSRTComp(op, IRComparison::Equal, true); break;//bnel

	case 22: BranchRSZeroComp(op, IRComparison::Greater, false, true); break;//blezl
	case 23: BranchRSZeroComp(op, IRComparison::LessEqual, false, true); break;//bgtzl

	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}

void IRFrontend::Comp_RelBranchRI(MIPSOpcode op) {
	switch ((op >> 16) & 0x1F) {
	case 0: BranchRSZeroComp(op, IRComparison::GreaterEqual, false, false); break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltz
	case 1: BranchRSZeroComp(op, IRComparison::Less, false, false); break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, IRComparison::GreaterEqual, false, true);  break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, IRComparison::Less, false, true);  break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	case 16: BranchRSZeroComp(op, IRComparison::GreaterEqual, true, false); break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltzal
	case 17: BranchRSZeroComp(op, IRComparison::Less, true, false);  break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgezal
	case 18: BranchRSZeroComp(op, IRComparison::GreaterEqual, true, true);  break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else SkipLikely(); break;//bltzall
	case 19: BranchRSZeroComp(op, IRComparison::Less, true, true);   break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezall
	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void IRFrontend::BranchFPFlag(MIPSOpcode op, IRComparison cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in FPFlag delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), false, likely);

	ir.Write(IROp::FpCondToReg, IRTEMP_LHS);
	if (!likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	FlushAll();
	// Not taken
	ir.Write(ComparisonToExit(cc), ir.AddConstant(ResolveNotTakenTarget(branchInfo)), IRTEMP_LHS, 0);
	// Taken
	if (likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();
	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			ir.WriteSetConstant(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
	}

	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::Comp_FPUBranch(MIPSOpcode op) {
	switch((op >> 16) & 0x1f) {
	case 0:	BranchFPFlag(op, IRComparison::NotEqual, false); break;  // bc1f
	case 1: BranchFPFlag(op, IRComparison::Equal, false); break;  // bc1t
	case 2: BranchFPFlag(op, IRComparison::NotEqual, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, IRComparison::Equal, true);  break;  // bc1tl
	default:
		_dbg_assert_msg_( 0, "Trying to interpret instruction that can't be interpreted");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void IRFrontend::BranchVFPUFlag(MIPSOpcode op, IRComparison cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in VFPU delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = TARGET16;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	BranchInfo branchInfo(GetCompilerPC(), op, GetOffsetInstruction(1), false, likely);

	js.downcountAmount += MIPSGetInstructionCycleEstimate(branchInfo.delaySlotOp);
	ir.Write(IROp::VfpuCtrlToReg, IRTEMP_LHS, VFPU_CTRL_CC);

	// Sometimes there's a VFPU branch in a delay slot (Disgaea 2: Dark Hero Days, Zettai Hero Project, La Pucelle)
	// The behavior is undefined - the CPU may take the second branch even if the first one passes.
	// However, it does consistently try each branch, which these games seem to expect.
	if (!likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	int imm3 = (op >> 18) & 7;

	ir.Write(IROp::AndConst, IRTEMP_LHS, IRTEMP_LHS, ir.AddConstant(1 << imm3));
	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(ResolveNotTakenTarget(branchInfo)), IRTEMP_LHS, 0);

	if (likely && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot();
	if (branchInfo.delaySlotIsBranch) {
		// We still link when the branch is taken (targetAddr case.)
		// Remember, it's from the perspective of the delay slot, so +12.
		if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
			ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 12);
		if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
			ir.WriteSetConstant(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
	}

	// Taken
	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::Comp_VBranch(MIPSOpcode op) {
	switch ((op >> 16) & 3) {
	case 0:	BranchVFPUFlag(op, IRComparison::NotEqual, false); break;  // bvf
	case 1: BranchVFPUFlag(op, IRComparison::Equal,  false); break;  // bvt
	case 2: BranchVFPUFlag(op, IRComparison::NotEqual, true);  break;  // bvfl
	case 3: BranchVFPUFlag(op, IRComparison::Equal,  true);  break;  // bvtl
	}
}

void IRFrontend::Comp_Jump(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in Jump delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}

	u32 off = TARGET26;
	u32 targetAddr = (GetCompilerPC() & 0xF0000000) | off;

	// Might be a stubbed address or something?
	if (!Memory::IsValidAddress(targetAddr)) {
		// If preloading, flush - this block will likely be fixed later.
		if (js.preloading)
			js.cancel = true;
		else
			ERROR_LOG_REPORT(Log::JIT, "Jump to invalid address: %08x", targetAddr);
		// TODO: Mark this block dirty or something?  May be indication it will be changed by imports.
		// Continue so the block gets completed and crashes properly.
	}

	switch (op >> 26) {
	case 2: //j
		CompileDelaySlot();
		break;

	case 3: //jal
		ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot();
		break;

	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::Comp_JumpReg(MIPSOpcode op) {
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

	int destReg;
	if (IsSyscall(delaySlotOp)) {
		ir.Write(IROp::SetPC, 0, rs);
		if (andLink)
			ir.WriteSetConstant(rd, GetCompilerPC() + 8);
		CompileDelaySlot();
		// Syscall (the delay slot) does FlushAll.

		// Account for the delay slot itself in total bytes.
		js.compilerPC += 4;
		return;  // Syscall (delay slot) wrote exit code.
	} else if (delaySlotIsNice) {
		if (andLink)
			ir.WriteSetConstant(rd, GetCompilerPC() + 8);
		CompileDelaySlot();
		destReg = rs;  // Safe because FlushAll doesn't change any regs
		FlushAll();
	} else {
		// Bad delay slot.
		ir.Write(IROp::Mov, IRTEMP_LHS, rs);
		destReg = IRTEMP_LHS;
		if (andLink)
			ir.WriteSetConstant(rd, GetCompilerPC() + 8);
		CompileDelaySlot();
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

	int dcAmount = js.downcountAmount;
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	ir.Write(IROp::ExitToReg, 0, destReg, 0);

	// Account for the delay slot.
	js.compilerPC += 4;
	js.compiling = false;
}

void IRFrontend::Comp_Syscall(MIPSOpcode op) {
	// Note: If we're in a delay slot, this is off by one compared to the interpreter.
	int dcAmount = js.downcountAmount + (js.inDelaySlot ? -1 : 0);
	ir.Write(IROp::Downcount, 0, ir.AddConstant(dcAmount));
	js.downcountAmount = 0;

	// If not in a delay slot, we need to update PC.
	if (!js.inDelaySlot) {
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC() + 4));
	}

	FlushAll();

	RestoreRoundingMode();
	ir.Write(IROp::Syscall, 0, ir.AddConstant(op.encoding));
	ApplyRoundingMode();
	ir.Write(IROp::ExitToPC);

	js.compiling = false;
}

void IRFrontend::Comp_Break(MIPSOpcode op) {
	ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));
	ir.Write(IROp::Break);
	js.compiling = false;
}

}   // namespace Mipscomp
