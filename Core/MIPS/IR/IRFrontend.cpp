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

namespace MIPSComp {

IRFrontend::IRFrontend(bool startDefaultPrefix) {
	logBlocks = 0;
	dontLogBlocks = 0;
	js.startDefaultPrefix = true;
	js.hasSetRounding = false;
	// js.currentRoundingFunc = convertS0ToSCRATCH1[0];

	// The debugger sets this so that "go" on a breakpoint will actually... go.
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void IRFrontend::DoState(PointerWrap &p) {
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
	// But if they reset, we can end up hitting it by mistake, since it's based on PC and ticks.
	CBreakPoints::SetSkipFirst(0);
}

void IRFrontend::FlushAll() {
	FlushPrefixV();
}

void IRFrontend::FlushPrefixV() {
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
}

void IRFrontend::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.");
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

bool IRFrontend::CheckRounding() {
	bool cleanSlate = false;
	if (js.hasSetRounding && !js.lastSetRounding) {
		WARN_LOG(JIT, "Detected rounding mode usage, rebuilding jit with checks");
		// Won't loop, since hasSetRounding is only ever set to 1.
		js.lastSetRounding = js.hasSetRounding;
		cleanSlate = true;
	}

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG(JIT, "An uneaten prefix at end of block");
		logBlocks = 1;
		js.LogPrefix();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		js.startDefaultPrefix = false;
		// TODO: Make sure this works.
		// cleanSlate = true;
	}

	return cleanSlate;
}


void IRFrontend::Comp_ReplacementFunc(MIPSOpcode op) {
	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(HLE, "Invalid replacement op %08x", op.encoding);
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
		ir.Write(IROp::CallReplacement, 0, ir.AddConstant(index));

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			MIPSCompileOp(Memory::Read_Instruction(GetCompilerPC(), true), this);
		} else {
			ApplyRoundingMode();
			ir.Write(IROp::Downcount, 0, js.downcountAmount & 0xFF, js.downcountAmount >> 8);
			ir.Write(IROp::ExitToReg, 0, MIPS_REG_RA, 0);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
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

void IRFrontend::DoJit(u32 em_address, std::vector<IRInst> &instructions, std::vector<u32> &constants, u32 &mipsBytes) {
	js.cancel = false;
	js.blockStart = em_address;
	js.compilerPC = em_address;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = nullptr;
	js.compiling = true;
	js.hadBreakpoints = false;
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

		if (ir.GetConstants().size() > 64) {
			// Need to break the block
			ir.Write(IROp::ExitToConst, ir.AddConstant(js.compilerPC));
			js.compiling = false;
		}
	}

	mipsBytes = js.compilerPC - em_address;

	IRWriter simplified;
	IRWriter *code = &ir;
	if (!js.hadBreakpoints) {
		static const IRPassFunc passes[] = {
			&OptimizeFPMoves,
			&PropagateConstants,
			&PurgeTemps,
			// &ReorderLoadStore,
			// &MergeLoadStore,
			// &ThreeOpToTwoOp,
		};
		if (IRApplyPasses(passes, ARRAY_SIZE(passes), ir, simplified))
			logBlocks = 1;
		code = &simplified;
		//if (ir.GetInstructions().size() >= 24)
		//	logBlocks = 1;
	}

	instructions = code->GetInstructions();
	constants = code->GetConstants();

	if (logBlocks > 0 && dontLogBlocks == 0) {
		char temp2[256];
		ILOG("=============== mips %08x ===============", em_address);
		for (u32 cpc = em_address; cpc != GetCompilerPC() + 4; cpc += 4) {
			temp2[0] = 0;
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp2, true);
			ILOG("M: %08x   %s", cpc, temp2);
		}
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		ILOG("=============== Original IR (%d instructions, %d const) ===============", (int)ir.GetInstructions().size(), (int)ir.GetConstants().size());
		for (size_t i = 0; i < ir.GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), ir.GetInstructions()[i], &ir.GetConstants()[0]);
			ILOG("%s", buf);
		}
		ILOG("===============        end         =================");
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		ILOG("=============== IR (%d instructions, %d const) ===============", (int)code->GetInstructions().size(), (int)code->GetConstants().size());
		for (size_t i = 0; i < code->GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), code->GetInstructions()[i], &code->GetConstants()[0]);
			ILOG("%s", buf);
		}
		ILOG("===============        end         =================");
	}

	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;
}

void IRFrontend::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

void IRFrontend::CheckBreakpoint(u32 addr) {
	if (CBreakPoints::IsAddressBreakPoint(addr)) {
		FlushAll();

		RestoreRoundingMode();
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));
		// 0 because we normally execute before increasing.
		// TODO: In likely branches, downcount will be incorrect.
		int downcountOffset = js.inDelaySlot && js.downcountAmount >= 2 ? -2 : 0;
		int downcountAmount = js.downcountAmount + downcountOffset;
		ir.Write(IROp::Downcount, 0, downcountAmount & 0xFF, downcountAmount >> 8);
		// Note that this means downcount can't be metadata on the block.
		js.downcountAmount = -downcountOffset;
		ir.Write(IROp::Breakpoint);
		ApplyRoundingMode();

		js.hadBreakpoints = true;
	}
}

void IRFrontend::CheckMemoryBreakpoint(int rs, int offset) {
	if (CBreakPoints::HasMemChecks()) {
		FlushAll();

		RestoreRoundingMode();
		ir.Write(IROp::SetPCConst, 0, ir.AddConstant(GetCompilerPC()));
		// 0 because we normally execute before increasing.
		int downcountOffset = js.inDelaySlot ? -2 : -1;
		// TODO: In likely branches, downcount will be incorrect.  This might make resume fail.
		if (js.downcountAmount == 0) {
			downcountOffset = 0;
		}
		int downcountAmount = js.downcountAmount + downcountOffset;
		ir.Write(IROp::Downcount, 0, downcountAmount & 0xFF, downcountAmount >> 8);
		// Note that this means downcount can't be metadata on the block.
		js.downcountAmount = -downcountOffset;
		ir.Write(IROp::MemoryCheck, 0, rs, ir.AddConstant(offset));
		ApplyRoundingMode();

		js.hadBreakpoints = true;
	}
}

}  // namespace
