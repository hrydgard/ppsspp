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

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

namespace MIPSComp {

// TODO : Use arena allocators. For now let's just malloc.
class IRBlock {
public:
	IRBlock() : instr_(nullptr), numInstructions_(0), origAddr_(0), origSize_(0) {}
	IRBlock(u32 emAddr) : instr_(nullptr), numInstructions_(0), origAddr_(emAddr), origSize_(0) {}
	IRBlock(IRBlock &&b) {
		instr_ = b.instr_;
		numInstructions_ = b.numInstructions_;
		origAddr_ = b.origAddr_;
		origSize_ = b.origSize_;
		origFirstOpcode_ = b.origFirstOpcode_;
		hash_ = b.hash_;
		b.instr_ = nullptr;
	}

	~IRBlock() {
		delete[] instr_;
	}

	void SetInstructions(const std::vector<IRInst> &inst) {
		instr_ = new IRInst[inst.size()];
		numInstructions_ = (u16)inst.size();
		if (!inst.empty()) {
			memcpy(instr_, &inst[0], sizeof(IRInst) * inst.size());
		}
	}

	const IRInst *GetInstructions() const { return instr_; }
	int GetNumInstructions() const { return numInstructions_; }
	MIPSOpcode GetOriginalFirstOp() const { return origFirstOpcode_; }
	bool HasOriginalFirstOp() const;
	bool RestoreOriginalFirstOp(int number);
	bool IsValid() const { return origAddr_ != 0 && origFirstOpcode_.encoding != 0x68FFFFFF; }
	void SetOriginalSize(u32 size) {
		origSize_ = size;
	}
	void UpdateHash() {
		hash_ = CalculateHash();
	}
	bool HashMatches() const {
		return origAddr_ && hash_ == CalculateHash();
	}
	bool OverlapsRange(u32 addr, u32 size) const;

	void GetRange(u32 &start, u32 &size) const {
		start = origAddr_;
		size = origSize_;
	}

	void Finalize(int number);
	void Destroy(int number);

private:
	u64 CalculateHash() const;

	IRInst *instr_;
	u16 numInstructions_;
	u32 origAddr_;
	u32 origSize_;
	u64 hash_ = 0;
	MIPSOpcode origFirstOpcode_ = MIPSOpcode(0x68FFFFFF);
};

class IRBlockCache : public JitBlockCacheDebugInterface {
public:
	IRBlockCache() {}
	void Clear();
	void InvalidateICache(u32 address, u32 length);
	void FinalizeBlock(int i, bool preload = false);
	int GetNumBlocks() const override { return (int)blocks_.size(); }
	int AllocateBlock(int emAddr) {
		blocks_.push_back(IRBlock(emAddr));
		return (int)blocks_.size() - 1;
	}
	IRBlock *GetBlock(int i) {
		if (i >= 0 && i < (int)blocks_.size()) {
			return &blocks_[i];
		} else {
			return nullptr;
		}
	}

	int FindPreloadBlock(u32 em_address);

	std::vector<u32> SaveAndClearEmuHackOps();
	void RestoreSavedEmuHackOps(std::vector<u32> saved);

	JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const override;
	void ComputeStats(BlockCacheStats &bcStats) const override;
	int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true) const override;

private:
	u32 AddressToPage(u32 addr) const;

	std::vector<IRBlock> blocks_;
	std::unordered_map<u32, std::vector<int>> byPage_;
};

class IRJit : public JitInterface {
public:
	IRJit(MIPSState *mips);
	virtual ~IRJit();

	void DoState(PointerWrap &p) override;

	const JitOptions &GetJitOptions() { return jo; }

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC
	void CompileFunction(u32 start_address, u32 length) override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	// Not using a regular block cache.
	JitBlockCache *GetBlockCache() override { return nullptr; }
	JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() override { return &blocks_; }
	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

	std::vector<u32> SaveAndClearEmuHackOps() override { return blocks_.SaveAndClearEmuHackOps(); }
	void RestoreSavedEmuHackOps(std::vector<u32> saved) override { blocks_.RestoreSavedEmuHackOps(saved); }

	void ClearCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override;
	void UpdateFCR31() override;

	bool CodeInRange(const u8 *ptr) const override {
		return false;
	}

	const u8 *GetDispatcher() const override { return nullptr; }
	const u8 *GetCrashHandler() const override { return nullptr; }

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

private:
	bool CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload);
	bool ReplaceJalTo(u32 dest);

	JitOptions jo;

	IRFrontend frontend_;
	IRBlockCache blocks_;

	MIPSState *mips_;

	// where to write branch-likely trampolines. not used atm
	// u32 blTrampolines_;
	// int blTrampolineCount_;
};

}	// namespace MIPSComp

