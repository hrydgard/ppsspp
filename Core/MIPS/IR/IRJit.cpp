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

#include <set>

#include "ext/xxhash.h"
#include "Common/Profiler/Profiler.h"

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
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

IRJit::IRJit(MIPSState *mipsState) : frontend_(mipsState->HasDefaultPrefix()), mips_(mipsState) {
	// u32 size = 128 * 1024;
	// blTrampolines_ = kernelMemory.Alloc(size, true, "trampoline");
	InitIR();

	IROptions opts{};
	opts.disableFlags = g_Config.uJitDisableFlags;
	opts.unalignedLoadStore = (opts.disableFlags & (uint32_t)JitDisable::LSU_UNALIGNED) == 0;
	frontend_.SetOptions(opts);
}

IRJit::~IRJit() {
}

void IRJit::DoState(PointerWrap &p) {
	frontend_.DoState(p);
}

void IRJit::UpdateFCR31() {
}

void IRJit::ClearCache() {
	INFO_LOG(JIT, "IRJit: Clearing the cache!");
	blocks_.Clear();
}

void IRJit::InvalidateCacheAt(u32 em_address, int length) {
	blocks_.InvalidateICache(em_address, length);
}

void IRJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");

	if (g_Config.bPreloadFunctions) {
		// Look to see if we've preloaded this block.
		int block_num = blocks_.FindPreloadBlock(em_address);
		if (block_num != -1) {
			IRBlock *b = blocks_.GetBlock(block_num);
			// Okay, let's link and finalize the block now.
			b->Finalize(block_num);
			if (b->IsValid()) {
				// Success, we're done.
				return;
			}
		}
	}

	std::vector<IRInst> instructions;
	u32 mipsBytes;
	if (!CompileBlock(em_address, instructions, mipsBytes, false)) {
		// Ran out of block numbers - need to reset.
		ERROR_LOG(JIT, "Ran out of block numbers, clearing cache");
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes, false);
	}

	if (frontend_.CheckRounding(em_address)) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes, false);
	}
}

bool IRJit::CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) {
	frontend_.DoJit(em_address, instructions, mipsBytes, preload);
	if (instructions.empty()) {
		_dbg_assert_(preload);
		// We return true when preloading so it doesn't abort.
		return preload;
	}

	int block_num = blocks_.AllocateBlock(em_address);
	if ((block_num & ~MIPS_EMUHACK_VALUE_MASK) != 0) {
		// Out of block numbers.  Caller will handle.
		return false;
	}

	IRBlock *b = blocks_.GetBlock(block_num);
	b->SetInstructions(instructions);
	b->SetOriginalSize(mipsBytes);
	if (preload) {
		// Hash, then only update page stats, don't link yet.
		b->UpdateHash();
		blocks_.FinalizeBlock(block_num, true);
	} else {
		// Overwrites the first instruction, and also updates stats.
		// TODO: Should we always hash?  Then we can reuse blocks.
		blocks_.FinalizeBlock(block_num);
	}

	return true;
}

void IRJit::CompileFunction(u32 start_address, u32 length) {
	PROFILE_THIS_SCOPE("jitc");

	// Note: we don't actually write emuhacks yet, so we can validate hashes.
	// This way, if the game changes the code afterward, we'll catch even without icache invalidation.

	// We may go up and down from branches, so track all block starts done here.
	std::set<u32> doneAddresses;
	std::vector<u32> pendingAddresses;
	pendingAddresses.push_back(start_address);
	while (!pendingAddresses.empty()) {
		u32 em_address = pendingAddresses.back();
		pendingAddresses.pop_back();

		// To be safe, also check if a real block is there.  This can be a runtime module load.
		u32 inst = Memory::ReadUnchecked_U32(em_address);
		if (MIPS_IS_RUNBLOCK(inst) || doneAddresses.find(em_address) != doneAddresses.end()) {
			// Already compiled this address.
			continue;
		}

		std::vector<IRInst> instructions;
		u32 mipsBytes;
		if (!CompileBlock(em_address, instructions, mipsBytes, true)) {
			// Ran out of block numbers - let's hope there's no more code it needs to run.
			// Will flush when actually compiling.
			ERROR_LOG(JIT, "Ran out of block numbers while compiling function");
			return;
		}

		doneAddresses.insert(em_address);

		for (const IRInst &inst : instructions) {
			u32 exit = 0;

			switch (inst.op) {
			case IROp::ExitToConst:
			case IROp::ExitToConstIfEq:
			case IROp::ExitToConstIfNeq:
			case IROp::ExitToConstIfGtZ:
			case IROp::ExitToConstIfGeZ:
			case IROp::ExitToConstIfLtZ:
			case IROp::ExitToConstIfLeZ:
			case IROp::ExitToConstIfFpTrue:
			case IROp::ExitToConstIfFpFalse:
				exit = inst.constant;
				break;

			case IROp::ExitToPC:
			case IROp::Break:
				// Don't add any, we'll do block end anyway (for jal, etc.)
				exit = 0;
				break;

			default:
				exit = 0;
				break;
			}

			// Only follow jumps internal to the function.
			if (exit != 0 && exit >= start_address && exit < start_address + length) {
				// Even if it's a duplicate, we check at loop start.
				pendingAddresses.push_back(exit);
			}
		}

		// Also include after the block for jal returns.
		if (em_address + mipsBytes < start_address + length) {
			pendingAddresses.push_back(em_address + mipsBytes);
		}
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
				if (!Memory::IsValidAddress(mips_->pc)) {
					Core_ExecException(mips_->pc, mips_->pc, ExecExceptionType::JUMP);
					break;
				}
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

void IRBlockCache::FinalizeBlock(int i, bool preload) {
	if (!preload) {
		blocks_[i].Finalize(i);
	}

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

int IRBlockCache::FindPreloadBlock(u32 em_address) {
	u32 page = AddressToPage(em_address);
	auto iter = byPage_.find(page);
	if (iter == byPage_.end())
		return -1;

	const std::vector<int> &blocksInPage = iter->second;
	for (int i : blocksInPage) {
		u32 start, mipsBytes;
		blocks_[i].GetRange(start, mipsBytes);

		if (start == em_address) {
			if (blocks_[i].HashMatches()) {
				return i;
			}
		}
	}

	return -1;
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
	int best = -1;
	for (int i : blocksInPage) {
		uint32_t start, size;
		blocks_[i].GetRange(start, size);
		if (start == em_address) {
			best = i;
			if (blocks_[i].IsValid()) {
				return i;
			}
		}
	}
	return best;
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
	// Check it wasn't invalidated, in case this is after preload.
	// TODO: Allow reusing blocks when the code matches hash_ again, instead.
	if (origAddr_) {
		origFirstOpcode_ = Memory::Read_Opcode_JIT(origAddr_);
		MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | number);
		Memory::Write_Opcode_JIT(origAddr_, opcode);
	}
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

u64 IRBlock::CalculateHash() const {
	if (origAddr_) {
		// This is unfortunate.  In case of emuhacks, we have to make a copy.
		std::vector<u32> buffer;
		buffer.resize(origSize_ / 4);
		size_t pos = 0;
		for (u32 off = 0; off < origSize_; off += 4) {
			// Let's actually hash the replacement, if any.
			MIPSOpcode instr = Memory::ReadUnchecked_Instruction(origAddr_ + off, false);
			buffer[pos++] = instr.encoding;
		}

		return XXH3_64bits(&buffer[0], origSize_);
	}

	return 0;
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
