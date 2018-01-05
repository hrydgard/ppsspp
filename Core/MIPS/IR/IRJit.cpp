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

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRPassSimplify.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/Reporting.h"

namespace MIPSComp {

IRJit::IRJit(MIPSState *mips) : frontend_(mips->HasDefaultPrefix()), mips_(mips) {
	u32 size = 128 * 1024;
	// blTrampolines_ = kernelMemory.Alloc(size, true, "trampoline");
	InitIR();

	IROptions opts{};
	opts.unalignedLoadStore = true;
	frontend_.SetOptions(opts);
}

IRJit::~IRJit() {
}

void IRJit::DoState(PointerWrap &p) {
	frontend_.DoState(p);
}

void IRJit::ClearCache() {
	ILOG("IRJit: Clearing the cache!");
	blocks_.Clear();
}

void IRJit::InvalidateCacheAt(u32 em_address, int length) {
	blocks_.InvalidateICache(em_address, length);
}

void IRJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");

	int block_num = blocks_.AllocateBlock(em_address);
	if ((block_num & ~MIPS_EMUHACK_VALUE_MASK) != 0) {
		// Ran out of block numbers - need to reset.
		ERROR_LOG(JIT, "Ran out of block numbers, clearing cache");
		ClearCache();
		block_num = blocks_.AllocateBlock(em_address);
	}
	IRBlock *b = blocks_.GetBlock(block_num);

	std::vector<IRInst> instructions;
	u32 mipsBytes;
	frontend_.DoJit(em_address, instructions, mipsBytes);
	b->SetInstructions(instructions);
	b->SetOriginalSize(mipsBytes);
	// Overwrites the first instruction, and also updates stats.
	blocks_.FinalizeBlock(block_num);

	if (frontend_.CheckRounding(em_address)) {
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
				mips_->pc = IRInterpret(mips_, block->GetInstructions(), block->GetNumInstructions());
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
	// Used in target disassembly viewer.
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
	for (int i = 0; i < (int)blocks_.size(); ++i) {
		blocks_[i].Destroy(i);
	}
	blocks_.clear();
	byPage_.clear();
}

void IRBlockCache::InvalidateICache(u32 address, u32 length) {
	u32 startPage = AddressToPage(address);
	u32 endPage = AddressToPage(address + length);

	for (u32 page = startPage; page <= endPage; ++page) {
		const auto iter = byPage_.find(page);
		if (iter == byPage_.end())
			continue;

		const std::vector<int> &blocksInPage = iter->second;
		for (int i : blocksInPage) {
			if (blocks_[i].OverlapsRange(address, length)) {
				// Not removing from the page, hopefully doesn't build up with small recompiles.
				blocks_[i].Destroy(i);
			}
		}
	}
}

void IRBlockCache::FinalizeBlock(int i) {
	blocks_[i].Finalize(i);

	u32 startAddr, size;
	blocks_[i].GetRange(startAddr, size);

	u32 startPage = AddressToPage(startAddr);
	u32 endPage = AddressToPage(startAddr + size);

	for (u32 page = startPage; page <= endPage; ++page) {
		byPage_[page].push_back(i);
	}
}

u32 IRBlockCache::AddressToPage(u32 addr) const {
	// Use relatively small pages since basic blocks are typically small.
	return (addr & 0x3FFFFFFF) >> 10;
}

std::vector<u32> IRBlockCache::SaveAndClearEmuHackOps() {
	std::vector<u32> result;
	result.resize(blocks_.size());

	for (int number = 0; number < (int)blocks_.size(); ++number) {
		IRBlock &b = blocks_[number];
		if (b.IsValid() && b.RestoreOriginalFirstOp(number)) {
			result[number] = number;
		} else {
			result[number] = 0;
		}
	}

	return result;
}

void IRBlockCache::RestoreSavedEmuHackOps(std::vector<u32> saved) {
	if ((int)blocks_.size() != (int)saved.size()) {
		ERROR_LOG(JIT, "RestoreSavedEmuHackOps: Wrong saved block size.");
		return;
	}

	for (int number = 0; number < (int)blocks_.size(); ++number) {
		IRBlock &b = blocks_[number];
		// Only if we restored it, write it back.
		if (b.IsValid() && saved[number] != 0 && b.HasOriginalFirstOp()) {
			b.Finalize(number);
		}
	}
}

JitBlockDebugInfo IRBlockCache::GetBlockDebugInfo(int blockNum) const {
	const IRBlock &ir = blocks_[blockNum];
	JitBlockDebugInfo debugInfo{};
	uint32_t start, size;
	ir.GetRange(start, size);
	debugInfo.originalAddress = start;  // TODO

	for (u32 addr = start; addr < start + size; addr += 4) {
		char temp[256];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, temp, true);
		std::string mipsDis = temp;
		debugInfo.origDisasm.push_back(mipsDis);
	}

	for (int i = 0; i < ir.GetNumInstructions(); i++) {
		IRInst inst = ir.GetInstructions()[i];
		char buffer[256];
		DisassembleIR(buffer, sizeof(buffer), inst);
		debugInfo.irDisasm.push_back(buffer);
	}
	return debugInfo;
}

void IRBlockCache::ComputeStats(BlockCacheStats &bcStats) const {
	double totalBloat = 0.0;
	double maxBloat = 0.0;
	double minBloat = 1000000000.0;
	for (const auto &b : blocks_) {
		double codeSize = (double)b.GetNumInstructions() * sizeof(IRInst);
		if (codeSize == 0)
			continue;

		u32 origAddr, mipsBytes;
		b.GetRange(origAddr, mipsBytes);
		double origSize = (double)mipsBytes;
		double bloat = codeSize / origSize;
		if (bloat < minBloat) {
			minBloat = bloat;
			bcStats.minBloatBlock = origAddr;
		}
		if (bloat > maxBloat) {
			maxBloat = bloat;
			bcStats.maxBloatBlock = origAddr;
		}
		totalBloat += bloat;
		bcStats.bloatMap[bloat] = origAddr;
	}
	bcStats.numBlocks = (int)blocks_.size();
	bcStats.minBloat = minBloat;
	bcStats.maxBloat = maxBloat;
	bcStats.avgBloat = totalBloat / (double)blocks_.size();
}

int IRBlockCache::GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly) const {
	u32 page = AddressToPage(em_address);

	const auto iter = byPage_.find(page);
	if (iter == byPage_.end())
		return -1;

	const std::vector<int> &blocksInPage = iter->second;
	for (int i : blocksInPage) {
		uint32_t start, size;
		blocks_[i].GetRange(start, size);
		if (start == em_address) {
			return i;
		}
	}
	return -1;
}

bool IRBlock::HasOriginalFirstOp() const {
	return Memory::ReadUnchecked_U32(origAddr_) == origFirstOpcode_.encoding;
}

bool IRBlock::RestoreOriginalFirstOp(int number) {
	const u32 emuhack = MIPS_EMUHACK_OPCODE | number;
	if (Memory::ReadUnchecked_U32(origAddr_) == emuhack) {
		Memory::Write_Opcode_JIT(origAddr_, origFirstOpcode_);
		return true;
	}
	return false;
}

void IRBlock::Finalize(int number) {
	origFirstOpcode_ = Memory::Read_Opcode_JIT(origAddr_);
	MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | number);
	Memory::Write_Opcode_JIT(origAddr_, opcode);
}

void IRBlock::Destroy(int number) {
	if (origAddr_) {
		MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | number);
		if (Memory::ReadUnchecked_U32(origAddr_) == opcode.encoding)
			Memory::Write_Opcode_JIT(origAddr_, origFirstOpcode_);

		// Let's mark this invalid so we don't try to clear it again.
		origAddr_ = 0;
	}
}

bool IRBlock::OverlapsRange(u32 addr, u32 size) const {
	addr &= 0x3FFFFFFF;
	u32 origAddr = origAddr_ & 0x3FFFFFFF;
	return addr + size > origAddr && addr < origAddr + origSize_;
}

MIPSOpcode IRJit::GetOriginalOp(MIPSOpcode op) {
	IRBlock *b = blocks_.GetBlock(op.encoding & 0xFFFFFF);
	if (b) {
		return b->GetOriginalFirstOp();
	}
	return op;
}

}  // namespace MIPSComp
