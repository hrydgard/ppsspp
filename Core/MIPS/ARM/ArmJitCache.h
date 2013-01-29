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

#pragma once

#include <map>
#include <vector>
#include <string>

#include "../MIPSAnalyst.h"
#include "../MIPS.h"
// Define this in order to get VTune profile support for the Jit generated code.
// Add the VTune include/lib directories to the project directories to get this to build.
// #define USE_VTUNE

// emulate CPU with unlimited instruction cache
// the only way to invalidate a region is the "icbi" instruction

#define JIT_ICACHE_SIZE 0x2000000
#define JIT_ICACHE_MASK 0x1ffffff
#define JIT_ICACHEEX_SIZE 0x4000000
#define JIT_ICACHEEX_MASK 0x3ffffff
#define JIT_ICACHE_EXRAM_BIT 0x10000000
#define JIT_ICACHE_VMEM_BIT 0x20000000
// this corresponds to opcode 5 which is invalid in PowerPC
#define JIT_ICACHE_INVALID_BYTE 0x14
#define JIT_ICACHE_INVALID_WORD 0x14141414

#define JIT_OPCODE 0xFFCCCCCC	// yeah this ain't gonna work

struct ArmJitBlock
{
	const u8 *checkedEntry;
	const u8 *normalEntry;

	u8 *exitPtrs[2];		 // to be able to rewrite the exit jum
	u32 exitAddress[2];	// 0xFFFFFFFF == unknown

	u32 originalAddress;
	u32 originalFirstOpcode; //to be able to restore
	u32 codeSize; 
	u32 originalSize;
	int runCount;	// for profiling.
	int blockNum;
	int flags;

	bool invalid;
	bool linkStatus[2];
	bool ContainsAddress(u32 em_address);

#ifdef _WIN32
	// we don't really need to save start and stop
	// TODO (mb2): ticStart and ticStop -> "local var" mean "in block" ... low priority ;)
	u64 ticStart;		// for profiling - time.
	u64 ticStop;		// for profiling - time.
	u64 ticCounter;	// for profiling - time.
#endif

#ifdef USE_VTUNE
	char blockName[32];
#endif
};

typedef void (*CompiledCode)();

class ArmJitBlockCache
{
public:
	ArmJitBlockCache(MIPSState *mips_) :
		mips(mips_), blockCodePointers(0), blocks(0), num_blocks(0),
		MAX_NUM_BLOCKS(0) { }
	~ArmJitBlockCache();
	int AllocateBlock(u32 em_address);
	void FinalizeBlock(int block_num, bool block_link, const u8 *code_ptr);

	void Clear();
	void ClearSafe();
	void Init();
	void Shutdown();
	void Reset();

	bool IsFull() const;

	// Code Cache
	ArmJitBlock *GetBlock(int block_num);
	int GetNumBlocks() const;
	const u8 **GetCodePointers();

	// Fast way to get a block. Only works on the first source-cpu instruction of a block.
	int GetBlockNumberFromStartAddress(u32 em_address);

	// slower, but can get numbers from within blocks, not just the first instruction.
	// WARNING! WILL NOT WORK WITH INLINING ENABLED (not yet a feature but will be soon)
	// Returns a list of block numbers - only one block can start at a particular address, but they CAN overlap.
	// This one is slow so should only be used for one-shots from the debugger UI, not for anything during runtime.
	void GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers);

	u32 GetOriginalFirstOp(int block_num);
	CompiledCode GetCompiledCodeFromBlock(int block_num);

	// DOES NOT WORK CORRECTLY WITH INLINING
	void InvalidateICache(u32 address, const u32 length);
	void DestroyBlock(int block_num, bool invalidate);

	std::string GetCompiledDisassembly(int block_num);

	// Not currently used
	//void DestroyBlocksWithFlag(BlockFlag death_flag);

private:
	MIPSState *mips;
	const u8 **blockCodePointers;
	ArmJitBlock *blocks;
	int num_blocks;
	std::multimap<u32, int> links_to;
	std::map<std::pair<u32,u32>, u32> block_map; // (end_addr, start_addr) -> number

	int MAX_NUM_BLOCKS;

	bool RangeIntersect(int s1, int e1, int s2, int e2) const;
	void LinkBlockExits(int i);
	void LinkBlock(int i);
	void UnlinkBlock(int i);
};
