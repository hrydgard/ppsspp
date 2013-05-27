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

#include "../../HLE/HLE.h"
#include "../../Host.h"

#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSAnalyst.h"
#include "../MIPSTables.h"

#include "Jit.h"
#include "RegCache.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define LOOPOPTIMIZATION 0

using namespace MIPSAnalyst;

// NOTE: Can't use CONDITIONAL_DISABLE in this file, branches are so special
// that they cannot be interpreted in the context of the Jit.

// But we can at least log and compare.
// #define DO_CONDITIONAL_LOG 1
#define DO_CONDITIONAL_LOG 0

// We can also disable nice delay slots.
// #define CONDITIONAL_NICE_DELAYSLOT delaySlotIsNice = false;
#define CONDITIONAL_NICE_DELAYSLOT ;

#if DO_CONDITIONAL_LOG
#define CONDITIONAL_LOG BranchLog(op);
#define CONDITIONAL_LOG_EXIT(addr) BranchLogExit(op, addr, false);
#define CONDITIONAL_LOG_EXIT_EAX() BranchLogExit(op, 0, true);
#else
#define CONDITIONAL_LOG ;
#define CONDITIONAL_LOG_EXIT(addr) ;
#define CONDITIONAL_LOG_EXIT_EAX() ;
#endif

namespace MIPSComp
{

static u32 intBranchExit;
static u32 jitBranchExit;

static void JitBranchLog(u32 op, u32 pc)
{
	currentMIPS->pc = pc;
	currentMIPS->inDelaySlot = false;

	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	u32 info = MIPSGetInfo(op);
	func(op);

	// Branch taken, use nextPC.
	if (currentMIPS->inDelaySlot)
		intBranchExit = currentMIPS->nextPC;
	else
	{
		// Branch not taken, likely delay slot skipped.
		if (info & LIKELY)
			intBranchExit = currentMIPS->pc;
		// Branch not taken, so increment over delay slot.
		else
			intBranchExit = currentMIPS->pc + 4;
	}

	currentMIPS->pc = pc;
	currentMIPS->inDelaySlot = false;
}

static void JitBranchLogMismatch(u32 op, u32 pc)
{
	char temp[256];
	MIPSDisAsm(op, pc, temp, true);
	ERROR_LOG(JIT, "Bad jump: %s - int:%08x jit:%08x", temp, intBranchExit, jitBranchExit);
	host->SetDebugMode(true);
}

void Jit::BranchLog(u32 op)
{
	FlushAll();
	ABI_CallFunctionCC(thunks.ProtectFunction((void *) &JitBranchLog, 2), op, js.compilerPC);
}

void Jit::BranchLogExit(u32 op, u32 dest, bool useEAX)
{
	OpArg destArg = useEAX ? R(EAX) : Imm32(dest);

	CMP(32, M((void *) &intBranchExit), destArg);
	FixupBranch skip = J_CC(CC_E);

	MOV(32, M((void *) &jitBranchExit), destArg);
	ABI_CallFunctionCC(thunks.ProtectFunction((void *) &JitBranchLogMismatch, 2), op, js.compilerPC);
	// Restore EAX, we probably ruined it.
	if (useEAX)
		MOV(32, R(EAX), M((void *) &jitBranchExit));

	SetJumpTarget(skip);
}

void Jit::BranchRSRTComp(u32 op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSRTComp delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rt = _RT;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::Read_Instruction(js.compilerPC+4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rt, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	if (rt == 0)
	{
		gpr.KillImmediate(rs, true, false);
		CMP(32, gpr.R(rs), Imm32(0));
	}
	else
	{
		gpr.BindToRegister(rs, true, false);
		CMP(32, gpr.R(rs), rt == 0 ? Imm32(0) : gpr.R(rt));
	}

	Gen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = J_CC(cc, true);
	}
	else
	{
		FlushAll();
		ptr = J_CC(cc, true);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}
	// Take the branch
	CONDITIONAL_LOG_EXIT(targetAddr);
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	CONDITIONAL_LOG_EXIT(js.compilerPC + 8);
	WriteExit(js.compilerPC + 8, 1);

	js.compiling = false;
}

void Jit::BranchRSZeroComp(u32 op, Gen::CCFlags cc, bool andLink, bool likely)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSZeroComp delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	gpr.BindToRegister(rs, true, false);
	CMP(32, gpr.R(rs), Imm32(0));

	Gen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = J_CC(cc, true);
	}
	else
	{
		FlushAll();
		ptr = J_CC(cc, true);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	if (andLink)
		MOV(32, M(&mips_->r[MIPS_REG_RA]), Imm32(js.compilerPC + 8));
	CONDITIONAL_LOG_EXIT(targetAddr);
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	CONDITIONAL_LOG_EXIT(js.compilerPC + 8);
	WriteExit(js.compilerPC + 8, 1);

	js.compiling = false;
}


void Jit::Comp_RelBranch(u32 op)
{
	switch (op>>26) 
	{
	case 4: BranchRSRTComp(op, CC_NZ, false); break;//beq
	case 5: BranchRSRTComp(op, CC_Z,  false); break;//bne

	case 6: BranchRSZeroComp(op, CC_G, false, false); break;//blez
	case 7: BranchRSZeroComp(op, CC_LE, false, false); break;//bgtz

	case 20: BranchRSRTComp(op, CC_NZ, true); break;//beql
	case 21: BranchRSRTComp(op, CC_Z,  true); break;//bnel

	case 22: BranchRSZeroComp(op, CC_G, false, true); break;//blezl
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
	case 1: BranchRSZeroComp(op, CC_L, false, false);  break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, CC_GE, false, true);  break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, CC_L, false, true);   break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	case 16: BranchRSZeroComp(op, CC_GE, true, false); break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltzal
	case 17: BranchRSZeroComp(op, CC_L, true, false);  break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgezal
	case 18: BranchRSZeroComp(op, CC_GE, true, true);  break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else SkipLikely(); break;//bltzall
	case 19: BranchRSZeroComp(op, CC_L, true, true);   break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezall
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}


// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchFPFlag(u32 op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in FPFlag delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();

	TEST(32, M((void *)&(mips_->fpcond)), Imm32(1));
	Gen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	CONDITIONAL_LOG_EXIT(targetAddr);
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	CONDITIONAL_LOG_EXIT(js.compilerPC + 8);
	WriteExit(js.compilerPC + 8, 1);

	js.compiling = false;
}


void Jit::Comp_FPUBranch(u32 op)
{
	switch((op >> 16) & 0x1f)
	{
	case 0:	BranchFPFlag(op, CC_NZ, false); break; //bc1f
	case 1: BranchFPFlag(op, CC_Z,	false); break; //bc1t
	case 2: BranchFPFlag(op, CC_NZ, true);	break; //bc1fl
	case 3: BranchFPFlag(op, CC_Z,	true);	break; //bc1tl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		break;
	}
	js.compiling = false;
}

// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchVFPUFlag(u32 op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in VFPU delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceVFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();

	// THE CONDITION
	int imm3 = (op >> 18) & 7;

	//int val = (mips_->vfpuCtrl[VFPU_CTRL_CC] >> imm3) & 1;
	TEST(32, M((void *)&(mips_->vfpuCtrl[VFPU_CTRL_CC])), Imm32(1 << imm3));
	Gen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	CONDITIONAL_LOG_EXIT(targetAddr);
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	CONDITIONAL_LOG_EXIT(js.compilerPC + 8);
	WriteExit(js.compilerPC + 8, 1);

	js.compiling = false;
}


void Jit::Comp_VBranch(u32 op)
{
	switch ((op >> 16) & 3)
	{
	case 0:	BranchVFPUFlag(op, CC_NZ, false); break; //bvf
	case 1: BranchVFPUFlag(op, CC_Z,	false); break; //bvt
	case 2: BranchVFPUFlag(op, CC_NZ, true);	break; //bvfl
	case 3: BranchVFPUFlag(op, CC_Z,	true);	break; //bvtl
	default:
		_dbg_assert_msg_(CPU,0,"Comp_VBranch: Invalid instruction");
		break;
	}
	js.compiling = false;
}

void Jit::Comp_Jump(u32 op)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in Jump delay slot at %08x", js.compilerPC);
		return;
	}
	u32 off = ((op & 0x3FFFFFF) << 2);
	u32 targetAddr = (js.compilerPC & 0xF0000000) | off;

	switch (op >> 26) 
	{
	case 2: //j
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		CONDITIONAL_LOG_EXIT(targetAddr);
		WriteExit(targetAddr, 0);
		break;

	case 3: //jal
		gpr.BindToRegister(MIPS_REG_RA, false, true);
		MOV(32, gpr.R(MIPS_REG_RA), Imm32(js.compilerPC + 8));	// Save return address
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		CONDITIONAL_LOG_EXIT(targetAddr);
		WriteExit(targetAddr, 0);
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}

static u32 savedPC;

void Jit::Comp_JumpReg(u32 op)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in JumpReg delay slot at %08x", js.compilerPC);
		return;
	}
	int rs = _RS;

	u32 delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;

	if (IsSyscall(delaySlotOp))
	{
		// If this is a syscall, write the pc (for thread switching and other good reasons.)
		gpr.BindToRegister(rs, true, false);
		MOV(32, M(&currentMIPS->pc), gpr.R(rs));
		CompileDelaySlot(DELAYSLOT_FLUSH);

		// Syscalls write the exit code for us.
		_dbg_assert_msg_(JIT, !js.compiling, "Expected syscall to write an exit code.");
		return;
	}
	else if (delaySlotIsNice)
	{
		CompileDelaySlot(DELAYSLOT_NICE);
		MOV(32, R(EAX), gpr.R(rs));
		FlushAll();
	}
	else
	{
		// Latch destination now - save it in memory.
		gpr.BindToRegister(rs, true, false);
		MOV(32, M(&savedPC), gpr.R(rs));
		CompileDelaySlot(DELAYSLOT_NICE);
		MOV(32, R(EAX), M(&savedPC));
		FlushAll();
	}

	switch (op & 0x3f)
	{
	case 8: //jr
		break;
	case 9: //jalr
		MOV(32, M(&mips_->r[MIPS_REG_RA]), Imm32(js.compilerPC + 8));
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}

	CONDITIONAL_LOG_EXIT_EAX();
	WriteExitDestInEAX();
	js.compiling = false;
}

void Jit::Comp_Syscall(u32 op)
{
	FlushAll();

	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	WriteDowncount(offset);
	js.downcountAmount = -offset;

	ABI_CallFunctionC((void *)&CallSyscall, op);

	WriteSyscallExit();
	js.compiling = false;
}

void Jit::Comp_Break(u32 op)
{
	Comp_Generic(op);
	WriteSyscallExit();
	js.compiling = false;
}

}	 // namespace Mipscomp
