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

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Reporting.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/MIPSTracer.h"

#include <iterator>

namespace MIPSComp {

IRFrontend::IRFrontend(bool startDefaultPrefix) {
	js.startDefaultPrefix = startDefaultPrefix;
	js.hasSetRounding = false;

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void IRFrontend::DoState(PointerWrap &p) {
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	Do(p, js.startDefaultPrefix);
	if (s >= 2) {
		Do(p, js.hasSetRounding);
		js.lastSetRounding = 0;
	} else {
		js.hasSetRounding = 1;
	}

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void IRFrontend::FlushAll() {
	FlushPrefixV();
}

void IRFrontend::FlushPrefixV() {
	if (js.startDefaultPrefix && !js.blockWrotePrefixes && js.HasNoPrefix()) {
		// They started default, we never modified in memory, and they're default now.
		// No reason to modify memory.  This is common at end of blocks.  Just clear dirty.
		js.prefixSFlag = (JitState::PrefixState)(js.prefixSFlag & ~JitState::PREFIX_DIRTY);
		js.prefixTFlag = (JitState::PrefixState)(js.prefixTFlag & ~JitState::PREFIX_DIRTY);
		js.prefixDFlag = (JitState::PrefixState)(js.prefixDFlag & ~JitState::PREFIX_DIRTY);
		return;
	}

	if ((js.prefixSFlag & JitState::PREFIX_DIRTY) != 0) {
		ir.Write(IROp::SetCtrlVFPU, VFPU_CTRL_SPREFIX, ir.AddConstant(js.prefixS));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0) {
		ir.Write(IROp::SetCtrlVFPU, VFPU_CTRL_TPREFIX, ir.AddConstant(js.prefixT));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0) {
		ir.Write(IROp::SetCtrlVFPU, VFPU_CTRL_DPREFIX, ir.AddConstant(js.prefixD));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}

	// If we got here, we must've written prefixes to memory in this block.
	js.blockWrotePrefixes = true;
}

void IRFrontend::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, Log::JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, Log::JIT, "Ate an instruction inside a delay slot.");
	}

	CheckBreakpoint(GetCompilerPC() + 4);
	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void IRFrontend::CompileDelaySlot() {
	js.inDelaySlot = true;
	CheckBreakpoint(GetCompilerPC() + 4);
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op, this);
	js.inDelaySlot = false;
}

bool IRFrontend::CheckRounding(u32 blockAddress) {
	bool cleanSlate = false;
	if (js.hasSetRounding && !js.lastSetRounding) {
		WARN_LOG(Log::JIT, "Detected rounding mode usage, rebuilding jit with checks");
		// Won't loop, since hasSetRounding is only ever set to 1.
		js.lastSetRounding = js.hasSetRounding;
		cleanSlate = true;
	}

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG_REPORT(Log::JIT, "An uneaten prefix at end of block for %08x", blockAddress);
		logBlocks = 1;
		js.LogPrefix();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		js.startDefaultPrefix = false;
		cleanSlate = true;
	}
	return cleanSlate;
}

void IRFrontend::Comp_ReplacementFunc(MIPSOpcode op) {
	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(Log::HLE, "Invalid replacement op %08x", op.encoding);
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
	} else if (entry->replaceFunc) {
		FlushAll();
		RestoreRoundingMode();
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));
		ir.Write(IROp::CallReplacement, IRTEMP_0, ir.AddConstant(index));

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
		} else {
			ApplyRoundingMode();
			// If IRTEMP_0 was set to 1, it means the replacement needs to run again (sliced.)
			// This is necessary for replacements that take a lot of cycles.
			ir.Write(IROp::Downcount, 0, ir.AddConstant(js.downcountAmount));
			ir.Write(IROp::ExitToConstIfNeq, ir.AddConstant(GetCompilerPC()), IRTEMP_0, MIPS_REG_ZERO);
			ir.Write(IROp::ExitToReg, 0, MIPS_REG_RA, 0);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(Log::HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void IRFrontend::Comp_Generic(MIPSOpcode op) {
	FlushAll();
	ir.Write(IROp::Interpret, 0, ir.AddConstant(op.encoding));
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

// Destroys SCRATCH2
void IRFrontend::RestoreRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		ir.Write(IROp::RestoreRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void IRFrontend::ApplyRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		ir.Write(IROp::ApplyRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void IRFrontend::UpdateRoundingMode() {
	// We must set js.hasSetRounding at compile time, or this block will use the wrong rounding mode.
	js.hasSetRounding = true;
	ir.Write(IROp::UpdateRoundingMode);
}

void IRFrontend::Comp_DoNothing(MIPSOpcode op) {
}

int IRFrontend::Replace_fabsf() {
	Crash();
	return 0;
}

u32 IRFrontend::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode IRFrontend::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

void IRFrontend::DoJit(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) {
	js.cancel = false;
	js.preloading = preload;
	js.blockStart = em_address;
	js.compilerPC = em_address;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = nullptr;
	js.compiling = true;
	js.hadBreakpoints = false;
	js.blockWrotePrefixes = false;
	js.inDelaySlot = false;
	js.PrefixStart();
	ir.Clear();

	js.numInstructions = 0;
	while (js.compiling) {
		// Jit breakpoints are quite fast, so let's do them in release too.
		CheckBreakpoint(GetCompilerPC());

		MIPSOpcode inst = Memory::Read_Opcode_JIT(GetCompilerPC());
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);
		MIPSCompileOp(inst, this);
		js.compilerPC += 4;
		js.numInstructions++;
	}

	if (js.cancel) {
		// Clear the instructions to signal this was not compiled.
		ir.Clear();
	}

	mipsBytes = js.compilerPC - em_address;

	IRWriter simplified;
	IRWriter *code = &ir;
	if (!js.hadBreakpoints) {
		std::vector<IRPassFunc> passes{
			&ApplyMemoryValidation,
			&RemoveLoadStoreLeftRight,
			&OptimizeFPMoves,
			&PropagateConstants,
			&PurgeTemps,
			&ReduceVec4Flush,
			&OptimizeLoadsAfterStores,
			// &ReorderLoadStore,
			// &MergeLoadStore,
			// &ThreeOpToTwoOp,
		};

		if (opts.optimizeForInterpreter) {
			// Add special passes here.
			passes.push_back(&OptimizeForInterpreter);
		}
		if (IRApplyPasses(passes.data(), passes.size(), ir, simplified, opts))
			logBlocks = 1;
		code = &simplified;
		//if (ir.GetInstructions().size() >= 24)
		//	logBlocks = 1;
	}

	if (!mipsTracer.tracing_enabled) {
		instructions = code->GetInstructions();
	}
	else {
		std::vector<IRInst> block_instructions = code->GetInstructions();
		instructions.reserve(block_instructions.capacity());
		// The first instruction is "Downcount"
		instructions.push_back(block_instructions.front());
		instructions.push_back({ IROp::LogIRBlock, 0, 0, 0, 0 });
		std::copy(block_instructions.begin() + 1, block_instructions.end(), std::back_inserter(instructions));
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		char temp2[256];
		NOTICE_LOG(Log::JIT, "=============== mips %08x ===============", em_address);
		for (u32 cpc = em_address; cpc != GetCompilerPC(); cpc += 4) {
			temp2[0] = 0;
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp2, sizeof(temp2), true);
			NOTICE_LOG(Log::JIT, "M: %08x   %s", cpc, temp2);
		}
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		NOTICE_LOG(Log::JIT, "=============== Original IR (%d instructions) ===============", (int)ir.GetInstructions().size());
		for (size_t i = 0; i < ir.GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), ir.GetInstructions()[i]);
			NOTICE_LOG(Log::JIT, "%s", buf);
		}
		NOTICE_LOG(Log::JIT, "===============        end         =================");
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		NOTICE_LOG(Log::JIT, "=============== IR (%d instructions) ===============", (int)code->GetInstructions().size());
		for (size_t i = 0; i < code->GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), code->GetInstructions()[i]);
			NOTICE_LOG(Log::JIT, "%s", buf);
		}
		NOTICE_LOG(Log::JIT, "===============        end         =================");
	}

	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;
}

void IRFrontend::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(Log::JIT, "Comp_RunBlock should never be reached!");
}

void IRFrontend::CheckBreakpoint(u32 addr) {
	if (CBreakPoints::IsAddressBreakPoint(addr)) {
		FlushAll();

		// Can't skip this even at the start of a block, might impact block linking.
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));

		RestoreRoundingMode();
		// At this point, downcount HAS the delay slot, but not the instruction itself.
		int downcountOffset = 0;
		if (js.inDelaySlot) {
			MIPSOpcode branchOp = Memory::Read_Opcode_JIT(GetCompilerPC());
			MIPSOpcode delayOp = Memory::Read_Opcode_JIT(addr);
			downcountOffset = -MIPSGetInstructionCycleEstimate(delayOp);
			if ((MIPSGetInfo(branchOp) & LIKELY) != 0) {
				// Okay, we're in a likely branch.  Also negate the branch cycles.
				downcountOffset += -MIPSGetInstructionCycleEstimate(branchOp);
			}
		}
		int downcountAmount = js.downcountAmount + downcountOffset;
		if (downcountAmount != 0)
			ir.Write(IROp::Downcount, 0, ir.AddConstant(downcountAmount));
		// Note that this means downcount can't be metadata on the block.
		js.downcountAmount = -downcountOffset;
		ir.Write(IROp::Breakpoint, 0, ir.AddConstant(addr));
		ApplyRoundingMode();

		js.hadBreakpoints = true;
	}
}

void IRFrontend::CheckMemoryBreakpoint(int rs, int offset) {
	if (CBreakPoints::HasMemChecks()) {
		FlushAll();

		// Can't skip this even at the start of a block, might impact block linking.
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));

		RestoreRoundingMode();
		// At this point, downcount HAS the delay slot, but not the instruction itself.
		int downcountOffset = 0;
		if (js.inDelaySlot) {
			// We assume delay slot in compilerPC + 4.
			MIPSOpcode branchOp = Memory::Read_Opcode_JIT(GetCompilerPC());
			MIPSOpcode delayOp = Memory::Read_Opcode_JIT(GetCompilerPC() + 4);
			downcountOffset = -MIPSGetInstructionCycleEstimate(delayOp);
			if ((MIPSGetInfo(branchOp) & LIKELY) != 0) {
				// Okay, we're in a likely branch.  Also negate the branch cycles.
				downcountOffset += -MIPSGetInstructionCycleEstimate(branchOp);
			}
		}
		int downcountAmount = js.downcountAmount + downcountOffset;
		if (downcountAmount != 0)
			ir.Write(IROp::Downcount, 0, ir.AddConstant(downcountAmount));
		// Note that this means downcount can't be metadata on the block.
		js.downcountAmount = -downcountOffset;
		ir.Write(IROp::MemoryCheck, js.inDelaySlot ? 4 : 0, rs, ir.AddConstant(offset));
		ApplyRoundingMode();

		js.hadBreakpoints = true;
	}
}

}  // namespace
