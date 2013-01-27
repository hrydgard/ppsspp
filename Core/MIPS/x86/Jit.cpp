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
#include "../../Config.h"
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
	gpr.Flush();
	fpr.Flush();
}

void Jit::WriteDowncount(int offset)
{
	const int downcount = js.downcountAmount + offset;
	SUB(32, M(&currentMIPS->downcount), downcount > 127 ? Imm32(downcount) : Imm8(downcount));
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

void Jit::CompileDelaySlot(int flags)
{
	const u32 addr = js.compilerPC + 4;

	// TODO: If we ever support conditional breakpoints, we need to handle the flags more carefully.
	// Need to offset the downcount which was already incremented for the branch + delay slot.
	CheckJitBreakpoint(addr, -2);

	if (flags & DELAYSLOT_SAFE)
		SAVE_FLAGS; // preserve flag around the delay slot!

	js.inDelaySlot = true;
	u32 op = Memory::Read_Instruction(addr);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		LOAD_FLAGS; // restore flag!
}

void Jit::CompileAt(u32 addr)
{
	CheckJitBreakpoint(addr, 0);
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
	js.PrefixStart();

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
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(js.compilerPC, 0);

		u32 inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

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
	_dbg_assert_msg_(JIT, (MIPSGetInfo(op) & DELAYSLOT) == 0, "Cannot use interpreter for branch ops.");

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
	WriteDowncount();

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
	// TODO: Some wasted potential, dispatcher will always read this back into EAX.
	MOV(32, M(&mips_->pc), R(EAX));
	WriteDowncount();
	JMP(asm_.dispatcher, true);
}

void Jit::WriteSyscallExit()
{
	WriteDowncount();
	JMP(asm_.dispatcherCheckCoreState, true);
}

bool Jit::CheckJitBreakpoint(u32 addr, int downcountOffset)
{
	if (CBreakPoints::IsAddressBreakPoint(addr))
	{
		FlushAll();
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		CALL((void *)&JitBreakpoint);

		WriteDowncount(downcountOffset);
		JMP(asm_.dispatcherCheckCoreState, true);

		return true;
	}

	return false;
}

Jit::JitSafeMem::JitSafeMem(Jit *jit, int raddr, s32 offset)
	: jit_(jit), raddr_(raddr), offset_(offset), needsCheck_(false), needsSkip_(false)
{
	// This makes it more instructions, so let's play it safe and say we need a far jump.
	far_ = !g_Config.bIgnoreBadMemAccess;
	if (jit_->gpr.IsImmediate(raddr_))
		iaddr_ = jit_->gpr.GetImmediate32(raddr_) + offset_;
	else
		iaddr_ = (u32) -1;
}

void Jit::JitSafeMem::SetFar()
{
	_dbg_assert_msg_(JIT, !needsSkip_, "Sorry, you need to call SetFar() earlier.");
	far_ = true;
}

bool Jit::JitSafeMem::PrepareWrite(OpArg &dest)
{
	// If it's an immediate, we can do the write if valid.
	if (iaddr_ != (u32) -1)
	{
		if (Memory::IsValidAddress(iaddr_))
		{
#ifdef _M_IX86
			dest = M(Memory::base + (iaddr_ & Memory::MEMVIEW32_MASK));
#else
			dest = MDisp(RBX, iaddr_);
#endif
			return true;
		}
		else
			return false;
	}
	// Otherwise, we always can do the write (conditionally.)
	else
		dest = PrepareMemoryOpArg();
	return true;
}

bool Jit::JitSafeMem::PrepareRead(OpArg &src)
{
	if (iaddr_ != (u32) -1)
	{
		if (Memory::IsValidAddress(iaddr_))
		{
#ifdef _M_IX86
			src = M(Memory::base + (iaddr_ & Memory::MEMVIEW32_MASK));
#else
			src = MDisp(RBX, iaddr_);
#endif
			return true;
		}
		else
			return false;
	}
	else
		src = PrepareMemoryOpArg();
	return true;
}

OpArg Jit::JitSafeMem::NextFastAddress(int suboffset)
{
	if (jit_->gpr.IsImmediate(raddr_))
	{
		u32 addr = jit_->gpr.GetImmediate32(raddr_) + offset_ + suboffset;

#ifdef _M_IX86
		return M(Memory::base + (addr & Memory::MEMVIEW32_MASK));
#else
		return MDisp(RBX, addr);
#endif
	}

#ifdef _M_IX86
	return MDisp(xaddr_, (u32) Memory::base + offset_ + suboffset);
#else
	return MComplex(RBX, xaddr_, SCALE_1, offset_ + suboffset);
#endif
}

OpArg Jit::JitSafeMem::PrepareMemoryOpArg()
{
	// We may not even need to move into EAX as a temporary.
	// TODO: Except on x86 in fastmem mode.
	if (jit_->gpr.R(raddr_).IsSimpleReg())
	{
		jit_->gpr.BindToRegister(raddr_, true, false);
		xaddr_ = jit_->gpr.RX(raddr_);
	}
	else
	{
		jit_->MOV(32, R(EAX), jit_->gpr.R(raddr_));
		xaddr_ = EAX;
	}

	if (!g_Config.bFastMemory)
	{
		// Is it in physical ram?
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetKernelMemoryBase()));
		tooLow_ = jit_->J_CC(CC_L);
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetUserMemoryEnd()));
		tooHigh_ = jit_->J_CC(CC_GE);

		// We may need to jump back up here.
		safe_ = jit_->GetCodePtr();
	}
	else
	{
#ifdef _M_IX86
		// Need to modify it, too bad.
		if (xaddr_ != EAX)
			jit_->MOV(32, R(EAX), R(xaddr_));
		jit_->AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
		xaddr_ = EAX;
#endif
	}

#ifdef _M_IX86
		return MDisp(xaddr_, (u32) Memory::base + offset_);
#else
		return MComplex(RBX, xaddr_, SCALE_1, offset_);
#endif
}

void Jit::JitSafeMem::PrepareSlowAccess()
{
	// Skip the fast path (which the caller wrote just now.)
	skip_ = jit_->J(far_);
	needsSkip_ = true;
	jit_->SetJumpTarget(tooLow_);
	jit_->SetJumpTarget(tooHigh_);

	// Might also be the scratchpad.
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryBase()));
	FixupBranch tooLow = jit_->J_CC(CC_L);
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryEnd()));
	jit_->J_CC(CC_L, safe_);
	jit_->SetJumpTarget(tooLow);
}

bool Jit::JitSafeMem::PrepareSlowWrite()
{
	// If it's immediate, we only need a slow write on invalid.
	if (iaddr_ != (u32) -1)
		return !g_Config.bFastMemory && !Memory::IsValidAddress(iaddr_);

	if (!g_Config.bFastMemory)
	{
		PrepareSlowAccess();
		return true;
	}
	else
		return false;
}

void Jit::JitSafeMem::DoSlowWrite(void *safeFunc, const OpArg src, int suboffset)
{
	if (iaddr_ != (u32) -1)
		jit_->MOV(32, R(EAX), Imm32(iaddr_ + suboffset));
	else
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));

	jit_->ABI_CallFunctionAA(jit_->thunks.ProtectFunction(safeFunc, 2), src, R(EAX));
	needsCheck_ = true;
}

bool Jit::JitSafeMem::PrepareSlowRead(void *safeFunc)
{
	if (!g_Config.bFastMemory)
	{
		if (iaddr_ != (u32) -1)
		{
			// No slow read necessary.
			if (Memory::IsValidAddress(iaddr_))
				return false;
			jit_->MOV(32, R(EAX), Imm32(iaddr_));
		}
		else
		{
			PrepareSlowAccess();
			jit_->LEA(32, EAX, MDisp(xaddr_, offset_));
		}

		jit_->ABI_CallFunctionA(jit_->thunks.ProtectFunction(safeFunc, 1), R(EAX));
		needsCheck_ = true;
		return true;
	}
	else
		return false;
}

void Jit::JitSafeMem::NextSlowRead(void *safeFunc, int suboffset)
{
	_dbg_assert_msg_(JIT, !g_Config.bFastMemory, "NextSlowRead() called in fast memory mode?");

	// For simplicity, do nothing for 0.  We already read in PrepareSlowRead().
	if (suboffset == 0)
		return;

	if (jit_->gpr.IsImmediate(raddr_))
	{
		_dbg_assert_msg_(JIT, !Memory::IsValidAddress(iaddr_), "NextSlowRead() for a valid immediate address?");

		jit_->MOV(32, R(EAX), Imm32(iaddr_ + suboffset));
	}
	// For GPR, if xaddr_ was the dest register, this will be wrong.  Don't use in GPR.
	else
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));

	jit_->ABI_CallFunctionA(jit_->thunks.ProtectFunction(safeFunc, 1), R(EAX));
}

void Jit::JitSafeMem::Finish()
{
	if (needsCheck_)
	{
		// Memory::Read_U32/etc. may have tripped coreState.
		if (!g_Config.bIgnoreBadMemAccess)
		{
			jit_->CMP(32, M((void*)&coreState), Imm32(0));
			FixupBranch skipCheck = jit_->J_CC(CC_E);
			jit_->MOV(32, M(&currentMIPS->pc), Imm32(jit_->js.compilerPC + 4));
			jit_->WriteSyscallExit();
			jit_->SetJumpTarget(skipCheck);
		}
	}
	if (needsSkip_)
		jit_->SetJumpTarget(skip_);
}

} // namespace
