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

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"
#include "Core/MemMap.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/RegCache.h"
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

using namespace MIPSAnalyst;

// NOTE: Can't use CONDITIONAL_DISABLE in this file, branches are so special
// that they cannot be interpreted in the context of the Jit.

// But we can at least log and compare.
// #define DO_CONDITIONAL_LOG 1
#define DO_CONDITIONAL_LOG 0

// We can also disable nice delay slots.
// #define CONDITIONAL_NICE_DELAYSLOT branchInfo.delaySlotIsNice = false;
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
using namespace Gen;

static void JitBranchLog(MIPSOpcode op, u32 pc) {
	currentMIPS->pc = pc;
	currentMIPS->inDelaySlot = false;

	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	MIPSInfo info = MIPSGetInfo(op);
	func(op);

	// Branch taken, use nextPC.
	if (currentMIPS->inDelaySlot)
		currentMIPS->intBranchExit = currentMIPS->nextPC;
	else
	{
		// Branch not taken, likely delay slot skipped.
		if (info & LIKELY)
			currentMIPS->intBranchExit = currentMIPS->pc;
		// Branch not taken, so increment over delay slot.
		else
			currentMIPS->intBranchExit = currentMIPS->pc + 4;
	}

	currentMIPS->pc = pc;
	currentMIPS->inDelaySlot = false;
}

static void JitBranchLogMismatch(MIPSOpcode op, u32 pc)
{
	char temp[256];
	MIPSDisAsm(op, pc, temp, sizeof(temp), true);
	ERROR_LOG(Log::JIT, "Bad jump: %s - int:%08x jit:%08x", temp, currentMIPS->intBranchExit, currentMIPS->jitBranchExit);
	Core_EnableStepping(true, "jit.branchdebug", pc);
}

void Jit::BranchLog(MIPSOpcode op)
{
	FlushAll();
	ABI_CallFunctionCC(thunks.ProtectFunction(&JitBranchLog), op.encoding, GetCompilerPC());
}

void Jit::BranchLogExit(MIPSOpcode op, u32 dest, bool useEAX)
{
	OpArg destArg = useEAX ? R(EAX) : Imm32(dest);

	CMP(32, MIPSSTATE_VAR(intBranchExit), destArg);
	FixupBranch skip = J_CC(CC_E);

	MOV(32, MIPSSTATE_VAR(jitBranchExit), destArg);
	ABI_CallFunctionCC(thunks.ProtectFunction(&JitBranchLogMismatch), op.encoding, GetCompilerPC());
	// Restore EAX, we probably ruined it.
	if (useEAX)
		MOV(32, R(EAX), MIPSSTATE_VAR(jitBranchExit));

	SetJumpTarget(skip);
}

CCFlags Jit::FlipCCFlag(CCFlags flag)
{
	switch (flag)
	{
	case CC_O: return CC_NO;
	case CC_NO: return CC_O;
	case CC_B: return CC_NB;
	case CC_NB: return CC_B;
	case CC_Z: return CC_NZ;
	case CC_NZ: return CC_Z;
	case CC_BE: return CC_NBE;
	case CC_NBE: return CC_BE;
	case CC_S: return CC_NS;
	case CC_NS: return CC_S;
	case CC_P: return CC_NP;
	case CC_NP: return CC_P;
	case CC_L: return CC_NL;
	case CC_NL: return CC_L;
	case CC_LE: return CC_NLE;
	case CC_NLE: return CC_LE;
	}
	ERROR_LOG_REPORT(Log::JIT, "FlipCCFlag: Unexpected CC flag: %d", flag);
	return CC_O;
}

CCFlags Jit::SwapCCFlag(CCFlags flag)
{
	// This swaps the comparison for an lhs/rhs swap, but doesn't flip/invert the logic.
	switch (flag)
	{
	case CC_O: return CC_O;
	case CC_NO: return CC_NO;
	case CC_B: return CC_A;
	case CC_NB: return CC_NA;
	case CC_Z: return CC_Z;
	case CC_NZ: return CC_NZ;
	case CC_BE: return CC_AE;
	case CC_NBE: return CC_NAE;
	case CC_S: return CC_S;
	case CC_NS: return CC_NS;
	case CC_P: return CC_P;
	case CC_NP: return CC_NP;
	case CC_L: return CC_G;
	case CC_NL: return CC_NG;
	case CC_LE: return CC_GE;
	case CC_NLE: return CC_NGE;
	}
	ERROR_LOG_REPORT(Log::JIT, "SwapCCFlag: Unexpected CC flag: %d", flag);
	return CC_O;
}

bool Jit::PredictTakeBranch(u32 targetAddr, bool likely) {
	// If it's likely, it's... probably likely, right?
	if (likely)
		return true;

	// TODO: Normal branch prediction would be to take branches going upward to lower addresses.
	// However, this results in worse performance as of this comment's writing.
	// The reverse check generally gives better or same performance.
	return targetAddr > GetCompilerPC();
}

void Jit::CompBranchExits(CCFlags cc, u32 targetAddr, u32 notTakenAddr, const BranchInfo &branchInfo) {
	if (branchInfo.andLink)
		gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);

	// We may want to try to continue along this branch a little while, to reduce reg flushing.
	bool predictTakeBranch = PredictTakeBranch(targetAddr, branchInfo.likely);
	if (!branchInfo.delaySlotIsBranch && CanContinueBranch(predictTakeBranch ? targetAddr : notTakenAddr))
	{
		if (predictTakeBranch)
			cc = FlipCCFlag(cc);

		Gen::FixupBranch ptr;
		RegCacheState state;
		if (!branchInfo.likely)
		{
			if (!branchInfo.delaySlotIsNice)
				CompileDelaySlot(DELAYSLOT_SAFE);
			ptr = J_CC(cc, true);
			GetStateAndFlushAll(state);
		}
		else
		{
			ptr = J_CC(cc, true);
			if (predictTakeBranch)
				GetStateAndFlushAll(state);
			else
			{
				// We need to get the state BEFORE the delay slot is compiled.
				gpr.GetState(state.gpr);
				fpr.GetState(state.fpr);
				CompileDelaySlot(DELAYSLOT_FLUSH);
			}
		}

		if (predictTakeBranch)
		{
			// We flipped the cc, the not taken case is first.
			CONDITIONAL_LOG_EXIT(notTakenAddr);
			WriteExit(notTakenAddr, js.nextExit++);

			// Now our taken path.  Bring the regs back, we didn't flush 'em after all.
			SetJumpTarget(ptr);
			RestoreState(state);
			CONDITIONAL_LOG_EXIT(targetAddr);

			// Don't forget to run the delay slot if likely.
			if (branchInfo.likely)
				CompileDelaySlot(DELAYSLOT_NICE);

			AddContinuedBlock(targetAddr);
			// Account for the increment in the loop.
			js.compilerPC = targetAddr - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
		}
		else
		{
			// Take the branch
			CONDITIONAL_LOG_EXIT(targetAddr);
			WriteExit(targetAddr, js.nextExit++);

			// Not taken
			SetJumpTarget(ptr);
			RestoreState(state);
			CONDITIONAL_LOG_EXIT(notTakenAddr);

			// Account for the delay slot.
			js.compilerPC += 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
		}
	}
	else
	{
		Gen::FixupBranch ptr;
		if (!branchInfo.likely)
		{
			if (!branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
				CompileDelaySlot(DELAYSLOT_SAFE_FLUSH);
			else
				FlushAll();
			ptr = J_CC(cc, true);
		}
		else
		{
			FlushAll();
			ptr = J_CC(cc, true);
			if (!branchInfo.delaySlotIsBranch)
				CompileDelaySlot(DELAYSLOT_FLUSH);
		}

		// Handle the linkage of a delay slot, even when we're taking the branch.
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
		CONDITIONAL_LOG_EXIT(targetAddr);
		WriteExit(targetAddr, js.nextExit++);

		// Not taken
		SetJumpTarget(ptr);
		CONDITIONAL_LOG_EXIT(notTakenAddr);
		WriteExit(notTakenAddr, js.nextExit++);
		js.compiling = false;
	}
}

void Jit::CompBranchExit(bool taken, u32 targetAddr, u32 notTakenAddr, const BranchInfo &branchInfo) {
	// Continuing is handled in the imm branch case... TODO: move it here?
	if (branchInfo.andLink)
		gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
	if (branchInfo.delaySlotIsBranch) {
		if (taken) {
			// We still link when the branch is taken (targetAddr case.)
			// Remember, it's from the perspective of the delay slot, so +12.
			if ((branchInfo.delaySlotInfo & OUT_RA) != 0)
				gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 12);
			if ((branchInfo.delaySlotInfo & OUT_RD) != 0)
				gpr.SetImm(MIPS_GET_RD(branchInfo.delaySlotOp), GetCompilerPC() + 12);
		}
		FlushAll();
	} else if (taken || !branchInfo.likely) {
		CompileDelaySlot(DELAYSLOT_FLUSH);
	} else {
		FlushAll();
	}

	const u32 destAddr = taken ? targetAddr : notTakenAddr;
	CONDITIONAL_LOG_EXIT(destAddr);
	WriteExit(destAddr, js.nextExit++);
	js.compiling = false;
}

void Jit::BranchRSRTComp(MIPSOpcode op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
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
		case CC_E: immBranchNotTaken = rsImm == rtImm; break;
		case CC_NE: immBranchNotTaken = rsImm != rtImm; break;
		default: immBranchNotTaken = false; _dbg_assert_msg_(false, "Bad cc flag in BranchRSRTComp().");
		}
		immBranch = true;
		immBranchTaken = !immBranchNotTaken;
	}

	if (jo.immBranches && immBranch && js.numInstructions < jo.continueMaxInstructions)
	{
		if (!immBranchTaken)
		{
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
	if (immBranch)
		CompBranchExit(immBranchTaken, targetAddr, notTakenTarget, branchInfo);
	else
	{
		if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_NICE);

		if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0)
		{
			gpr.KillImmediate(rs, true, false);
			CMP(32, gpr.R(rs), Imm32(0));
		}
		else
		{
			gpr.MapReg(rs, true, false);
			CMP(32, gpr.R(rs), gpr.R(rt));
		}

		CompBranchExits(cc, targetAddr, notTakenTarget, branchInfo);
	}
}

void Jit::BranchRSZeroComp(MIPSOpcode op, Gen::CCFlags cc, bool andLink, bool likely)
{
	CONDITIONAL_LOG;
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
	CONDITIONAL_NICE_DELAYSLOT;

	bool immBranch = false;
	bool immBranchTaken = false;
	if (gpr.IsImm(rs) && !branchInfo.delaySlotIsBranch) {
		// The cc flags are opposites: when NOT to take the branch.
		bool immBranchNotTaken;
		s32 imm = (s32)gpr.GetImm(rs);

		switch (cc)
		{
		case CC_G: immBranchNotTaken = imm > 0; break;
		case CC_GE: immBranchNotTaken = imm >= 0; break;
		case CC_L: immBranchNotTaken = imm < 0; break;
		case CC_LE: immBranchNotTaken = imm <= 0; break;
		default: immBranchNotTaken = false; _dbg_assert_msg_(false, "Bad cc flag in BranchRSZeroComp().");
		}
		immBranch = true;
		immBranchTaken = !immBranchNotTaken;
	}

	if (jo.immBranches && immBranch && js.numInstructions < jo.continueMaxInstructions)
	{
		if (!immBranchTaken)
		{
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
	if (immBranch)
		CompBranchExit(immBranchTaken, targetAddr, notTakenTarget, branchInfo);
	else
	{
		if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
			CompileDelaySlot(DELAYSLOT_NICE);

		gpr.MapReg(rs, true, false);
		CMP(32, gpr.R(rs), Imm32(0));

		CompBranchExits(cc, targetAddr, notTakenTarget, branchInfo);
	}
}


void Jit::Comp_RelBranch(MIPSOpcode op)
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
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}

void Jit::Comp_RelBranchRI(MIPSOpcode op)
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
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
}


// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchFPFlag(MIPSOpcode op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
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

	gpr.KillImmediate(MIPS_REG_FPCOND, true, false);
	TEST(32, gpr.R(MIPS_REG_FPCOND), Imm32(1));

	u32 notTakenTarget = ResolveNotTakenTarget(branchInfo);
	CompBranchExits(cc, targetAddr, notTakenTarget, branchInfo);
}


void Jit::Comp_FPUBranch(MIPSOpcode op)
{
	switch((op >> 16) & 0x1f)
	{
	case 0:	BranchFPFlag(op, CC_NZ, false); break; //bc1f
	case 1: BranchFPFlag(op, CC_Z,	false); break; //bc1t
	case 2: BranchFPFlag(op, CC_NZ, true);	break; //bc1fl
	case 3: BranchFPFlag(op, CC_Z,	true);	break; //bc1tl
	default:
		_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
		break;
	}
}

// If likely is set, discard the branch slot if NOT taken.
void Jit::BranchVFPUFlag(MIPSOpcode op, Gen::CCFlags cc, bool likely)
{
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		// I think we can safely just warn-log this without reporting, it's pretty clear that this type
		// of branch is ignored.
		WARN_LOG(Log::JIT, "Branch in VFPU delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
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
	if (!likely && branchInfo.delaySlotIsNice && !branchInfo.delaySlotIsBranch)
		CompileDelaySlot(DELAYSLOT_NICE);

	// THE CONDITION
	int imm3 = (op >> 18) & 7;

	gpr.KillImmediate(MIPS_REG_VFPUCC, true, false);
	TEST(32, gpr.R(MIPS_REG_VFPUCC), Imm32(1 << imm3));

	u32 notTakenTarget = ResolveNotTakenTarget(branchInfo);
	CompBranchExits(cc, targetAddr, notTakenTarget, branchInfo);
}


void Jit::Comp_VBranch(MIPSOpcode op)
{
	switch ((op >> 16) & 3)
	{
	case 0:	BranchVFPUFlag(op, CC_NZ, false); break; //bvf
	case 1: BranchVFPUFlag(op, CC_Z,	false); break; //bvt
	case 2: BranchVFPUFlag(op, CC_NZ, true);	break; //bvfl
	case 3: BranchVFPUFlag(op, CC_Z,	true);	break; //bvtl
	default:
		_dbg_assert_msg_(false,"Comp_VBranch: Invalid instruction");
		break;
	}
}

static void HitInvalidJump(uint32_t dest) {
	Core_ExecException(dest, currentMIPS->pc - 8, ExecExceptionType::JUMP);
}

void Jit::Comp_Jump(MIPSOpcode op) {
	CONDITIONAL_LOG;
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT(Log::JIT, "Branch in Jump delay slot at %08x in block starting at %08x", GetCompilerPC(), js.blockStart);
		return;
	}
	u32 off = TARGET26;
	u32 targetAddr = (GetCompilerPC() & 0xF0000000) | off;

	// Might be a stubbed address or something?
	if (!Memory::IsValidAddress(targetAddr) || (targetAddr & 3) != 0) {
		if (js.nextExit == 0) {
			ERROR_LOG_REPORT(Log::JIT, "Jump to invalid address: %08x PC %08x LR %08x", targetAddr, GetCompilerPC(), currentMIPS->r[MIPS_REG_RA]);
		} else {
			js.compiling = false;
		}
		// TODO: Mark this block dirty or something?  May be indication it will be changed by imports.

		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		MOV(32, MIPSSTATE_VAR(pc), Imm32(GetCompilerPC() + 8));
		ABI_CallFunctionC(&HitInvalidJump, targetAddr);
		WriteSyscallExit();
		return;
	}

	switch (op >> 26) {
	case 2: //j
		CompileDelaySlot(DELAYSLOT_NICE);
		if (CanContinueJump(targetAddr))
		{
			AddContinuedBlock(targetAddr);
			// Account for the increment in the loop.
			js.compilerPC = targetAddr - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}
		FlushAll();
		CONDITIONAL_LOG_EXIT(targetAddr);
		WriteExit(targetAddr, js.nextExit++);
		break;

	case 3: //jal
		// Special case for branches to "replace functions":
		if (ReplaceJalTo(targetAddr))
			return;

		// Check for small function inlining (future)
		

		// Save return address - might be overwritten by delay slot.
		gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		if (CanContinueJump(targetAddr))
		{
			AddContinuedBlock(targetAddr);
			// Account for the increment in the loop.
			js.compilerPC = targetAddr - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}
		FlushAll();
		CONDITIONAL_LOG_EXIT(targetAddr);
		WriteExit(targetAddr, js.nextExit++);
		break;

	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}
	js.compiling = false;
}

void Jit::Comp_JumpReg(MIPSOpcode op)
{
	CONDITIONAL_LOG;
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

	X64Reg destReg = EAX;
	if (IsSyscall(delaySlotOp))
	{
		// If this is a syscall, write the pc (for thread switching and other good reasons.)
		gpr.MapReg(rs, true, false);
		MOV(32, MIPSSTATE_VAR(pc), gpr.R(rs));
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_FLUSH);

		// Syscalls write the exit code for us.
		_dbg_assert_msg_(!js.compiling, "Expected syscall to write an exit code.");
		return;
	}
	else if (delaySlotIsNice)
	{
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);

		if (!andLink && rs == MIPS_REG_RA && g_Config.bDiscardRegsOnJRRA) {
			// According to the MIPS ABI, there are some regs we don't need to preserve.
			// Let's discard them so we don't need to write them back.
			// NOTE: Not all games follow the MIPS ABI! Tekken 6, for example, will crash
			// with this enabled.
			gpr.DiscardRegContentsIfCached(MIPS_REG_COMPILER_SCRATCH);
			for (int i = MIPS_REG_A0; i <= MIPS_REG_T7; i++)
				gpr.DiscardRegContentsIfCached((MIPSGPReg)i);
			gpr.DiscardRegContentsIfCached(MIPS_REG_T8);
			gpr.DiscardRegContentsIfCached(MIPS_REG_T9);
		}

		if (gpr.IsImm(rs) && CanContinueJump(gpr.GetImm(rs)))
		{
			AddContinuedBlock(gpr.GetImm(rs));
			// Account for the increment in the loop.
			js.compilerPC = gpr.GetImm(rs) - 4;
			// In case the delay slot was a break or something.
			js.compiling = true;
			return;
		}

		if (gpr.R(rs).IsSimpleReg()) {
			destReg = gpr.R(rs).GetSimpleReg();
		} else {
			MOV(32, R(EAX), gpr.R(rs));
		}
		FlushAll();
	} else {
		// Latch destination now - save it in memory.
		gpr.MapReg(rs, true, false);
		MOV(32, MIPSSTATE_VAR(savedPC), gpr.R(rs));
		if (andLink)
			gpr.SetImm(rd, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		MOV(32, R(EAX), MIPSSTATE_VAR(savedPC));
		FlushAll();
	}

	switch (op & 0x3f) {
	case 8: //jr
		break;
	case 9: //jalr
		break;
	default:
		_dbg_assert_msg_(false,"Trying to compile instruction that can't be compiled");
		break;
	}

	CONDITIONAL_LOG_EXIT_EAX();
	WriteExitDestInReg(destReg);
	js.compiling = false;
}

void Jit::Comp_Syscall(MIPSOpcode op)
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
	FlushAll();

	// If we're in a delay slot, this is off by one.
	const int offset = js.inDelaySlot ? -1 : 0;
	WriteDowncount(offset);
	RestoreRoundingMode();
	js.downcountAmount = -offset;

	if (!js.inDelaySlot) {
		MOV(32, MIPSSTATE_VAR(pc), Imm32(GetCompilerPC() + 4));
	}

#ifdef USE_PROFILER
	// When profiling, we can't skip CallSyscall, since it times syscalls.
	ABI_CallFunctionC(&CallSyscall, op.encoding);
#else
	// Skip the CallSyscall where possible.
	void *quickFunc = GetQuickSyscallFunc(op);
	if (quickFunc)
		ABI_CallFunctionP(quickFunc, (void *)GetSyscallFuncPointer(op));
	else
		ABI_CallFunctionC(&CallSyscall, op.encoding);
#endif

	ApplyRoundingMode();
	WriteSyscallExit();
	js.compiling = false;
}

void Jit::Comp_Break(MIPSOpcode op)
{
	Comp_Generic(op);
	WriteSyscallExit();
	js.compiling = false;
}

}	 // namespace Mipscomp

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
