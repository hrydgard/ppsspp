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

#include "profiler/profiler.h"

#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/IR/IRJit.h"
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
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

#define LOOPOPTIMIZATION 0

using namespace MIPSAnalyst;

namespace MIPSComp
{
	using namespace Arm64Gen;

void IRJit::BranchRSRTComp(MIPSOpcode op, IRComparison cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSRTComp delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = _IMM16 << 2;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rt, rs);

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	MIPSGPReg lhs = rs;
	MIPSGPReg rhs = rt;
	if (!delaySlotIsNice) {  // if likely, we don't need this
		if (rs != 0) {
			ir.Write(IROp::Mov, IRTEMP_0, rs);
			lhs = (MIPSGPReg)IRTEMP_0;
		}
		if (rt != 0) {
			ir.Write(IROp::Mov, IRTEMP_1, rt);
			rhs = (MIPSGPReg)IRTEMP_1;
		}
	}

	if (!likely)
		CompileDelaySlot();

	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(GetCompilerPC() + 8), lhs, rhs);
	// This makes the block "impure" :(
	if (likely)
		CompileDelaySlot();

	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));

	js.compiling = false;
}

void IRJit::BranchRSZeroComp(MIPSOpcode op, IRComparison cc, bool andLink, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSZeroComp delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = _IMM16 << 2;
	MIPSGPReg rs = _RS;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	MIPSGPReg lhs = rs;
	if (!delaySlotIsNice) {  // if likely, we don't need this
		ir.Write(IROp::Mov, IRTEMP_0, rs);
		lhs = (MIPSGPReg)IRTEMP_0;
	}
	if (andLink)
		ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 8);

	if (!likely)
		CompileDelaySlot();

	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(GetCompilerPC() + 8), lhs);
	if (likely)
		CompileDelaySlot();
	// Taken
	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));
	js.compiling = false;
}

void IRJit::Comp_RelBranch(MIPSOpcode op) {
	// The CC flags here should be opposite of the actual branch becuase they skip the branching action.
	switch (op >> 26) {
	case 4: BranchRSRTComp(op, IRComparison::NotEqual, false); break;//beq
	case 5: BranchRSRTComp(op, IRComparison::Equal,  false); break;//bne

	case 6: BranchRSZeroComp(op, IRComparison::Greater, false, false); break;//blez
	case 7: BranchRSZeroComp(op, IRComparison::LessEqual, false, false); break;//bgtz

	case 20: BranchRSRTComp(op, IRComparison::NotEqual, true); break;//beql
	case 21: BranchRSRTComp(op, IRComparison::Equal,  true); break;//bnel

	case 22: BranchRSZeroComp(op, IRComparison::Greater, false, true); break;//blezl
	case 23: BranchRSZeroComp(op, IRComparison::LessEqual, false, true); break;//bgtzl

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
}

void IRJit::Comp_RelBranchRI(MIPSOpcode op) {
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
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void IRJit::BranchFPFlag(MIPSOpcode op, IRComparison cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in FPFlag delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = _IMM16 << 2;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);
	ir.Write(IROp::FpCondToReg, IRTEMP_0);
	if (!likely)
		CompileDelaySlot();

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	FlushAll();
	// Not taken
	ir.Write(ComparisonToExit(cc), ir.AddConstant(GetCompilerPC() + 8), IRTEMP_0, 0);
	// Taken
	if (likely)
		CompileDelaySlot();
	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));
	js.compiling = false;
}

void IRJit::Comp_FPUBranch(MIPSOpcode op) {
	switch((op >> 16) & 0x1f) {
	case 0:	BranchFPFlag(op, IRComparison::NotEqual, false); break;  // bc1f
	case 1: BranchFPFlag(op, IRComparison::Equal, false); break;  // bc1t
	case 2: BranchFPFlag(op, IRComparison::NotEqual, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, IRComparison::Equal, true);  break;  // bc1tl
	default:
		_dbg_assert_msg_(CPU, 0, "Trying to interpret instruction that can't be interpreted");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void IRJit::BranchVFPUFlag(MIPSOpcode op, IRComparison cc, bool likely) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in VFPU delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	int offset = _IMM16 << 2;
	u32 targetAddr = GetCompilerPC() + offset + 4;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);

	ir.Write(IROp::VfpuCtrlToReg, IRTEMP_0, VFPU_CTRL_CC);

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	// Sometimes there's a VFPU branch in a delay slot (Disgaea 2: Dark Hero Days, Zettai Hero Project, La Pucelle)
	// The behavior is undefined - the CPU may take the second branch even if the first one passes.
	// However, it does consistently try each branch, which these games seem to expect.
	bool delaySlotIsBranch = MIPSCodeUtils::IsVFPUBranch(delaySlotOp);
	if (!likely)
		CompileDelaySlot();

	if (delaySlotIsBranch && (signed short)(delaySlotOp & 0xFFFF) != (signed short)(op & 0xFFFF) - 1)
		ERROR_LOG_REPORT(JIT, "VFPU branch in VFPU delay slot at %08x with different target", GetCompilerPC());

	int imm3 = (op >> 18) & 7;
	
	u32 notTakenTarget = GetCompilerPC() + (delaySlotIsBranch ? 4 : 8);

	ir.Write(IROp::AndConst, IRTEMP_0, IRTEMP_0, ir.AddConstant(1 << imm3));
	FlushAll();
	ir.Write(ComparisonToExit(cc), ir.AddConstant(notTakenTarget), IRTEMP_0, 0);

	if (likely)
		CompileDelaySlot();

	// Taken
	FlushAll();
	ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));
	js.compiling = false;
}

void IRJit::Comp_VBranch(MIPSOpcode op) {
	switch ((op >> 16) & 3) {
	case 0:	BranchVFPUFlag(op, IRComparison::NotEqual, false); break;  // bvf
	case 1: BranchVFPUFlag(op, IRComparison::Equal,  false); break;  // bvt
	case 2: BranchVFPUFlag(op, IRComparison::NotEqual, true);  break;  // bvfl
	case 3: BranchVFPUFlag(op, IRComparison::Equal,  true);  break;  // bvtl
	}
}

void IRJit::Comp_Jump(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in Jump delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}

	u32 off = _IMM26 << 2;
	u32 targetAddr = (GetCompilerPC() & 0xF0000000) | off;

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	// Might be a stubbed address or something?
	if (!Memory::IsValidAddress(targetAddr)) {
		if (js.nextExit == 0) {
			ERROR_LOG_REPORT(JIT, "Jump to invalid address: %08x", targetAddr);
		} else {
			js.compiling = false;
		}
		// TODO: Mark this block dirty or something?  May be indication it will be changed by imports.
		return;
	}

	switch (op >> 26) {
	case 2: //j
		CompileDelaySlot();
		FlushAll();
		ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));
		break;

	case 3: //jal
		ir.WriteSetConstant(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot();
		FlushAll();
		ir.Write(IROp::ExitToConst, ir.AddConstant(targetAddr));
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}

void IRJit::Comp_JumpReg(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in JumpReg delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;
	bool andLink = (op & 0x3f) == 9 && rd != MIPS_REG_ZERO;

	MIPSOpcode delaySlotOp = GetOffsetInstruction(1);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	if (andLink && rs == rd)
		delaySlotIsNice = false;

	ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);

	int destReg;
	if (IsSyscall(delaySlotOp)) {
		ir.Write(IROp::SetPC, 0, rs);
		if (andLink)
			ir.WriteSetConstant(rd, GetCompilerPC() + 8);
		CompileDelaySlot();
		// Syscall (the delay slot) does FlushAll.
		return;  // Syscall (delay slot) wrote exit code.
	} else if (delaySlotIsNice) {
		if (andLink)
			ir.WriteSetConstant(rd, GetCompilerPC() + 8);
		CompileDelaySlot();
		destReg = rs;  // Safe because FlushAll doesn't change any regs
		FlushAll();
	} else {
		// Bad delay slot.
		ir.Write(IROp::Mov, IRTEMP_0, rs);
		destReg = IRTEMP_0;
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
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}

	ir.Write(IROp::ExitToReg, destReg, 0, 0);
	js.compiling = false;
}

void IRJit::Comp_Syscall(MIPSOpcode op) {
	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	RestoreRoundingMode();
	js.downcountAmount = -offset;

	FlushAll();

	ir.Write(IROp::Syscall, 0, ir.AddConstant(op.encoding));

	ApplyRoundingMode();
	js.compiling = false;
}

void IRJit::Comp_Break(MIPSOpcode op) {
	ir.Write(IROp::Break);
	js.compiling = false;
}

}   // namespace Mipscomp
