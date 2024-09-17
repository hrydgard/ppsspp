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
#if PPSSPP_ARCH(ARM)

#include "Common/Profiler/Profiler.h"

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"

#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"

#include "ArmRegCache.h"
#include "ArmJit.h"
#include "CPUDetect.h"

#include "ext/disarm.h"

using namespace ArmJitConstants;

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
				snprintf(temp, sizeof(temp), "%08x MOV32 %s, %04x%04x", (u32)inst, ArmRegName(reg0), hi, low);
				INFO_LOG(Log::JIT, "A:   %s", temp);
				i += 4;
				continue;
			}
		}
		ArmDis((u32)codePtr, inst, temp, sizeof(temp), true);
		INFO_LOG(Log::JIT, "A:   %s", temp);
	}
}

static u32 JitBreakpoint(uint32_t addr) {
	// Should we skip this breakpoint?
	if (CBreakPoints::CheckSkipFirst() == currentMIPS->pc || CBreakPoints::CheckSkipFirst() == addr)
		return 0;

	BreakAction result = CBreakPoints::ExecBreakPoint(addr);
	if ((result & BREAK_ACTION_PAUSE) == 0)
		return 0;

	return 1;
}

static u32 JitMemCheck(u32 pc) {
	if (CBreakPoints::CheckSkipFirst() == currentMIPS->pc)
		return 0;

	// Note: pc may be the delay slot.
	const auto op = Memory::Read_Instruction(pc, true);
	s32 offset = SignExtend16ToS32(op & 0xFFFF);
	if (MIPSGetInfo(op) & IS_VFPU)
		offset &= 0xFFFC;
	u32 addr = currentMIPS->r[MIPS_GET_RS(op)] + offset;

	CBreakPoints::ExecOpMemCheck(addr, pc);
	return coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME ? 0 : 1;
}

namespace MIPSComp
{
using namespace ArmGen;
using namespace ArmJitConstants;

ArmJit::ArmJit(MIPSState *mipsState) : blocks(mipsState, this), gpr(mipsState, &js, &jo), fpr(mipsState, &js, &jo), mips_(mipsState) {
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode();

	INFO_LOG(Log::JIT, "ARM JIT initialized: %lld MB of code space", (long long)(GetSpaceLeft() / (1024 * 1024)));

	js.startDefaultPrefix = mips_->HasDefaultPrefix();

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

ArmJit::~ArmJit() {
}

void ArmJit::DoState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	if (p.mode == PointerWrap::MODE_READ && !js.startDefaultPrefix) {
		WARN_LOG(Log::CPU, "Jit: An uneaten prefix was previously detected. Jitting in unknown-prefix mode.");
	}
	Do(p, js.startDefaultPrefix);
	if (s >= 2) {
		Do(p, js.hasSetRounding);
		if (p.mode == PointerWrap::MODE_READ) {
			js.lastSetRounding = 0;
		}
	} else {
		js.hasSetRounding = 1;
	}

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void ArmJit::UpdateFCR31() {
}

void ArmJit::FlushAll()
{
	gpr.FlushAll();
	fpr.FlushAll();
	FlushPrefixV();
}

void ArmJit::FlushPrefixV() {
	if (js.startDefaultPrefix && !js.blockWrotePrefixes && js.HasNoPrefix()) {
		// They started default, we never modified in memory, and they're default now.
		// No reason to modify memory.  This is common at end of blocks.  Just clear dirty.
		js.prefixSFlag = (JitState::PrefixState)(js.prefixSFlag & ~JitState::PREFIX_DIRTY);
		js.prefixTFlag = (JitState::PrefixState)(js.prefixTFlag & ~JitState::PREFIX_DIRTY);
		js.prefixDFlag = (JitState::PrefixState)(js.prefixDFlag & ~JitState::PREFIX_DIRTY);
		return;
	}

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

	// If we got here, we must've written prefixes to memory in this block.
	js.blockWrotePrefixes = true;
}

void ArmJit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace(0);
	GenerateFixedCode();
}

void ArmJit::InvalidateCacheAt(u32 em_address, int length) {
	if (blocks.RangeMayHaveEmuHacks(em_address, em_address + length)) {
		blocks.InvalidateICache(em_address, length);
	}
}

void ArmJit::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, Log::JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, Log::JIT, "Ate an instruction inside a delay slot.");
	}

	CheckJitBreakpoint(GetCompilerPC() + 4, 0);
	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void ArmJit::CompileDelaySlot(int flags) {
	// Need to offset the downcount which was already incremented for the branch + delay slot.
	CheckJitBreakpoint(GetCompilerPC() + 4, -2);

	// preserve flag around the delay slot! Maybe this is not always necessary on ARM where 
	// we can (mostly) control whether we set the flag or not. Of course, if someone puts an slt in to the
	// delay slot, we're screwed.
	if (flags & DELAYSLOT_SAFE)
		MRS(R8);  // Save flags register. R8 is preserved through function calls and is not allocated.

	js.inDelaySlot = true;
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op, this);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		_MSR(true, false, R8);  // Restore flags register
}

void ArmJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");

	// INFO_LOG(Log::JIT, "Compiling at %08x", em_address);

	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		ClearCache();
	}

	BeginWrite(JitBlockCache::MAX_BLOCK_INSTRUCTIONS * 16);

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	_assert_msg_(b->originalAddress == em_address, "original %08x != em_address %08x (block %d)", b->originalAddress, em_address, b->blockNum);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	EndWrite();

	bool cleanSlate = false;

	if (js.hasSetRounding && !js.lastSetRounding) {
		WARN_LOG(Log::JIT, "Detected rounding mode usage, rebuilding jit with checks");
		// Won't loop, since hasSetRounding is only ever set to 1.
		js.lastSetRounding = js.hasSetRounding;
		cleanSlate = true;
	}

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG_REPORT(Log::JIT, "An uneaten prefix at end of block: %08x", GetCompilerPC() - 4);
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

void ArmJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher)();
}

u32 ArmJit::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode ArmJit::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

const u8 *ArmJit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = em_address;
	js.compilerPC = em_address;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.blockWrotePrefixes = false;
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

	js.numInstructions = 0;
	while (js.compiling)
	{
		gpr.SetCompilerPC(GetCompilerPC());  // Let it know for log messages
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(GetCompilerPC(), 0);

		MIPSOpcode inst = Memory::Read_Opcode_JIT(GetCompilerPC());
		//MIPSInfo info = MIPSGetInfo(inst);
		//if (info & IS_VFPU) {
		//	logBlocks = 1;
		//}

		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst, this);
	
		js.compilerPC += 4;
		js.numInstructions++;

		if (jo.Disabled(JitDisable::REGALLOC_GPR)) {
			gpr.FlushAll();
		}
		if (jo.Disabled(JitDisable::REGALLOC_FPR)) {
			fpr.FlushAll();
			FlushPrefixV();
		}

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800 || js.numInstructions >= JitBlockCache::MAX_BLOCK_INSTRUCTIONS)
		{
			FlushAll();
			WriteExit(GetCompilerPC(), js.nextExit++);
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
		INFO_LOG(Log::JIT, "=============== mips ===============");
		for (u32 cpc = em_address; cpc != GetCompilerPC() + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp, sizeof(temp), true);
			INFO_LOG(Log::JIT, "M: %08x   %s", cpc, temp);
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;

	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(Log::JIT, "=============== ARM ===============");
		DisassembleArm(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

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

void ArmJit::AddContinuedBlock(u32 dest)
{
	// The first block is the root block.  When we continue, we create proxy blocks after that.
	if (js.lastContinuedPC == 0)
		js.initialBlockSize = js.numInstructions;
	else
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (GetCompilerPC() - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
	js.lastContinuedPC = dest;
}

bool ArmJit::DescribeCodePtr(const u8 *ptr, std::string &name)
{
	// TODO: Not used by anything yet (except the modified VerySleepy on Windows)
	return false;
}

void ArmJit::Comp_RunBlock(MIPSOpcode op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(Log::JIT, "Comp_RunBlock should never be reached!");
}

void ArmJit::LinkBlock(u8 *exitPoint, const u8 *checkedEntry) {
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(exitPoint, 32, MEM_PROT_READ | MEM_PROT_WRITE);
	}

	ARMXEmitter emit(exitPoint);
	u32 op = *((const u32 *)emit.GetCodePointer());
	bool prelinked = (op & 0xFF000000) == 0xEA000000;
	// Jump directly to the block, yay.
	emit.B(checkedEntry);

	if (!prelinked) {
		do {
			op = *((const u32 *)emit.GetCodePointer());
			// Overwrite whatever is here with a breakpoint.
			emit.BKPT(1);
			// Stop after overwriting the next unconditional branch or BKPT.
			// It can be a BKPT if we unlinked, and are now linking a different one.
		} while ((op & 0xFF000000) != 0xEA000000 && (op & 0xFFF000F0) != 0xE1200070);
	}
	emit.FlushIcache();
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(exitPoint, 32, MEM_PROT_READ | MEM_PROT_EXEC);
	}
}

void ArmJit::UnlinkBlock(u8 *checkedEntry, u32 originalAddress) {
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(checkedEntry, 16, MEM_PROT_READ | MEM_PROT_WRITE);
	}
	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// I hope there's enough space...
	// checkedEntry is the only "linked" entrance so it's enough to overwrite that.
	ARMXEmitter emit(checkedEntry);
	emit.MOVI2R(R0, originalAddress);
	emit.STR(R0, CTXREG, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->GetDispatcher());
	emit.FlushIcache();
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(checkedEntry, 16, MEM_PROT_READ | MEM_PROT_EXEC);
	}
}

bool ArmJit::ReplaceJalTo(u32 dest) {
#if PPSSPP_ARCH(ARM)
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
		SaveDowncount();
		RestoreRoundingMode();

		if (BLInRange((const void *)(entry->replaceFunc))) {
			BL((const void *)(entry->replaceFunc));
		} else {
			MOVI2R(R0, (uintptr_t)entry->replaceFunc);
			BL(R0);
		}
		ApplyRoundingMode();
		RestoreDowncount();

		WriteDownCountR(R0);
	}

	js.compilerPC += 4;
	// No writing exits, keep going!

	if (CBreakPoints::HasMemChecks()) {
		// We could modify coreState, so we need to write PC and check.
		// Otherwise, PC may end up on the jal.  We add 4 to skip the delay slot.
		FlushAll();
		WriteExit(GetCompilerPC() + 4, js.nextExit++);
		js.compiling = false;
	}

	// Add a trigger so that if the inlined code changes, we invalidate this block.
	blocks.ProxyBlock(js.blockStart, dest, funcSize / sizeof(u32), GetCodePtr());
#endif
	return true;
}

void ArmJit::Comp_ReplacementFunc(MIPSOpcode op)
{
	// We get here if we execute the first instruction of a replaced function. This means
	// that we do need to return to RA.

	// Inlined function calls (caught in jal) are handled differently.

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG_REPORT_ONCE(replFunc, Log::HLE, "Invalid replacement op %08x at %08x", op.encoding, js.compilerPC);
		return;
	}

	u32 funcSize = g_symbolMap->GetFunctionSize(GetCompilerPC());
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
		MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
	} else if (entry->jitReplaceFunc) {
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
		} else {
			FlushAll();
			// Flushed, so R1 is safe.
			LDR(R1, CTXREG, MIPS_REG_RA * 4);
			js.downcountAmount += cycles;
			WriteExitDestInR(R1);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		FlushAll();
		SaveDowncount();
		RestoreRoundingMode();
		gpr.SetRegImm(SCRATCHREG1, GetCompilerPC());
		MovToPC(SCRATCHREG1);

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		if (BLInRange((const void *)(entry->replaceFunc))) {
			BL((const void *)(entry->replaceFunc));
		} else {
			MOVI2R(R0, (uintptr_t)entry->replaceFunc);
			BL(R0);
		}

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			RestoreDowncount();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
		} else {
			ApplyRoundingMode();
			RestoreDowncount();

			CMPI2R(R0, 0, SCRATCHREG2);
			FixupBranch positive = B_CC(CC_GE);

			RSB(R0, R0, Operand2(0));
			MovFromPC(R1);
			FixupBranch done = B();

			SetJumpTarget(positive);
			LDR(R1, CTXREG, MIPS_REG_RA * 4);

			SetJumpTarget(done);
			WriteDownCountR(R0);
			WriteExitDestInR(R1);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(Log::HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void ArmJit::Comp_Generic(MIPSOpcode op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		SaveDowncount();
		// TODO: Perhaps keep the rounding mode for interp?
		RestoreRoundingMode();
		gpr.SetRegImm(SCRATCHREG1, GetCompilerPC());
		MovToPC(SCRATCHREG1);
		gpr.SetRegImm(R0, op.encoding);
		QuickCallFunction(R1, (void *)func);
		ApplyRoundingMode();
		RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();

		// Even if DISABLE'd, we want to set this flag so we overwrite.
		if ((info & OUT_VFPU_PREFIX) != 0)
			js.blockWrotePrefixes = true;
	}
}

void ArmJit::MovFromPC(ARMReg r) {
	LDR(r, CTXREG, offsetof(MIPSState, pc));
}

void ArmJit::MovToPC(ARMReg r) {
	STR(r, CTXREG, offsetof(MIPSState, pc));
}

void ArmJit::SaveDowncount() {
	if (jo.downcountInRegister)
		STR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void ArmJit::RestoreDowncount() {
	if (jo.downcountInRegister)
		LDR(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void ArmJit::WriteDownCount(int offset) {
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
void ArmJit::WriteDownCountR(ARMReg reg) {
	if (jo.downcountInRegister) {
		SUBS(DOWNCOUNTREG, DOWNCOUNTREG, reg);
	} else {
		LDR(R2, CTXREG, offsetof(MIPSState, downcount));
		SUBS(R2, R2, reg);
		STR(R2, CTXREG, offsetof(MIPSState, downcount));
	}
}

// Destroys SCRATCHREG2. Does not destroy SCRATCHREG1.
void ArmJit::RestoreRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		QuickCallFunction(R1, restoreRoundingMode);
	}
}

// Does not destroy R0 (SCRATCHREG1). Destroys R14 (SCRATCHREG2).
void ArmJit::ApplyRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		QuickCallFunction(R1, applyRoundingMode);
	}
}

// Does (must!) not destroy R0 (SCRATCHREG1). Destroys R14 (SCRATCHREG2).
void ArmJit::UpdateRoundingMode(u32 fcr31) {
	// We must set js.hasSetRounding at compile time, or this block will use the wrong rounding mode.
	// The fcr31 parameter is -1 when not known at compile time, so we just assume it was changed.
	if (fcr31 & 0x01000003) {
		js.hasSetRounding = true;
	}
}

// IDEA - could have a WriteDualExit that takes two destinations and two condition flags,
// and just have conditional that set PC "twice". This only works when we fall back to dispatcher
// though, as we need to have the SUBS flag set in the end. So with block linking in the mix,
// I don't think this gives us that much benefit.
void ArmJit::WriteExit(u32 destination, int exit_num)
{
	// NOTE: Can't blindly check for bad destination addresses here, sometimes exits with bad destinations are written intentionally (like breaks).
	_assert_msg_(exit_num < MAX_JIT_BLOCK_EXITS, "Expected a valid exit_num. dest=%08x", destination);

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

void ArmJit::WriteExitDestInR(ARMReg Reg) 
{
	// TODO: If not fast memory, check for invalid address in reg and trigger exception.
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void ArmJit::WriteSyscallExit()
{
	WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}

bool ArmJit::CheckJitBreakpoint(u32 addr, int downcountOffset) {
	if (CBreakPoints::IsAddressBreakPoint(addr)) {
		MRS(R8);
		FlushAll();
		MOVI2R(SCRATCHREG1, GetCompilerPC());
		MovToPC(SCRATCHREG1);
		SaveDowncount();
		RestoreRoundingMode();
		MOVI2R(R0, addr);
		QuickCallFunction(SCRATCHREG2, &JitBreakpoint);

		// If 0, the conditional breakpoint wasn't taken.
		CMPI2R(R0, 0, SCRATCHREG2);
		FixupBranch skip = B_CC(CC_EQ);
		WriteDownCount(downcountOffset);
		ApplyRoundingMode();
		RestoreDowncount();
		B((const void *)dispatcherCheckCoreState);
		SetJumpTarget(skip);

		ApplyRoundingMode();
		_MSR(true, false, R8);
		return true;
	}

	return false;
}

bool ArmJit::CheckMemoryBreakpoint(int instructionOffset) {
	if (CBreakPoints::HasMemChecks()) {
		int off = instructionOffset + (js.inDelaySlot ? 1 : 0);

		MRS(R8);
		FlushAll();
		SaveDowncount();
		RestoreRoundingMode();
		MOVI2R(R0, GetCompilerPC());
		MovToPC(R0);
		if (off != 0)
			ADDI2R(R0, R0, off * 4, SCRATCHREG2);
		QuickCallFunction(SCRATCHREG2, &JitMemCheck);

		// If 0, the breakpoint wasn't tripped.
		CMPI2R(R0, 0, SCRATCHREG2);
		FixupBranch skip = B_CC(CC_EQ);
		WriteDownCount(-1 - off);
		ApplyRoundingMode();
		RestoreDowncount();
		B((const void *)dispatcherCheckCoreState);
		SetJumpTarget(skip);

		ApplyRoundingMode();
		_MSR(true, false, R8);
		return true;
	}

	return false;
}

void ArmJit::Comp_DoNothing(MIPSOpcode op) { }

MIPSOpcode ArmJit::GetOriginalOp(MIPSOpcode op) {
	JitBlockCache *bc = GetBlockCache();
	int block_num = bc->GetBlockNumberFromEmuHackOp(op, true);
	if (block_num >= 0) {
		return bc->GetOriginalFirstOp(block_num);
	} else {
		return op;
	}
}

}  // namespace

#endif // PPSSPP_ARCH(ARM)
