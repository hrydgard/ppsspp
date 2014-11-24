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
#include <unordered_map>
#include <vector>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPS.h"

#if defined(ARM)
#include "Common/ArmEmitter.h"
namespace ArmGen { class ARMXEmitter; }
typedef ArmGen::ARMXCodeBlock NativeCodeBlock;
#elif defined(ARM64)
#include "Common/Arm64Emitter.h"
namespace Arm64Gen { class ARM64XEmitter; }
typedef Arm64Gen::ARM64CodeBlock NativeCodeBlock;
#elif defined(_M_IX86) || defined(_M_X64)
#include "Common/x64Emitter.h"
namespace Gen { class XEmitter; }
typedef Gen::XCodeBlock NativeCodeBlock;
#elif defined(MIPS)
#include "Common/MipsEmitter.h"
namespace MIPSGen { class MIPSEmitter; }
typedef MIPSGen::MIPSCodeBlock NativeCodeBlock;
#else
#include "Common/FakeEmitter.h"
namespace FakeGen { class FakeXEmitter; }
typedef FakeGen::FakeXCodeBlock NativeCodeBlock;
#endif

#if defined(ARM) || defined(ARM64)
const int MAX_JIT_BLOCK_EXITS = 2;
#else
const int MAX_JIT_BLOCK_EXITS = 8;
#endif

struct BlockCacheStats {
	int numBlocks;
	float avgBloat;  // In code bytes, not instructions!
	float minBloat;
	u32 minBloatBlock;
	float maxBloat;
	u32 maxBloatBlock;
	std::map<float, u32> bloatMap;
};

// Define this in order to get VTune profile support for the Jit generated code.
// Add the VTune include/lib directories to the project directories to get this to build.
// #define USE_VTUNE

// We should be careful not to access these block structures during runtime as they are large.
// Fine to mess with them at block compile time though.
struct JitBlock {
	bool ContainsAddress(u32 em_address);

	const u8 *checkedEntry;
	const u8 *normalEntry;

	u8 *exitPtrs[MAX_JIT_BLOCK_EXITS];      // to be able to rewrite the exit jump
	u32 exitAddress[MAX_JIT_BLOCK_EXITS];   // 0xFFFFFFFF == unknown

	u32 originalAddress;
	MIPSOpcode originalFirstOpcode; //to be able to restore
	u16 codeSize;
	u16 originalSize;
	u16 blockNum;

	bool invalid;
	bool linkStatus[MAX_JIT_BLOCK_EXITS];

#ifdef USE_VTUNE
	char blockName[32];
#endif

	// By having a pointer, we avoid a constructor/destructor being generated and dog slow
	// performance in debug.
	std::vector<u32> *proxyFor;

	bool IsPureProxy() const {
		return originalFirstOpcode.encoding == 0x68FF0000;
	}
	void SetPureProxy() {
		// Magic number that won't be a real opcode.
		originalFirstOpcode.encoding = 0x68FF0000;
	}
};

typedef void (*CompiledCode)();

class JitBlockCache {
public:
	JitBlockCache(MIPSState *mips_, NativeCodeBlock *codeBlock);
	~JitBlockCache();

	int AllocateBlock(u32 em_address);
	// When a proxy block is invalidated, the block located at the rootAddress
	// is invalidated too.
	void ProxyBlock(u32 rootAddress, u32 startAddress, u32 size, const u8 *codePtr);
	void FinalizeBlock(int block_num, bool block_link);

	void Clear();
	void Init();
	void Shutdown();
	void Reset();

	bool IsFull() const;
	void ComputeStats(BlockCacheStats &bcStats);

	// Code Cache
	JitBlock *GetBlock(int block_num);

	// Fast way to get a block. Only works on the first source-cpu instruction of a block.
	int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true);

	// slower, but can get numbers from within blocks, not just the first instruction.
	// WARNING! WILL NOT WORK WITH JIT INLINING ENABLED (not yet a feature but will be soon)
	// Returns a list of block numbers - only one block can start at a particular address, but they CAN overlap.
	// This one is slow so should only be used for one-shots from the debugger UI, not for anything during runtime.
	void GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers);
	int GetBlockNumberFromEmuHackOp(MIPSOpcode inst, bool ignoreBad = false) const;

	u32 GetAddressFromBlockPtr(const u8 *ptr) const;

	MIPSOpcode GetOriginalFirstOp(int block_num);

	bool RangeMayHaveEmuHacks(u32 start, u32 end) const;

	// DOES NOT WORK CORRECTLY WITH JIT INLINING
	void InvalidateICache(u32 address, const u32 length);
	void DestroyBlock(int block_num, bool invalidate);

	// No jit operations may be run between these calls.
	// Meant to be used to make memory safe for savestates, memcpy, etc.
	std::vector<u32> SaveAndClearEmuHackOps();
	void RestoreSavedEmuHackOps(std::vector<u32> saved);

	int GetNumBlocks() const { return num_blocks_; }

	static int GetBlockExitSize();

	enum {
		MAX_BLOCK_INSTRUCTIONS = 0x4000,
	};

private:
	void LinkBlockExits(int i);
	void LinkBlock(int i);
	void UnlinkBlock(int i);

	void AddBlockMap(int block_num);
	void RemoveBlockMap(int block_num);

	MIPSOpcode GetEmuHackOpForBlock(int block_num) const;

	MIPSState *mips_;
	NativeCodeBlock *codeBlock_;
	JitBlock *blocks_;
	std::unordered_multimap<u32, int> proxyBlockMap_;

	int num_blocks_;
	std::unordered_multimap<u32, int> links_to_;
	std::map<std::pair<u32,u32>, u32> block_map_; // (end_addr, start_addr) -> number

	enum {
		JITBLOCK_RANGE_SCRATCH = 0,
		JITBLOCK_RANGE_RAMBOTTOM = 1,
		JITBLOCK_RANGE_RAMTOP = 2,
		JITBLOCK_RANGE_COUNT = 3,
	};
	std::pair<u32, u32> blockMemRanges_[3];

	enum {
		MAX_NUM_BLOCKS = 65536*2
	};
};

