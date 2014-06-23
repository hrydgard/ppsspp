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

#include "base/logging.h"
#include "Common/ChunkFile.h"
#include "Core/Reporting.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"

#include "ArmRegCache.h"
#include "ArmJit.h"
#include "CPUDetect.h"

#include "ext/disarm.h"

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
				ILOG("A:   %s", temp);
				i += 4;
				continue;
			}
		}
		ArmDis((u32)codePtr, inst, temp);
		ILOG("A:   %s", temp);
	}
}

namespace MIPSComp
{

ArmJitOptions::ArmJitOptions() {
	enableBlocklink = true;
	downcountInRegister = true;
	useBackJump = false;
	useForwardJump = false;
	cachePointers = true;
	// WARNING: These options don't work properly with cache clearing or jit compare.
	// Need to find a smart way to handle before enabling.
	immBranches = false;
	continueBranches = false;
	continueJumps = false;
	continueMaxInstructions = 300;

	useNEONVFPU = false;  // true
	if (!cpu_info.bNEON)
		useNEONVFPU = false;
}

Jit::Jit(MIPSState *mips) : blocks(mips, this), gpr(mips, &jo), fpr(mips), mips_(mips)
{ 
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode();

	js.startDefaultPrefix = true;
}

void Jit::DoState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1);
	if (!s)
		return;

	p.Do(js.startDefaultPrefix);
}

// This is here so the savestate matches between jit and non-jit.
void Jit::DoDummyState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1);
	if (!s)
		return;

	bool dummy = false;
	p.Do(dummy);
}

void Jit::FlushAll()
{
	gpr.FlushAll();
	fpr.FlushAll();
	FlushPrefixV();
}

void Jit::FlushPrefixV()
{
	if ((js.prefixSFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCHREG1, js.prefixS);
		STR(SCRATCHREG1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_SPREFIX]));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCHREG1, js.prefixT);
		STR(SCRATCHREG1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_TPREFIX]));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCHREG1, js.prefixD);
		STR(SCRATCHREG1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_DPREFIX]));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
	GenerateFixedCode();
}

void Jit::InvalidateCache()
{
	blocks.Clear();
}

void Jit::InvalidateCacheAt(u32 em_address, int length)
{
	blocks.InvalidateICache(em_address, length);
}

void Jit::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.")
	}

	js.numInstructions++;
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
	MIPSOpcode op = Memory::Read_Opcode_JIT(js.compilerPC + 4);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		_MSR(true, false, R8);  // Restore flags register
}


void Jit::Compile(u32 em_address) {
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		ClearCache();
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG(JIT, "An uneaten prefix at end of block: %08x", js.compilerPC - 4);
		js.LogPrefix();

		js.startDefaultPrefix = false;

		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		Compile(em_address);
	}
}

void Jit::RunLoopUntil(u64 globalticks)
{
	((void (*)())enterCode)();
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();

	// We add a downcount flag check before the block, used when entering from a linked block.
	// The last block decremented downcounter, and the flag should still be available.
	// Got three variants here of where we position the code, needs detailed benchmarking.

	FixupBranch bail;
	if (jo.useBackJump) {
		// Moves the MOVI2R and B *before* checkedEntry, and just branch backwards there.
		// Speedup seems to be zero unfortunately but I guess it may vary from device to device.
		// Not intrusive so keeping it around here to experiment with, may help on ARMv6 due to
		// large/slow construction of 32-bit immediates?
		JumpTarget backJump = GetCodePtr();
		gpr.SetRegImm(R0, js.blockStart);
		B((const void *)outerLoopPCInR0);
		b->checkedEntry = GetCodePtr();
		SetCC(CC_LT);
		B(backJump);
		SetCC(CC_AL);
	} else if (jo.useForwardJump) {
		b->checkedEntry = GetCodePtr();
		SetCC(CC_LT);
		bail = B();
		SetCC(CC_AL);
	} else {
		b->checkedEntry = GetCodePtr();
		SetCC(CC_LT);
		gpr.SetRegImm(R0, js.blockStart);
		B((const void *)outerLoopPCInR0);
		SetCC(CC_AL);
	}

	b->normalEntry = GetCodePtr();
	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(analysis);
	fpr.Start(analysis);

	int partialFlushOffset = 0;

	js.numInstructions = 0;
	while (js.compiling)
	{
		gpr.SetCompilerPC(js.compilerPC);  // Let it know for log messages
		fpr.SetCompilerPC(js.compilerPC);
		MIPSOpcode inst = Memory::Read_Opcode_JIT(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);
	
		js.compilerPC += 4;
		js.numInstructions++;
#ifndef HAVE_ARMV7
		// Disabled for now as it is crashing since Vertex Decoder JIT
		if (false && (GetCodePtr() - b->checkedEntry - partialFlushOffset) > 3200)
		{
			// We need to prematurely flush as we are out of range
			FixupBranch skip = B_CC(CC_AL);
			FlushLitPool();
			SetJumpTarget(skip);
			partialFlushOffset = GetCodePtr() - b->checkedEntry;
		}
#endif

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800)
		{
			FlushAll();
			WriteExit(js.compilerPC, js.nextExit++);
			js.compiling = false;
		}
	}

	if (jo.useForwardJump) {
		SetJumpTarget(bail);
		gpr.SetRegImm(R0, js.blockStart);
		B((const void *)outerLoopPCInR0);
	}

	FlushLitPool();

	char temp[256];
	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(JIT, "=============== mips ===============");
		for (u32 cpc = em_address; cpc != js.compilerPC + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp, true);
			INFO_LOG(JIT, "M: %08x   %s", cpc, temp);
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;

	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(JIT, "=============== ARM ===============");
		DisassembleArm(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

	b->originalSize = js.numInstructions;
	return b->normalEntry;
}

bool Jit::DescribeCodePtr(const u8 *ptr, std::string &name)
{
	// TODO: Not used by anything yet.
	return false;
}

void Jit::Comp_RunBlock(MIPSOpcode op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

bool Jit::ReplaceJalTo(u32 dest) {
	MIPSOpcode op(Memory::Read_Opcode_JIT(dest));
	if (!MIPS_IS_REPLACEMENT(op.encoding))
		return false;

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;
	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(HLE, "ReplaceJalTo: Invalid replacement op %08x at %08x", op.encoding, dest);
		return false;
	}

	if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
		// If it's a hook, we can't replace the jal, we have to go inside the func.
		return false;
	}

	// Warning - this might be bad if the code at the destination changes...
	if (entry->flags & REPFLAG_ALLOWINLINE) {
		// Jackpot! Just do it, no flushing. The code will be entirely inlined.

		// First, compile the delay slot. It's unconditional so no issues.
		CompileDelaySlot(DELAYSLOT_NICE);
		// Technically, we should write the unused return address to RA, but meh.
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();
		js.downcountAmount += cycles;
	} else {
		gpr.SetImm(MIPS_REG_RA, js.compilerPC + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		if (BLInRange((const void *)(entry->replaceFunc))) {
			BL((const void *)(entry->replaceFunc));
		} else {
			MOVI2R(R0, (u32)entry->replaceFunc);
			BL(R0);
		}
		WriteDownCountR(R0);
		js.downcountAmount = 0;  // we just subtracted most of it
	}

	js.compilerPC += 4;
	// No writing exits, keep going!

	// Add a trigger so that if the inlined code changes, we invalidate this block.
	// TODO: Correctly determine the size of this block.
	blocks.ProxyBlock(js.blockStart, dest, 4, GetCodePtr());
	return true;
}

void Jit::Comp_ReplacementFunc(MIPSOpcode op)
{
	// We get here if we execute the first instruction of a replaced function. This means
	// that we do need to return to RA.

	// Inlined function calls (caught in jal) are handled differently.

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(HLE, "Invalid replacement op %08x", op.encoding);
		return;
	}

	// JIT goes first.
	if (entry->jitReplaceFunc) {
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(js.compilerPC, true));
		} else {
			FlushAll();
			// Flushed, so R1 is safe.
			LDR(R1, CTXREG, MIPS_REG_RA * 4);
			js.downcountAmount = cycles;
			WriteExitDestInR(R1);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		FlushAll();
		gpr.SetRegImm(SCRATCHREG1, js.compilerPC);
		MovToPC(SCRATCHREG1);

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		if (BLInRange((const void *)(entry->replaceFunc))) {
			BL((const void *)(entry->replaceFunc));
		} else {
			MOVI2R(R0, (u32)entry->replaceFunc);
			BL(R0);
		}

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(js.compilerPC, true));
		} else {
			LDR(R1, CTXREG, MIPS_REG_RA * 4);
			WriteDownCountR(R0);
			js.downcountAmount = 0;  // we just subtracted most of it
			WriteExitDestInR(R1);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void Jit::Comp_Generic(MIPSOpcode op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		SaveDowncount();
		gpr.SetRegImm(SCRATCHREG1, js.compilerPC);
		MovToPC(SCRATCHREG1);
		gpr.SetRegImm(R0, op.encoding);
		QuickCallFunction(R1, (void *)func);
		RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Jit::MovFromPC(ARMReg r) {
	LDR(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::MovToPC(ARMReg r) {
	STR(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::SaveDowncount() {
	if (jo.downcountInRegister)
		STR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Jit::RestoreDowncount() {
	if (jo.downcountInRegister)
		LDR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Jit::WriteDownCount(int offset)
{
	if (jo.downcountInRegister) {
		int theDowncount = js.downcountAmount + offset;
		Operand2 op2;
		if (TryMakeOperand2(theDowncount, op2)) {
			SUBS(DOWNCOUNTREG, DOWNCOUNTREG, op2);
		} else {
			// Should be fine to use R2 here, flushed the regcache anyway.
			// If js.downcountAmount can be expressed as an Imm8, we don't need this anyway.
			gpr.SetRegImm(R2, theDowncount);
			SUBS(DOWNCOUNTREG, DOWNCOUNTREG, R2);
		}
	} else {
		int theDowncount = js.downcountAmount + offset;
		LDR(SCRATCHREG2, CTXREG, offsetof(MIPSState, downcount));
		Operand2 op2;
		if (TryMakeOperand2(theDowncount, op2)) {
			SUBS(SCRATCHREG2, SCRATCHREG2, op2);
		} else {
			// Should be fine to use R2 here, flushed the regcache anyway.
			// If js.downcountAmount can be expressed as an Imm8, we don't need this anyway.
			gpr.SetRegImm(R2, theDowncount);
			SUBS(SCRATCHREG2, SCRATCHREG2, R2);
		}
		STR(SCRATCHREG2, CTXREG, offsetof(MIPSState, downcount));
	}
}

// Abuses R2
void Jit::WriteDownCountR(ARMReg reg)
{
	if (jo.downcountInRegister) {
		SUBS(DOWNCOUNTREG, DOWNCOUNTREG, reg);
	} else {
		LDR(R2, CTXREG, offsetof(MIPSState, downcount));
		SUBS(R2, R2, reg);
		STR(R2, CTXREG, offsetof(MIPSState, downcount));
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
		gpr.SetRegImm(R0, destination);
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

void Jit::Comp_DoNothing(MIPSOpcode op) { }

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
