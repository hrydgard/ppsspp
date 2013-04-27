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

#include "Common/ChunkFile.h"
#include "../../Core.h"
#include "../../CoreTiming.h"
#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSInt.h"
#include "../MIPSTables.h"

#include "ArmRegCache.h"
#include "ArmJit.h"
#include "CPUDetect.h"

#include "../../../ext/disarm.h"

void DisassembleArm(const u8 *data, int size) {
	char temp[256];
	for (int i = 0; i < size; i += 4) {
		const u32 *codePtr = (const u32 *)(data + i);
		u32 inst = codePtr[0];
		u32 next = (i < size - 4) ? codePtr[1] : 0;
		// MAGIC SPECIAL CASE for MOVW/MOVT readability!
		if ((inst & 0x0FF00000) == 0x03000000 && (next & 0x0FF00000) == 0x03400000) {
			u32 low = ((inst & 0x000F0000) >> 4) | (inst & 0x0FFF);
			u32 hi = ((next & 0x000F0000) >> 4) | (next	 & 0x0FFF);
			int reg0 = (inst & 0x0000F000) >> 12;
			int reg1 = (next & 0x0000F000) >> 12;
			if (reg0 == reg1) {
				sprintf(temp, "%08x MOV32? %s, %04x%04x", (u32)inst, ArmRegName(reg0), hi, low);
				INFO_LOG(DYNA_REC, "A:   %s", temp);
				i += 4;
				continue;
			}
		}
		ArmDis((u32)codePtr, inst, temp);
		INFO_LOG(DYNA_REC, "A:   %s", temp);
	}
}

namespace MIPSComp
{

Jit::Jit(MIPSState *mips) : blocks(mips, this), gpr(mips), fpr(mips), mips_(mips)
{ 
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode();

	js.startDefaultPrefix = true;
}

void Jit::DoState(PointerWrap &p)
{
	p.Do(js.startDefaultPrefix);
	p.DoMarker("Jit");
}

// This is here so the savestate matches between jit and non-jit.
void Jit::DoDummyState(PointerWrap &p)
{
	bool dummy = false;
	p.Do(dummy);
	p.DoMarker("Jit");
}

void Jit::FlushAll()
{
	gpr.FlushAll();
	fpr.FlushAll();
	FlushPrefixV();
}

void Jit::FlushPrefixV()
{
	if ((js.prefixSFlag & ArmJitState::PREFIX_DIRTY) != 0)
	{
		MOVI2R(R0, js.prefixS);
		STR(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_SPREFIX]));
		js.prefixSFlag = (ArmJitState::PrefixState) (js.prefixSFlag & ~ArmJitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & ArmJitState::PREFIX_DIRTY) != 0)
	{
		MOVI2R(R0, js.prefixT);
		STR(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_TPREFIX]));
		js.prefixTFlag = (ArmJitState::PrefixState) (js.prefixTFlag & ~ArmJitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & ArmJitState::PREFIX_DIRTY) != 0)
	{
		MOVI2R(R0, js.prefixD);
		STR(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_DPREFIX]));
		js.prefixDFlag = (ArmJitState::PrefixState) (js.prefixDFlag & ~ArmJitState::PREFIX_DIRTY);
	}
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
	GenerateFixedCode();
}

void Jit::ClearCacheAt(u32 em_address)
{
	// TODO: Properly.
	ClearCache();
}

void Jit::CompileAt(u32 addr)
{
	u32 op = Memory::Read_Instruction(addr);
	MIPSCompileOp(op);
}

void Jit::EatInstruction(u32 op)
{
	u32 info = MIPSGetInfo(op);
	_dbg_assert_msg_(JIT, !(info & DELAYSLOT), "Never eat a branch op.");
	_dbg_assert_msg_(JIT, !js.inDelaySlot, "Never eat an instruction inside a delayslot.");

	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void Jit::CompileDelaySlot(int flags)
{
	// preserve flag around the delay slot! Maybe this is not always necessary on ARM where 
	// we can (mostly) control whether we set the flag or not. Of course, if someone puts an slt in to the
	// delay slot, we're screwed.
	if (flags & DELAYSLOT_SAFE)
		MRS(R8);  // Save flags register. R8 is preserved through function calls and is not allocated.
	
	js.inDelaySlot = true;
	u32 op = Memory::Read_Instruction(js.compilerPC + 4);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		_MSR(true, false, R8);  // Restore flags register
}

void Jit::Compile(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull())
	{
		ClearCache();
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix())
	{
		js.startDefaultPrefix = false;
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		Compile(em_address);
	}
}

void Jit::RunLoopUntil(u64 globalticks)
{
	// TODO: copy globalticks somewhere
	((void (*)())enterCode)();
}
static int dontLogBlocks = 20;
static int logBlocks = 40;

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();

	// We add a check before the block, used when entering from a linked block.
	b->checkedEntry = GetCodePtr();
	// Downcount flag check. The last block decremented downcounter, and the flag should still be available.
	SetCC(CC_LT);
	MOVI2R(R0, js.blockStart);
	B((const void *)outerLoopPCInR0);
	SetCC(CC_AL);

	b->normalEntry = GetCodePtr();
	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(analysis);
	fpr.Start(analysis);

	int numInstructions = 0;
	int cycles = 0;
	int partialFlushOffset = 0;
	if (logBlocks > 0) logBlocks--;
	if (dontLogBlocks > 0) dontLogBlocks--;

// #define LOGASM
#ifdef LOGASM
	char temp[256];
#endif
	while (js.compiling)
	{
		gpr.SetCompilerPC(js.compilerPC);  // Let it know for log messages
		fpr.SetCompilerPC(js.compilerPC);
		u32 inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);
	
		js.compilerPC += 4;
		numInstructions++;
		if (!cpu_info.bArmV7 && (GetCodePtr() - b->checkedEntry - partialFlushOffset) > 4020)
		{
			// We need to prematurely flush as we are out of range
			FixupBranch skip = B_CC(CC_AL);
			FlushLitPool();
			SetJumpTarget(skip);
			partialFlushOffset = GetCodePtr() - b->checkedEntry;
		}
	}
	FlushLitPool();
#ifdef LOGASM
	if (logBlocks > 0 && dontLogBlocks == 0) {
		for (u32 cpc = em_address; cpc != js.compilerPC + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Instruction(cpc), cpc, temp, true);
			INFO_LOG(DYNA_REC, "M: %08x   %s", cpc, temp);
		}
	}
#endif

	b->codeSize = GetCodePtr() - b->normalEntry;

#ifdef LOGASM
	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(DYNA_REC, "=============== ARM ===============");
		DisassembleArm(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
#endif
	AlignCode16();

	// Don't forget to zap the instruction cache!
	FlushIcache();

	b->originalSize = numInstructions;
	return b->normalEntry;
}

void Jit::Comp_RunBlock(u32 op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(DYNA_REC, "Comp_RunBlock should never be reached!");
}

void Jit::Comp_Generic(u32 op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		MOVI2R(R0, js.compilerPC);
		MovToPC(R0);
		MOVI2R(R0, op);
		QuickCallFunction(R1, (void *)func);
	}

	// Might have eaten prefixes, hard to tell...
	if ((MIPSGetInfo(op) & IS_VFPU) != 0)
		js.PrefixStart();
}

void Jit::MovFromPC(ARMReg r) {
	LDR(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::MovToPC(ARMReg r) {
	STR(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::WriteDownCount(int offset)
{
	int theDowncount = js.downcountAmount + offset;
	LDR(R1, CTXREG, offsetof(MIPSState, downcount));
	Operand2 op2;
	if (TryMakeOperand2(theDowncount, op2)) // We can enlarge this if we used rotations
	{
		SUBS(R1, R1, op2);
		STR(R1, CTXREG, offsetof(MIPSState, downcount));
	} else {
		// Should be fine to use R2 here, flushed the regcache anyway.
		// If js.downcountAmount can be expressed as an Imm8, we don't need this anyway.
		MOVI2R(R2, theDowncount);
		SUBS(R1, R1, R2);
		STR(R1, CTXREG, offsetof(MIPSState, downcount));
	}
}

// IDEA - could have a WriteDualExit that takes two destinations and two condition flags,
// and just have conditional that set PC "twice". This only works when we fall back to dispatcher
// though, as we need to have the SUBS flag set in the end. So with block linking in the mix,
// I don't think this gives us that much benefit.
void Jit::WriteExit(u32 destination, int exit_num)
{
	WriteDownCount(); 
	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) {
		// It exists! Joy of joy!
		B(blocks.GetBlock(block)->checkedEntry);
		b->linkStatus[exit_num] = true;
	} else {
		MOVI2R(R0, destination);
		B((const void *)dispatcherPCInR0);	
	}
}

void Jit::WriteExitDestInR(ARMReg Reg) 
{
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void Jit::WriteSyscallExit()
{
	WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}

void Jit::Comp_DoNothing(u32 op) { }

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6) & 0x1F)
#define _POS ((op>>6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

//memory regions:
//
// 08-0A
// 48-4A
// 04-05
// 44-45
// mov eax, addrreg
	// shr eax, 28
// mov eax, [table+eax]
// mov dreg, [eax+offreg]
	
}