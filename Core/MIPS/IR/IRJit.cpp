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
#include "profiler/profiler.h"
#include "Common/ChunkFile.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"

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
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

namespace MIPSComp {

IRJit::IRJit(MIPSState *mips) : mips_(mips) { 
	logBlocks = 0;
	dontLogBlocks = 0;
	js.startDefaultPrefix = mips_->HasDefaultPrefix();
	js.currentRoundingFunc = convertS0ToSCRATCH1[0];
	u32 size = 128 * 1024;
	// blTrampolines_ = kernelMemory.Alloc(size, true, "trampoline");
	InitIR();
}

IRJit::~IRJit() {
}

void IRJit::DoState(PointerWrap &p) {
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

	if (p.GetMode() == PointerWrap::MODE_READ) {
		js.currentRoundingFunc = convertS0ToSCRATCH1[(mips_->fcr31) & 3];
	}
}

// This is here so the savestate matches between jit and non-jit.
void IRJit::DoDummyState(PointerWrap &p) {
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

void IRJit::FlushAll() {
	FlushPrefixV();
}

void IRJit::FlushPrefixV() {
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

void IRJit::ClearCache() {
	ILOG("IRJit: Clearing the cache!");
	blocks_.Clear();
}

void IRJit::InvalidateCache() {
	blocks_.Clear();
}

void IRJit::InvalidateCacheAt(u32 em_address, int length) {
	blocks_.InvalidateICache(em_address, length);
}

void IRJit::EatInstruction(MIPSOpcode op) {
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

void IRJit::CompileDelaySlot() {
	js.inDelaySlot = true;
	MIPSOpcode op = GetOffsetInstruction(1);
	MIPSCompileOp(op, this);
	js.inDelaySlot = false;
}

void IRJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");

	int block_num = blocks_.AllocateBlock(em_address);
	IRBlock *b = blocks_.GetBlock(block_num);
	DoJit(em_address, b);
	b->Finalize(block_num);  // Overwrites the first instruction

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

void IRJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");

	// ApplyRoundingMode(true);
	// IR Dispatcher
	
	while (true) {
		// RestoreRoundingMode(true);
		CoreTiming::Advance();
		// ApplyRoundingMode(true);
		if (coreState != 0) {
			break;
		}
		while (mips_->downcount >= 0) {
			u32 inst = Memory::ReadUnchecked_U32(mips_->pc);
			u32 opcode = inst >> 24;
			u32 data = inst & 0xFFFFFF;
			if (opcode == (MIPS_EMUHACK_OPCODE >> 24)) {
				IRBlock *block = blocks_.GetBlock(data);
				mips_->pc = IRInterpret(mips_, block->GetInstructions(), block->GetConstants(), block->GetNumInstructions());
			} else {
				// RestoreRoundingMode(true);
				Compile(mips_->pc);
				// ApplyRoundingMode(true);
			}
		}
	}

	// RestoreRoundingMode(true);
}

u32 IRJit::GetCompilerPC() {
	return js.compilerPC;
}

MIPSOpcode IRJit::GetOffsetInstruction(int offset) {
	return Memory::Read_Instruction(GetCompilerPC() + 4 * offset);
}

void IRJit::DoJit(u32 em_address, IRBlock *b) {
	js.cancel = false;
	js.blockStart = mips_->pc;
	js.compilerPC = mips_->pc;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = nullptr;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();
	ir.Clear();

	js.numInstructions = 0;
	while (js.compiling) {
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

	ir.Simplify();

	IRWriter simplified;

	IRWriter *code = &ir;
	if (true) {
		if (PropagateConstants(ir, simplified))
			logBlocks = 1;
		code = &simplified;
		// Some blocks in tekken generate curious numbers of constants after propagation.
		//if (ir.GetConstants().size() >= 64)
		//	logBlocks = 1;
	}

	b->SetInstructions(code->GetInstructions(), code->GetConstants());

	if (logBlocks > 0 && dontLogBlocks == 0) {
		char temp2[256];
		ILOG("=============== mips %d %08x ===============", blocks_.GetNumBlocks(), em_address);
		for (u32 cpc = em_address; cpc != GetCompilerPC() + 4; cpc += 4) {
			temp2[0] = 0;
			MIPSDisAsm(Memory::Read_Opcode_JIT(cpc), cpc, temp2, true);
			ILOG("M: %08x   %s", cpc, temp2);
		}
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		ILOG("=============== Original IR (%d instructions, %d const) ===============", (int)ir.GetInstructions().size(), (int)ir.GetConstants().size());
		for (int i = 0; i < ir.GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), ir.GetInstructions()[i], ir.GetConstants().data());
			ILOG("%s", buf);
		}
		ILOG("===============        end         =================");
	}

	if (logBlocks > 0 && dontLogBlocks == 0) {
		ILOG("=============== IR (%d instructions, %d const) ===============", (int)code->GetInstructions().size(), (int)code->GetConstants().size());
		for (int i = 0; i < code->GetInstructions().size(); i++) {
			char buf[256];
			DisassembleIR(buf, sizeof(buf), code->GetInstructions()[i], code->GetConstants().data());
			ILOG("%s", buf);
		}
		ILOG("===============        end         =================");
	}

	if (logBlocks > 0)
		logBlocks--;
	if (dontLogBlocks > 0)
		dontLogBlocks--;
}

bool IRJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in disassembly viewer.
	return false;
}

void IRJit::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

void IRJit::LinkBlock(u8 *exitPoint, const u8 *checkedEntry) {
	Crash();
}

void IRJit::UnlinkBlock(u8 *checkedEntry, u32 originalAddress) {
	Crash();
}

bool IRJit::ReplaceJalTo(u32 dest) {
	Crash();
	return false;
}

void IRJit::Comp_ReplacementFunc(MIPSOpcode op) {
	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(HLE, "Invalid replacement op %08x", op.encoding);
		return;
	}

	if (entry->flags & REPFLAG_DISABLED) {
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
			js.compiling = false;
		}
	} else {
		ERROR_LOG(HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
} 

void IRJit::Comp_Generic(MIPSOpcode op) {
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
void IRJit::RestoreRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		ir.Write(IROp::RestoreRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void IRJit::ApplyRoundingMode(bool force) {
	// If the game has never set an interesting rounding mode, we can safely skip this.
	if (force || js.hasSetRounding) {
		ir.Write(IROp::ApplyRoundingMode);
	}
}

// Destroys SCRATCH1 and SCRATCH2
void IRJit::UpdateRoundingMode() {
	ir.Write(IROp::UpdateRoundingMode);
}

void IRJit::Comp_DoNothing(MIPSOpcode op) { 
}

int IRJit::Replace_fabsf() {
	Crash();
	return 0;
}

void IRBlockCache::Clear() {
	blocks_.clear();
}

void IRBlockCache::InvalidateICache(u32 addess, u32 length) {
	// TODO
}

void IRBlock::Finalize(int number) {
	origFirstOpcode_ = Memory::Read_Opcode_JIT(origAddr_);
	MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | number);
	Memory::Write_Opcode_JIT(origAddr_, opcode);
}

MIPSOpcode IRJit::GetOriginalOp(MIPSOpcode op) {
	IRBlock *b = blocks_.GetBlock(op.encoding & 0xFFFFFF);
	return b->GetOriginalFirstOp();
}

}  // namespace MIPSComp