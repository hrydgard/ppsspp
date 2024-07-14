// Copyright (c) 2012- PPSSPP Project / Dolphin Project.

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
#include <cstddef>
#include <algorithm>

#include "ext/xxhash.h"
#include "Common/CommonTypes.h"
#include "Common/Profiler/Profiler.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/Config.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"

#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

// #include "JitBase.h"

// Enable define below to enable oprofile integration. For this to work,
// it requires at least oprofile version 0.9.4, and changing the build
// system to link the Dolphin executable against libopagent.	Since the
// dependency is a little inconvenient and this is possibly a slight
// performance hit, it's not enabled by default, but it's useful for
// locating performance issues.
#if defined USE_OPROFILE && USE_OPROFILE
#include <opagent.h>

op_agent_t agent;
#endif

#if defined USE_VTUNE
#include <jitprofiling.h>
#pragma comment(lib, "libittnotify.lib")
#pragma comment(lib, "jitprofiling.lib")
#endif

const u32 INVALID_EXIT = 0xFFFFFFFF;

static uint64_t HashJitBlock(const JitBlock &b) {
	PROFILE_THIS_SCOPE("jithash");
	if (JIT_USE_COMPILEDHASH) {
		// Includes the emuhack (or emuhacks) in memory.
		if (Memory::IsValidRange(b.originalAddress, b.originalSize * 4)) {
			return XXH3_64bits(Memory::GetPointerUnchecked(b.originalAddress), b.originalSize * 4);
		} else {
			// Hm, this would be bad.
			return 0;
		}
	}
	return 0;
}

JitBlockCache::JitBlockCache(MIPSState *mipsState, CodeBlockCommon *codeBlock) :
	codeBlock_(codeBlock) {
}

JitBlockCache::~JitBlockCache() {
	Shutdown();
}

bool JitBlock::ContainsAddress(u32 em_address) const {
	// WARNING - THIS DOES NOT WORK WITH JIT INLINING ENABLED.
	// However, that doesn't exist yet so meh.
	return (em_address >= originalAddress && em_address < originalAddress + 4 * originalSize);
}

bool JitBlockCache::IsFull() const {
	// -10 to safely leave space for some proxy blocks, which we don't check before we allocate (not ideal, but should work).
	return num_blocks_ >= MAX_NUM_BLOCKS - 10;
}

void JitBlockCache::Init() {
#if defined USE_OPROFILE && USE_OPROFILE
	agent = op_open_agent();
#endif
	blocks_ = new JitBlock[MAX_NUM_BLOCKS];
	Clear();
}

void JitBlockCache::Shutdown() {
	Clear(); // Make sure proxy block links are deleted
	delete [] blocks_;
	blocks_ = 0;
	num_blocks_ = 0;
#if defined USE_OPROFILE && USE_OPROFILE
	op_close_agent(agent);
#endif

#ifdef USE_VTUNE
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_SHUTDOWN, NULL);
#endif
}

// This clears the JIT cache. It's called from JitCache.cpp when the JIT cache
// is full and when saving and loading states.
void JitBlockCache::Clear() {
	block_map_.clear();
	proxyBlockMap_.clear();
	for (int i = 0; i < num_blocks_; i++)
		DestroyBlock(i, DestroyType::CLEAR);
	links_to_.clear();
	num_blocks_ = 0;

	blockMemRanges_[JITBLOCK_RANGE_SCRATCH] = std::make_pair(0xFFFFFFFF, 0x00000000);
	blockMemRanges_[JITBLOCK_RANGE_RAMBOTTOM] = std::make_pair(0xFFFFFFFF, 0x00000000);
	blockMemRanges_[JITBLOCK_RANGE_RAMTOP] = std::make_pair(0xFFFFFFFF, 0x00000000);
}

void JitBlockCache::Reset() {
	Shutdown();
	Init();
}

JitBlock *JitBlockCache::GetBlock(int no) {
	return &blocks_[no];
}

const JitBlock *JitBlockCache::GetBlock(int no) const {
	return &blocks_[no];
}

int JitBlockCache::AllocateBlock(u32 startAddress) {
	JitBlock &b = blocks_[num_blocks_];

	b.proxyFor = 0;
	// If there's an existing pure proxy block at the address, we need to ditch it and create a new one,
	// taking over the proxied blocks.
	int num = GetBlockNumberFromStartAddress(startAddress, false);
	if (num >= 0) {
		if (blocks_[num].IsPureProxy()) {
			RemoveBlockMap(num);
			blocks_[num].invalid = true;
			b.proxyFor = new std::vector<u32>();
			*b.proxyFor = *blocks_[num].proxyFor;
			blocks_[num].proxyFor->clear();
			delete blocks_[num].proxyFor;
			blocks_[num].proxyFor = 0;
		}
	}

	b.invalid = false;
	b.originalAddress = startAddress;
	for (int i = 0; i < MAX_JIT_BLOCK_EXITS; ++i) {
		b.exitAddress[i] = INVALID_EXIT;
		b.exitPtrs[i] = 0;
		b.linkStatus[i] = false;
	}
	b.blockNum = num_blocks_;
	num_blocks_++; //commit the current block
	return num_blocks_ - 1;
}

void JitBlockCache::ProxyBlock(u32 rootAddress, u32 startAddress, u32 size, const u8 *codePtr) {
	// If there's an existing block at the startAddress, add rootAddress as a proxy root of that block
	// instead of creating a new block.
	int num = GetBlockNumberFromStartAddress(startAddress, false);
	if (num != -1) {
		DEBUG_LOG(Log::HLE, "Adding proxy root %08x to block at %08x", rootAddress, startAddress);
		if (!blocks_[num].proxyFor) {
			blocks_[num].proxyFor = new std::vector<u32>();
		}
		blocks_[num].proxyFor->push_back(rootAddress);
	}

	JitBlock &b = blocks_[num_blocks_];
	b.invalid = false;
	b.originalAddress = startAddress;
	b.originalSize = size;
	for (int i = 0; i < MAX_JIT_BLOCK_EXITS; ++i) {
		b.exitAddress[i] = INVALID_EXIT;
		b.exitPtrs[i] = 0;
		b.linkStatus[i] = false;
	}
	b.exitAddress[0] = rootAddress;
	b.blockNum = num_blocks_;
	b.proxyFor = new std::vector<u32>();
	b.SetPureProxy();  // flag as pure proxy block.

	// Make binary searches and stuff work ok
	b.normalEntry = codePtr;
	b.checkedEntry = codePtr;
	proxyBlockMap_.emplace(startAddress, num_blocks_);
	AddBlockMap(num_blocks_);

	num_blocks_++; //commit the current block
}

void JitBlockCache::AddBlockMap(int block_num) {
	const JitBlock &b = blocks_[block_num];
	// Convert the logical address to a physical address for the block map
	// Yeah, this'll work fine for PSP too I think.
	u32 pAddr = b.originalAddress & 0x1FFFFFFF;
	block_map_[std::make_pair(pAddr + 4 * b.originalSize, pAddr)] = block_num;
}

void JitBlockCache::RemoveBlockMap(int block_num) {
	const JitBlock &b = blocks_[block_num];
	if (b.invalid) {
		return;
	}

	const u32 pAddr = b.originalAddress & 0x1FFFFFFF;
	auto it = block_map_.find(std::make_pair(pAddr + 4 * b.originalSize, pAddr));
	if (it != block_map_.end() && it->second == (u32)block_num) {
		block_map_.erase(it);
	} else {
		// It wasn't in there, or it has the wrong key.  Let's search...
		for (auto it = block_map_.begin(); it != block_map_.end(); ++it) {
			if (it->second == (u32)block_num) {
				block_map_.erase(it);
				break;
			}
		}
	}
}

static void ExpandRange(std::pair<u32, u32> &range, u32 newStart, u32 newEnd) {
	range.first = std::min(range.first, newStart);
	range.second = std::max(range.second, newEnd);
}

void JitBlockCache::FinalizeBlock(int block_num, bool block_link) {
	JitBlock &b = blocks_[block_num];
	_assert_msg_(Memory::IsValidAddress(b.originalAddress), "FinalizeBlock: Bad originalAddress %08x in block %d (b.num: %d) proxy: %s sz: %d", b.originalAddress, block_num, b.blockNum, b.proxyFor ? "y" : "n", b.codeSize);

	b.originalFirstOpcode = Memory::Read_Opcode_JIT(b.originalAddress);
	MIPSOpcode opcode = GetEmuHackOpForBlock(block_num);
	Memory::Write_Opcode_JIT(b.originalAddress, opcode);

	// Note that this hashes the emuhack too, which is intentional.
	b.compiledHash = HashJitBlock(b);

	AddBlockMap(block_num);

	if (block_link) {
		for (int i = 0; i < MAX_JIT_BLOCK_EXITS; i++) {
			if (b.exitAddress[i] != INVALID_EXIT) {
				links_to_.emplace(b.exitAddress[i], block_num);
			}
		}

		LinkBlock(block_num);
		LinkBlockExits(block_num);
	}

	const u32 blockEnd = b.originalAddress + b.originalSize * 4 - 4;
	if (Memory::IsScratchpadAddress(b.originalAddress)) {
		ExpandRange(blockMemRanges_[JITBLOCK_RANGE_SCRATCH], b.originalAddress, blockEnd);
	}
	const u32 halfUserMemory = (PSP_GetUserMemoryEnd() - PSP_GetUserMemoryBase()) / 2;
	if (b.originalAddress < PSP_GetUserMemoryBase() + halfUserMemory) {
		ExpandRange(blockMemRanges_[JITBLOCK_RANGE_RAMBOTTOM], b.originalAddress, blockEnd);
	}
	if (blockEnd > PSP_GetUserMemoryBase() + halfUserMemory) {
		ExpandRange(blockMemRanges_[JITBLOCK_RANGE_RAMTOP], b.originalAddress, blockEnd);
	}

#if defined USE_OPROFILE && USE_OPROFILE
	char buf[100];
	snprintf(buf, sizeof(buf), "EmuCode%x", b.originalAddress);
	const u8* blockStart = blocks_[block_num].checkedEntry;
	op_write_native_code(agent, buf, (uint64_t)blockStart, blockStart, b.normalEntry + b.codeSize - b.checkedEntry);
#endif

#ifdef USE_VTUNE
	snprintf(b.blockName, sizeof(b.blockName), "EmuCode_0x%08x", b.originalAddress);

	iJIT_Method_Load jmethod = {0};
	jmethod.method_id = iJIT_GetNewMethodID();
	jmethod.class_file_name = "";
	jmethod.source_file_name = __FILE__;
	jmethod.method_load_address = (void*)blocks_[block_num].checkedEntry;
	jmethod.method_size = b.normalEntry + b.codeSize - b.checkedEntry;
	jmethod.line_number_size = 0;
	jmethod.method_name = b.blockName;
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jmethod);
#endif
}

bool JitBlockCache::RangeMayHaveEmuHacks(u32 start, u32 end) const {
	for (int i = 0; i < JITBLOCK_RANGE_COUNT; ++i) {
		if (end >= blockMemRanges_[i].first && start <= blockMemRanges_[i].second) {
			return true;
		}
	}
	return false;
}

static int binary_search(const JitBlock blocks_[], const u8 *baseoff, int imin, int imax) {
	while (imin < imax) {
		int imid = (imin + imax) / 2;
		if (blocks_[imid].normalEntry < baseoff)
			imin = imid + 1;
		else
			imax = imid;
	}
	if ((imax == imin) && (blocks_[imin].normalEntry == baseoff))
		return imin;
	else
		return -1;
}

int JitBlockCache::GetBlockNumberFromEmuHackOp(MIPSOpcode inst, bool ignoreBad) const {
	if (!num_blocks_ || !MIPS_IS_EMUHACK(inst)) // definitely not a JIT block
		return -1;
	int off = (inst & MIPS_EMUHACK_VALUE_MASK);

	const u8 *baseoff = codeBlock_->GetBasePtr() + off;
	if (baseoff < codeBlock_->GetBasePtr() || baseoff >= codeBlock_->GetCodePtr()) {
		if (!ignoreBad) {
			ERROR_LOG(Log::JIT, "JitBlockCache: Invalid Emuhack Op %08x", inst.encoding);
		}
		return -1;
	}

	int bl = binary_search(blocks_, baseoff, 0, num_blocks_ - 1);
	if (bl >= 0 && blocks_[bl].invalid) {
		return -1;
	} else {
		return bl;
	}
}

MIPSOpcode JitBlockCache::GetEmuHackOpForBlock(int blockNum) const {
	int off = (int)(blocks_[blockNum].normalEntry - codeBlock_->GetBasePtr());
	return MIPSOpcode(MIPS_EMUHACK_OPCODE | off);
}

int JitBlockCache::GetBlockNumberFromStartAddress(u32 addr, bool realBlocksOnly) const {
	if (!blocks_ || !Memory::IsValidAddress(addr))
		return -1;

	MIPSOpcode inst = MIPSOpcode(Memory::Read_U32(addr));
	int bl = GetBlockNumberFromEmuHackOp(inst);
	if (bl < 0) {
		if (!realBlocksOnly) {
			// Wasn't an emu hack op, look through proxyBlockMap_.
			auto range = proxyBlockMap_.equal_range(addr);
			for (auto it = range.first; it != range.second; ++it) {
				const int blockIndex = it->second;
				if (blocks_[blockIndex].originalAddress == addr && !blocks_[blockIndex].proxyFor && !blocks_[blockIndex].invalid)
					return blockIndex;
			}
		}
		return -1;
	}

	if (blocks_[bl].originalAddress != addr)
		return -1;

	return bl;
}

void JitBlockCache::GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers) {
	for (int i = 0; i < num_blocks_; i++)
		if (blocks_[i].ContainsAddress(em_address))
			block_numbers->push_back(i);
}

int JitBlockCache::GetBlockNumberFromAddress(u32 em_address) {
	for (int i = 0; i < num_blocks_; i++) {
		if (blocks_[i].ContainsAddress(em_address))
			return i;
	}

	return -1;
}

u32 JitBlockCache::GetAddressFromBlockPtr(const u8 *ptr) const {
	if (!codeBlock_->IsInSpace(ptr))
		return (u32)-1;

	for (int i = 0; i < num_blocks_; ++i) {
		const auto &b = blocks_[i];
		if (!b.invalid && ptr >= b.checkedEntry && ptr < b.normalEntry + b.codeSize) {
			return b.originalAddress;
		}
	}

	// It's in jit somewhere, but we must have deleted it.
	return 0;
}

MIPSOpcode JitBlockCache::GetOriginalFirstOp(int block_num) {
	if (block_num >= num_blocks_ || block_num < 0) {
		return MIPSOpcode(block_num);
	}
	return blocks_[block_num].originalFirstOpcode;
}

void JitBlockCache::LinkBlockExits(int i) {
	JitBlock &b = blocks_[i];
	if (b.invalid) {
		// This block is dead. Don't relink it.
		return;
	}
	if (b.IsPureProxy()) {
		// Pure proxies can't link, since they don't have code.
		return;
	}

	for (int e = 0; e < MAX_JIT_BLOCK_EXITS; e++) {
		if (b.exitAddress[e] != INVALID_EXIT && !b.linkStatus[e]) {
			int destinationBlock = GetBlockNumberFromStartAddress(b.exitAddress[e], true);
			if (destinationBlock == -1) {
				continue;
			}

			JitBlock &eb = blocks_[destinationBlock];
			// Make sure the destination is not invalid.
			if (!eb.invalid) {
				MIPSComp::jit->LinkBlock(b.exitPtrs[e], eb.checkedEntry);
				b.linkStatus[e] = true;
			}
		}
	}
}

void JitBlockCache::LinkBlock(int i) {
	LinkBlockExits(i);
	JitBlock &b = blocks_[i];
	// equal_range(b) returns pair<iterator,iterator> representing the range
	// of element with key b
	auto ppp = links_to_.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (auto iter = ppp.first; iter != ppp.second; ++iter) {
		// INFO_LOG(Log::JIT, "Linking block %i to block %i", iter->second, i);
		LinkBlockExits(iter->second);
	}
}

void JitBlockCache::UnlinkBlock(int i) {
	JitBlock &b = blocks_[i];
	auto ppp = links_to_.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (auto iter = ppp.first; iter != ppp.second; ++iter) {
		if ((size_t)iter->second >= num_blocks_) {
			// Something probably went very wrong. Try to stumble along nevertheless.
			ERROR_LOG(Log::JIT, "UnlinkBlock: Invalid block number %d", iter->second);
			continue;
		}
		JitBlock &sourceBlock = blocks_[iter->second];
		for (int e = 0; e < MAX_JIT_BLOCK_EXITS; e++) {
			if (sourceBlock.exitAddress[e] == b.originalAddress)
				sourceBlock.linkStatus[e] = false;
		}
	}
}

std::vector<u32> JitBlockCache::SaveAndClearEmuHackOps() {
	std::vector<u32> result;
	result.resize(num_blocks_);

	for (int block_num = 0; block_num < num_blocks_; ++block_num) {
		JitBlock &b = blocks_[block_num];
		if (b.invalid)
			continue;

		const u32 emuhack = GetEmuHackOpForBlock(block_num).encoding;
		if (Memory::ReadUnchecked_U32(b.originalAddress) == emuhack)
		{
			result[block_num] = emuhack;
			Memory::Write_Opcode_JIT(b.originalAddress, b.originalFirstOpcode);
		}
		else
			result[block_num] = 0;
	}

	return result;
}

void JitBlockCache::RestoreSavedEmuHackOps(const std::vector<u32> &saved) {
	if (num_blocks_ != (int)saved.size()) {
		ERROR_LOG(Log::JIT, "RestoreSavedEmuHackOps: Wrong saved block size.");
		return;
	}

	for (int block_num = 0; block_num < num_blocks_; ++block_num) {
		const JitBlock &b = blocks_[block_num];
		if (b.invalid || saved[block_num] == 0)
			continue;

		// Only if we restored it, write it back.
		if (Memory::ReadUnchecked_U32(b.originalAddress) == b.originalFirstOpcode.encoding)
			Memory::Write_Opcode_JIT(b.originalAddress, MIPSOpcode(saved[block_num]));
	}
}

void JitBlockCache::DestroyBlock(int block_num, DestroyType type) {
	if (block_num < 0 || block_num >= num_blocks_) {
		ERROR_LOG_REPORT(Log::JIT, "DestroyBlock: Invalid block number %d", block_num);
		return;
	}
	JitBlock *b = &blocks_[block_num];
	// No point it being in there anymore.
	RemoveBlockMap(block_num);

	// Pure proxy blocks always point directly to a real block, there should be no chains of
	// proxy-only blocks pointing to proxy-only blocks.
	// Follow a block proxy chain.
	// Destroy the block that transitively has this as a proxy. Likely the root block once inlined
	// this block or its 'parent', so now that this block has changed, the root block must be destroyed.
	if (b->proxyFor) {
		for (size_t i = 0; i < b->proxyFor->size(); i++) {
			int proxied_blocknum = GetBlockNumberFromStartAddress((*b->proxyFor)[i], false);
			// If it was already cleared, we don't know which to destroy.
			if (proxied_blocknum != -1) {
				DestroyBlock(proxied_blocknum, type);
			}
		}
		b->proxyFor->clear();
		delete b->proxyFor;
		b->proxyFor = 0;
	}
	auto range = proxyBlockMap_.equal_range(b->originalAddress);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second == block_num) {
			// Found it.  Delete and bail.
			proxyBlockMap_.erase(it);
			break;
		}
	}

	// TODO: Handle the case when there's a proxy block and a regular JIT block at the same location.
	// In this case we probably "leak" the proxy block currently (no memory leak but it'll stay enabled).

	if (b->invalid) {
		if (type == DestroyType::INVALIDATE)
			ERROR_LOG(Log::JIT, "Invalidating invalid block %d", block_num);
		return;
	}

	b->invalid = true;
	if (!b->IsPureProxy()) {
		if (Memory::ReadUnchecked_U32(b->originalAddress) == GetEmuHackOpForBlock(block_num).encoding)
			Memory::Write_Opcode_JIT(b->originalAddress, b->originalFirstOpcode);
	}

	// It's not safe to set normalEntry to 0 here, since we use a binary search
	// that looks at that later to find blocks. Marking it invalid is enough.

	UnlinkBlock(block_num);

	// Don't change the jit code when invalidating a pure proxy block.
	if (b->IsPureProxy()) {
		return;
	}

	if (b->checkedEntry) {
		// We can skip this if we're clearing anyway, which cuts down on protect back and forth on WX exclusive.
		if (type != DestroyType::CLEAR) {
			u8 *writableEntry = codeBlock_->GetWritablePtrFromCodePtr(b->checkedEntry);
			MIPSComp::jit->UnlinkBlock(writableEntry, b->originalAddress);
		}
	} else {
		ERROR_LOG(Log::JIT, "Unlinking block with no entry: %08x (%d)", b->originalAddress, block_num);
	}
}

void JitBlockCache::InvalidateICache(u32 address, const u32 length) {
	// Convert the logical address to a physical address for the block map
	const u32 pAddr = address & 0x1FFFFFFF;
	const u32 pEnd = pAddr + length;

	if (pEnd < pAddr) {
		ERROR_LOG(Log::JIT, "Bad InvalidateICache: %08x with len=%d", address, length);
		return;
	}

	if (pAddr == 0 && pEnd >= 0x1FFFFFFF) {
		InvalidateChangedBlocks();
		return;
	}

	// Blocks may start and end in overlapping ways, and destroying one invalidates iterators.
	// So after destroying one, we start over.
	do {
	restart:
		auto next = block_map_.lower_bound(std::make_pair(pAddr, 0));
		auto last = block_map_.upper_bound(std::make_pair(pEnd + MAX_BLOCK_INSTRUCTIONS, 0));
		// Note that if next is end(), last will be end() too (equal.)
		for (; next != last; ++next) {
			const u32 blockStart = next->first.second;
			const u32 blockEnd = next->first.first;
			if (blockStart < pEnd && blockEnd > pAddr) {
				DestroyBlock(next->second, DestroyType::INVALIDATE);
				// Our iterator is now invalid.  Break and search again.
				// Most of the time there shouldn't be a bunch of matching blocks.
				goto restart;
			}
		}
		// We got here - it wasn't in the map at all (or anymore.)
	} while (false);
}

void JitBlockCache::InvalidateChangedBlocks() {
	// The primary goal of this is to make sure block linking is cleared up.
	for (int block_num = 0; block_num < num_blocks_; ++block_num) {
		JitBlock &b = blocks_[block_num];
		if (b.invalid || b.IsPureProxy())
			continue;

		bool changed = false;
		if (JIT_USE_COMPILEDHASH) {
			changed = b.compiledHash != HashJitBlock(b);
		} else {
			const u32 emuhack = GetEmuHackOpForBlock(block_num).encoding;
			changed = Memory::ReadUnchecked_U32(b.originalAddress) != emuhack;
		}

		if (changed) {
			DEBUG_LOG(Log::JIT, "Invalidating changed block at %08x", b.originalAddress);
			DestroyBlock(block_num, DestroyType::INVALIDATE);
		}
	}
}

int JitBlockCache::GetBlockExitSize() {
#if PPSSPP_ARCH(ARM)
	// Will depend on the sequence found to encode the destination address.
	return 0;
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	return 15;
#elif PPSSPP_ARCH(ARM64)
	// Will depend on the sequence found to encode the destination address.
	return 0;
#elif PPSSPP_ARCH(RISCV64)
	// Will depend on the sequence found to encode the destination address.
	return 0;
#else
#warning GetBlockExitSize unimplemented
	return 0;
#endif
}

void JitBlockCache::ComputeStats(BlockCacheStats &bcStats) const {
	double totalBloat = 0.0;
	double maxBloat = 0.0;
	double minBloat = 1000000000.0;
	for (int i = 0; i < num_blocks_; i++) {
		const JitBlock *b = GetBlock(i);
		double codeSize = (double)b->codeSize;
		if (codeSize == 0)
			continue;
		double origSize = (double)(4 * b->originalSize);
		double bloat = codeSize / origSize;
		if (bloat < minBloat) {
			minBloat = bloat;
			bcStats.minBloatBlock = b->originalAddress;
		}
		if (bloat > maxBloat) {
			maxBloat = bloat;
			bcStats.maxBloatBlock = b->originalAddress;
		}
		totalBloat += bloat;
	}
	bcStats.numBlocks = num_blocks_;
	bcStats.minBloat = (float)minBloat;
	bcStats.maxBloat = (float)maxBloat;
	bcStats.avgBloat = (float)(totalBloat / (double)num_blocks_);
}

JitBlockDebugInfo JitBlockCache::GetBlockDebugInfo(int blockNum) const {
	JitBlockDebugInfo debugInfo{};
	const JitBlock *block = GetBlock(blockNum);
	debugInfo.originalAddress = block->originalAddress;
	debugInfo.origDisasm.reserve(((block->originalAddress + block->originalSize * 4) - block->originalAddress) / 4);
	for (u32 addr = block->originalAddress; addr <= block->originalAddress + block->originalSize * 4; addr += 4) {
		char temp[256];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, temp, sizeof(temp), true);
		std::string mipsDis = temp;
		debugInfo.origDisasm.push_back(mipsDis);
	}

#if PPSSPP_ARCH(ARM)
	debugInfo.targetDisasm = DisassembleArm2(block->normalEntry, block->codeSize);
#elif PPSSPP_ARCH(ARM64)
	debugInfo.targetDisasm = DisassembleArm64(block->normalEntry, block->codeSize);
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	debugInfo.targetDisasm = DisassembleX86(block->normalEntry, block->codeSize);
#elif PPSSPP_ARCH(RISCV64)
	debugInfo.targetDisasm = DisassembleRV64(block->normalEntry, block->codeSize);
#endif
	return debugInfo;
}
