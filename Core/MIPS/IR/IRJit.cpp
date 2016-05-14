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
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

namespace MIPSComp {

IRJit::IRJit(MIPSState *mips) : mips_(mips), frontend_(mips->HasDefaultPrefix()) {
	u32 size = 128 * 1024;
	// blTrampolines_ = kernelMemory.Alloc(size, true, "trampoline");
	InitIR();
}

IRJit::~IRJit() {
}

void IRJit::DoState(PointerWrap &p) {
	frontend_.DoState(p);
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

void IRJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");

	int block_num = blocks_.AllocateBlock(em_address);
	IRBlock *b = blocks_.GetBlock(block_num);

	std::vector<IRInst> instructions;
	std::vector<u32> constants;
	frontend_.DoJit(em_address, instructions, constants);
	b->SetInstructions(instructions, constants);
	b->Finalize(block_num);  // Overwrites the first instruction

	if (frontend_.CheckRounding()) {
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
			u32 opcode = inst & 0xFF000000;
			if (opcode == MIPS_EMUHACK_OPCODE) {
				u32 data = inst & 0xFFFFFF;
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

bool IRJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in disassembly viewer.
	return false;
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
