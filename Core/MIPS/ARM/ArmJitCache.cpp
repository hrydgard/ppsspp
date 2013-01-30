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

#include "Common.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "../../Core.h"
#include "../../MemMap.h"
#include "../../CoreTiming.h"

#include "../MIPS.h"
#include "../MIPSTables.h"
#include "../MIPSAnalyst.h"

#include "ArmEmitter.h"

#include "ArmJitCache.h"
#include "../JitCommon/JitCommon.h"
#include "ArmAsm.h"

#if defined USE_OPROFILE && USE_OPROFILE
#include <opagent.h>

op_agent_t agent;
#endif

#if defined USE_VTUNE
#include <jitprofiling.h>
#pragma comment(lib, "libittnotify.lib")
#pragma comment(lib, "jitprofiling.lib")
#endif

using namespace ArmGen;

#define INVALID_EXIT 0xFFFFFFFF

bool ArmJitBlock::ContainsAddress(u32 em_address)
{
	// WARNING - THIS DOES NOT WORK WITH INLINING ENABLED.
	return (em_address >= originalAddress && em_address < originalAddress + 4 * originalSize);
}

bool ArmJitBlockCache::IsFull() const 
{
	return GetNumBlocks() >= MAX_NUM_BLOCKS - 1;
}

void ArmJitBlockCache::Init()
{
	MAX_NUM_BLOCKS = 65536*2;

#if defined USE_OPROFILE && USE_OPROFILE
	agent = op_open_agent();
#endif
	blocks = new ArmJitBlock[MAX_NUM_BLOCKS];
	blockCodePointers = new const u8*[MAX_NUM_BLOCKS];
	Clear();
}

void ArmJitBlockCache::Shutdown()
{
	delete[] blocks;
	delete[] blockCodePointers;
	blocks = 0;
	blockCodePointers = 0;
	num_blocks = 0;
#if defined USE_OPROFILE && USE_OPROFILE
	op_close_agent(agent);
#endif

#ifdef USE_VTUNE
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_SHUTDOWN, NULL);
#endif
}

ArmJitBlockCache::~ArmJitBlockCache()
{
	Shutdown();
}

// This clears the JIT block cache. It's called from JitCache.cpp when the JIT cache
// is full and when saving and loading states.
void ArmJitBlockCache::Clear()
{
	for (int i = 0; i < num_blocks; i++)
	{
		DestroyBlock(i, false);
	}
	links_to.clear();
	block_map.clear();
	num_blocks = 0;
	memset(blockCodePointers, 0xCC, sizeof(u8*)*MAX_NUM_BLOCKS);
}

void ArmJitBlockCache::ClearSafe()
{
#ifdef JIT_UNLIMITED_ICACHE
	memset(iCache, JIT_ICACHE_INVALID_BYTE, JIT_ICACHE_SIZE);
#endif
}

void ArmJitBlockCache::Reset()
{
	Shutdown();
	Init();
}

ArmJitBlock *ArmJitBlockCache::GetBlock(int no)
{
	return &blocks[no];
}

int ArmJitBlockCache::GetNumBlocks() const
{
	return num_blocks;
}

bool ArmJitBlockCache::RangeIntersect(int s1, int e1, int s2, int e2) const
{
	// check if any endpoint is inside the other range
	if ((s1 >= s2 && s1 <= e2) ||
		(e1 >= s2 && e1 <= e2) ||
		(s2 >= s1 && s2 <= e1) ||
		(e2 >= s1 && e2 <= e1)) 
		return true;
	else
		return false;
}

int ArmJitBlockCache::AllocateBlock(u32 em_address)
{
	ArmJitBlock &b = blocks[num_blocks];
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

void ArmJitBlockCache::FinalizeBlock(int block_num, bool block_link, const u8 *code_ptr)
{
	blockCodePointers[block_num] = code_ptr;
	ArmJitBlock &b = blocks[block_num];

	b.originalFirstOpcode = Memory::Read_Opcode_JIT(b.originalAddress);
	u32 opcode = MIPS_MAKE_EMUHACK(0, block_num);
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
	const u8* blockStart = blockCodePointers[block_num];
	op_write_native_code(agent, buf, (uint64_t)blockStart,
												blockStart, b.codeSize);
#endif

#ifdef USE_VTUNE
	sprintf(b.blockName, "EmuCode_0x%08x", b.originalAddress);

	iJIT_Method_Load jmethod = {0};
	jmethod.method_id = iJIT_GetNewMethodID();
	jmethod.class_file_name = "";
	jmethod.source_file_name = __FILE__;
	jmethod.method_load_address = (void*)blockCodePointers[block_num];
	jmethod.method_size = b.codeSize;
	jmethod.line_number_size = 0;
	jmethod.method_name = b.blockName;
	iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jmethod);
#endif
}

const u8 **ArmJitBlockCache::GetCodePointers()
{
	return blockCodePointers;
}

int ArmJitBlockCache::GetBlockNumberFromStartAddress(u32 addr)
{
	if (!blocks)
		return -1;		
	u32 inst = Memory::Read_U32(addr);
	if (!MIPS_IS_EMUHACK(inst)) // definitely not a JIT block
		return -1;
	int bl = (inst & MIPS_EMUHACK_VALUE_MASK);
	if (bl >= num_blocks)
		return -1;
	if (blocks[bl].originalAddress != addr)
		return -1;		
	return bl;
}

void ArmJitBlockCache::GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers)
{
	for (int i = 0; i < num_blocks; i++)
		if (blocks[i].ContainsAddress(em_address))
			block_numbers->push_back(i);
}

u32 ArmJitBlockCache::GetOriginalFirstOp(int block_num)
{
	if (block_num >= num_blocks)
	{
		//PanicAlert("JitBlockCache::GetOriginalFirstOp - block_num = %u is out of range", block_num);
		return block_num;
	}
	return blocks[block_num].originalFirstOpcode;
}

CompiledCode ArmJitBlockCache::GetCompiledCodeFromBlock(int block_num)
{		
	return (CompiledCode)blockCodePointers[block_num];
}

std::string ArmJitBlockCache::GetCompiledDisassembly(int block_num)
{
	/*
	std::string buf;
	const u8 *ptr = blockCodePointers[block_num];

	while (ptr < blockCodePointers[block_num] + blocks[block_num].codeSize)
	{
		int len;
		buf += std::string(disasmx86((unsigned char*)ptr, 0, &len)) + "\n";
		ptr += len;
	}*/
	return "No ARM disassembler";
}


//Make sure to have as many blocks as possible compiled before calling this
//It's O(1), so it's fast :)
void ArmJitBlockCache::LinkBlockExits(int i)
{
	ArmJitBlock &b = blocks[i];
	if (b.invalid)
	{
		// This block is dead. Don't relink it.
		return;
	}
	for (int e = 0; e < 2; e++)
	{
		if (b.exitAddress[e] != INVALID_EXIT && !b.linkStatus[e])
		{
			int destinationBlock = GetBlockNumberFromStartAddress(b.exitAddress[e]);
			if (destinationBlock != -1)
			{
				ARMXEmitter emit(b.exitPtrs[e]);
				emit.B(blocks[destinationBlock].checkedEntry);
				emit.FlushIcache();
				b.linkStatus[e] = true;
			}
		}
	}
}

using namespace std;

void ArmJitBlockCache::LinkBlock(int i)
{
	LinkBlockExits(i);
	ArmJitBlock &b = blocks[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	// equal_range(b) returns pair<iterator,iterator> representing the range
	// of element with key b
	ppp = links_to.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
		// PanicAlert("Linking block %i to block %i", iter2->second, i);
		LinkBlockExits(iter->second);
	}
}

void ArmJitBlockCache::UnlinkBlock(int i)
{
	ArmJitBlock &b = blocks[i];
	pair<multimap<u32, int>::iterator, multimap<u32, int>::iterator> ppp;
	ppp = links_to.equal_range(b.originalAddress);
	if (ppp.first == ppp.second)
		return;
	for (multimap<u32, int>::iterator iter = ppp.first; iter != ppp.second; ++iter) {
		ArmJitBlock &sourceBlock = blocks[iter->second];
		for (int e = 0; e < 2; e++)
		{
			if (sourceBlock.exitAddress[e] == b.originalAddress)
				sourceBlock.linkStatus[e] = false;
		}
	}
}

void ArmJitBlockCache::DestroyBlock(int block_num, bool invalidate)
{
	if (block_num < 0 || block_num >= num_blocks)
	{
		ERROR_LOG(JIT, "DestroyBlock: Invalid block number %d", block_num);
		return;
	}
	ArmJitBlock &b = blocks[block_num];
	if (b.invalid)
	{
		if (invalidate)
			ERROR_LOG(JIT, "Invalidating invalid block %d", block_num);
		return;
	}
	b.invalid = true;
	if ((int)Memory::ReadUnchecked_U32(b.originalAddress) == (MIPS_EMUHACK_OPCODE | block_num))
		Memory::WriteUnchecked_U32(b.originalFirstOpcode, b.originalAddress);

	UnlinkBlock(block_num);

	blockCodePointers[block_num] = 0;
	// Send anyone who tries to run this block back to the dispatcher.
	// Not entirely ideal, but .. pretty good.
	// I hope there's enough space...
	// checkedEntry is the only "linked" entrance so it's enough to overwrite that.
	ARMXEmitter emit((u8 *)b.checkedEntry);
	emit.MOVI2R(R0, b.originalAddress);
	emit.STR(R10, R0, offsetof(MIPSState, pc));
	emit.B(MIPSComp::jit->dispatcher);
	emit.FlushIcache();
}

void ArmJitBlockCache::InvalidateICache(u32 address, const u32 length)
{
	u32 pAddr = address & 0x3FFFFFFF;

	// destroy JIT blocks
	// !! this works correctly under assumption that any two overlapping blocks end at the same address
	std::map<pair<u32,u32>, u32>::iterator it1 = block_map.lower_bound(std::make_pair(pAddr, 0)), it2 = it1;
	while (it2 != block_map.end() && it2->first.second < pAddr + length)
	{
		DestroyBlock(it2->second, true);
		it2++;
	}
	if (it1 != it2)
	{
		block_map.erase(it1, it2);
	}
}
