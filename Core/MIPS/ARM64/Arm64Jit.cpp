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
#include "Common/CPUDetect.h"

#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Core/MIPS/ARM64/Arm64RegCacheFPU.h"

#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

using namespace Arm64JitConstants;

void DisassembleArm64Print(const u8 *data, int size) {
	std::vector<std::string> lines = DisassembleArm64(data, size);
	for (auto s : lines) {
		ILOG("%s", s.c_str());
	}
	/*
	ILOG("+++");
	// A format friendly to Online Disassembler which gets endianness wrong
	for (size_t i = 0; i < lines.size(); i++) {
		uint32_t opcode = ((const uint32_t *)data)[i];
		ILOG("%d/%d: %08x", (int)(i+1), (int)lines.size(), swap32(opcode));
	}
	ILOG("===");
	ILOG("===");*/
}

namespace MIPSComp
{
using namespace Arm64Gen;
using namespace Arm64JitConstants;

Arm64Jit::Arm64Jit(MIPSState *mips) : blocks(mips, this), gpr(mips, &js, &jo), fpr(mips, &js, &jo), mips_(mips), fp(this) { 
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	gpr.SetEmitter(this);
	fpr.SetEmitter(this, &fp);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode();

	js.startDefaultPrefix = mips_->HasDefaultPrefix();
}

Arm64Jit::~Arm64Jit() {
}

void Arm64Jit::DoState(PointerWrap &p) {
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
}

// This is here so the savestate matches between jit and non-jit.
void Arm64Jit::DoDummyState(PointerWrap &p) {
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

void Arm64Jit::FlushAll()
{
	gpr.FlushAll();
	fpr.FlushAll();
	FlushPrefixV();
}

void Arm64Jit::FlushPrefixV() {
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
}

void Arm64Jit::ClearCache() {
	ILOG("ARM64Jit: Clearing the cache!");
	blocks.Clear();
	ClearCodeSpace();
	GenerateFixedCode();
}

void Arm64Jit::InvalidateCache() {
	blocks.Clear();
}

void Arm64Jit::InvalidateCacheAt(u32 em_address, int length) {
	blocks.InvalidateICache(em_address, length);
}

void Arm64Jit::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.");
	}

	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void Arm64Jit::CompileDelaySlot(int flags) {
	// preserve flag around the delay slot! Maybe this is not always necessary on ARM where 
	// we can (mostly) control whether we set the flag or not. Of course, if someone puts an slt in to the
	// delay slot, we're screwed.
	if (flags & DELAYSLOT_SAFE)
		MRS(FLAGTEMPREG, FIELD_NZCV);  // Save flags register. FLAGTEMPREG is preserved through function calls and is not allocated.

	js.inDelaySlot = true;
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	if (flags & DELAYSLOT_SAFE)
		_MSR(FIELD_NZCV, FLAGTEMPREG);  // Restore flags register
}


void Arm64Jit::Compile(u32 em_address) {
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		INFO_LOG(JIT, "Space left: %i", GetSpaceLeft());
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
	((void (*)())enterCode)();
}

u32 Arm64Jit::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode Arm64Jit::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

const u8 *Arm64Jit::DoJit(u32 em_address, JitBlock *b) {
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
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

	int partialFlushOffset = 0;

	js.numInstructions = 0;
	while (js.compiling) {
		gpr.SetCompilerPC(GetCompilerPC());  // Let it know for log messages
		MIPSOpcode inst = Memory::Read_Opcode_JIT(GetCompilerPC());
	
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);
	
		js.compilerPC += 4;
		js.numInstructions++;

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
		ILOG("=============== mips ===============");
		for (u32 cpc = em_address; cpc != GetCompilerPC() + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp, true);
			ILOG("M: %08x   %s", cpc, temp);
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;
	if (logBlocks > 0 && dontLogBlocks == 0) {
		ILOG("=============== ARM (%d instructions -> %d bytes) ===============", js.numInstructions, b->codeSize);
		DisassembleArm64Print(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

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
	// TODO: Not used by anything yet.
	return false;
}

void Arm64Jit::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

bool Arm64Jit::ReplaceJalTo(u32 dest) {
#ifdef ARM64
	const ReplacementTableEntry *entry = nullptr;
	u32 funcSize = 0;
	if (!CanReplaceJalTo(dest, &entry, &funcSize)) {
		return false;
	}
	INFO_LOG(HLE, "ReplaceJalTo to %s", entry->name);

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
		RestoreRoundingMode();
		QuickCallFunction(SCRATCH1_64, (const void *)(entry->replaceFunc));
		ApplyRoundingMode();
		WriteDownCountR(W0);
	}

	js.compilerPC += 4;
	// No writing exits, keep going!

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
		ERROR_LOG(HLE, "Invalid replacement op %08x", op.encoding);
		return;
	}

	if (entry->flags & REPFLAG_DISABLED) {
		MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
	} else if (entry->jitReplaceFunc) {
		INFO_LOG(HLE, "JitReplaceFunc to %s", entry->name);
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
		} else {
			FlushAll();
			// Flushed, so R1 is safe.
			LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, MIPS_REG_RA * 4);
			js.downcountAmount += cycles;
			WriteExitDestInR(SCRATCH1);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		INFO_LOG(HLE, "ReplaceFunc to %s", entry->name);
		FlushAll();
		RestoreRoundingMode();
		gpr.SetRegImm(SCRATCH1, GetCompilerPC());
		MovToPC(SCRATCH1);

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		QuickCallFunction(SCRATCH1_64, (const void *)(entry->replaceFunc));

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true));
		} else {
			ApplyRoundingMode();
			LDR(INDEX_UNSIGNED, W1, CTXREG, MIPS_REG_RA * 4);
			WriteDownCountR(W0);
			WriteExitDestInR(W1);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void Arm64Jit::Comp_Generic(MIPSOpcode op) {
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func) {
		SaveDowncount();
		// TODO: Perhaps keep the rounding mode for interp?
		RestoreRoundingMode();
		MOVI2R(SCRATCH1, GetCompilerPC());
		MovToPC(SCRATCH1);
		MOVI2R(W0, op.encoding);
		QuickCallFunction(SCRATCH2_64, (void *)func);
		ApplyRoundingMode();
		RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0) {
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Arm64Jit::MovFromPC(ARM64Reg r) {
	LDR(INDEX_UNSIGNED, r, CTXREG, offsetof(MIPSState, pc));
}

void Arm64Jit::MovToPC(ARM64Reg r) {
	STR(INDEX_UNSIGNED, r, CTXREG, offsetof(MIPSState, pc));
}

// Should not really be necessary except when entering Advance
void Arm64Jit::SaveDowncount() {
	STR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Arm64Jit::RestoreDowncount() {
	LDR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Arm64Jit::WriteDownCount(int offset) {
	int theDowncount = js.downcountAmount + offset;
	SUBSI2R(DOWNCOUNTREG, DOWNCOUNTREG, theDowncount, SCRATCH1);
}

void Arm64Jit::WriteDownCountR(ARM64Reg reg) {
	SUBS(DOWNCOUNTREG, DOWNCOUNTREG, reg);
}

void Arm64Jit::RestoreRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (g_Config.bSetRoundingMode && (force || !g_Config.bForceFlushToZero || js.hasSetRounding)) {
		MRS(SCRATCH2_64, FIELD_FPCR);
		// Assume we're always in round-to-nearest mode beforehand.
		// Also on ARM, we're always in flush-to-zero in C++, so stay that way.
		if (!g_Config.bForceFlushToZero) {
			ORRI2R(SCRATCH2, SCRATCH2, 4 << 22);
		}
		ANDI2R(SCRATCH2, SCRATCH2, ~(3 << 22));
		_MSR(FIELD_FPCR, SCRATCH2_64);
	}
}

void Arm64Jit::ApplyRoundingMode(bool force) {
	// NOTE: Must not destroy SCRATCH1.
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (g_Config.bSetRoundingMode && (force || !g_Config.bForceFlushToZero || js.hasSetRounding)) {
		LDR(INDEX_UNSIGNED, SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));
		if (!g_Config.bForceFlushToZero) {
			TSTI2R(SCRATCH2, 1 << 24);
			ANDI2R(SCRATCH2, SCRATCH2, 3);
			FixupBranch skip1 = B(CC_EQ);
			ADDI2R(SCRATCH2, SCRATCH2, 4);
			SetJumpTarget(skip1);
			// We can only skip if the rounding mode is zero and flush is set.
			CMPI2R(SCRATCH2, 4);
		} else {
			ANDSI2R(SCRATCH2, SCRATCH2, 3);
		}
		// At this point, if it was zero, we can skip the rest.
		FixupBranch skip = B(CC_EQ);
		PUSH(SCRATCH1);

		// MIPS Rounding Mode:       ARM Rounding Mode
		//   0: Round nearest        0
		//   1: Round to zero        3
		//   2: Round up (ceil)      1
		//   3: Round down (floor)   2
		if (!g_Config.bForceFlushToZero) {
			ANDI2R(SCRATCH1, SCRATCH2, 3);
			CMPI2R(SCRATCH1, 1);
		} else {
			CMPI2R(SCRATCH2, 1);
		}

		FixupBranch skipadd = B(CC_NEQ);
		ADDI2R(SCRATCH2, SCRATCH2, 2);
		SetJumpTarget(skipadd);
		FixupBranch skipsub = B(CC_LE);
		SUBI2R(SCRATCH2, SCRATCH2, 1);
		SetJumpTarget(skipsub);

		MRS(SCRATCH1_64, FIELD_FPCR);
		// Assume we're always in round-to-nearest mode beforehand.
		if (!g_Config.bForceFlushToZero) {
			// But we need to clear flush to zero in this case anyway.
			ANDI2R(SCRATCH1, SCRATCH1, ~(7 << 22));
		}
		ORR(SCRATCH1, SCRATCH1, SCRATCH2, ArithOption(SCRATCH2, ST_LSL, 22));
		_MSR(FIELD_FPCR, SCRATCH1_64);

		POP(SCRATCH1);
		SetJumpTarget(skip);
	}
}

void Arm64Jit::UpdateRoundingMode() {
	// NOTE: Must not destroy SCRATCH1.
	if (g_Config.bSetRoundingMode) {
		LDR(INDEX_UNSIGNED, SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));
		if (!g_Config.bForceFlushToZero) {
			TSTI2R(SCRATCH2, 1 << 24);
			ANDI2R(SCRATCH2, SCRATCH2, 3);
			FixupBranch skip = B(CC_EQ);
			ADDI2R(SCRATCH2, SCRATCH2, 4);
			SetJumpTarget(skip);
			// We can only skip if the rounding mode is zero and flush is set.
			CMPI2R(SCRATCH2, 4);
		} else {
			ANDSI2R(SCRATCH2, SCRATCH2, 3);
		}

		FixupBranch skip = B(CC_EQ);
		PUSH(SCRATCH1_64);
		MOVI2R(SCRATCH2, 1);
		MOVP2R(SCRATCH1_64, &js.hasSetRounding);
		STRB(INDEX_UNSIGNED, SCRATCH2, SCRATCH1_64, 0);
		POP(SCRATCH1_64);
		SetJumpTarget(skip);
	}
}

// IDEA - could have a WriteDualExit that takes two destinations and two condition flags,
// and just have conditional that set PC "twice". This only works when we fall back to dispatcher
// though, as we need to have the SUBS flag set in the end. So with block linking in the mix,
// I don't think this gives us that much benefit.
void Arm64Jit::WriteExit(u32 destination, int exit_num) {
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
		MOVI2R(SCRATCH1, destination);
		B((const void *)dispatcherPCInSCRATCH1);	
	}
}

void Arm64Jit::WriteExitDestInR(ARM64Reg Reg) {
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void Arm64Jit::WriteSyscallExit() {
	WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}

void Arm64Jit::Comp_DoNothing(MIPSOpcode op) { }

}
