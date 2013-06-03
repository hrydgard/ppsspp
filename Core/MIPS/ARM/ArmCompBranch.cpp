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

#include "Core/Reporting.h"

#include "Core/HLE/HLE.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

#include "Common/ArmEmitter.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define LOOPOPTIMIZATION 0

// We can disable nice delay slots.
#define CONDITIONAL_NICE_DELAYSLOT delaySlotIsNice = false;
// #define CONDITIONAL_NICE_DELAYSLOT ;

using namespace MIPSAnalyst;

namespace MIPSComp
{

void Jit::BranchRSRTComp(u32 op, ArmGen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSRTComp delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rt = _RT;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;
		
	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC+4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rt, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);
	
	if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0)
	{
		gpr.MapReg(rs);
		CMP(gpr.R(rs), Operand2(0, TYPE_IMM));
	}
	else if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0 && (cc == CC_EQ || cc == CC_NEQ))  // only these are easily 'flippable'
	{
		gpr.MapReg(rt);
		CMP(gpr.R(rt), Operand2(0, TYPE_IMM));
	}
	else 
	{
		gpr.MapInIn(rs, rt);
		CMP(gpr.R(rs), gpr.R(rt));
	}

	ArmGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = B_CC(cc);
	}
	else
	{
		FlushAll();
		ptr = B_CC(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC+8, 1);

	js.compiling = false;
}


void Jit::BranchRSZeroComp(u32 op, ArmGen::CCFlags cc, bool andLink, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSZeroComp delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	gpr.MapReg(rs);
	CMP(gpr.R(rs), Operand2(0, TYPE_IMM));

	ArmGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = B_CC(cc);
	}
	else
	{
		FlushAll();
		ptr = B_CC(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	if (andLink)
	{
		MOVI2R(R0, js.compilerPC + 8);
		STR(R0, CTXREG, MIPS_REG_RA * 4);
	}

	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC + 8, 1);
	js.compiling = false;
}


void Jit::Comp_RelBranch(u32 op)
{
	// The CC flags here should be opposite of the actual branch becuase they skip the branching action.
	switch (op>>26) 
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
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  js.compiling = false;
}

void Jit::Comp_RelBranchRI(u32 op)
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
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  js.compiling = false;
}

// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchFPFlag(u32 op, ArmGen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in FPFlag delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();

	LDR(R0, CTXREG, offsetof(MIPSState, fpcond));
	TST(R0, Operand2(1, TYPE_IMM));

	ArmGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B_CC(cc);
	}
	else
	{
		ptr = B_CC(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC + 8, 1);
	js.compiling = false;
}

void Jit::Comp_FPUBranch(u32 op)
{
	switch((op >> 16) & 0x1f)
	{
	case 0:	BranchFPFlag(op, CC_NEQ, false); break;  // bc1f
	case 1: BranchFPFlag(op, CC_EQ,  false); break;  // bc1t
	case 2: BranchFPFlag(op, CC_NEQ, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, CC_EQ,  true);  break;  // bc1tl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		break;
	}
  js.compiling = false;
}

// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchVFPUFlag(u32 op, ArmGen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in VFPU delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

	bool delaySlotIsNice = IsDelaySlotNiceVFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();

	int imm3 = (op >> 18) & 7;

	MOVI2R(R0, (u32)&(mips_->vfpuCtrl[VFPU_CTRL_CC]));
	LDR(R0, R0, Operand2(0, TYPE_IMM));
	TST(R0, Operand2(1 << imm3, TYPE_IMM));

	ArmGen::FixupBranch ptr;
	js.inDelaySlot = true;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B_CC(cc);
	}
	else
	{
		ptr = B_CC(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}
	js.inDelaySlot = false;

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC + 8, 1);
	js.compiling = false;
}

void Jit::Comp_VBranch(u32 op)
{
	switch ((op >> 16) & 3)
	{
	case 0:	BranchVFPUFlag(op, CC_NEQ, false); break;  // bvf
	case 1: BranchVFPUFlag(op, CC_EQ,  false); break;  // bvt
	case 2: BranchVFPUFlag(op, CC_NEQ, true);  break;  // bvfl
	case 3: BranchVFPUFlag(op, CC_EQ,  true);  break;  // bvtl
	}
	js.compiling = false;
}

void Jit::Comp_Jump(u32 op)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in Jump delay slot at %08x", js.compilerPC);
		return;
	}
	u32 off = ((op & 0x03FFFFFF) << 2);
	u32 targetAddr = (js.compilerPC & 0xF0000000) | off;

	switch (op >> 26) 
	{
	case 2: //j
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		WriteExit(targetAddr, 0);
		break;

	case 3: //jal
		gpr.MapReg(MIPS_REG_RA, MAP_NOINIT | MAP_DIRTY);
		MOVI2R(gpr.R(MIPS_REG_RA), js.compilerPC + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		WriteExit(targetAddr, 0);
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}

void Jit::Comp_JumpReg(u32 op)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in JumpReg delay slot at %08x", js.compilerPC);
		return;
	}
	int rs = _RS;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;

	if (IsSyscall(delaySlotOp)) {
		gpr.MapReg(rs);
		MOV(R8, gpr.R(rs)); 
		MovToPC(R8);  // For syscall to be able to return.
		CompileDelaySlot(DELAYSLOT_FLUSH);
		return;  // Syscall wrote exit code.
	} else if (delaySlotIsNice) {
		CompileDelaySlot(DELAYSLOT_NICE);
		gpr.MapReg(rs);
		MOV(R8, gpr.R(rs));  // Save the destination address through the delay slot. Could use isNice to avoid when the jit is fully implemented
		FlushAll();
	} else {
		// Delay slot
		gpr.MapReg(rs);
		MOV(R8, gpr.R(rs));  // Save the destination address through the delay slot. Could use isNice to avoid when the jit is fully implemented
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
	}

	switch (op & 0x3f) 
	{
	case 8: //jr
		break;
	case 9: //jalr
		MOVI2R(R0, js.compilerPC + 8);
		STR(R0, CTXREG, MIPS_REG_RA * 4);
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}

	WriteExitDestInR(R8);
	js.compiling = false;
}

	
void Jit::Comp_Syscall(u32 op)
{
	FlushAll();

	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	WriteDownCount(offset);
	js.downcountAmount = -offset;

	MOVI2R(R0, op);
	QuickCallFunction(R1, (void *)&CallSyscall);

	WriteSyscallExit();
	js.compiling = false;
}

void Jit::Comp_Break(u32 op)
{
	Comp_Generic(op);
	WriteSyscallExit();
	js.compiling = false;
}

}   // namespace Mipscomp
