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

#pragma once

#include <map>
#include <vector>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPS.h"


#if defined(ARM)
#include "Common/ArmEmitter.h"
namespace ArmGen { class ARMXEmitter; }
using namespace ArmGen;
typedef ArmGen::ARMXCodeBlock CodeBlock;
#elif defined(_M_IX86) || defined(_M_X64)
#include "Common/x64Emitter.h"
namespace Gen { class XEmitter; }
using namespace Gen;
typedef Gen::XCodeBlock CodeBlock;
#else
#error "Unsupported arch!"
#endif

// Define this in order to get VTune profile support for the Jit generated code.
// Add the VTune include/lib directories to the project directories to get this to build.
// #define USE_VTUNE

struct JitBlock {
	bool ContainsAddress(u32 em_address);
	
	const u8 *checkedEntry;
	const u8 *normalEntry;

	u8 *exitPtrs[2];		 // to be able to rewrite the exit jump
	u32 exitAddress[2];	// 0xFFFFFFFF == unknown

	u32 originalAddress;
	u32 originalFirstOpcode; //to be able to restore
	u16 codeSize; 
	u16 originalSize;
	u16 blockNum;

	bool invalid;
	bool linkStatus[2];

#ifdef USE_VTUNE
	char blockName[32];
#endif
};

typedef void (*CompiledCode)();

class JitBlockCache
{
public:
	JitBlockCache(MIPSState *mips_, CodeBlock *codeBlock);
	~JitBlockCache();

	int AllocateBlock(u32 em_address);
	void FinalizeBlock(int block_num, bool block_link);

	void Clear();
	void Init();
	void Shutdown();
	void Reset();

	bool IsFull() const;

	// Code Cache
	JitBlock *GetBlock(int block_num);

	// Fast way to get a block. Only works on the first source-cpu instruction of a block.
	int GetBlockNumberFromStartAddress(u32 em_address);

	// slower, but can get numbers from within blocks, not just the first instruction.
	// WARNING! WILL NOT WORK WITH JIT INLINING ENABLED (not yet a feature but will be soon)
	// Returns a list of block numbers - only one block can start at a particular address, but they CAN overlap.
	// This one is slow so should only be used for one-shots from the debugger UI, not for anything during runtime.
	void GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers);
	int GetBlockNumberFromEmuHackOp(u32 inst) const;

	u32 GetOriginalFirstOp(int block_num);

	// DOES NOT WORK CORRECTLY WITH JIT INLINING
	void InvalidateICache(u32 address, const u32 length);
	void DestroyBlock(int block_num, bool invalidate);

private:
	void LinkBlockExits(int i);
	void LinkBlock(int i);
	void UnlinkBlock(int i);

	u32 GetEmuHackOpForBlock(int block_num) const;

	MIPSState *mips;
	CodeBlock *codeBlock_;
	JitBlock *blocks;

	int num_blocks;
	std::multimap<u32, int> links_to;
	std::map<std::pair<u32,u32>, u32> block_map; // (end_addr, start_addr) -> number

	enum {
		MAX_NUM_BLOCKS = 65536*2
	};
};

