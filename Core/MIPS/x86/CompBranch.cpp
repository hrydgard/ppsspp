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

#include "../../HLE/HLE.h"

#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSAnalyst.h"
#include "../MIPSTables.h"

#include "Jit.h"
#include "RegCache.h"
#include "JitCache.h"

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

namespace MIPSComp
{

#ifdef _M_IX86

#define SAVE_FLAGS PUSHF();
#define LOAD_FLAGS POPF();

#else

static u64 saved_flags;

#define SAVE_FLAGS {PUSHF(); POP(64, R(EAX)); MOV(64, M(&saved_flags), R(EAX));}
#define LOAD_FLAGS {MOV(64, R(EAX), M(&saved_flags)); PUSH(64, R(EAX)); POPF();}

#endif

void Jit::BranchRSRTComp(u32 op, Gen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rt = _RT;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC+4);

	//Compile the delay slot
	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rt && GetOutReg(delaySlotOp) != rs;// IsDelaySlotNice(op, delaySlotOp);
	if (!delaySlotIsNice)
	{
		//ERROR_LOG(CPU, "Not nice delay slot in BranchRSRTComp :( %08x", js.compilerPC);
	}
	delaySlotIsNice = false;	// Until we have time to fully fix this

	if (rt == 0)
	{
		gpr.KillImmediate(rs, true, true);
		CMP(32, gpr.R(rs), Imm32(0));
	}
	else
	{
		gpr.BindToRegister(rs, true, false);
		CMP(32, gpr.R(rs), rt == 0 ? Imm32(0) : gpr.R(rt));
	}
	FlushAll();

	js.inDelaySlot = true;
	Gen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			SAVE_FLAGS; // preserve flag around the delay slot!
		CompileAt(js.compilerPC + 4);
		FlushAll();
		if (!delaySlotIsNice)
			LOAD_FLAGS; // restore flag!
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileAt(js.compilerPC + 4);
		FlushAll();
	}
	js.inDelaySlot = false;

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC+8, 1);

	js.compiling = false;
}

void Jit::BranchRSZeroComp(u32 op, Gen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	int rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rs; //IsDelaySlotNice(op, delaySlotOp);
	if (!delaySlotIsNice)
	{
		//ERROR_LOG(CPU, "Not nice delay slot in BranchRSZeroComp :( %08x", js.compilerPC);
	}
	delaySlotIsNice = false;	// Until we have time to fully fix this
	
	gpr.BindToRegister(rs, true, false);
	CMP(32, gpr.R(rs), Imm32(0));
	FlushAll();

	Gen::FixupBranch ptr;
	js.inDelaySlot = true;
	if (!likely)
	{
		if (!delaySlotIsNice)
			SAVE_FLAGS; // preserve flag around the delay slot! Better hope the delay slot instruction doesn't need to fall back to interpreter...
		CompileAt(js.compilerPC + 4);
		FlushAll();
		if (!delaySlotIsNice)
			LOAD_FLAGS; // restore flag!
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileAt(js.compilerPC + 4);
		FlushAll();
	}
	js.inDelaySlot = false;

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC + 8, 1);
	js.compiling = false;
}


void Jit::Comp_RelBranch(u32 op)
{
	switch (op>>26) 
	{
	case 4: BranchRSRTComp(op, CC_NZ, false); break;//beq
	case 5: BranchRSRTComp(op, CC_Z,	false); break;//bne

	case 6: BranchRSZeroComp(op, CC_G, false); break;//blez
	case 7: BranchRSZeroComp(op, CC_LE, false); break;//bgtz

	case 20: BranchRSRTComp(op, CC_NZ, true); break;//beql
	case 21: BranchRSRTComp(op, CC_Z,	true); break;//bnel

	case 22: BranchRSZeroComp(op, CC_G, true); break;//blezl
	case 23: BranchRSZeroComp(op, CC_LE, true); break;//bgtzl

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
	case 0: BranchRSZeroComp(op, CC_GE, false); break; //if ((s32)R(rs) <	0) DelayBranchTo(addr); else PC += 4; break;//bltz
	case 1: BranchRSZeroComp(op, CC_L, false);	break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, CC_GE, true);	break; //if ((s32)R(rs) <	0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, CC_L, true);	 break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}


// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchFPFlag(u32 op, Gen::CCFlags cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

	bool delaySlotIsNice = IsDelaySlotNice(op, delaySlotOp);
	if (!delaySlotIsNice)
	{
		//ERROR_LOG(CPU, "Not nice delay slot in BranchFPFlag :(");
	}

	delaySlotIsNice = false;	// Until we have time to fully fix this

	FlushAll();

	TEST(32, M((void *)&(mips_->fpcond)), Imm32(1));
	Gen::FixupBranch ptr;
	js.inDelaySlot = true;
	if (!likely)
	{
		if (!delaySlotIsNice)
			SAVE_FLAGS; // preserve flag around the delay slot!
		CompileAt(js.compilerPC + 4);
		FlushAll();
		if (!delaySlotIsNice)
			LOAD_FLAGS; // restore flag!
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileAt(js.compilerPC + 4);
		FlushAll();
	}
	js.inDelaySlot = false;

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
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);

	bool delaySlotIsNice = IsDelaySlotNice(op, delaySlotOp);
	if (!delaySlotIsNice)
	{
		//ERROR_LOG(CPU, "Not nice delay slot in BranchVFPUFlag :(");
	}

	delaySlotIsNice = false;	// Until we have time to fully fix this

	FlushAll();

	// THE CONDITION
	int imm3 = (op >> 18) & 7;

	//int val = (mips_->vfpuCtrl[VFPU_CTRL_CC] >> imm3) & 1;
	TEST(32, M((void *)&(mips_->vfpuCtrl[VFPU_CTRL_CC])), Imm32(1 << imm3));
	Gen::FixupBranch ptr;
	js.inDelaySlot = true;
	if (!likely)
	{
		if (!delaySlotIsNice)
			SAVE_FLAGS; // preserve flag around the delay slot!
		CompileAt(js.compilerPC + 4);
		FlushAll();
		if (!delaySlotIsNice)
			LOAD_FLAGS; // restore flag!
		ptr = J_CC(cc, true);
	}
	else
	{
		ptr = J_CC(cc, true);
		CompileAt(js.compilerPC + 4);
		FlushAll();
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
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	u32 off = ((op & 0x3FFFFFF) << 2);
	u32 targetAddr = (js.compilerPC & 0xF0000000) | off;
	CompileAt(js.compilerPC + 4);
	FlushAll();

	switch (op >> 26) 
	{
	case 2: //j
		WriteExit(targetAddr, 0);
		break; 

	case 3: //jal
		MOV(32, M(&mips_->r[MIPS_REG_RA]), Imm32(js.compilerPC + 8));	// Save return address
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
	if (js.inDelaySlot) {
		ERROR_LOG(JIT, "Branch in delay slot at %08x", js.compilerPC);
		return;
	}
	int rs = _RS;

	u32 delaySlotOp = Memory::ReadUnchecked_U32(js.compilerPC + 4);
	bool delaySlotIsNice = GetOutReg(delaySlotOp) != rs;
	// Do what with that information?
	delaySlotIsNice = false;	// Until we have time to fully fix this

	if (delaySlotIsNice)
	{
		CompileAt(js.compilerPC + 4);
		MOV(32, R(EAX), gpr.R(rs));
		FlushAll();
	}
	else
	{
		// Latch destination now - save it on the stack.
		gpr.BindToRegister(rs, true, false);
		MOV(32, M(&currentMIPS->pc), gpr.R(rs));	// for syscalls in delay slot - could be avoided
		MOV(32, M(&savedPC), gpr.R(rs));
		CompileAt(js.compilerPC + 4);
		FlushAll();

		if (!js.compiling)
		{
			// Oh, there was a syscall in the delay slot
			// It took care of writing the exit code for us.
			return;
		}
		MOV(32, R(EAX), M(&savedPC));
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

	WriteExitDestInEAX();
	js.compiling = false;
}

void Jit::Comp_Syscall(u32 op)
{
	FlushAll();

	ABI_CallFunctionC((void *)&CallSyscall, op);

	WriteSyscallExit();
	js.compiling = false;
}

}	 // namespace Mipscomp
