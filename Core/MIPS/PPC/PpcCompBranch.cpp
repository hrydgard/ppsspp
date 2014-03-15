#include "Common/ChunkFile.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"

#include "PpcRegCache.h"
#include "ppcEmitter.h"
#include "PpcJit.h"

#include <ppcintrinsics.h>


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

// We can disable nice delay slots.
#define CONDITIONAL_NICE_DELAYSLOT delaySlotIsNice = false;
// #define CONDITIONAL_NICE_DELAYSLOT ;

#define SHOW_JS_COMPILER_PC { printf("js.compilerPC: %08x\n", js.compilerPC); }

#define BRANCH_COMPILE_LOG {	printf("JIT(%8x): %s => %d - %08x\n", (u32)GetCodePtr() ,__FUNCTION__, cc, js.compilerPC); }

using namespace MIPSAnalyst;

using namespace PpcGen;

namespace MIPSComp
{
	
void Jit::BranchRSRTComp(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSRTComp delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;
		
	MIPSOpcode delaySlotOp = Memory::Read_Instruction(js.compilerPC+4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rt, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);


	if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0)
	{
		gpr.MapReg(rs);
		CMPLI(gpr.R(rs), 0);
	}
	else if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0)  // only these are easily 'flippable'
	{
		gpr.MapReg(rt);
		CMPLI(gpr.R(rt), 0);
	}
	else 
	{
		gpr.MapInIn(rs, rt);
		CMPL(gpr.R(rs), gpr.R(rt));
	}

	PpcGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = B_Cond(cc);
	}
	else
	{
		FlushAll();
		ptr = B_Cond(cc);		
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);

	// Not taken
	WriteExit(js.compilerPC+8, 1);

	js.compiling = false;
}


void Jit::BranchRSZeroComp(MIPSOpcode op, PpcGen::FixupBranchType cc, bool andLink, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in RSZeroComp delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
		return;
	}
	int offset = (signed short)(op&0xFFFF)<<2;
	MIPSGPReg rs = _RS;
	u32 targetAddr = js.compilerPC + offset + 4;

	MIPSOpcode delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	gpr.MapReg(rs);
	CMPI(gpr.R(rs), 0);

	PpcGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		else
			FlushAll();
		ptr = B_Cond(cc);
	}
	else
	{
		FlushAll();
		ptr = B_Cond(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	if (andLink)
	{
		MOVI2R(SREG, js.compilerPC + 8);
		STW(SREG, CTXREG, MIPS_REG_RA * 4);
	}

	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);

	// Not taken
	WriteExit(js.compilerPC + 8, 1);

	js.compiling = false;
}

void Jit::Comp_RelBranch(MIPSOpcode op) {
	// The CC flags here should be opposite of the actual branch becuase they skip the branching action.
	switch (op>>26) 
	{
	case 4: BranchRSRTComp(op, _BNE, false); break;//beq
	case 5: BranchRSRTComp(op, _BEQ,  false); break;//bne

	case 6: BranchRSZeroComp(op, _BGT, false, false); break;//blez
	case 7: BranchRSZeroComp(op, _BLE, false, false); break;//bgtz

	case 20: BranchRSRTComp(op, _BNE, true); break;//beql
	case 21: BranchRSRTComp(op, _BEQ,  true); break;//bnel

	case 22: BranchRSZeroComp(op, _BGT, false, true); break;//blezl
	case 23: BranchRSZeroComp(op, _BLE, false, true); break;//bgtzl

	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  js.compiling = false;
}

void Jit::Comp_RelBranchRI(MIPSOpcode op) {
	switch ((op >> 16) & 0x1F)
	{
	case 0: BranchRSZeroComp(op, _BGE, false, false); break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltz
	case 1: BranchRSZeroComp(op, _BLT, false, false); break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
	case 2: BranchRSZeroComp(op, _BGE, false, true);  break; //if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 8; break;//bltzl
	case 3: BranchRSZeroComp(op, _BLT, false, true);  break; //if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 8; break;//bgezl
	case 16: BranchRSZeroComp(op, _BGE, true, false); break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltzal
	case 17: BranchRSZeroComp(op, _BLT, true, false);  break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgezal
	case 18: BranchRSZeroComp(op, _BGE, true, true);  break;  //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else SkipLikely(); break;//bltzall
	case 19: BranchRSZeroComp(op, _BLT, true, true);   break; //R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezall
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}
  js.compiling = false;
}


// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchFPFlag(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in FPFlag delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	MIPSOpcode delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();
	
	LWZ(SREG, CTXREG, offsetof(MIPSState, fpcond));
	// change CR0
	ANDI(SREG, SREG, 1);

	PpcGen::FixupBranch ptr;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B_Cond(cc);
	}
	else
	{
		ptr = B_Cond(cc);
		CompileDelaySlot(DELAYSLOT_FLUSH);
	}

	// Take the branch
	WriteExit(targetAddr, 0);

	SetJumpTarget(ptr);
	// Not taken
	WriteExit(js.compilerPC + 8, 1);
	js.compiling = false;
}

void Jit::Comp_FPUBranch(MIPSOpcode op) {
	switch((op >> 16) & 0x1f)
	{
	case 0:	BranchFPFlag(op, _BNE, false); break;  // bc1f
	case 1: BranchFPFlag(op, _BEQ,  false); break;  // bc1t
	case 2: BranchFPFlag(op, _BNE, true);  break;  // bc1fl
	case 3: BranchFPFlag(op, _BEQ,  true);  break;  // bc1tl
	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		break;
	}
  js.compiling = false;
}


// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchVFPUFlag(MIPSOpcode op, PpcGen::FixupBranchType cc, bool likely)
{
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in VFPU delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
		return;
	}
	int offset = (signed short)(op & 0xFFFF) << 2;
	u32 targetAddr = js.compilerPC + offset + 4;

	MIPSOpcode delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);

	bool delaySlotIsNice = IsDelaySlotNiceVFPU(op, delaySlotOp);
	CONDITIONAL_NICE_DELAYSLOT;
	if (!likely && delaySlotIsNice)
		CompileDelaySlot(DELAYSLOT_NICE);

	FlushAll();

	int imm3 = (op >> 18) & 7;


	MOVI2R(SREG,  (u32)&(mips_->vfpuCtrl[VFPU_CTRL_CC]));
	LWZ(SREG, SREG, 0);
	// change CR0
	ANDI(SREG, SREG, 1 << imm3);

	PpcGen::FixupBranch ptr;
	js.inDelaySlot = true;
	if (!likely)
	{
		if (!delaySlotIsNice)
			CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
		ptr = B_Cond(cc);
	}
	else
	{
		ptr = B_Cond(cc);
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

void Jit::Comp_VBranch(MIPSOpcode op) {
	switch ((op >> 16) & 3)
	{
	case 0:	BranchVFPUFlag(op, _BNE, false); break;  // bvf
	case 1: BranchVFPUFlag(op, _BEQ,  false); break;  // bvt
	case 2: BranchVFPUFlag(op, _BNE, true);  break;  // bvfl
	case 3: BranchVFPUFlag(op, _BEQ,  true);  break;  // bvtl
	}
	js.compiling = false;
}

void Jit::Comp_Jump(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in Jump delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
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

void Jit::Comp_JumpReg(MIPSOpcode op) {
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(JIT, "Branch in JumpReg delay slot at %08x in block starting at %08x", js.compilerPC, js.blockStart);
		return;
	}
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	MIPSOpcode delaySlotOp = Memory::Read_Instruction(js.compilerPC + 4);
	bool delaySlotIsNice = IsDelaySlotNiceReg(op, delaySlotOp, rs);
	CONDITIONAL_NICE_DELAYSLOT;

	if (IsSyscall(delaySlotOp)) {
		gpr.MapReg(rs);
		PPCReg mRs = gpr.R(rs);
		MR(FLAGREG, mRs); 
		MovToPC(FLAGREG);  // For syscall to be able to return.
		CompileDelaySlot(DELAYSLOT_FLUSH);
		return;  // Syscall wrote exit code.
	} else if (delaySlotIsNice) {
		CompileDelaySlot(DELAYSLOT_NICE);
		gpr.MapReg(rs);
		PPCReg mRs = gpr.R(rs);
		MR(FLAGREG, mRs);  // Save the destination address through the delay slot. Could use isNice to avoid when the jit is fully implemented
		FlushAll();
	} else {
		// Delay slot
		gpr.MapReg(rs);
		PPCReg mRs = gpr.R(rs);
		MR(FLAGREG, mRs);  // Save the destination address through the delay slot. Could use isNice to avoid when the jit is fully implemented
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
	}

	switch (op & 0x3f) 
	{
	case 8: //jr
		break;
	case 9: //jalr
		// mips->reg = js.compilerPC + 8;
		MOVI2R(SREG, js.compilerPC + 8);
		STW(SREG, CTXREG, (int)rd * 4);
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile instruction that can't be compiled");
		break;
	}

	WriteExitDestInR(FLAGREG);
	js.compiling = false;
}

void Jit::Comp_Syscall(MIPSOpcode op) {
	FlushAll();

	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	WriteDownCount(offset);
	js.downcountAmount = -offset;

	// CallSyscall(op);
	MOVI2R(R3, op.encoding);
	SaveDowncount(DCNTREG);
	QuickCallFunction((void *)&CallSyscall);
	RestoreDowncount(DCNTREG);

	WriteSyscallExit();
	js.compiling = false;
}

void Jit::Comp_Break(MIPSOpcode op) {
	Comp_Generic(op);
	WriteSyscallExit();
	js.compiling = false;
}


}