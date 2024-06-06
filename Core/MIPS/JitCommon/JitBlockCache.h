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

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>

#include "ppsspp_config.h"
#include "Common/CommonTypes.h"
#include "Common/CodeBlock.h"
#include "Core/MIPS/MIPS.h"

#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)
const int MAX_JIT_BLOCK_EXITS = 4;
#else
const int MAX_JIT_BLOCK_EXITS = 8;
#endif
constexpr bool JIT_USE_COMPILEDHASH = true;

struct BlockCacheStats {
	int numBlocks;
	float avgBloat;  // In code bytes, not instructions!
	float minBloat;
	u32 minBloatBlock;
	float maxBloat;
	u32 maxBloatBlock;
};

enum class DestroyType {
	DESTROY,
	INVALIDATE,
	// Skips jit unlink, since it'll be poisoned anyway.
	CLEAR,
};

// Define this in order to get VTune profile support for the Jit generated code.
// Add the VTune include/lib directories to the project directories to get this to build.
// #define USE_VTUNE

// We should be careful not to access these block structures during runtime as they are large.
// Fine to mess with them at block compile time though.
struct JitBlock {
	bool ContainsAddress(u32 em_address) const;

	const u8 *checkedEntry;  // const, we have to translate to writable.
	const u8 *normalEntry;

	u8 *exitPtrs[MAX_JIT_BLOCK_EXITS];      // to be able to rewrite the exit jump
	u32 exitAddress[MAX_JIT_BLOCK_EXITS];   // 0xFFFFFFFF == unknown

	u32 originalAddress;
	MIPSOpcode originalFirstOpcode; //to be able to restore
	uint64_t compiledHash;
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

struct JitBlockDebugInfo {
	uint32_t originalAddress;
	std::vector<std::string> origDisasm;
	std::vector<std::string> irDisasm;  // if any
	std::vector<std::string> targetDisasm;
};

struct JitBlockMeta {
	bool valid;
	uint32_t addr;
	uint32_t sizeInBytes;
};

struct JitBlockProfileStats {
	int64_t executions;
	int64_t totalNanos;
};

class JitBlockCacheDebugInterface {
public:
	virtual int GetNumBlocks() const = 0;
	virtual int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true) const = 0;
	virtual JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const = 0; // Expensive
	virtual JitBlockMeta GetBlockMeta(int blockNum) const = 0;
	virtual JitBlockProfileStats GetBlockProfileStats(int blockNum) const = 0;
	virtual void ComputeStats(BlockCacheStats &bcStats) const = 0;
	virtual bool IsValidBlock(int blockNum) const = 0;
	virtual bool SupportsProfiling() const { return false; }

	virtual ~JitBlockCacheDebugInterface() {}
};

class JitBlockCache : public JitBlockCacheDebugInterface {
public:
	JitBlockCache(MIPSState *mipsState, CodeBlockCommon *codeBlock);
	~JitBlockCache();

	int AllocateBlock(u32 em_address);
	// When a proxy block is invalidated, the block located at the rootAddress is invalidated too.
	void ProxyBlock(u32 rootAddress, u32 startAddress, u32 size, const u8 *codePtr);
	void FinalizeBlock(int block_num, bool block_link);

	void Clear();
	void Init();
	void Shutdown();
	void Reset();

	bool IsFull() const;
	void ComputeStats(BlockCacheStats &bcStats) const override;

	// Code Cache
	JitBlock *GetBlock(int block_num);
	const JitBlock *GetBlock(int block_num) const;

	// Fast way to get a block. Only works on the first source-cpu instruction of a block.
	int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true) const override;

	// slower, but can get numbers from within blocks, not just the first instruction.
	// WARNING! WILL NOT WORK WITH JIT INLINING ENABLED (not yet a feature but will be soon)
	// Returns a list of block numbers - only one block can start at a particular address, but they CAN overlap.
	// This one is slow so should only be used for one-shots from the debugger UI, not for anything during runtime.
	void GetBlockNumbersFromAddress(u32 em_address, std::vector<int> *block_numbers);
	// Similar to above, but only the first matching address.
	int GetBlockNumberFromAddress(u32 em_address);
	int GetBlockNumberFromEmuHackOp(MIPSOpcode inst, bool ignoreBad = false) const;

	u32 GetAddressFromBlockPtr(const u8 *ptr) const;

	MIPSOpcode GetOriginalFirstOp(int block_num);

	bool RangeMayHaveEmuHacks(u32 start, u32 end) const;

	// DOES NOT WORK CORRECTLY WITH JIT INLINING
	void InvalidateICache(u32 address, const u32 length);
	void InvalidateChangedBlocks();
	void DestroyBlock(int block_num, DestroyType type);

	// No jit operations may be run between these calls.
	// Meant to be used to make memory safe for savestates, memcpy, etc.
	std::vector<u32> SaveAndClearEmuHackOps();
	void RestoreSavedEmuHackOps(const std::vector<u32> &saved);

	int GetNumBlocks() const override { return num_blocks_; }
	bool IsValidBlock(int blockNum) const override { return blockNum >= 0 && blockNum < num_blocks_ && !blocks_[blockNum].invalid; }
	JitBlockMeta GetBlockMeta(int blockNum) const override {
		JitBlockMeta meta{};
		if (IsValidBlock(blockNum)) {
			meta.valid = true;
			meta.addr = blocks_[blockNum].originalAddress;
			meta.sizeInBytes = blocks_[blockNum].originalSize;
		}
		return meta;
	}
	JitBlockProfileStats GetBlockProfileStats(int blockNum) const override {
		return JitBlockProfileStats{};
	}

	static int GetBlockExitSize();

	JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const override;

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

	CodeBlockCommon *codeBlock_;
	JitBlock *blocks_ = nullptr;
	std::unordered_multimap<u32, int> proxyBlockMap_;

	int num_blocks_ = 0;
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

