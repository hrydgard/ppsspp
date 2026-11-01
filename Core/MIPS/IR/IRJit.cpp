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
#include "Common/File/FileUtil.h"

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/Interpreter.h"
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

IRJit::IRJit(MIPSState *mipsState, bool compileToNative) : frontend_(mipsState->HasDefaultPrefix()), mips_(mipsState) {
	// u32 size = 128 * 1024;
	InitIR();

	// If this IRJit instance will be used to drive a "JIT using IR", don't optimize for interpretation.
	jo.optimizeForInterpreter = !compileToNative;

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
	// The IR interpreter, and the backends with a routine for it.
#if PPSSPP_ARCH(ARM64)
	opts.vfpuMultiDot = true;
	opts.moreVec4Ops = true;
#elif PPSSPP_ARCH(RISCV64) || PPSSPP_ARCH(LOONGARCH64)
	opts.vfpuMultiDot = !compileToNative;
	opts.moreVec4Ops = true;
#else
	opts.vfpuMultiDot = !compileToNative;
	opts.moreVec4Ops = !compileToNative;
#endif
	opts.continueBranches = IR_CONTINUE_BRANCHES;
	frontend_.SetOptions(opts);

	profiling_ = !g_Config.sJitProfileFile.empty();
	blocks_.SetAddressSpace(mipsState->space);
	frontend_.SetAddressSpace(mipsState->space);
}

IRJit::~IRJit() {
}

IRInterpreterJit::IRInterpreterJit(MIPSState *mipsState) : IRJit(mipsState, false) {}

void IRJit::DoState(PointerWrap &p) {
	if (frontend_.DoState(p)) {
		ClearCache();
	}
}

void IRJit::UpdateFCR31() {
	if (frontend_.UpdateFCR31(mips_->fcr31)) {
		mips_->ClearJitCacheDeferred();
	}
}

void IRJit::ClearCache() {
	INFO_LOG(Log::JIT, "IRJit: Clearing the block cache!");
	if (profiling_)
		WriteProfileReport();
	blocks_.Clear();
}

void IRInterpreterJit::ClearCache() {
	IRJit::ClearCache();
	arena_.clear();
	arena_.shrink_to_fit();
}

void IRJit::FlushProfile() {
	if (profiling_)
		WriteProfileReport();
}

void IRInterpreterJit::WriteProfileReport() {
	int64_t totalNanos = 0;
	std::vector<int> hot;
	for (int i = 0; i < blocks_.GetNumBlocks(); ++i) {
		const JitBlock *b = blocks_.GetBlock(i);
		if (b->profileStats_.executions == 0)
			continue;
		totalNanos += b->profileStats_.totalNanos;
		hot.push_back(i);
	}
	// Skip the near-empty profiles of a cache cleared while booting or loading a state.
	if (totalNanos < 100'000'000)
		return;
	std::sort(hot.begin(), hot.end(), [&](int a, int b) {
		return blocks_.GetBlock(a)->profileStats_.totalNanos > blocks_.GetBlock(b)->profileStats_.totalNanos;
	});

	FILE *f = File::OpenCFile(Path(g_Config.sJitProfileFile), "a");
	if (!f)
		return;
	fprintf(f, "IR interpreter profile: %.3f s in %d blocks\n\n", totalNanos * 1e-9, (int)hot.size());
	for (int rank = 0; rank < (int)hot.size() && rank < 40; ++rank) {
		const int blockNum = hot[rank];
		const JitBlock *b = blocks_.GetBlock(blockNum);
		const JitBlockProfileStats &stats = b->profileStats_;
		const std::string label = g_symbolMap ? g_symbolMap->GetDescription(b->GetOriginalStart()) : "";
		fprintf(f, "#%d %.2f%% block %d at %08x %s: %lld runs, %.1f ns/run\n", rank + 1, 100.0 * stats.totalNanos / totalNanos, blockNum,
			b->GetOriginalStart(), label.c_str(), (long long)stats.executions, (double)stats.totalNanos / stats.executions);
		const IRInst *inst = arena_.data() + b->GetEntry();
		for (int i = 0; i < b->GetNumIRInstructions(); ++i) {
			char temp[256];
			DisassembleIR(temp, sizeof(temp), inst[i]);
			fprintf(f, "    %s\n", temp);
		}
		fprintf(f, "\n");
	}
	fclose(f);

	for (int blockNum : hot)
		blocks_.GetBlock(blockNum)->profileStats_ = JitBlockProfileStats{};
}

void IRJit::InvalidateCacheAt(u32 em_address, int length) {
	std::vector<int> numbers = blocks_.FindInvalidatedBlockNumbers(em_address, length);
	if (numbers.empty()) {
		return;
	}

	DEBUG_LOG(Log::JIT, "Invalidating IR block cache at %08x (%d bytes): %d blocks", em_address, length, (int)numbers.size());
	invalidations_++;
	invalidatedBlocks_ += (int)numbers.size();

	for (int block_num : numbers) {
		// TODO: We are invalidating a lot of blocks that are already invalid (yu gi oh).
		InvalidateBlock(block_num);
	}
}

void IRJit::InvalidateBlock(int block_num) {
	JitBlock *block = blocks_.GetBlock(block_num);
	// Before Destroy, which forgets the block's address. Blocks linked to it jump straight in.
	InvalidateTargetBlock(block_num);
	blocks_.RemoveBlockFromPageLookup(block_num);
	block->Destroy(blocks_.Space());
}

void IRJit::InvalidateCacheExcept(const std::vector<std::pair<u32, u32>> &keep) {
	if (keep.empty()) {
		ClearCache();
		return;
	}

	int kept = 0;
	int invalidated = 0;
	for (int block_num = 0; block_num < blocks_.GetNumBlocks(); ++block_num) {
		const JitBlock *block = blocks_.GetBlock(block_num);
		if (!block->IsValid()) {
			continue;
		}
		u32 start, size;
		block->GetRange(&start, &size);
		start &= 0x3FFFFFFF;
		bool inside = false;
		for (const auto &[keepStart, keepSize] : keep) {
			const u32 maskedStart = keepStart & 0x3FFFFFFF;
			if (start >= maskedStart && start + size <= maskedStart + keepSize) {
				inside = true;
				break;
			}
		}
		if (inside) {
			kept++;
		} else {
			InvalidateBlock(block_num);
			invalidated++;
		}
	}
	INFO_LOG(Log::JIT, "IRJit: Kept %d blocks, invalidated %d", kept, invalidated);
}

void IRJit::Compile(u32 em_address) {
	_dbg_assert_(compilerEnabled_);

	PROFILE_THIS_SCOPE("jitc");

	std::vector<IRInst> instructions;
	u32 mipsBytes;
	const CompileResult result = CompileBlock(em_address, instructions, mipsBytes);
	if (result != CompileResult::OK) {
		if (result == CompileResult::ARENA_FULL) {
			ERROR_LOG(Log::JIT, "IR arena full (%d blocks), clearing cache", blocks_.GetNumBlocks());
			irArenaFullClears_++;
		} else {
			ERROR_LOG(Log::JIT, "Native code space full (%d blocks), clearing cache", blocks_.GetNumBlocks());
			codeFullClears_++;
		}
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes);
	}

	if (frontend_.CheckRounding(em_address)) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		CompileBlock(em_address, instructions, mipsBytes);
	}
}

// WARNING! This can be called from IRInterpret / the JIT, through the function preload stuff!
IRJit::CompileResult IRJit::CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes) {
	_dbg_assert_(compilerEnabled_);

	frontend_.DoJit(em_address, instructions, mipsBytes);
	_dbg_assert_(!instructions.empty());

	if (mipsTracer.tracing_enabled && instructions.size() > 1 && instructions[1].op == IROp::LogIRBlock) {
		// Before the IR interpreter's block cache takes its copy.
		instructions[1].constant = mipsTracer.prepare_block(em_address, mipsBytes);
	}

	int block_num = blocks_.AllocateBlock(em_address, mipsBytes, (u32)instructions.size());
	JitBlock *b = blocks_.GetBlock(block_num);
	if (mipsTracer.tracing_enabled) {
		// Hash, then only update page stats, don't link yet.
		// TODO: Should we always hash?  Then we can reuse blocks.
		b->UpdateHash();
	}

	// Caller handles a full arena or code space.
	const CompileResult result = CompileTargetBlock(block_num, instructions);
	if (result != CompileResult::OK)
		return result;

	// Updates stats, also enters the block in the block shadow.
	blocks_.FinalizeBlock(block_num);
	FinalizeTargetBlock(block_num);
	return CompileResult::OK;
}

IRJit::CompileResult IRInterpreterJit::CompileTargetBlock(int block_num, const std::vector<IRInst> &instructions) {
	if (arena_.empty()) {
		// Offset 0 would mean "no block" in the block shadow, and 1 a replacement hook.
		arena_.push_back(IRInst{ IROp::Bad });
		arena_.push_back(IRInst{ IROp::Bad });
	}
	const int offset = (int)arena_.size();
	if (offset >= MAX_ARENA_SIZE) {
		WARN_LOG(Log::JIT, "Filled IR arena, restarting");
		return CompileResult::ARENA_FULL;
	}
	arena_.insert(arena_.end(), instructions.begin(), instructions.end());
	blocks_.GetBlock(block_num)->SetEntry(offset);
	return CompileResult::OK;
}

void IRInterpreterJit::GetBlockInstructions(const JitBlock &block, std::vector<IRInst> &instructions) const {
	if (block.GetEntry() == 0) {
		instructions.clear();
		return;
	}
	const IRInst *start = arena_.data() + block.GetEntry();
	instructions.assign(start, start + block.GetNumIRInstructions());
}

void IRJit::GetStats(JitStats *stats) const {
	stats->numBlocks = blocks_.GetNumBlocks();
	stats->numValidBlocks = blocks_.CountValidBlocks();
	stats->irArenaFullClears = irArenaFullClears_;
	stats->codeFullClears = codeFullClears_;
	stats->invalidations = invalidations_;
	stats->invalidatedBlocks = invalidatedBlocks_;
}

int IRInterpreterJit::GetBlockCodeSize(int block_num) const {
	// Bloat is counted in instructions, as if IR were 4 bytes like MIPS.
	return blocks_.GetBlock(block_num)->GetNumIRInstructions() * 4;
}

void IRInterpreterJit::GetStats(JitStats *stats) const {
	IRJit::GetStats(stats);
	stats->irArenaUsed = arena_.size();
	stats->irArenaSize = MAX_ARENA_SIZE;
}

void IRInterpreterJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	if (profiling_)
		RunBlocks<true>();
	else
		RunBlocks<false>();
}

// Runs blocks, compiling as needed, until the downcount runs out (or the core stops).
template <bool profile>
inline void IRInterpreterJit::RunBlocksUntilDowncount() {
	MIPSState *mips = mips_;
	while (mips->downcount >= 0) {
		// The block's offset into the IR arena, or 0 for none, or a replacement hook (handled by
		// compiling a block).
		const u32 offset = *mips->space->GetBlockShadowEntry(mips->pc);
		static_assert(Memory::BLOCK_SHADOW_HOOK_NO_JIT == 1, "IR arena reserves offsets 0 and 1");
		if (offset > Memory::BLOCK_SHADOW_HOOK_NO_JIT) {
			const IRInst *instPtr = arena_.data() + offset;
			// First op is always, except when using breakpoints, downcount, to save one dispatch inside IRInterpret.
			// This branch is very cpu-branch-predictor-friendly so this still beats the dispatch.
			if (instPtr->op == IROp::Downcount) {
				mips->downcount -= instPtr->constant;
				instPtr++;
			}
			if constexpr (profile) {
				JitBlock *block = blocks_.GetBlock(blocks_.FindByEntry(offset));
				Instant start = Instant::Now();
				mips->pc = IRInterpret(mips, instPtr);
				block->profileStats_.executions += 1;
				block->profileStats_.totalNanos += start.ElapsedNanos();
			} else {
				// Runs on through the blocks it can, as long as downcount lasts.
				mips->pc = IRInterpret(mips, instPtr, arena_.data());
			}
			// Note: this will "jump to zero" on a badly constructed block missing exits.
			if (!mips->space->IsValid4AlignedAddress(mips->pc)) {
				int blockNum = blocks_.FindByEntry(offset);
				JitBlock *block = blocks_.GetBlockUnchecked(blockNum);
				Core_ExecException(mips->pc, block->GetOriginalStart(), ExecExceptionType::JUMP);
				return;
			}
		} else {
			RestoreHostRoundingMode();
#ifdef _DEBUG
			compilerEnabled_ = true;
#endif
			Compile(mips->pc);
#ifdef _DEBUG
			compilerEnabled_ = false;
#endif
			ApplyHostRoundingMode(mips);
		}
	}
}

template <bool profile>
void IRInterpreterJit::RunBlocks() {
	// IR Dispatcher
	// The guest's rounding mode applies while running blocks, like in the native JITs' dispatchers
	// (see ApplyHostRoundingMode).
	MIPSState *mips = mips_;
	while (true) {
		RestoreHostRoundingMode();
		CoreTiming::Advance(mips);
		if (coreState != 0) {
			break;
		}
		ApplyHostRoundingMode(mips);

#ifdef _DEBUG
		compilerEnabled_ = false;
#endif
		RunBlocksUntilDowncount<profile>();
#ifdef _DEBUG
		compilerEnabled_ = true;
#endif
	}

	RestoreHostRoundingMode();
}

void IRInterpreterJit::RunDowncount() {
	ApplyHostRoundingMode(mips_);
#ifdef _DEBUG
	compilerEnabled_ = false;
#endif
	if (profiling_)
		RunBlocksUntilDowncount<true>();
	else
		RunBlocksUntilDowncount<false>();
#ifdef _DEBUG
	compilerEnabled_ = true;
#endif
	RestoreHostRoundingMode();
}

void JitBlockTable::Clear() {
	for (JitBlock &block : blocks_) {
		block.Destroy(*space_);
	}
	blocks_.clear();
	byPage_.clear();
}

int JitBlockTable::CountValidBlocks() const {
	int count = 0;
	for (const JitBlock &block : blocks_) {
		if (block.IsValid()) {
			count++;
		}
	}
	return count;
}

int JitBlockTable::AllocateBlock(int emAddr, u32 origSize, u32 numIRInstructions) {
	int newBlockIndex = (int)blocks_.size();
	blocks_.push_back(JitBlock(emAddr, origSize, numIRInstructions));
	return newBlockIndex;
}

int JitBlockTable::FindByEntry(int entry) const {
	// Entries rise with the block number (we don't go back and replace them when invalidated). So we can binary search.
	int low = 0;
	int high = (int)blocks_.size() - 1;
	while (low <= high) {
		int mid = low + (high - low) / 2;
		const int blockEntry = blocks_[mid].GetEntry();
		if (blockEntry == entry) {
			return mid;
		}
		if (blockEntry < entry) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	return -1;
}

std::vector<int> JitBlockTable::FindInvalidatedBlockNumbers(u32 address, u32 lengthInBytes) {
	u32 startPage = AddressToPage(address);
	u32 endPage = AddressToPage(address + lengthInBytes);

	std::vector<int> found;
	// sceKernelIcacheInvalidateAll covers all of memory, a million pages.
	if (endPage - startPage >= byPage_.size()) {
		for (const auto &[page, blocksInPage] : byPage_) {
			if (page < startPage || page > endPage)
				continue;
			for (int i : blocksInPage) {
				if (blocks_[i].OverlapsRange(address, lengthInBytes))
					found.push_back(i);
			}
		}
		return found;
	}
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

void JitBlockTable::FinalizeBlock(int blockIndex) {
	// TODO: What's different about preload blocks?
	JitBlock &block = blocks_[blockIndex];
	block.Finalize(*space_);

	u32 startAddr, size;
	block.GetRange(&startAddr, &size);

	u32 startPage = AddressToPage(startAddr);
	u32 endPage = AddressToPage(startAddr + size);

	for (u32 page = startPage; page <= endPage; ++page) {
		byPage_[page].push_back(blockIndex);
	}
}

// Call after Destroy-ing it.
void JitBlockTable::RemoveBlockFromPageLookup(int blockIndex) {
	// We need to remove the block from the byPage lookup.
	JitBlock &block = blocks_[blockIndex];

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
}

u32 JitBlockTable::AddressToPage(u32 addr) const {
	// Use relatively small pages since basic blocks are typically small.
	return (addr & 0x3FFFFFFF) >> 10;
}

int JitBlockTable::FindPreloadBlock(u32 em_address) {
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

int JitBlockTable::GetBlockNumberFromStartAddress(u32 em_address) const {
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

void JitBlock::Finalize(const Memory::AddressSpace &space) {
	// Check it wasn't invalidated, in case this is after preload.
	// TODO: Allow reusing blocks when the code matches hash_ again, instead.
	if (origAddr_ && space.IsValid4AlignedAddress(origAddr_)) {
		origFirstOpcode_ = space.IsMediaEngine() ? MIPSOpcode(space.ReadUnchecked_U32(origAddr_)) : Memory::ReadUnchecked_Instruction(origAddr_);
		_dbg_assert_(entry_ != 0);
		space.WriteBlockShadow(origAddr_, entry_);
	} else {
		WARN_LOG(Log::JIT, "Finalizing invalid block (entry: %d)", entry_);
	}
}

void JitBlock::Destroy(const Memory::AddressSpace &space) {
	if (origAddr_ && space.IsValid4AlignedAddress(origAddr_)) {
		// A block at a mirror may have taken over the entry, leave that alone. One that never got an
		// entry (the arena or code space filled while compiling it) has nothing in the shadow.
		if (entry_ != 0)
			space.ClearBlockShadow(origAddr_, entry_);
		// Let's mark this invalid so we don't try to clear it again.
		origAddr_ = 0;
	}
}

u64 JitBlock::CalculateHash() const {
	if (origAddr_ && Memory::IsValidRange(origAddr_, origSize_)) {
		return XXH3_64bits(Memory::GetPointerUnchecked(origAddr_), origSize_);
	}
	return 0;
}

bool JitBlock::OverlapsRange(u32 addr, u32 size) const {
	addr &= 0x3FFFFFFF;
	u32 origAddr = origAddr_ & 0x3FFFFFFF;
	return addr + size > origAddr && addr < origAddr + origSize_;
}

int IRJitDebugInterface::GetNumBlocks() const {
	return jit_.blocks_.GetNumBlocks();
}

int IRJitDebugInterface::GetBlockNumberFromStartAddress(u32 em_address) const {
	return jit_.blocks_.GetBlockNumberFromStartAddress(em_address);
}

bool IRJitDebugInterface::IsValidBlock(int blockNum) const {
	return jit_.blocks_.IsValidBlock(blockNum);
}

JitBlockMeta IRJitDebugInterface::GetBlockMeta(int blockNum) const {
	JitBlockMeta meta{};
	if (IsValidBlock(blockNum)) {
		meta.valid = true;
		jit_.blocks_.GetBlock(blockNum)->GetRange(&meta.addr, &meta.sizeInBytes);
	}
	return meta;
}

JitBlockProfileStats IRJitDebugInterface::GetBlockProfileStats(int blockNum) const {
	return jit_.blocks_.GetBlock(blockNum)->profileStats_;
}

bool IRJitDebugInterface::SupportsProfiling() const {
	return jit_.HasBlockProfile();
}

JitBlockDebugInfo IRJitDebugInterface::GetBlockDebugInfo(int blockNum) const {
	const JitBlock &block = *jit_.blocks_.GetBlock(blockNum);
	JitBlockDebugInfo debugInfo{};
	uint32_t start, size;
	block.GetRange(&start, &size);
	debugInfo.originalAddress = start;  // TODO
	if (!Memory::IsValid4AlignedAddress(start)) {
		return debugInfo;
	}

	debugInfo.origDisasm.reserve(size / 4);
	for (u32 addr = start; addr < start + size; addr += 4) {
		char temp[256];
		MIPSDisAsm(Replacement_GetDisplayOp(addr), addr, temp, sizeof(temp), true);
		debugInfo.origDisasm.push_back(temp);
	}

	std::vector<IRInst> instructions;
	jit_.GetBlockInstructions(block, instructions);
	debugInfo.irDisasm.reserve(instructions.size());
	for (const IRInst &inst : instructions) {
		char buffer[256];
		DisassembleIR(buffer, sizeof(buffer), inst);
		debugInfo.irDisasm.push_back(buffer);
	}

	jit_.GetBlockTargetDisasm(blockNum, debugInfo.targetDisasm);
	return debugInfo;
}

void IRJitDebugInterface::ComputeStats(BlockCacheStats &bcStats) const {
	double totalBloat = 0.0;
	double maxBloat = 0.0;
	double minBloat = 1000000000.0;
	const int numBlocks = GetNumBlocks();
	for (int i = 0; i < numBlocks; ++i) {
		const int codeSize = jit_.GetBlockCodeSize(i);
		if (codeSize == 0)
			continue;
		u32 origAddr, mipsBytes;
		jit_.blocks_.GetBlock(i)->GetRange(&origAddr, &mipsBytes);
		double bloat = (double)codeSize / (double)mipsBytes;
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
	bcStats.numBlocks = numBlocks;
	bcStats.minBloat = (float)minBloat;
	bcStats.maxBloat = (float)maxBloat;
	bcStats.avgBloat = (float)(totalBloat / (double)numBlocks);
}

}  // namespace MIPSComp
