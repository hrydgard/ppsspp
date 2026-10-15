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
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

struct BlockCacheStats {
	int numBlocks;
	float avgBloat;  // In code bytes, not instructions!
	float minBloat;
	u32 minBloatBlock;
	float maxBloat;
	u32 maxBloatBlock;
};

struct JitBlockDebugInfo {
	uint32_t originalAddress;
	std::vector<std::string> origDisasm;
	std::vector<std::string> irDisasm;  // if any
	std::vector<std::string> targetDisasm;
};

struct JitBlockProfileStats {
	int64_t executions;
	int64_t totalNanos;
};

// Only used for debugging, so keeping it tight is not that important.
struct JitBlockMeta {
	bool valid;
	uint32_t addr;
	uint32_t sizeInBytes;
};

class JitBlockCacheDebugInterface {
public:
	virtual int GetNumBlocks() const = 0;
	virtual int GetBlockNumberFromStartAddress(u32 em_address) const = 0;
	virtual JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const = 0; // Expensive
	virtual JitBlockMeta GetBlockMeta(int blockNum) const = 0;
	virtual JitBlockProfileStats GetBlockProfileStats(int blockNum) const = 0;
	virtual void ComputeStats(BlockCacheStats &bcStats) const = 0;
	virtual bool IsValidBlock(int blockNum) const = 0;
	virtual bool SupportsProfiling() const { return false; }

	virtual ~JitBlockCacheDebugInterface() {}
};
