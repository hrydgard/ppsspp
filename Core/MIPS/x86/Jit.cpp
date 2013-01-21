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

#include <algorithm>
#include <iterator>
#include "../../Core.h"
#include "../../CoreTiming.h"
#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSInt.h"
#include "../MIPSTables.h"

#include "RegCache.h"
#include "Jit.h"

#include "../../Host.h"
#include "../../Debugger/Breakpoints.h"

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

const bool USE_JIT_MISSMAP = false;
static std::map<std::string, u32> notJitOps;

template<typename A, typename B>
std::pair<B,A> flip_pair(const std::pair<A,B> &p)
{
    return std::pair<B, A>(p.second, p.first);
}

void JitBreakpoint()
{
	Core_EnableStepping(true);
	host->SetDebugMode(true);

	if (CBreakPoints::IsTempBreakPoint(currentMIPS->pc))
		CBreakPoints::RemoveBreakPoint(currentMIPS->pc);

	// There's probably a better place for this.
	if (USE_JIT_MISSMAP)
	{
		std::map<u32, std::string> notJitSorted;
		std::transform(notJitOps.begin(), notJitOps.end(), std::inserter(notJitSorted, notJitSorted.begin()), flip_pair<std::string, u32>);

		std::string message;
		char temp[256];
		int remaining = 15;
		for (auto it = notJitSorted.rbegin(), end = notJitSorted.rend(); it != end && --remaining >= 0; ++it)
		{
			snprintf(temp, 256, " (%d), ", it->first);
			message += it->second + temp;
		}

		if (message.size() > 2)
			message.resize(message.size() - 2);

		NOTICE_LOG(JIT, "Top ops compiled to interpreter: %s", message.c_str());
	}
}

static void JitLogMiss(u32 op)
{
	if (USE_JIT_MISSMAP)
		notJitOps[MIPSGetName(op)]++;

	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	func(op);
}

Jit::Jit(MIPSState *mips) : blocks(mips), mips_(mips)
{
	blocks.Init();
	asm_.Init(mips, this);
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);
}

void Jit::FlushAll()
{
	gpr.Flush(FLUSH_ALL);
	fpr.Flush(FLUSH_ALL);
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
}

void Jit::ClearCacheAt(u32 em_address)
{
	// TODO: Properly.
	ClearCache();
}

void Jit::CompileDelaySlot(bool saveFlags)
{
	const u32 addr = js.compilerPC + 4;

	// TODO: If we ever support conditional breakpoints, we need to handle the flags more carefully.
	CheckJitBreakpoint(addr);

	if (saveFlags)
		SAVE_FLAGS; // preserve flag around the delay slot!

	u32 op = Memory::Read_Instruction(addr);
	MIPSCompileOp(op);

	FlushAll();
	if (saveFlags)
		LOAD_FLAGS; // restore flag!
}

void Jit::CompileAt(u32 addr)
{
	CheckJitBreakpoint(addr);
	u32 op = Memory::Read_Instruction(addr);
	MIPSCompileOp(op);
}

void Jit::Compile(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull())
	{
		ClearCache();
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink, DoJit(em_address, b));
}

void Jit::RunLoopUntil(u64 globalticks)
{
	// TODO: copy globalticks somewhere
	((void (*)())asm_.enterCode)();
	// NOTICE_LOG(HLE, "Exited jitted code at %i, corestate=%i, dc=%i", CoreTiming::GetTicks() / 1000, (int)coreState, CoreTiming::downcount);
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;

	// We add a check before the block, used when entering from a linked block.
	b->checkedEntry = GetCodePtr();
	// Downcount flag check. The last block decremented downcounter, and the flag should still be available.
	FixupBranch skip = J_CC(CC_NBE);
	MOV(32, M(&mips_->pc), Imm32(js.blockStart));
	JMP(asm_.outerLoop, true);  // downcount hit zero - go advance.
	SetJumpTarget(skip);

	b->normalEntry = GetCodePtr();

	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(mips_, analysis);
	fpr.Start(mips_, analysis);

	int numInstructions = 0;
	while (js.compiling)
	{
		u32 inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(js.compilerPC);

		MIPSCompileOp(inst);

		js.compilerPC += 4;
		numInstructions++;
	}

	b->codeSize = (u32)(GetCodePtr() - b->normalEntry);
	NOP();
	AlignCode4();
	b->originalSize = numInstructions;
	return b->normalEntry;
}

void Jit::Comp_RunBlock(u32 op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(DYNA_REC, "Comp_RunBlock");
}

void Jit::Comp_Generic(u32 op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		if (USE_JIT_MISSMAP)
			ABI_CallFunctionC((void *)&JitLogMiss, op);
		else
			ABI_CallFunctionC((void *)func, op);
	}
	else
		_dbg_assert_msg_(JIT, 0, "Trying to compile instruction that can't be interpreted");
}

void Jit::WriteExit(u32 destination, int exit_num)
{
	SUB(32, M(&currentMIPS->downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));

	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) {
		// It exists! Joy of joy!
		JMP(blocks.GetBlock(block)->checkedEntry, true);
		b->linkStatus[exit_num] = true;
	} else {
		// No blocklinking.
		MOV(32, M(&mips_->pc), Imm32(destination));
		JMP(asm_.dispatcher, true);
	}
}

void Jit::WriteExitDestInEAX()
{
	// TODO: Some wasted potential, dispatcher will alwa
	MOV(32, M(&mips_->pc), R(EAX));
	SUB(32, M(&currentMIPS->downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));
	JMP(asm_.dispatcher, true);
}

void Jit::WriteSyscallExit()
{
	SUB(32, M(&currentMIPS->downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));
	JMP(asm_.dispatcherCheckCoreState, true);
}

bool Jit::CheckJitBreakpoint(u32 addr)
{
	if (CBreakPoints::IsAddressBreakPoint(addr))
	{
		FlushAll();
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		CALL((void *)&JitBreakpoint);
		WriteSyscallExit();

		return true;
	}

	return false;
}

} // namespace
