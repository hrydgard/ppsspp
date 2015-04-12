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

#include "math/math_util.h"

#include "Common/ChunkFile.h"
#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"

#include "RegCache.h"
#include "Jit.h"

#include "Core/Host.h"
#include "Core/Debugger/Breakpoints.h"

namespace MIPSComp
{
using namespace Gen;

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
std::pair<B,A> flip_pair(const std::pair<A,B> &p) {
	return std::pair<B, A>(p.second, p.first);
}

u32 JitBreakpoint()
{
	// Should we skip this breakpoint?
	if (CBreakPoints::CheckSkipFirst() == currentMIPS->pc)
		return 0;

	auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
	if (cond && !cond->Evaluate())
		return 0;

	Core_EnableStepping(true);
	host->SetDebugMode(true);

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

	return 1;
}

extern void JitMemCheckCleanup();

static void JitLogMiss(MIPSOpcode op)
{
	if (USE_JIT_MISSMAP)
		notJitOps[MIPSGetName(op)]++;

	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	func(op);
}

#ifdef _MSC_VER
// JitBlockCache doesn't use this, just stores it.
#pragma warning(disable:4355)
#endif
Jit::Jit(MIPSState *mips) : blocks(mips, this), mips_(mips)
{
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);
	asm_.Init(mips, this, &jo);
	safeMemFuncs.Init(&thunks);

	js.startDefaultPrefix = mips_->HasDefaultPrefix();

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

Jit::~Jit() {
}

void Jit::DoState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	p.Do(js.startDefaultPrefix);
	if (s >= 2) {
		p.Do(js.hasSetRounding);
		js.lastSetRounding = 0;
	} else {
		js.hasSetRounding = 1;
	}

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they load a state, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

// This is here so the savestate matches between jit and non-jit.
void Jit::DoDummyState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	bool dummy = false;
	p.Do(dummy);
	if (s >= 2) {
		dummy = true;
		p.Do(dummy);
	}
}


void Jit::GetStateAndFlushAll(RegCacheState &state)
{
	gpr.GetState(state.gpr);
	fpr.GetState(state.fpr);
	FlushAll();
}

void Jit::RestoreState(const RegCacheState& state)
{
	gpr.RestoreState(state.gpr);
	fpr.RestoreState(state.fpr);
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
		MOV(32, M(&mips_->vfpuCtrl[VFPU_CTRL_SPREFIX]), Imm32(js.prefixS));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0)
	{
		MOV(32, M(&mips_->vfpuCtrl[VFPU_CTRL_TPREFIX]), Imm32(js.prefixT));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0)
	{
		MOV(32, M(&mips_->vfpuCtrl[VFPU_CTRL_DPREFIX]), Imm32(js.prefixD));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}
}

void Jit::WriteDowncount(int offset)
{
	const int downcount = js.downcountAmount + offset;
	SUB(32, M(&mips_->downcount), downcount > 127 ? Imm32(downcount) : Imm8(downcount));
}

void Jit::RestoreRoundingMode(bool force, XEmitter *emitter)
{
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (g_Config.bSetRoundingMode && (force || g_Config.bForceFlushToZero || js.hasSetRounding))
	{
		if (emitter == NULL)
			emitter = this;
		emitter->STMXCSR(M(&mips_->temp));
		// Clear the rounding mode and flush-to-zero bits back to 0.
		emitter->AND(32, M(&mips_->temp), Imm32(~(7 << 13)));
		emitter->LDMXCSR(M(&mips_->temp));
	}
}

void Jit::ApplyRoundingMode(bool force, XEmitter *emitter)
{
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (g_Config.bSetRoundingMode && (force || g_Config.bForceFlushToZero || js.hasSetRounding))
	{
		if (emitter == NULL)
			emitter = this;
		emitter->MOV(32, R(EAX), M(&mips_->fcr31));
		emitter->AND(32, R(EAX), Imm32(0x1000003));

		// If it's 0, we don't actually bother setting.  This is the most common.
		// We always use nearest as the default rounding mode with
		// flush-to-zero disabled.
		FixupBranch skip;
		if (!g_Config.bForceFlushToZero)
			skip = emitter->J_CC(CC_Z);

		emitter->STMXCSR(M(&mips_->temp));

		// The MIPS bits don't correspond exactly, so we have to adjust.
		// 0 -> 0 (skip2), 1 -> 3, 2 -> 2 (skip2), 3 -> 1
		emitter->TEST(8, R(AL), Imm8(1));
		FixupBranch skip2 = emitter->J_CC(CC_Z);
		emitter->XOR(32, R(EAX), Imm8(2));
		emitter->SetJumpTarget(skip2);

		emitter->SHL(32, R(EAX), Imm8(13));
		emitter->OR(32, M(&mips_->temp), R(EAX));

		if (g_Config.bForceFlushToZero) {
			emitter->OR(32, M(&mips_->temp), Imm32(1 << 15));
		} else {
			emitter->TEST(32, M(&mips_->fcr31), Imm32(1 << 24));
			FixupBranch skip3 = emitter->J_CC(CC_Z);
			emitter->OR(32, M(&mips_->temp), Imm32(1 << 15));
			emitter->SetJumpTarget(skip3);
		}

		emitter->LDMXCSR(M(&mips_->temp));

		if (!g_Config.bForceFlushToZero)
			emitter->SetJumpTarget(skip);
	}
}

void Jit::UpdateRoundingMode(XEmitter *emitter)
{
	if (g_Config.bSetRoundingMode)
	{
		if (emitter == NULL)
			emitter = this;

		// If it's only ever 0, we don't actually bother applying or restoring it.
		// This is the most common situation.
		emitter->TEST(32, M(&mips_->fcr31), Imm32(0x01000003));
		FixupBranch skip = emitter->J_CC(CC_Z);
		emitter->MOV(8, M(&js.hasSetRounding), Imm8(1));
		emitter->SetJumpTarget(skip);
	}
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
}

void Jit::InvalidateCache()
{
	blocks.Clear();
}

void Jit::CompileDelaySlot(int flags, RegCacheState *state)
{
	// Need to offset the downcount which was already incremented for the branch + delay slot.
	CheckJitBreakpoint(GetCompilerPC() + 4, -2);

	if (flags & DELAYSLOT_SAFE)
		SAVE_FLAGS; // preserve flag around the delay slot!

	js.inDelaySlot = true;
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
	{
		if (state != NULL)
			GetStateAndFlushAll(*state);
		else
			FlushAll();
	}
	if (flags & DELAYSLOT_SAFE)
		LOAD_FLAGS; // restore flag!
}

void Jit::EatInstruction(MIPSOpcode op)
{
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.");
	}

	CheckJitBreakpoint(GetCompilerPC() + 4, 0);
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

	bool cleanSlate = false;

	if (js.hasSetRounding && !js.lastSetRounding) {
		WARN_LOG(JIT, "Detected rounding mode usage, rebuilding jit with checks");
		// Won't loop, since hasSetRounding is only ever set to 1.
		js.lastSetRounding = js.hasSetRounding;
		cleanSlate = true;
	}

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG(JIT, "An uneaten prefix at end of block: %08x", GetCompilerPC() - 4);
		js.LogPrefix();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		js.startDefaultPrefix = false;
		cleanSlate = true;
	}

	if (cleanSlate) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		Compile(em_address);
	}
}

void Jit::RunLoopUntil(u64 globalticks)
{
	((void (*)())asm_.enterCode)();
}

u32 Jit::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode Jit::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.afterOp = JitState::AFTER_NONE;
	js.PrefixStart();

	// We add a check before the block, used when entering from a linked block.
	b->checkedEntry = GetCodePtr();
	// Downcount flag check. The last block decremented downcounter, and the flag should still be available.
	FixupBranch skip = J_CC(CC_NS);
	MOV(32, M(&mips_->pc), Imm32(js.blockStart));
	JMP(asm_.outerLoop, true);  // downcount hit zero - go advance.
	SetJumpTarget(skip);

	b->normalEntry = GetCodePtr();

	MIPSAnalyst::AnalysisResults analysis = MIPSAnalyst::Analyze(em_address);

	gpr.Start(mips_, &js, &jo, analysis);
	fpr.Start(mips_, &js, &jo, analysis);

	js.numInstructions = 0;
	while (js.compiling) {
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(GetCompilerPC(), 0);

		MIPSOpcode inst = Memory::Read_Opcode_JIT(GetCompilerPC());
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);

		if (js.afterOp & JitState::AFTER_CORE_STATE) {
			// TODO: Save/restore?
			FlushAll();

			// If we're rewinding, CORE_NEXTFRAME should not cause a rewind.
			// It doesn't really matter either way if we're not rewinding.
			// CORE_RUNNING is <= CORE_NEXTFRAME.
			CMP(32, M(&coreState), Imm32(CORE_NEXTFRAME));
			FixupBranch skipCheck = J_CC(CC_LE);
			if (js.afterOp & JitState::AFTER_REWIND_PC_BAD_STATE)
				MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
			else
				MOV(32, M(&mips_->pc), Imm32(GetCompilerPC() + 4));
			WriteSyscallExit();
			SetJumpTarget(skipCheck);

			js.afterOp = JitState::AFTER_NONE;
		}
		if (js.afterOp & JitState::AFTER_MEMCHECK_CLEANUP) {
			js.afterOp &= ~JitState::AFTER_MEMCHECK_CLEANUP;
		}

		js.compilerPC += 4;
		js.numInstructions++;

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800 || js.numInstructions >= JitBlockCache::MAX_BLOCK_INSTRUCTIONS)
		{
			FlushAll();
			WriteExit(GetCompilerPC(), js.nextExit++);
			js.compiling = false;
		}
	}

	b->codeSize = (u32)(GetCodePtr() - b->normalEntry);
	NOP();
	AlignCode4();
	if (js.lastContinuedPC == 0)
		b->originalSize = js.numInstructions;
	else
	{
		// We continued at least once.  Add the last proxy and set the originalSize correctly.
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (GetCompilerPC() - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
		b->originalSize = js.initialBlockSize;
	}
	return b->normalEntry;
}

void Jit::AddContinuedBlock(u32 dest)
{
	// The first block is the root block.  When we continue, we create proxy blocks after that.
	if (js.lastContinuedPC == 0)
		js.initialBlockSize = js.numInstructions;
	else
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (GetCompilerPC() - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
	js.lastContinuedPC = dest;
}

bool Jit::DescribeCodePtr(const u8 *ptr, std::string &name)
{
	u32 jitAddr = blocks.GetAddressFromBlockPtr(ptr);

	// Returns 0 when it's valid, but unknown.
	if (jitAddr == 0)
		name = "UnknownOrDeletedBlock";
	else if (jitAddr != (u32)-1)
	{
		char temp[1024];
		const std::string label = symbolMap.GetDescription(jitAddr);
		if (!label.empty())
			snprintf(temp, sizeof(temp), "%08x_%s", jitAddr, label.c_str());
		else
			snprintf(temp, sizeof(temp), "%08x", jitAddr);
		name = temp;
	}
	else if (asm_.IsInSpace(ptr))
		name = "RunLoopUntil";
	else if (thunks.IsInSpace(ptr))
		name = "Thunk";
	else if (safeMemFuncs.IsInSpace(ptr))
		name = "JitSafeMem";
	else if (IsInSpace(ptr))
		name = "Unknown";
	// Not anywhere in jit, then.
	else
		return false;

	// If we got here, one of the above cases matched.
	return true;
}

void Jit::Comp_RunBlock(MIPSOpcode op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock");
}

bool Jit::ReplaceJalTo(u32 dest) {
	const ReplacementTableEntry *entry = nullptr;
	u32 funcSize = 0;
	if (!CanReplaceJalTo(dest, &entry, &funcSize)) {
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
		gpr.SetImm(MIPS_REG_RA, GetCompilerPC() + 8);
		CompileDelaySlot(DELAYSLOT_NICE);
		FlushAll();
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		RestoreRoundingMode();
		ABI_CallFunction(entry->replaceFunc);
		SUB(32, M(&mips_->downcount), R(EAX));
		ApplyRoundingMode();
	}

	js.compilerPC += 4;
	// No writing exits, keep going!

	// Add a trigger so that if the inlined code changes, we invalidate this block.
	blocks.ProxyBlock(js.blockStart, dest, funcSize / sizeof(u32), GetCodePtr());
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

	u32 funcSize = symbolMap.GetFunctionSize(GetCompilerPC());
	bool disabled = (entry->flags & REPFLAG_DISABLED) != 0;
	if (!disabled && funcSize != SymbolMap::INVALID_ADDRESS && funcSize > sizeof(u32)) {
		// We don't need to disable hooks, the code will still run.
		if ((entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) == 0) {
			// Any breakpoint at the func entry was already tripped, so we can still run the replacement.
			// That's a common case - just to see how often the replacement hits.
			disabled = CBreakPoints::RangeContainsBreakPoint(GetCompilerPC() + sizeof(u32), funcSize - sizeof(u32));
		}
	}

	if (disabled) {
		MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
	} else if (entry->jitReplaceFunc) {
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
		} else {
			FlushAll();
			MOV(32, R(ECX), M(&mips_->r[MIPS_REG_RA]));
			js.downcountAmount += cycles;
			WriteExitDestInReg(ECX);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		FlushAll();

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		RestoreRoundingMode();
		ABI_CallFunction(entry->replaceFunc);

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
		} else {
			MOV(32, R(ECX), M(&mips_->r[MIPS_REG_RA]));
			SUB(32, M(&mips_->downcount), R(EAX));
			ApplyRoundingMode();
			// Need to set flags again, ApplyRoundingMode destroyed them (and EAX.)
			SUB(32, M(&mips_->downcount), Imm8(0));
			WriteExitDestInReg(ECX);
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
	_dbg_assert_msg_(JIT, (MIPSGetInfo(op) & DELAYSLOT) == 0, "Cannot use interpreter for branch ops.");

	if (func)
	{
		// TODO: Maybe we'd be better off keeping the rounding mode within interp?
		RestoreRoundingMode();
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		if (USE_JIT_MISSMAP)
			ABI_CallFunctionC(&JitLogMiss, op.encoding);
		else
			ABI_CallFunctionC(func, op.encoding);
		ApplyRoundingMode();
	}
	else
		ERROR_LOG_REPORT(JIT, "Trying to compile instruction %08x that can't be interpreted", op.encoding);

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Jit::WriteExit(u32 destination, int exit_num)
{
	_dbg_assert_msg_(JIT, exit_num < MAX_JIT_BLOCK_EXITS, "Expected a valid exit_num");

	if (!Memory::IsValidAddress(destination)) {
		ERROR_LOG_REPORT(JIT, "Trying to write block exit to illegal destination %08x: pc = %08x", destination, currentMIPS->pc);
	}
	// If we need to verify coreState and rewind, we may not jump yet.
	if (js.afterOp & (JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE))
	{
		// CORE_RUNNING is <= CORE_NEXTFRAME.
		CMP(32, M(&coreState), Imm32(CORE_NEXTFRAME));
		FixupBranch skipCheck = J_CC(CC_LE);
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		WriteSyscallExit();
		SetJumpTarget(skipCheck);
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

		// Normally, exits are 15 bytes (MOV + &pc + dest + JMP + dest) on 64 or 32 bit.
		// But just in case we somehow optimized, pad.
		ptrdiff_t actualSize = GetWritableCodePtr() - b->exitPtrs[exit_num];
		int pad = JitBlockCache::GetBlockExitSize() - (int)actualSize;
		for (int i = 0; i < pad; ++i) {
			INT3();
		}
	}
}

void Jit::WriteExitDestInReg(X64Reg reg)
{
	// If we need to verify coreState and rewind, we may not jump yet.
	if (js.afterOp & (JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE))
	{
		// CORE_RUNNING is <= CORE_NEXTFRAME.
		CMP(32, M(&coreState), Imm32(CORE_NEXTFRAME));
		FixupBranch skipCheck = J_CC(CC_LE);
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		WriteSyscallExit();
		SetJumpTarget(skipCheck);
	}

	MOV(32, M(&mips_->pc), R(reg));
	WriteDowncount();

	// Validate the jump to avoid a crash?
	if (!g_Config.bFastMemory)
	{
		CMP(32, R(reg), Imm32(PSP_GetKernelMemoryBase()));
		FixupBranch tooLow = J_CC(CC_B);
		CMP(32, R(reg), Imm32(PSP_GetUserMemoryEnd()));
		FixupBranch tooHigh = J_CC(CC_AE);

		// Need to set neg flag again.
		SUB(32, M(&mips_->downcount), Imm8(0));
		if (reg == EAX)
			J_CC(CC_NS, asm_.dispatcherInEAXNoCheck, true);
		JMP(asm_.dispatcher, true);

		SetJumpTarget(tooLow);
		SetJumpTarget(tooHigh);

		ABI_CallFunctionA((const void *)&Memory::GetPointer, R(reg));

		// If we're ignoring, coreState didn't trip - so trip it now.
		if (g_Config.bIgnoreBadMemAccess)
		{
			CMP(32, R(EAX), Imm32(0));
			FixupBranch skip = J_CC(CC_NE);
			ABI_CallFunctionA((const void *)&Core_UpdateState, Imm32(CORE_ERROR));
			SetJumpTarget(skip);
		}

		SUB(32, M(&mips_->downcount), Imm8(0));
		JMP(asm_.dispatcherCheckCoreState, true);
	}
	else if (reg == EAX)
	{
		J_CC(CC_NS, asm_.dispatcherInEAXNoCheck, true);
		JMP(asm_.dispatcher, true);
	}
	else
		JMP(asm_.dispatcher, true);
}

void Jit::WriteSyscallExit()
{
	WriteDowncount();
	if (js.afterOp & JitState::AFTER_MEMCHECK_CLEANUP) {
		RestoreRoundingMode();
		ABI_CallFunction(&JitMemCheckCleanup);
		ApplyRoundingMode();
	}
	JMP(asm_.dispatcherCheckCoreState, true);
}

bool Jit::CheckJitBreakpoint(u32 addr, int downcountOffset)
{
	if (CBreakPoints::IsAddressBreakPoint(addr))
	{
		SAVE_FLAGS;
		FlushAll();
		MOV(32, M(&mips_->pc), Imm32(GetCompilerPC()));
		RestoreRoundingMode();
		ABI_CallFunction(&JitBreakpoint);

		// If 0, the conditional breakpoint wasn't taken.
		CMP(32, R(EAX), Imm32(0));
		FixupBranch skip = J_CC(CC_Z);
		WriteDowncount(downcountOffset);
		ApplyRoundingMode();
		// Just to fix the stack.
		LOAD_FLAGS;
		JMP(asm_.dispatcherCheckCoreState, true);
		SetJumpTarget(skip);

		ApplyRoundingMode();
		LOAD_FLAGS;

		return true;
	}

	return false;
}

void Jit::CallProtectedFunction(const void *func, const OpArg &arg1)
{
	// We don't regcache RCX, so the below is safe (and also faster, maybe branch prediction?)
	ABI_CallFunctionA(thunks.ProtectFunction(func, 1), arg1);
}

void Jit::CallProtectedFunction(const void *func, const OpArg &arg1, const OpArg &arg2)
{
	// We don't regcache RCX/RDX, so the below is safe (and also faster, maybe branch prediction?)
	ABI_CallFunctionAA(thunks.ProtectFunction(func, 2), arg1, arg2);
}

void Jit::CallProtectedFunction(const void *func, const u32 arg1, const u32 arg2, const u32 arg3)
{
	// On x64, we need to save R8, which is caller saved.
	thunks.Enter(this);
	ABI_CallFunctionCCC(func, arg1, arg2, arg3);
	thunks.Leave(this);
}

void Jit::CallProtectedFunction(const void *func, const OpArg &arg1, const u32 arg2, const u32 arg3)
{
	// On x64, we need to save R8, which is caller saved.
	thunks.Enter(this);
	ABI_CallFunctionACC(func, arg1, arg2, arg3);
	thunks.Leave(this);
}

void Jit::Comp_DoNothing(MIPSOpcode op) { }

} // namespace
