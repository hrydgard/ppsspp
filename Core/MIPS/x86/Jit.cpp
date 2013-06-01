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
#include "Common/ChunkFile.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"

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

// JitBlockCache doesn't use this, just stores it.
#pragma warning(disable:4355)
Jit::Jit(MIPSState *mips) : blocks(mips, this), mips_(mips)
{
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);
	asm_.Init(mips, this);

	// TODO: If it becomes possible to switch from the interpreter, this should be set right.
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
	gpr.Flush();
	fpr.Flush();
	FlushPrefixV();
}

void Jit::FlushPrefixV()
{
	if ((js.prefixSFlag & JitState::PREFIX_DIRTY) != 0)
	{
		MOV(32, M((void *)&mips_->vfpuCtrl[VFPU_CTRL_SPREFIX]), Imm32(js.prefixS));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0)
	{
		MOV(32, M((void *)&mips_->vfpuCtrl[VFPU_CTRL_TPREFIX]), Imm32(js.prefixT));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0)
	{
		MOV(32, M((void *)&mips_->vfpuCtrl[VFPU_CTRL_DPREFIX]), Imm32(js.prefixD));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}
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

void Jit::EatInstruction(u32 op)
{
	u32 info = MIPSGetInfo(op);
	_dbg_assert_msg_(JIT, !(info & DELAYSLOT), "Never eat a branch op.");
	_dbg_assert_msg_(JIT, !js.inDelaySlot, "Never eat an instruction inside a delayslot.");

	CheckJitBreakpoint(js.compilerPC + 4, 0);
	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
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
	js.afterOp = JitState::AFTER_NONE;
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

	js.numInstructions = 0;
	while (js.compiling)
	{
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(js.compilerPC, 0);

		u32 inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);

		if (js.afterOp & JitState::AFTER_CORE_STATE)
		{
			// TODO: Save/restore?
			FlushAll();
			CMP(32, M((void*)&coreState), Imm32(0));
			FixupBranch skipCheck = J_CC(CC_E);
			if (js.afterOp & JitState::AFTER_REWIND_PC_BAD_STATE)
				MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
			else
				MOV(32, M(&mips_->pc), Imm32(js.compilerPC + 4));
			WriteSyscallExit();
			SetJumpTarget(skipCheck);

			js.afterOp = JitState::AFTER_NONE;
		}

		js.compilerPC += 4;
		js.numInstructions++;
	}

	b->codeSize = (u32)(GetCodePtr() - b->normalEntry);
	NOP();
	AlignCode4();
	b->originalSize = js.numInstructions;
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

	const int info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Jit::WriteExit(u32 destination, int exit_num)
{
	if (!Memory::IsValidAddress(destination)) {
		ERROR_LOG(JIT, "Trying to write block exit to illegal destination %08x: pc = %08x", destination, currentMIPS->pc);
	}
	// If we need to verify coreState and rewind, we may not jump yet.
	if (js.afterOp & (JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE))
	{
		CMP(32, M((void*)&coreState), Imm32(0));
		FixupBranch skipCheck = J_CC(CC_E);
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		WriteSyscallExit();
		SetJumpTarget(skipCheck);

		js.afterOp = JitState::AFTER_NONE;
	}

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

	// If we need to verify coreState and rewind, we may not jump yet.
	if (js.afterOp & (JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE))
	{
		CMP(32, M((void*)&coreState), Imm32(0));
		FixupBranch skipCheck = J_CC(CC_E);
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		WriteSyscallExit();
		SetJumpTarget(skipCheck);

		js.afterOp = JitState::AFTER_NONE;
	}

	WriteDowncount();

	// Validate the jump to avoid a crash?
	if (!g_Config.bFastMemory)
	{
		CMP(32, R(EAX), Imm32(PSP_GetKernelMemoryBase()));
		FixupBranch tooLow = J_CC(CC_B);
		CMP(32, R(EAX), Imm32(PSP_GetUserMemoryEnd()));
		FixupBranch tooHigh = J_CC(CC_AE);

		// Need to set neg flag again if necessary.
		SUB(32, M(&currentMIPS->downcount), Imm32(0));
		JMP(asm_.dispatcher, true);

		SetJumpTarget(tooLow);
		SetJumpTarget(tooHigh);

		ABI_CallFunctionA(thunks.ProtectFunction((void *) Memory::GetPointer, 1), R(EAX));
		CMP(32, R(EAX), Imm32(0));
		FixupBranch skip = J_CC(CC_NE);

		// TODO: "Ignore" this so other threads can continue?
		if (g_Config.bIgnoreBadMemAccess)
			ABI_CallFunctionA(thunks.ProtectFunction((void *) Core_UpdateState, 1), Imm32(CORE_ERROR));

		SUB(32, M(&currentMIPS->downcount), Imm32(0));
		JMP(asm_.dispatcherCheckCoreState, true);
		SetJumpTarget(skip);

		SUB(32, M(&currentMIPS->downcount), Imm32(0));
		J_CC(CC_NE, asm_.dispatcher, true);
	}
	else
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
		ABI_CallFunction((void *)&JitBreakpoint);

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
	far_ = !g_Config.bIgnoreBadMemAccess || !CBreakPoints::MemChecks.empty();
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

bool Jit::JitSafeMem::PrepareWrite(OpArg &dest, int size)
{
	size_ = size;
	// If it's an immediate, we can do the write if valid.
	if (iaddr_ != (u32) -1)
	{
		if (ImmValid())
		{
			MemCheckImm(MEM_WRITE);

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
		dest = PrepareMemoryOpArg(MEM_WRITE);
	return true;
}

bool Jit::JitSafeMem::PrepareRead(OpArg &src, int size)
{
	size_ = size;
	if (iaddr_ != (u32) -1)
	{
		if (ImmValid())
		{
			MemCheckImm(MEM_READ);

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
		src = PrepareMemoryOpArg(MEM_READ);
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

OpArg Jit::JitSafeMem::PrepareMemoryOpArg(ReadType type)
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

	MemCheckAsm(type);

	if (!g_Config.bFastMemory)
	{
		// Is it in physical ram?
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetKernelMemoryBase() - offset_));
		tooLow_ = jit_->J_CC(CC_B);
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetUserMemoryEnd() - offset_ - (size_ - 1)));
		tooHigh_ = jit_->J_CC(CC_AE);

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
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryBase() - offset_));
	FixupBranch tooLow = jit_->J_CC(CC_B);
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryEnd() - offset_ - (size_ - 1)));
	jit_->J_CC(CC_B, safe_);
	jit_->SetJumpTarget(tooLow);
}

bool Jit::JitSafeMem::PrepareSlowWrite()
{
	// If it's immediate, we only need a slow write on invalid.
	if (iaddr_ != (u32) -1)
		return !g_Config.bFastMemory && !ImmValid();

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
			if (ImmValid())
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
		_dbg_assert_msg_(JIT, !Memory::IsValidAddress(iaddr_ + suboffset), "NextSlowRead() for a valid immediate address?");

		jit_->MOV(32, R(EAX), Imm32(iaddr_ + suboffset));
	}
	// For GPR, if xaddr_ was the dest register, this will be wrong.  Don't use in GPR.
	else
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));

	jit_->ABI_CallFunctionA(jit_->thunks.ProtectFunction(safeFunc, 1), R(EAX));
}

bool Jit::JitSafeMem::ImmValid()
{
	return iaddr_ != (u32) -1 && Memory::IsValidAddress(iaddr_) && Memory::IsValidAddress(iaddr_ + size_ - 1);
}

void Jit::JitSafeMem::Finish()
{
	// Memory::Read_U32/etc. may have tripped coreState.
	if (needsCheck_ && !g_Config.bIgnoreBadMemAccess)
		jit_->js.afterOp = JitState::AFTER_CORE_STATE;
	if (needsSkip_)
		jit_->SetJumpTarget(skip_);
	for (auto it = skipChecks_.begin(), end = skipChecks_.end(); it != end; ++it)
		jit_->SetJumpTarget(*it);
}

void JitMemCheck(u32 addr, int size, int isWrite)
{
	MemCheck *check = CBreakPoints::GetMemCheck(addr, size);
	if (check)
		check->Action(addr, isWrite == 1, size, currentMIPS->pc);
}

void Jit::JitSafeMem::MemCheckImm(ReadType type)
{
	MemCheck *check = CBreakPoints::GetMemCheck(iaddr_, size_);
	if (check)
	{
		if (!check->bOnRead && type == MEM_READ)
			return;
		if (!check->bOnWrite && type == MEM_WRITE)
			return;

		jit_->MOV(32, M(&jit_->mips_->pc), Imm32(jit_->js.compilerPC));
		jit_->ABI_CallFunctionCCC(jit_->thunks.ProtectFunction((void *)&JitMemCheck, 3), iaddr_, size_, type == MEM_WRITE ? 1 : 0);

		jit_->CMP(32, M((void*)&coreState), Imm32(0));
		skipChecks_.push_back(jit_->J_CC(CC_NE, true));
		jit_->js.afterOp = JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE;
	}
}

void Jit::JitSafeMem::MemCheckAsm(ReadType type)
{
	for (auto it = CBreakPoints::MemChecks.begin(), end = CBreakPoints::MemChecks.end(); it != end; ++it)
	{
		if (!it->bOnRead && type == MEM_READ)
			continue;
		if (!it->bOnWrite && type == MEM_WRITE)
			continue;

		FixupBranch skipNext, skipNextRange;
		if (it->bRange)
		{
			jit_->CMP(32, R(xaddr_), Imm32(it->iStartAddress - offset_));
			skipNext = jit_->J_CC(CC_B);
			jit_->CMP(32, R(xaddr_), Imm32(it->iEndAddress - offset_ - size_));
			skipNextRange = jit_->J_CC(CC_AE);
		}
		else
		{
			jit_->CMP(32, R(xaddr_), Imm32(it->iStartAddress - offset_));
			skipNext = jit_->J_CC(CC_NE);
		}

		jit_->PUSH(xaddr_);
		jit_->MOV(32, M(&jit_->mips_->pc), Imm32(jit_->js.compilerPC));
		jit_->ADD(32, R(xaddr_), Imm32(offset_));
		jit_->ABI_CallFunctionACC(jit_->thunks.ProtectFunction((void *)&JitMemCheck, 3), R(xaddr_), size_, type == MEM_WRITE ? 1 : 0);
		jit_->POP(xaddr_);

		jit_->CMP(32, M((void*)&coreState), Imm32(0));
		skipChecks_.push_back(jit_->J_CC(CC_NE, true));
		jit_->js.afterOp = JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE;

		jit_->SetJumpTarget(skipNext);
		if (it->bRange)
			jit_->SetJumpTarget(skipNextRange);
	}
}

void Jit::Comp_DoNothing(u32 op) { }

} // namespace
