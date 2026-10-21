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

#include <cstring>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

struct IRInst;

namespace MIPSComp {

// TODO : Use arena allocators. For now let's just malloc.
class JitBlock {
public:
	JitBlock() {}
	JitBlock(u32 emAddr, u32 origSize, u32 numInstructions)
		: origAddr_(emAddr), origSize_(origSize), numIRInstructions_(numInstructions) {}
	JitBlock(JitBlock &&b) noexcept {
		entry_ = b.entry_;
		hash_ = b.hash_;
		origAddr_ = b.origAddr_;
		origSize_ = b.origSize_;
		origFirstOpcode_ = b.origFirstOpcode_;
		numIRInstructions_ = b.numIRInstructions_;
		b.entry_ = 0;
	}

	~JitBlock() {}

	// What the block shadow holds for the block: its offset into the IR interpreter's arena, or into
	// the native code space. Rises with the block number, until the cache is cleared.
	int GetEntry() const { return entry_; }
	void SetEntry(int entry) { entry_ = entry; }
	int GetNumIRInstructions() const { return numIRInstructions_; }
	MIPSOpcode GetOriginalFirstOp() const { return origFirstOpcode_; }
	bool IsValid() const { return origAddr_ != 0 && origFirstOpcode_.encoding != 0x68FFFFFF; }
	void UpdateHash() {
		hash_ = CalculateHash();
	}
	bool HashMatches() const {
		return origAddr_ && hash_ == CalculateHash();
	}
	bool OverlapsRange(u32 addr, u32 size) const;

	void GetRange(u32 *start, u32 *size) const {
		*start = origAddr_;
		*size = origSize_;
	}
	u32 GetOriginalStart() const {
		return origAddr_;
	}
	u64 GetHash() const {
		return hash_;
	}

	// Enter the block in the space's block shadow, and take it out.
	void Finalize(const Memory::AddressSpace &space);
	void Destroy(const Memory::AddressSpace &space);

	// Only filled in by the IR interpreter, when profiling.
	JitBlockProfileStats profileStats_{};

private:
	u64 CalculateHash() const;

	int entry_ = 0;
	u64 hash_ = 0;
	u32 origAddr_ = 0;
	u32 origSize_ = 0;
	MIPSOpcode origFirstOpcode_ = MIPSOpcode(0x68FFFFFF);
	u32 numIRInstructions_ = 0;
};

// The guest blocks a JIT has compiled: where they came from, and how to find and invalidate them.
// Holds no IR or code, those are the JIT's.
class JitBlockTable {
public:
	~JitBlockTable() {
		Clear();
	}

	// Whose block shadow the blocks go in.
	void SetAddressSpace(const Memory::AddressSpace *space) {
		space_ = space;
	}
	const Memory::AddressSpace &Space() const {
		return *space_;
	}

	void Clear();
	std::vector<int> FindInvalidatedBlockNumbers(u32 address, u32 length);
	void FinalizeBlock(int blockNum);
	int GetNumBlocks() const { return (int)blocks_.size(); }
	int CountValidBlocks() const;
	int AllocateBlock(int emAddr, u32 origSize, u32 numIRInstructions);
	JitBlock *GetBlock(int blockNum) {
		if (blockNum >= 0 && blockNum < (int)blocks_.size()) {
			return &blocks_[blockNum];
		} else {
			return nullptr;
		}
	}
	void RemoveBlockFromPageLookup(int blockNum);
	int FindByEntry(int entry) const;
	bool IsValidBlock(int blockNum) const {
		return blockNum >= 0 && blockNum < (int)blocks_.size() && blocks_[blockNum].IsValid();
	}
	JitBlock *GetBlockUnchecked(int blockNum) {
		return &blocks_[blockNum];
	}
	const JitBlock *GetBlock(int blockNum) const {
		if (blockNum >= 0 && blockNum < (int)blocks_.size()) {
			return &blocks_[blockNum];
		} else {
			return nullptr;
		}
	}

	int FindPreloadBlock(u32 em_address);

	int GetBlockNumberFromStartAddress(u32 em_address) const;

private:
	u32 AddressToPage(u32 addr) const;
	std::vector<JitBlock> blocks_;
	std::unordered_map<u32, std::vector<int>> byPage_;
	const Memory::AddressSpace *space_ = nullptr;
};

class IRJit;

// The debugger's view of an IR JIT's blocks: the MIPS code from the block table, the IR and any host
// code from the JIT.
class IRJitDebugInterface : public JitBlockCacheDebugInterface {
public:
	explicit IRJitDebugInterface(const IRJit &jit) : jit_(jit) {}

	int GetNumBlocks() const override;
	int GetBlockNumberFromStartAddress(u32 em_address) const override;
	JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const override;
	JitBlockMeta GetBlockMeta(int blockNum) const override;
	JitBlockProfileStats GetBlockProfileStats(int blockNum) const override;
	void ComputeStats(BlockCacheStats &bcStats) const override;
	bool IsValidBlock(int blockNum) const override;
	bool SupportsProfiling() const override;

private:
	const IRJit &jit_;
};

// What the IR JITs share: the frontend, the table of compiled blocks, and compiling and invalidating
// them. What a block compiles to, and how it runs, is up to the subclass.
class IRJit : public JitInterface {
public:
	~IRJit();

	void DoState(PointerWrap &p) override;

	const JitOptions &GetJitOptions() { return jo; }

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC

	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override { return &debugInterface_; }

	void ClearCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override;
	void InvalidateCacheExcept(const std::vector<std::pair<u32, u32>> &keep) override;
	void UpdateFCR31() override;
	void GetStats(JitStats *stats) const override;

	void FlushProfile() override;

protected:
	IRJit(MIPSState *mipsState, bool compileToNative);

	virtual void WriteProfileReport() = 0;

	enum class CompileResult {
		OK,
		ARENA_FULL,
		CODE_FULL,
	};
	CompileResult CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes);
	// Makes the allocated block runnable, and sets its entry.
	virtual CompileResult CompileTargetBlock(int block_num, const std::vector<IRInst> &instructions) = 0;
	// For the debugger: the IR the block was compiled from, the size of what it compiled to (for the
	// bloat stats), and its host code if any.
	virtual void GetBlockInstructions(const JitBlock &block, std::vector<IRInst> &instructions) const = 0;
	virtual int GetBlockCodeSize(int block_num) const = 0;
	virtual void GetBlockTargetDisasm(int block_num, std::vector<std::string> &lines) const {}
	// Whether blocks_ has per-block profile stats.
	virtual bool HasBlockProfile() const { return false; }
	virtual void FinalizeTargetBlock(int block_num) {}
	virtual void InvalidateTargetBlock(int block_num) {}
	void InvalidateBlock(int block_num);

	JitOptions jo;

	IRFrontend frontend_;
	JitBlockTable blocks_;
	IRJitDebugInterface debugInterface_{ *this };

	MIPSState *mips_;

	bool compilerEnabled_ = true;
	// Set by --jit-profile. The IR interpreter times every block, the native JITs sample the host PC.
	bool profiling_ = false;

	int irArenaFullClears_ = 0;
	int codeFullClears_ = 0;
	int invalidations_ = 0;
	int invalidatedBlocks_ = 0;

	friend class IRJitDebugInterface;
};

// Runs the IR itself.
class IRInterpreterJit : public IRJit {
public:
	IRInterpreterJit(MIPSState *mipsState);

	void RunLoopUntil(u64 globalticks) override;
	void RunDowncount() override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override {
		return false;
	}
	bool CodeInRange(const u8 *ptr) const override {
		return false;
	}
	const u8 *GetDispatcher() const override { return nullptr; }
	const u8 *GetCrashHandler() const override { return nullptr; }
	const u8 *GetCodeBase() const override { return nullptr; }

	void ClearCache() override;
	void GetStats(JitStats *stats) const override;

protected:
	template <bool profile>
	void RunBlocks();
	template <bool profile>
	void RunBlocksUntilDowncount();
	void WriteProfileReport() override;
	CompileResult CompileTargetBlock(int block_num, const std::vector<IRInst> &instructions) override;
	void GetBlockInstructions(const JitBlock &block, std::vector<IRInst> &instructions) const override;
	int GetBlockCodeSize(int block_num) const override;
	bool HasBlockProfile() const override {
		return profiling_;
	}

private:
	// In IR instructions (8 bytes each).
	static constexpr u32 MAX_ARENA_SIZE = 4 * 1024 * 1024;

	// The blocks' IR. Offsets 0 and 1 are reserved, see RunBlocks.
	std::vector<IRInst> arena_;
};

}	// namespace MIPSComp
