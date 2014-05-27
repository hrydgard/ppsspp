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

// Enable define below to enable oprofile integration. For this to work,
// it requires at least oprofile version 0.9.4, and changing the build
// system to link the Dolphin executable against libopagent.	Since the
// dependency is a little inconvenient and this is possibly a slight
// performance hit, it's not enabled by default, but it's useful for
// locating performance issues.

#include <cstddef>

#include "Common.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"

#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#if defined(ARM)
#include "Common/ArmEmitter.h"
#include "Core/MIPS/ARM/ArmAsm.h"
using namespace ArmGen;
#elif defined(_M_IX86) || defined(_M_X64)
#include "Common/x64Emitter.h"
#include "Common/x64Analyzer.h"
#include "Core/MIPS/x86/Asm.h"
using namespace Gen;
#elif defined(PPC)
#include "Common/ppcEmitter.h"
#include "Core/MIPS/MIPS.h"
using namespace PpcGen;
#else
#error "Unsupported arch!"
#endif
// #include "JitBase.h"

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
const MIPSOpcode INVALID_ORIGINAL_OP = MIPSOpcode(0x00000001);

JitBlockCache::JitBlockCache(MIPSState *mips, CodeBlock *codeBlock) :
	mips_(mips), codeBlock_(codeBlock), blocks_(0), num_blocks_(0) {
}

JitBlockCache::~JitBlockCache() {
	Shutdown();
}

bool JitBlock::ContainsAddress(u32 em_address) {
	// WARNING - THIS DOES NOT WORK WITH JIT INLINING ENABLED.
	// However, that doesn't exist yet so meh.
	return (em_address >= originalAddress && em_address < originalAddress + 4 * originalSize);
}

bool JitBlockCache::IsFull() const {
	return num_blocks_ >= MAX_NUM_BLOCKS - 1;
}

void JitBlockCache::Init() {
#if defined USE_OPROFILE && USE_OPROFILE
	agent = op_open_agent();
#endif
	blocks_ = new JitBlock[MAX_NUM_BLOCKS];
	Clear();
}

void JitBlockCache::Shutdown() {
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
	for (int i = 0; i < num_blocks_; i++)
		DestroyBlock(i, false);
	links_to_.clear();
	block_map_.clear();
	proxyBlockIndices_.clear();
	num_blocks_ = 0;
}

void JitBlockCache::Reset() {
	Shutdown();
	Init();
}

JitBlock *JitBlockCache::GetBlock(int no) {
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
		INFO_LOG(HLE, "Adding proxy root %08x to block at %08x", rootAddress, startAddress);
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
	proxyBlockIndices_.push_back(num_blocks_);
	num_blocks_++; //commit the current block
}

void JitBlockCache::FinalizeBlock(int block_num, bool block_link) {
	JitBlock &b = blocks_[block_num];

	b.originalFirstOpcode = Memory::Read_Opcode_JIT(b.originalAddress);
	MIPSOpcode opcode = GetEmuHackOpForBlock(block_num);
	Memory::Write_Opcode_JIT(b.originalAddress, opcode);

	// Convert the logical address to a physical address for the block map
	// Yeah, this'll work fine for PSP too I think.
	u32 pAddr = b.originalAddress & 0x1FFFFFFF;

	block_map_[std::make_pair(pAddr + 4 * b.originalSize - 1, pAddr)] = block_num;
	if (block_link) {
		for (int i = 0; i < MAX_JIT_BLOCK_EXITS; i++) {
			if (b.exitAddress[i] != INVALID_EXIT)
				links_to_.insert(std::pair<u32, int>(b.exitAddress[i], block_num));
		}

		LinkBlock(block_num);
		LinkBlockExits(block_num);
	}

#if defined USE_OPROFILE && USE_OPROFILE
	char buf[100];
	sprintf(buf, "EmuCode%x", b.originalAddress);
	const u8* blockStart = blocks_[block_num].checkedEntry;
	op_write_native_code(agent, buf, (uint64_t)blockStart, blockStart, b.normalEntry + b.codeSize - b.checkedEntry);
#endif

#ifdef USE_VTUNE
	sprintf(b.blockName, "EmuCode_0x%08x", b.originalAddress);

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

static int binary_search(JitBlock blocks_[], const u8 *baseoff, int imin, int imax) {
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
			ERROR_LOG(JIT, "JitBlockCache: Invalid Emuhack Op %08x", inst.encoding);
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

int JitBlockCache::GetBlockNumberFromStartAddress(u32 addr, bool realBlocksOnly) {
	if (!blocks_)
		return -1;

	MIPSOpcode inst = MIPSOpcode(Memory::Read_U32(addr));
	int bl = GetBlockNumberFromEmuHackOp(inst);
	if (bl < 0) {
		if (!realBlocksOnly) {
			// Wasn't an emu hack op, look through proxyBlockIndices_.
			for (size_t i = 0; i < proxyBlockIndices_.size(); i++) {
				int blockIndex = proxyBlockIndices_[i];
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

	for (int e = 0; e < MAX_JIT_BLOCK_EXITS; e++) {
		if (b.exitAddress[e] != INVALID_EXIT && !b.linkStatus[e]) {
			int destinationBlock = GetBlockNumberFromStartAddress(b.exitAddress[e]);
			if (destinationBlock != -1) 	{
#if defined(ARM)
				ARMXEmitter emit(b.exitPtrs[e]);
				emit.B(blocks_[destinationBlock].checkedEntry);
				emit.FlushIcache();

#elif defined(_M_IX86) || defined(_M_X64)
				XEmitter emit(b.exitPtrs[e]);
				emit.JMP(blocks_[destinationBlock].checkedEntry, true);
#elif defined(PPC)
				PPCXEmitter emit(b.exitPtrs[e]);
				emit.B(blocks_[destinationBlock].checkedEntry);
				emit.FlushIcache();
#endif
				b.linkStatus[e] = true;
			}
		}
	}
}

using namespace std;

void JitBlockCache::LinkBlock(int i) {
	LinkBlockExits(i);
	JitBlock &b = blocks_[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	// equal_range(b) returns pair<iterator,iterator> representing the range
	// of element with key b
	ppp = links_to_.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
		// PanicAlert("Linking block %i to block %i", iter->second, i);
		LinkBlockExits(iter->second);
	}
}

void JitBlockCache::UnlinkBlock(int i) {
	JitBlock &b = blocks_[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	ppp = links_to_.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
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
		result[block_num] = emuhack;
		// The goal here is to prevent restoring it if it did not match (in case originalFirstOpcode does match.)
		if (Memory::ReadUnchecked_U32(b.originalAddress) != emuhack)
			b.originalFirstOpcode = INVALID_ORIGINAL_OP;
		else
			Memory::Write_Opcode_JIT(b.originalAddress, b.originalFirstOpcode);
	}

	return result;
}

void JitBlockCache::RestoreSavedEmuHackOps(std::vector<u32> saved) {
	if (num_blocks_ != (int)saved.size()) {
		ERROR_LOG(JIT, "RestoreSavedEmuHackOps: Wrong saved block size.");
		return;
	}

	for (int block_num = 0; block_num < num_blocks_; ++block_num) {
		const JitBlock &b = blocks_[block_num];
		if (b.invalid)
			continue;

		// Only if we restored it, write it back.
		if (Memory::ReadUnchecked_U32(b.originalAddress) == b.originalFirstOpcode.encoding)
			Memory::Write_Opcode_JIT(b.originalAddress, MIPSOpcode(saved[block_num]));
	}
}

void JitBlockCache::DestroyBlock(int block_num, bool invalidate) {
	if (block_num < 0 || block_num >= num_blocks_) {
		ERROR_LOG_REPORT(JIT, "DestroyBlock: Invalid block number %d", block_num);
		return;
	}
	JitBlock *b = &blocks_[block_num];

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
				DestroyBlock(proxied_blocknum, invalidate);
			}
		}
		b->proxyFor->clear();
		delete b->proxyFor;
		b->proxyFor = 0;
	}
	// TODO: Remove from proxyBlockIndices_.

	// TODO: Handle the case when there's a proxy block and a regular JIT block at the same location.
	// In this case we probably "leak" the proxy block currently (no memory leak but it'll stay enabled).

	if (b->invalid) {
		if (invalidate)
			ERROR_LOG(JIT, "Invalidating invalid block %d", block_num);
		return;
	}

	b->invalid = true;
	if (Memory::ReadUnchecked_U32(b->originalAddress) == GetEmuHackOpForBlock(block_num).encoding)
		Memory::Write_Opcode_JIT(b->originalAddress, b->originalFirstOpcode);

	// It's not safe to set normalEntry to 0 here, since we use a binary search
	// that looks at that later to find blocks. Marking it invalid is enough.

	UnlinkBlock(block_num);

#if defined(ARM)

	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// I hope there's enough space...
	// checkedEntry is the only "linked" entrance so it's enough to overwrite that.
	ARMXEmitter emit((u8 *)b->checkedEntry);
	emit.MOVI2R(R0, b->originalAddress);
	emit.STR(R0, CTXREG, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->dispatcher);
	emit.FlushIcache();

#elif defined(_M_IX86) || defined(_M_X64)

	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// Spurious entrances from previously linked blocks can only come through checkedEntry
	XEmitter emit((u8 *)b->checkedEntry);
	emit.MOV(32, M(&mips_->pc), Imm32(b->originalAddress));
	emit.JMP(MIPSComp::jit->Asm().dispatcher, true);
#elif defined(PPC)
	PPCXEmitter emit((u8 *)b->checkedEntry);
	emit.MOVI2R(R3, b->originalAddress);
	emit.STW(R0, CTXREG, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->dispatcher);
	emit.FlushIcache();
#endif
}

void JitBlockCache::InvalidateICache(u32 address, const u32 length) {
	// Convert the logical address to a physical address for the block map
	u32 pAddr = address & 0x1FFFFFFF;

	// destroy JIT blocks
	// !! this works correctly under assumption that any two overlapping blocks end at the same address
	// TODO: This may not be a safe assumption with jit continuing enabled.
	std::map<pair<u32,u32>, u32>::iterator it1 = block_map_.lower_bound(std::make_pair(pAddr, 0)), it2 = it1;
	while (it2 != block_map_.end() && it2->first.second < pAddr + length) {
		DestroyBlock(it2->second, true);
		it2++;
	}

	if (it1 != it2)
		block_map_.erase(it1, it2);
}
