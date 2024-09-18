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

#if PPSSPP_ARCH(ARM64)

#include "Common/Profiler/Profiler.h"
#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"

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
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/ARM64/Arm64RegCacheFPU.h"

#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

using namespace Arm64JitConstants;

static void DisassembleArm64Print(const u8 *data, int size) {
	std::vector<std::string> lines = DisassembleArm64(data, size);
	for (auto s : lines) {
		INFO_LOG(Log::JIT, "%s", s.c_str());
	}
	/*
	INFO_LOG(Log::JIT, "+++");
	// A format friendly to Online Disassembler which gets endianness wrong
	for (size_t i = 0; i < lines.size(); i++) {
		uint32_t opcode = ((const uint32_t *)data)[i];
		INFO_LOG(Log::JIT, "%d/%d: %08x", (int)(i+1), (int)lines.size(), swap32(opcode));
	}
	INFO_LOG(Log::JIT, "===");
	INFO_LOG(Log::JIT, "===");*/
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
using namespace Arm64Gen;
using namespace Arm64JitConstants;

Arm64Jit::Arm64Jit(MIPSState *mipsState) : blocks(mipsState, this), gpr(mipsState, &js, &jo), fpr(mipsState, &js, &jo), mips_(mipsState), fp(this) {
	// Automatically disable incompatible options.
	if (((intptr_t)Memory::base & 0x00000000FFFFFFFFUL) != 0) {
		jo.enablePointerify = false;
	}

#ifdef MASKED_PSP_MEMORY
	jo.enablePointerify = false;
#endif

	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this, &fp);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode(jo);
	js.startDefaultPrefix = mips_->HasDefaultPrefix();
	js.currentRoundingFunc = convertS0ToSCRATCH1[mips_->fcr31 & 3];

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

Arm64Jit::~Arm64Jit() {
}

void Arm64Jit::DoState(PointerWrap &p) {
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	Do(p, js.startDefaultPrefix);
	if (p.mode == PointerWrap::MODE_READ && !js.startDefaultPrefix) {
		WARN_LOG(Log::CPU, "Jit: An uneaten prefix was previously detected. Jitting in unknown-prefix mode.");
	}
	if (s >= 2) {
		Do(p, js.hasSetRounding);
		if (p.mode == PointerWrap::MODE_READ) {
			js.lastSetRounding = 0;
		}
	} else {
		js.hasSetRounding = 1;
	}

	// Note: we can't update the currentRoundingFunc here because fcr31 wasn't loaded yet.

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void Arm64Jit::UpdateFCR31() {
	js.currentRoundingFunc = convertS0ToSCRATCH1[mips_->fcr31 & 3];
}

void Arm64Jit::FlushAll() {
	gpr.FlushAll();
	fpr.FlushAll();
	FlushPrefixV();
}

void Arm64Jit::FlushPrefixV() {
	if (js.startDefaultPrefix && !js.blockWrotePrefixes && js.HasNoPrefix()) {
		// They started default, we never modified in memory, and they're default now.
		// No reason to modify memory.  This is common at end of blocks.  Just clear dirty.
		js.prefixSFlag = (JitState::PrefixState)(js.prefixSFlag & ~JitState::PREFIX_DIRTY);
		js.prefixTFlag = (JitState::PrefixState)(js.prefixTFlag & ~JitState::PREFIX_DIRTY);
		js.prefixDFlag = (JitState::PrefixState)(js.prefixDFlag & ~JitState::PREFIX_DIRTY);
		return;
	}

	if ((js.prefixSFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCH1, js.prefixS);
		STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_SPREFIX]));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCH1, js.prefixT);
		STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_TPREFIX]));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0) {
		gpr.SetRegImm(SCRATCH1, js.prefixD);
		STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_DPREFIX]));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}

	// If we got here, we must've written prefixes to memory in this block.
	js.blockWrotePrefixes = true;
}

void Arm64Jit::ClearCache() {
	INFO_LOG(Log::JIT, "ARM64Jit: Clearing the cache!");
	blocks.Clear();
	ClearCodeSpace(jitStartOffset);
	FlushIcacheSection(region + jitStartOffset, region + region_size - jitStartOffset);
}

void Arm64Jit::InvalidateCacheAt(u32 em_address, int length) {
	if (blocks.RangeMayHaveEmuHacks(em_address, em_address + length)) {
		blocks.InvalidateICache(em_address, length);
	}
}

void Arm64Jit::EatInstruction(MIPSOpcode op) {
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

void Arm64Jit::CompileDelaySlot(int flags) {
	// Need to offset the downcount which was already incremented for the branch + delay slot.
	CheckJitBreakpoint(GetCompilerPC() + 4, -2);

	// preserve flag around the delay slot! Maybe this is not always necessary on ARM where 
	// we can (mostly) control whether we set the flag or not. Of course, if someone puts an slt in to the
	// delay slot, we're screwed.
	if (flags & DELAYSLOT_SAFE)
		MRS(FLAGTEMPREG, FIELD_NZCV);  // Save flags register. FLAGTEMPREG is preserved through function calls and is not allocated.

	js.inDelaySlot = true;
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op, this);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		_MSR(FIELD_NZCV, FLAGTEMPREG);  // Restore flags register
}


void Arm64Jit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		INFO_LOG(Log::JIT, "Space left: %d", (int)GetSpaceLeft());
		ClearCache();
	}

	BeginWrite(JitBlockCache::MAX_BLOCK_INSTRUCTIONS * 16);

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	_assert_msg_(b->originalAddress == em_address, "original %08x != em_address %08x (block %d)", b->originalAddress, em_address, b->blockNum);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);
	EndWrite();

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

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
		// TODO ARM64: This crashes.
		//cleanSlate = true;
	}

	if (cleanSlate) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		Compile(em_address);
	}
}

void Arm64Jit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher)();
}

u32 Arm64Jit::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode Arm64Jit::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

const u8 *Arm64Jit::DoJit(u32 em_address, JitBlock *b) {
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
		const u8 *backJump = GetCodePtr();
		MOVI2R(SCRATCH1, js.blockStart);
		B((const void *)outerLoopPCInSCRATCH1);
		b->checkedEntry = GetCodePtr();
		B(CC_LT, backJump);
	} else if (jo.useForwardJump) {
		b->checkedEntry = GetCodePtr();
		bail = B(CC_LT);
	} else if (jo.enableBlocklink) {
		b->checkedEntry = GetCodePtr();
		MOVI2R(SCRATCH1, js.blockStart);
		FixupBranch skip = B(CC_GE);
		B((const void *)outerLoopPCInSCRATCH1);
		SetJumpTarget(skip);
	} else {
		// No block linking, no need to add headers to blocks.
	}

	b->normalEntry = GetCodePtr();
	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(analysis);
	fpr.Start(analysis);

	js.numInstructions = 0;
	while (js.compiling) {
		gpr.SetCompilerPC(GetCompilerPC());  // Let it know for log messages
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckJitBreakpoint(GetCompilerPC(), 0);

		MIPSOpcode inst = Memory::Read_Opcode_JIT(GetCompilerPC());
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
		if (GetSpaceLeft() < 0x800 || js.numInstructions >= JitBlockCache::MAX_BLOCK_INSTRUCTIONS) {
			FlushAll();
			WriteExit(GetCompilerPC(), js.nextExit++);
			js.compiling = false;
		}
	}

	if (jo.useForwardJump) {
		SetJumpTarget(bail);
		gpr.SetRegImm(SCRATCH1, js.blockStart);
		B((const void *)outerLoopPCInSCRATCH1);
	}

	char temp[256];
	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(Log::JIT, "=============== mips %d ===============", blocks.GetNumBlocks());
		for (u32 cpc = em_address; cpc != GetCompilerPC() + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp, sizeof(temp), true);
			INFO_LOG(Log::JIT, "M: %08x   %s", cpc, temp);
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;
	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(Log::JIT, "=============== ARM (%d instructions -> %d bytes) ===============", js.numInstructions, b->codeSize);
		DisassembleArm64Print(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;

	if (js.lastContinuedPC == 0) {
		b->originalSize = js.numInstructions;
	} else {
		// We continued at least once.  Add the last proxy and set the originalSize correctly.
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (GetCompilerPC() - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
		b->originalSize = js.initialBlockSize;
	}

	return b->normalEntry;
}

void Arm64Jit::AddContinuedBlock(u32 dest) {
	// The first block is the root block.  When we continue, we create proxy blocks after that.
	if (js.lastContinuedPC == 0)
		js.initialBlockSize = js.numInstructions;
	else
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (GetCompilerPC() - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
	js.lastContinuedPC = dest;
}

bool Arm64Jit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in disassembly viewer.
	if (ptr == applyRoundingMode)
		name = "applyRoundingMode";
	else if (ptr == updateRoundingMode)
		name = "updateRoundingMode";
	else if (ptr == dispatcher)
		name = "dispatcher";
	else if (ptr == dispatcherPCInSCRATCH1)
		name = "dispatcher (PC in SCRATCH1)";
	else if (ptr == dispatcherNoCheck)
		name = "dispatcherNoCheck";
	else if (ptr == enterDispatcher)
		name = "enterDispatcher";
	else if (ptr == restoreRoundingMode)
		name = "restoreRoundingMode";
	else if (ptr == saveStaticRegisters)
		name = "saveStaticRegisters";
	else if (ptr == loadStaticRegisters)
		name = "loadStaticRegisters";
	else {
		u32 addr = blocks.GetAddressFromBlockPtr(ptr);
		// Returns 0 when it's valid, but unknown.
		if (addr == 0) {
			name = "(unknown or deleted block)";
			return true;
		} else if (addr != (u32)-1) {
			name = "(outside space)";
			return true;
		}

		int number = blocks.GetBlockNumberFromAddress(addr);
		if (number != -1) {
			const JitBlock *block = blocks.GetBlock(number);
			if (block) {
				name = StringFromFormat("(block %d at %08x)", number, block->originalAddress);
				return true;
			}
		}
		return false;
	}
	return true;
}

void Arm64Jit::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(Log::JIT, "Comp_RunBlock should never be reached!");
}

void Arm64Jit::LinkBlock(u8 *exitPoint, const u8 *checkedEntry) {
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(exitPoint, 32, MEM_PROT_READ | MEM_PROT_WRITE);
	}
	ARM64XEmitter emit(GetCodePtrFromWritablePtr(exitPoint), exitPoint);
	emit.B(checkedEntry);
	// TODO: Write stuff after, convering up the now-unused instructions.
	emit.FlushIcache();
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(exitPoint, 32, MEM_PROT_READ | MEM_PROT_EXEC);
	}
}

void Arm64Jit::UnlinkBlock(u8 *checkedEntry, u32 originalAddress) {
	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. works.
	// Spurious entrances from previously linked blocks can only come through checkedEntry
	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(checkedEntry, 16, MEM_PROT_READ | MEM_PROT_WRITE);
	}

	ARM64XEmitter emit(GetCodePtrFromWritablePtr(checkedEntry), checkedEntry);
	emit.MOVI2R(SCRATCH1, originalAddress);
	emit.STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->GetDispatcher());
	emit.FlushIcache();

	if (PlatformIsWXExclusive()) {
		ProtectMemoryPages(checkedEntry, 16, MEM_PROT_READ | MEM_PROT_EXEC);
	}
}

bool Arm64Jit::ReplaceJalTo(u32 dest) {
#if PPSSPP_ARCH(ARM64)
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
		SaveStaticRegisters();
		RestoreRoundingMode();
		QuickCallFunction(SCRATCH1_64, (const void *)(entry->replaceFunc));
		ApplyRoundingMode();
		LoadStaticRegisters();
		WriteDownCountR(W0);  // W0 is the return value from entry->replaceFunc. Neither LoadStaticRegisters nor ApplyRoundingMode can trash it.
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

void Arm64Jit::Comp_ReplacementFunc(MIPSOpcode op)
{
	// We get here if we execute the first instruction of a replaced function. This means
	// that we do need to return to RA.

	// Inlined function calls (caught in jal) are handled differently.

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG_REPORT_ONCE(replFunc, Log::HLE, "Invalid replacement op %08x at %08x", op.encoding, js.compilerPC);
		// TODO: What should we do here? We're way off in the weeds probably.
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
			LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, MIPS_REG_RA * 4);
			js.downcountAmount += cycles;
			WriteExitDestInR(SCRATCH1);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		FlushAll();
		SaveStaticRegisters();
		RestoreRoundingMode();
		gpr.SetRegImm(SCRATCH1, GetCompilerPC());
		MovToPC(SCRATCH1);

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		QuickCallFunction(SCRATCH1_64, (const void *)(entry->replaceFunc));

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			LoadStaticRegisters();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
		} else {
			ApplyRoundingMode();
			LoadStaticRegisters();

			CMPI2R(W0, 0);
			FixupBranch positive = B(CC_GE);

			NEG(W0, W0);
			MovFromPC(W1);
			FixupBranch done = B();

			SetJumpTarget(positive);
			LDR(INDEX_UNSIGNED, W1, CTXREG, MIPS_REG_RA * 4);

			SetJumpTarget(done);
			WriteDownCountR(W0);
			WriteExitDestInR(W1);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(Log::HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void Arm64Jit::Comp_Generic(MIPSOpcode op) {
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func) {
		SaveStaticRegisters();
		// TODO: Perhaps keep the rounding mode for interp? Should probably, right?
		RestoreRoundingMode();
		MOVI2R(SCRATCH1, GetCompilerPC());
		MovToPC(SCRATCH1);
		MOVI2R(W0, op.encoding);
		QuickCallFunction(SCRATCH2_64, (void *)func);
		ApplyRoundingMode();
		LoadStaticRegisters();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0) {
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();

		// Even if DISABLE'd, we want to set this flag so we overwrite.
		if ((info & OUT_VFPU_PREFIX) != 0)
			js.blockWrotePrefixes = true;
	}
}

void Arm64Jit::MovFromPC(ARM64Reg r) {
	LDR(INDEX_UNSIGNED, r, CTXREG, offsetof(MIPSState, pc));
}

void Arm64Jit::MovToPC(ARM64Reg r) {
	STR(INDEX_UNSIGNED, r, CTXREG, offsetof(MIPSState, pc));
}

// Should not really be necessary except when entering Advance
void Arm64Jit::SaveStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(SCRATCH2_64, saveStaticRegisters);
	} else {
		// Inline the single operation
		STR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void Arm64Jit::LoadStaticRegisters() {
	if (jo.useStaticAlloc) {
		QuickCallFunction(SCRATCH2_64, loadStaticRegisters);
	} else {
		LDR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
	}
}

void Arm64Jit::WriteDownCount(int offset, bool updateFlags) {
	int theDowncount = js.downcountAmount + offset;
	if (updateFlags) {
		SUBSI2R(DOWNCOUNTREG, DOWNCOUNTREG, theDowncount, SCRATCH1);
	} else {
		SUBI2R(DOWNCOUNTREG, DOWNCOUNTREG, theDowncount, SCRATCH1);
	}
}

void Arm64Jit::WriteDownCountR(ARM64Reg reg, bool updateFlags) {
	if (updateFlags) {
		SUBS(DOWNCOUNTREG, DOWNCOUNTREG, reg);
	} else {
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, reg);
	}
}

// Destroys SCRATCH2
void Arm64Jit::RestoreRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		QuickCallFunction(SCRATCH2_64, restoreRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void Arm64Jit::ApplyRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		QuickCallFunction(SCRATCH2_64, applyRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void Arm64Jit::UpdateRoundingMode(u32 fcr31) {
	// We must set js.hasSetRounding at compile time, or this block will use the wrong rounding mode.
	// The fcr31 parameter is -1 when not known at compile time, so we just assume it was changed.
	if (fcr31 & 0x01000003) {
		js.hasSetRounding = true;
	}
	QuickCallFunction(SCRATCH2_64, updateRoundingMode);
}

// IDEA - could have a WriteDualExit that takes two destinations and two condition flags,
// and just have conditional that set PC "twice". This only works when we fall back to dispatcher
// though, as we need to have the SUBS flag set in the end. So with block linking in the mix,
// I don't think this gives us that much benefit.
void Arm64Jit::WriteExit(u32 destination, int exit_num) {
	// NOTE: Can't blindly check for bad destination addresses here, sometimes exits with bad destinations are written intentionally (like breaks).
	_assert_msg_(exit_num < MAX_JIT_BLOCK_EXITS, "Expected a valid exit_num. dest=%08x", destination);

	// NOTE: Can't blindly check for bad destination addresses here, sometimes exits with bad destinations are written intentionally (like breaks).
	WriteDownCount();
	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) {
		// The target block exists! Directly link to its checked entrypoint.
		B(blocks.GetBlock(block)->checkedEntry);
		b->linkStatus[exit_num] = true;
	} else {
		MOVI2R(SCRATCH1, destination);
		B((const void *)dispatcherPCInSCRATCH1);	
	}
}

void Arm64Jit::WriteExitDestInR(ARM64Reg Reg) {
	// TODO: If not fast memory, check for invalid address in reg and trigger exception.
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void Arm64Jit::WriteSyscallExit() {
	WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}

bool Arm64Jit::CheckJitBreakpoint(u32 addr, int downcountOffset) {
	if (CBreakPoints::IsAddressBreakPoint(addr)) {
		MRS(FLAGTEMPREG, FIELD_NZCV);
		FlushAll();
		MOVI2R(SCRATCH1, GetCompilerPC());
		MovToPC(SCRATCH1);
		SaveStaticRegisters();
		RestoreRoundingMode();
		MOVI2R(W0, addr);
		QuickCallFunction(SCRATCH1_64, &JitBreakpoint);

		// If 0, the conditional breakpoint wasn't taken.
		CMPI2R(W0, 0);
		FixupBranch skip = B(CC_EQ);
		WriteDownCount(downcountOffset);
		ApplyRoundingMode();
		LoadStaticRegisters();
		B((const void *)dispatcherCheckCoreState);
		SetJumpTarget(skip);

		ApplyRoundingMode();
		LoadStaticRegisters();
		_MSR(FIELD_NZCV, FLAGTEMPREG);
		return true;
	}

	return false;
}

bool Arm64Jit::CheckMemoryBreakpoint(int instructionOffset) {
	if (CBreakPoints::HasMemChecks()) {
		int off = instructionOffset + (js.inDelaySlot ? 1 : 0);

		MRS(FLAGTEMPREG, FIELD_NZCV);
		FlushAll();
		RestoreRoundingMode();
		MOVI2R(W0, GetCompilerPC());
		MovToPC(W0);
		if (off != 0)
			ADDI2R(W0, W0, off * 4);
		QuickCallFunction(SCRATCH2_64, &JitMemCheck);

		// If 0, the breakpoint wasn't tripped.
		CMPI2R(W0, 0);
		FixupBranch skip = B(CC_EQ);
		WriteDownCount(-1 - off);
		ApplyRoundingMode();
		B((const void *)dispatcherCheckCoreState);
		SetJumpTarget(skip);

		ApplyRoundingMode();
		_MSR(FIELD_NZCV, FLAGTEMPREG);
		return true;
	}

	return false;
}

void Arm64Jit::Comp_DoNothing(MIPSOpcode op) { }

MIPSOpcode Arm64Jit::GetOriginalOp(MIPSOpcode op) {
	JitBlockCache *bc = GetBlockCache();
	int block_num = bc->GetBlockNumberFromEmuHackOp(op, true);
	if (block_num >= 0) {
		return bc->GetOriginalFirstOp(block_num);
	} else {
		return op;
	}
}

}  // namespace

#endif // PPSSPP_ARCH(ARM64)
