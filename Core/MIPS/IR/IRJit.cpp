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
#include <set>
#include <algorithm>

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
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/Reporting.h"
#include "Common/TimeUtil.h"
#include "Core/MIPS/MIPSTracer.h"


namespace MIPSComp {

IRJit::IRJit(MIPSState *mipsState, bool actualJit) : frontend_(mipsState->HasDefaultPrefix()), mips_(mipsState), blocks_(actualJit) {
	// u32 size = 128 * 1024;
	InitIR();

	compileToNative_ = actualJit;

	// If this IRJit instance will be used to drive a "JIT using IR", don't optimize for interpretation.
	jo.optimizeForInterpreter = !actualJit;

	IROptions opts{};
	opts.disableFlags = g_Config.uJitDisableFlags;
#if PPSSPP_ARCH(RISCV64)
	// Assume RISC-V always has very slow unaligned memory accesses.
	opts.unalignedLoadStore = false;
	opts.unalignedLoadStoreVec4 = true;
	opts.preferVec4 = cpu_info.RiscV_V;
#elif PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)
	opts.unalignedLoadStore = (opts.disableFlags & (uint32_t)JitDisable::LSU_UNALIGNED) == 0;
	opts.unalignedLoadStoreVec4 = true;
	opts.preferVec4 = true;
#else
	opts.unalignedLoadStore = (opts.disableFlags & (uint32_t)JitDisable::LSU_UNALIGNED) == 0;
	// TODO: Could allow on x86 pretty easily...
	opts.unalignedLoadStoreVec4 = false;
	opts.preferVec4 = true;
#endif
	opts.optimizeForInterpreter = jo.optimizeForInterpreter;
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
	INFO_LOG(Log::JIT, "IRJit: Clearing the block cache!");
	blocks_.Clear();
}

void IRJit::InvalidateCacheAt(u32 em_address, int length) {
	std::vector<int> numbers = blocks_.FindInvalidatedBlockNumbers(em_address, length);
	if (numbers.empty()) {
		return;
	}

	DEBUG_LOG(Log::JIT, "Invalidating IR block cache at %08x (%d bytes): %d blocks", em_address, length, (int)numbers.size());

	for (int block_num : numbers) {
		auto block = blocks_.GetBlock(block_num);
		// TODO: We are invalidating a lot of blocks that are already invalid (yu gi oh).
		// INFO_LOG(Log::JIT, "Block at %08x invalidated: valid: %d", block->GetOriginalStart(), block->IsValid());
		// If we're a native JIT (IR->JIT, not just IR interpreter), we write native offsets into the blocks.
		int cookie = compileToNative_ ? block->GetNativeOffset() : block->GetIRArenaOffset();
		blocks_.RemoveBlockFromPageLookup(block_num);
		block->Destroy(cookie);
	}
}

void IRJit::Compile(u32 em_address) {
	_dbg_assert_(compilerEnabled_);

	PROFILE_THIS_SCOPE("jitc");

	if (g_Config.bPreloadFunctions) {
		// Look to see if we've preloaded this block.
		int block_num = blocks_.FindPreloadBlock(em_address);
		if (block_num != -1) {
			IRBlock *block = blocks_.GetBlock(block_num);
			// Okay, let's link and finalize the block now.
			int cookie = compileToNative_ ? block->GetNativeOffset() : block->GetIRArenaOffset();
			block->Finalize(cookie);
			if (block->IsValid()) {
				// Success, we're done.
				FinalizeNativeBlock(&blocks_, block_num);
				return;
			}
		}
	}

	std::vector<IRInst> instructions;
	u32 mipsBytes;
	if (!CompileBlock(em_address, instructions, mipsBytes, false)) {
		// Ran out of block numbers - need to reset.
		ERROR_LOG(Log::JIT, "Ran out of block numbers, clearing cache");
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes, false);
	}

	if (frontend_.CheckRounding(em_address)) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes, false);
	}
}

// WARNING! This can be called from IRInterpret / the JIT, through the function preload stuff!
bool IRJit::CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) {
	_dbg_assert_(compilerEnabled_);

	frontend_.DoJit(em_address, instructions, mipsBytes, preload);
	if (instructions.empty()) {
		_dbg_assert_(preload);
		// We return true when preloading so it doesn't abort.
		return preload;
	}

	int block_num = blocks_.AllocateBlock(em_address, mipsBytes, instructions);
	if ((block_num & ~MIPS_EMUHACK_VALUE_MASK) != 0) {
		WARN_LOG(Log::JIT, "Failed to allocate block for %08x (%d instructions)", em_address, (int)instructions.size());
		// Out of block numbers.  Caller will handle.
		return false;
	}

	IRBlock *b = blocks_.GetBlock(block_num);
	if (preload || mipsTracer.tracing_enabled) {
		// Hash, then only update page stats, don't link yet.
		// TODO: Should we always hash?  Then we can reuse blocks.
		b->UpdateHash();
	}

	if (!CompileNativeBlock(&blocks_, block_num, preload))
		return false;

	if (mipsTracer.tracing_enabled) {
		mipsTracer.prepare_block(b, blocks_);
	}

	// Updates stats, also patches the first MIPS instruction into an emuhack if 'preload == false'
	blocks_.FinalizeBlock(block_num, preload);
	if (!preload)
		FinalizeNativeBlock(&blocks_, block_num);
	return true;
}

void IRJit::CompileFunction(u32 start_address, u32 length) {
	_dbg_assert_(compilerEnabled_);

	PROFILE_THIS_SCOPE("jitc");

	// Note: we don't actually write emuhacks yet, so we can validate hashes.
	// This way, if the game changes the code afterward, we'll catch even without icache invalidation.

	// We may go up and down from branches, so track all block starts done here.
	std::set<u32> doneAddresses;
	std::vector<u32> pendingAddresses;
	pendingAddresses.reserve(16);
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
			ERROR_LOG(Log::JIT, "Ran out of block numbers while compiling function");
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

		MIPSState *mips = mips_;
#ifdef _DEBUG
		compilerEnabled_ = false;
#endif
		while (mips->downcount >= 0) {
			u32 inst = Memory::ReadUnchecked_U32(mips->pc);
			u32 opcode = inst & 0xFF000000;
			if (opcode == MIPS_EMUHACK_OPCODE) {
				u32 offset = inst & 0x00FFFFFF; // Alternatively, inst - opcode
				const IRInst *instPtr = blocks_.GetArenaPtr() + offset;
				// First op is always, except when using breakpoints, downcount, to save one dispatch inside IRInterpret.
				// This branch is very cpu-branch-predictor-friendly so this still beats the dispatch.
				if (instPtr->op == IROp::Downcount) {
					mips->downcount -= instPtr->constant;
					instPtr++;
				}
#ifdef IR_PROFILING
				IRBlock *block = blocks_.GetBlock(blocks_.GetBlockNumFromOffset(offset));
				Instant start = Instant::Now();
				mips->pc = IRInterpret(mips, instPtr);
				int64_t elapsedNanos = start.ElapsedNanos();
				block->profileStats_.executions += 1;
				block->profileStats_.totalNanos += elapsedNanos;
#else
				mips->pc = IRInterpret(mips, instPtr);
#endif
				// Note: this will "jump to zero" on a badly constructed block missing exits.
				if (!Memory::IsValid4AlignedAddress(mips->pc)) {
					int blockNum = blocks_.GetBlockNumFromIRArenaOffset(offset);
					IRBlock *block = blocks_.GetBlockUnchecked(blockNum);
					Core_ExecException(mips->pc, block->GetOriginalStart(), ExecExceptionType::JUMP);
					break;
				}
			} else {
				// RestoreRoundingMode(true);
#ifdef _DEBUG
				compilerEnabled_ = true;
#endif
				Compile(mips->pc);
#ifdef _DEBUG
				compilerEnabled_ = false;
#endif
				// ApplyRoundingMode(true);
			}
		}
#ifdef _DEBUG
		compilerEnabled_ = true;
#endif
	}

	// RestoreRoundingMode(true);
}

bool IRJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// Used in native disassembly viewer.
	return false;
}

void IRJit::LinkBlock(u8 *exitPoint, const u8 *checkedEntry) {
	Crash();
}

void IRJit::UnlinkBlock(u8 *checkedEntry, u32 originalAddress) {
	Crash();
}

void IRBlockCache::Clear() {
	for (int i = 0; i < (int)blocks_.size(); ++i) {
		int cookie = compileToNative_ ? blocks_[i].GetNativeOffset() : blocks_[i].GetIRArenaOffset();
		blocks_[i].Destroy(cookie);
	}
	blocks_.clear();
	byPage_.clear();
	arena_.clear();
	arena_.shrink_to_fit();
}

IRBlockCache::IRBlockCache(bool compileToNative) : compileToNative_(compileToNative) {}

int IRBlockCache::AllocateBlock(int emAddr, u32 origSize, const std::vector<IRInst> &insts) {
	// We have 24 bits to represent offsets with.
	const u32 MAX_ARENA_SIZE = 0x1000000 - 1;
	int offset = (int)arena_.size();
	if (offset >= MAX_ARENA_SIZE) {
		WARN_LOG(Log::JIT, "Filled JIT arena, restarting");
		return -1;
	}
	// TODO: Use memcpy.
	for (int i = 0; i < insts.size(); i++) {
		arena_.push_back(insts[i]);
	}
	int newBlockIndex = (int)blocks_.size();
	blocks_.push_back(IRBlock(emAddr, origSize, offset, (u32)insts.size()));
	return newBlockIndex;
}

int IRBlockCache::GetBlockNumFromIRArenaOffset(int offset) const {
	// Block offsets are always in rising order (we don't go back and replace them when invalidated). So we can binary search.
	int low = 0;
	int high = (int)blocks_.size() - 1;
	int found = -1;
	while (low <= high) {
		int mid = low + (high - low) / 2;
		const int blockOffset = blocks_[mid].GetIRArenaOffset();
		if (blockOffset == offset) {
			found = mid;
			break;
		}
		if (blockOffset < offset) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

#ifndef _DEBUG
	// Then, in debug builds, cross check the result.
	return found;
#else
	// TODO: Optimize if we need to call this often.
	for (int i = 0; i < (int)blocks_.size(); i++) {
		if (blocks_[i].GetIRArenaOffset() == offset) {
			_dbg_assert_(i == found);
			return i;
		}
	}
#endif
	_dbg_assert_(found == -1);
	return -1;
}

std::vector<int> IRBlockCache::FindInvalidatedBlockNumbers(u32 address, u32 lengthInBytes) {
	u32 startPage = AddressToPage(address);
	u32 endPage = AddressToPage(address + lengthInBytes);

	std::vector<int> found;
	for (u32 page = startPage; page <= endPage; ++page) {
		const auto iter = byPage_.find(page);
		if (iter == byPage_.end())
			continue;

		const std::vector<int> &blocksInPage = iter->second;
		for (int i : blocksInPage) {
			if (blocks_[i].OverlapsRange(address, lengthInBytes)) {
				// We now try to remove these during invalidation.
				found.push_back(i);
			}
		}
	}

	return found;
}

void IRBlockCache::FinalizeBlock(int blockIndex, bool preload) {
	// TODO: What's different about preload blocks?
	IRBlock &block = blocks_[blockIndex];
	if (!preload) {
		int cookie = compileToNative_ ? block.GetNativeOffset() : block.GetIRArenaOffset();
		block.Finalize(cookie);
	}

	u32 startAddr, size;
	block.GetRange(&startAddr, &size);

	u32 startPage = AddressToPage(startAddr);
	u32 endPage = AddressToPage(startAddr + size);

	for (u32 page = startPage; page <= endPage; ++page) {
		byPage_[page].push_back(blockIndex);
	}
}

// Call after Destroy-ing it.
void IRBlockCache::RemoveBlockFromPageLookup(int blockIndex) {
	// We need to remove the block from the byPage lookup.
	IRBlock &block = blocks_[blockIndex];

	u32 startAddr, size;
	block.GetRange(&startAddr, &size);

	u32 startPage = AddressToPage(startAddr);
	u32 endPage = AddressToPage(startAddr + size);

	for (u32 page = startPage; page <= endPage; ++page) {
		auto iter = std::find(byPage_[page].begin(), byPage_[page].end(), blockIndex);
		if (iter != byPage_[page].end()) {
			byPage_[page].erase(iter);
		} else if (block.IsValid()) {
			// If it was previously invalidated, we don't care, hence the above check.
			WARN_LOG(Log::JIT, "RemoveBlock: Block at %08x was not found where expected in byPage table.", startAddr);
		}
	}

	// Additionally, we'd like to zap the block in the IR arena.
	// However, this breaks if calling sceKernelIcacheClearAll(), since as soon as we return, we'll be executing garbage.
	/*
	IRInst bad{ IROp::Bad };
	for (int off = block.GetIRArenaOffset(); off < (int)(block.GetIRArenaOffset() + block.GetNumIRInstructions()); off++) {
		arena_[off] = bad;
	}
	*/
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
		if (blocks_[i].GetOriginalStart() == em_address) {
			if (blocks_[i].HashMatches()) {
				return i;
			}
		}
	}

	return -1;
}

int IRBlockCache::FindByCookie(int cookie) {
	if (blocks_.empty())
		return -1;

	// TODO: Maybe a flag to determine native offset mode?
	if (!compileToNative_) {
		return GetBlockNumFromIRArenaOffset(cookie);
	}

	// TODO: This could also use a binary search.
	for (int i = 0; i < GetNumBlocks(); ++i) {
		int offset = blocks_[i].GetNativeOffset();
		if (offset == cookie)
			return i;
	}
	return -1;
}

std::vector<u32> IRBlockCache::SaveAndClearEmuHackOps() {
	std::vector<u32> result;
	result.resize(blocks_.size());

	for (int number = 0; number < (int)blocks_.size(); ++number) {
		IRBlock &b = blocks_[number];
		int cookie = compileToNative_ ? b.GetNativeOffset() : b.GetIRArenaOffset();
		if (b.IsValid() && b.RestoreOriginalFirstOp(cookie)) {
			result[number] = number;
		} else {
			result[number] = 0;
		}
	}

	return result;
}

void IRBlockCache::RestoreSavedEmuHackOps(const std::vector<u32> &saved) {
	if ((int)blocks_.size() != (int)saved.size()) {
		ERROR_LOG(Log::JIT, "RestoreSavedEmuHackOps: Wrong saved block size.");
		return;
	}

	for (int number = 0; number < (int)blocks_.size(); ++number) {
		IRBlock &b = blocks_[number];
		// Only if we restored it, write it back.
		if (b.IsValid() && saved[number] != 0 && b.HasOriginalFirstOp()) {
			int cookie = compileToNative_ ? b.GetNativeOffset() : b.GetIRArenaOffset();
			b.Finalize(cookie);
		}
	}
}

JitBlockDebugInfo IRBlockCache::GetBlockDebugInfo(int blockNum) const {
	const IRBlock &ir = blocks_[blockNum];
	JitBlockDebugInfo debugInfo{};
	uint32_t start, size;
	ir.GetRange(&start, &size);
	debugInfo.originalAddress = start;  // TODO

	debugInfo.origDisasm.reserve(((start + size) - start) / 4);
	for (u32 addr = start; addr < start + size; addr += 4) {
		char temp[256];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, temp, sizeof(temp), true);
		std::string mipsDis = temp;
		debugInfo.origDisasm.push_back(mipsDis);
	}

	debugInfo.irDisasm.reserve(ir.GetNumIRInstructions());
	const IRInst *instructions = GetBlockInstructionPtr(ir);
	for (int i = 0; i < ir.GetNumIRInstructions(); i++) {
		IRInst inst = instructions[i];
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
		double codeSize = (double)b.GetNumIRInstructions() * 4;  // We count bloat in instructions, not bytes. sizeof(IRInst);
		if (codeSize == 0)
			continue;
		u32 origAddr, mipsBytes;
		b.GetRange(&origAddr, &mipsBytes);
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
		if (blocks_[i].GetOriginalStart() == em_address) {
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

bool IRBlock::RestoreOriginalFirstOp(int cookie) {
	const u32 emuhack = MIPS_EMUHACK_OPCODE | cookie;
	if (Memory::ReadUnchecked_U32(origAddr_) == emuhack) {
		Memory::Write_Opcode_JIT(origAddr_, origFirstOpcode_);
		return true;
	}
	return false;
}

void IRBlock::Finalize(int cookie) {
	// Check it wasn't invalidated, in case this is after preload.
	// TODO: Allow reusing blocks when the code matches hash_ again, instead.
	if (origAddr_) {
		origFirstOpcode_ = Memory::Read_Opcode_JIT(origAddr_);
		MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | cookie);
		Memory::Write_Opcode_JIT(origAddr_, opcode);
	} else {
		WARN_LOG(Log::JIT, "Finalizing invalid block (cookie: %d)", cookie);
	}
}

void IRBlock::Destroy(int cookie) {
	if (origAddr_) {
		MIPSOpcode opcode = MIPSOpcode(MIPS_EMUHACK_OPCODE | cookie);
		u32 memOp = Memory::ReadUnchecked_U32(origAddr_);
		if (memOp == opcode.encoding) {
			Memory::Write_Opcode_JIT(origAddr_, origFirstOpcode_);
		} else {
			// NOTE: This is not an error. Just interesting to log.
			DEBUG_LOG(Log::JIT, "IRBlock::Destroy: Note: Block at %08x was overwritten - checked for %08x, got %08x when restoring the MIPS op to %08x", origAddr_, opcode.encoding, memOp, origFirstOpcode_.encoding);
		}
		// TODO: Also wipe the block in the IR opcode arena.
		// Let's mark this invalid so we don't try to clear it again.
		origAddr_ = 0;
	}
}

u64 IRBlock::CalculateHash() const {
	if (origAddr_) {
		// This is unfortunate. In case there are emuhacks, we have to make a copy.
		// If we could hash while reading we could avoid this.
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
	IRBlock *b = blocks_.GetBlock(blocks_.FindByCookie(op.encoding & 0xFFFFFF));
	if (b) {
		return b->GetOriginalFirstOp();
	}
	return op;
}

}  // namespace MIPSComp
