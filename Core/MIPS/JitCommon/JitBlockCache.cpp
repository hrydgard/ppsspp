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

#include "Common.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/CoreTiming.h"

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


#define INVALID_EXIT 0xFFFFFFFF

JitBlockCache::JitBlockCache(MIPSState *mips_, CodeBlock *codeBlock) :
	mips(mips_), codeBlock_(codeBlock), blocks(0), num_blocks(0) {
}

JitBlockCache::~JitBlockCache() {
	Shutdown();
}

bool JitBlock::ContainsAddress(u32 em_address) {
	// WARNING - THIS DOES NOT WORK WITH JIT INLINING ENABLED.
	// However, that doesn't exist yet so meh.
	return (em_address >= originalAddress && em_address < originalAddress + 4 * originalSize);
}

bool JitBlockCache::IsFull() const 
{
	return num_blocks >= MAX_NUM_BLOCKS - 1;
}

void JitBlockCache::Init()
{
#if defined USE_OPROFILE && USE_OPROFILE
	agent = op_open_agent();
#endif
	blocks = new JitBlock[MAX_NUM_BLOCKS];
	Clear();
}

void JitBlockCache::Shutdown()
{
	delete[] blocks;
	blocks = 0;
	num_blocks = 0;
#if defined USE_OPROFILE && USE_OPROFILE
	op_close_agent(agent);
#endif

#ifdef USE_VTUNE
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_SHUTDOWN, NULL);
#endif
}

// This clears the JIT cache. It's called from JitCache.cpp when the JIT cache
// is full and when saving and loading states.
void JitBlockCache::Clear()
{
	for (int i = 0; i < num_blocks; i++)
		DestroyBlock(i, false);
	links_to.clear();
	block_map.clear();
	num_blocks = 0;
}

void JitBlockCache::Reset()
{
	Shutdown();
	Init();
}

JitBlock *JitBlockCache::GetBlock(int no)
{
	return &blocks[no];
}

int JitBlockCache::AllocateBlock(u32 em_address)
{
	JitBlock &b = blocks[num_blocks];
	b.invalid = false;
	b.originalAddress = em_address;
	b.exitAddress[0] = INVALID_EXIT;
	b.exitAddress[1] = INVALID_EXIT;
	b.exitPtrs[0] = 0;
	b.exitPtrs[1] = 0;
	b.linkStatus[0] = false;
	b.linkStatus[1] = false;
	b.blockNum = num_blocks;
	num_blocks++; //commit the current block
	return num_blocks - 1;
}

void JitBlockCache::FinalizeBlock(int block_num, bool block_link)
{
	JitBlock &b = blocks[block_num];

	b.originalFirstOpcode = Memory::Read_Opcode_JIT(b.originalAddress);
	u32 opcode = GetEmuHackOpForBlock(block_num);
	Memory::Write_Opcode_JIT(b.originalAddress, opcode);
	
	// Convert the logical address to a physical address for the block map
	// Yeah, this'll work fine for PSP too I think.
	u32 pAddr = b.originalAddress & 0x1FFFFFFF;

	block_map[std::make_pair(pAddr + 4 * b.originalSize - 1, pAddr)] = block_num;
	if (block_link)
	{
		for (int i = 0; i < 2; i++)
		{
			if (b.exitAddress[i] != INVALID_EXIT) 
				links_to.insert(std::pair<u32, int>(b.exitAddress[i], block_num));
		}
			
		LinkBlock(block_num);
		LinkBlockExits(block_num);
	}

#if defined USE_OPROFILE && USE_OPROFILE
	char buf[100];
	sprintf(buf, "EmuCode%x", b.originalAddress);
	const u8* blockStart = blocks[block_num].checkedEntry;
	op_write_native_code(agent, buf, (uint64_t)blockStart, blockStart, b.codeSize);
#endif

#ifdef USE_VTUNE
	sprintf(b.blockName, "EmuCode_0x%08x", b.originalAddress);

	iJIT_Method_Load jmethod = {0};
	jmethod.method_id = iJIT_GetNewMethodID();
	jmethod.class_file_name = "";
	jmethod.source_file_name = __FILE__;
	jmethod.method_load_address = (void*)blocks[block_num].checkedEntry;
	jmethod.method_size = b.codeSize;
	jmethod.line_number_size = 0;
	jmethod.method_name = b.blockName;
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jmethod);
#endif
}

int binary_search(JitBlock blocks[], const u8 *baseoff, int imin, int imax)
{
	while (imin < imax)
	{
		int imid = (imin + imax) / 2;
		if (blocks[imid].normalEntry < baseoff)
			imin = imid + 1;
		else
			imax = imid;
	}
	if ((imax == imin) && (blocks[imin].normalEntry == baseoff))
		return imin;
	else
		return -1;
}

int JitBlockCache::GetBlockNumberFromEmuHackOp(u32 inst) const {
	if (!num_blocks || !MIPS_IS_EMUHACK(inst)) // definitely not a JIT block
		return -1;
	int off = (inst & MIPS_EMUHACK_VALUE_MASK);

	const u8 *baseoff = codeBlock_->GetBasePtr() + off;
	return binary_search(blocks, baseoff, 0, num_blocks-1);
}

u32 JitBlockCache::GetEmuHackOpForBlock(int blockNum) const {
	int off = (int)(blocks[blockNum].normalEntry - codeBlock_->GetBasePtr());
	return (MIPS_EMUHACK_OPCODE | off);
}

int JitBlockCache::GetBlockNumberFromStartAddress(u32 addr)
{
	if (!blocks)
		return -1;		
	u32 inst = Memory::Read_U32(addr);
	int bl = GetBlockNumberFromEmuHackOp(inst);
	if (bl < 0)
		return -1;
	if (blocks[bl].originalAddress != addr)
		return -1;		
	return bl;
}

void JitBlockCache::GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers)
{
	for (int i = 0; i < num_blocks; i++)
		if (blocks[i].ContainsAddress(em_address))
			block_numbers->push_back(i);
}

u32 JitBlockCache::GetOriginalFirstOp(int block_num)
{
	if (block_num >= num_blocks || block_num < 0)
	{
		//PanicAlert("JitBlockCache::GetOriginalFirstOp - block_num = %u is out of range", block_num);
		return block_num;
	}
	return blocks[block_num].originalFirstOpcode;
}

void JitBlockCache::LinkBlockExits(int i)
{
	JitBlock &b = blocks[i];
	if (b.invalid) {
		// This block is dead. Don't relink it.
		return;
	}
	for (int e = 0; e < 2; e++) {
		if (b.exitAddress[e] != INVALID_EXIT && !b.linkStatus[e]) {
			int destinationBlock = GetBlockNumberFromStartAddress(b.exitAddress[e]);
			if (destinationBlock != -1) 	{
#if defined(ARM)
				ARMXEmitter emit(b.exitPtrs[e]);
				emit.B(blocks[destinationBlock].checkedEntry);
				emit.FlushIcache();

#elif defined(_M_IX86) || defined(_M_X64)
				XEmitter emit(b.exitPtrs[e]);
				emit.JMP(blocks[destinationBlock].checkedEntry, true);
#endif
				b.linkStatus[e] = true;
			}
		}
	}
}

using namespace std;

void JitBlockCache::LinkBlock(int i)
{
	LinkBlockExits(i);
	JitBlock &b = blocks[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	// equal_range(b) returns pair<iterator,iterator> representing the range
	// of element with key b
	ppp = links_to.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
		// PanicAlert("Linking block %i to block %i", iter->second, i);
		LinkBlockExits(iter->second);
	}
}

void JitBlockCache::UnlinkBlock(int i)
{
	JitBlock &b = blocks[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	ppp = links_to.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
		JitBlock &sourceBlock = blocks[iter->second];
		for (int e = 0; e < 2; e++)
		{
			if (sourceBlock.exitAddress[e] == b.originalAddress)
				sourceBlock.linkStatus[e] = false;
		}
	}
}

void JitBlockCache::DestroyBlock(int block_num, bool invalidate)
{
	if (block_num < 0 || block_num >= num_blocks) {
		ERROR_LOG(JIT, "DestroyBlock: Invalid block number %d", block_num);
		return;
	}
	JitBlock &b = blocks[block_num];
	if (b.invalid) {
		if (invalidate)
			ERROR_LOG(JIT, "Invalidating invalid block %d", block_num);
		return;
	}
	b.invalid = true;
	if ((int)Memory::ReadUnchecked_U32(b.originalAddress) == GetEmuHackOpForBlock(block_num))
		Memory::WriteUnchecked_U32(b.originalFirstOpcode, b.originalAddress);
	b.normalEntry = 0;

	UnlinkBlock(block_num);


#if defined(ARM)

	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// I hope there's enough space...
	// checkedEntry is the only "linked" entrance so it's enough to overwrite that.
	ARMXEmitter emit((u8 *)b.checkedEntry);
	emit.MOVI2R(R0, b.originalAddress);
	emit.STR(R0, CTXREG, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->dispatcher);
	emit.FlushIcache();

#elif defined(_M_IX86) || defined(_M_X64)

	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// Spurious entrances from previously linked blocks can only come through checkedEntry
	XEmitter emit((u8 *)b.checkedEntry);
	emit.MOV(32, M(&mips->pc), Imm32(b.originalAddress));
	emit.JMP(MIPSComp::jit->Asm().dispatcher, true);
#endif
}

void JitBlockCache::InvalidateICache(u32 address, const u32 length)
{
	// Convert the logical address to a physical address for the block map
	u32 pAddr = address & 0x1FFFFFFF;

	// destroy JIT blocks
	// !! this works correctly under assumption that any two overlapping blocks end at the same address
	std::map<pair<u32,u32>, u32>::iterator it1 = block_map.lower_bound(std::make_pair(pAddr, 0)), it2 = it1;
	while (it2 != block_map.end() && it2->first.second < pAddr + length) {
		DestroyBlock(it2->second, true);
		it2++;
	}
	if (it1 != it2)
		block_map.erase(it1, it2);
}
