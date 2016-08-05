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
	IRBlock() : instr_(nullptr), const_(nullptr), numInstructions_(0), numConstants_(0), origAddr_(0), origSize_(0) {}
	IRBlock(u32 emAddr) : instr_(nullptr), const_(nullptr), numInstructions_(0), numConstants_(0), origAddr_(emAddr), origSize_(0) {}
	IRBlock(IRBlock &&b) {
		instr_ = b.instr_;
		const_ = b.const_;
		numInstructions_ = b.numInstructions_;
		numConstants_ = b.numConstants_;
		origAddr_ = b.origAddr_;
		origSize_ = b.origSize_;
		origFirstOpcode_ = b.origFirstOpcode_;
		b.instr_ = nullptr;
		b.const_ = nullptr;
	}

#ifdef __SYMBIAN32__
	IRBlock(const IRBlock &b) {
		*this = b;
	}

	IRBlock &operator=(const IRBlock &b) {
		// No std::move on Symbian...  But let's try not to use elsewhere.
		numInstructions_ = b.numInstructions_;
		numConstants_ = b.numConstants_;
		instr_ = new IRInst[numInstructions_];
		if (numInstructions_) {
			memcpy(instr_, b.instr_, sizeof(IRInst) * numInstructions_);
		}
		const_ = new u32[numConstants_];
		if (numConstants_) {
			memcpy(const_, b.const_, sizeof(u32) * numConstants_);
		}
		origAddr_ = b.origAddr_;
		origSize_ = b.origSize_;
		origFirstOpcode_ = b.origFirstOpcode_;

		return *this;
	}
#endif

	~IRBlock() {
		delete[] instr_;
		delete[] const_;
	}

	void SetInstructions(const std::vector<IRInst> &inst, const std::vector<u32> &constants) {
		instr_ = new IRInst[inst.size()];
		numInstructions_ = (u16)inst.size();
		if (!inst.empty()) {
			memcpy(instr_, &inst[0], sizeof(IRInst) * inst.size());
		}
		const_ = new u32[constants.size()];
		numConstants_ = (u16)constants.size();
		if (!constants.empty()) {
			memcpy(const_, &constants[0], sizeof(u32) * constants.size());
		}
	}

	const IRInst *GetInstructions() const { return instr_; }
	const u32 *GetConstants() const { return const_; }
	int GetNumInstructions() const { return numInstructions_; }
	MIPSOpcode GetOriginalFirstOp() const { return origFirstOpcode_; }
	bool HasOriginalFirstOp();
	bool RestoreOriginalFirstOp(int number);
	bool IsValid() const { return origAddr_ != 0; }
	void SetOriginalSize(u32 size) {
		origSize_ = size;
	}
	bool OverlapsRange(u32 addr, u32 size);

	void Finalize(int number);
	void Destroy(int number);

private:
	IRInst *instr_;
	u32 *const_;
	u16 numInstructions_;
	u16 numConstants_;
	u32 origAddr_;
	u32 origSize_;
	MIPSOpcode origFirstOpcode_;
};

class IRBlockCache {
public:
	IRBlockCache() : size_(0) {}
	void Clear();
	void InvalidateICache(u32 address, u32 length);
	int GetNumBlocks() const { return (int)blocks_.size(); }
	int AllocateBlock(int emAddr) {
		blocks_.push_back(IRBlock(emAddr));
		size_ = (int)blocks_.size();
		return (int)blocks_.size() - 1;
	}
	IRBlock *GetBlock(int i) {
		if (i >= 0 && i < size_) {
			return &blocks_[i];
		} else {
			return nullptr;
		}
	}

	std::vector<u32> SaveAndClearEmuHackOps();
	void RestoreSavedEmuHackOps(std::vector<u32> saved);

private:
	int size_;  // Hm, is this a cache for speed in debug mode, or what?
	std::vector<IRBlock> blocks_;
};

class IRJit : public JitInterface {
public:
	IRJit(MIPSState *mips);
	virtual ~IRJit();

	void DoState(PointerWrap &p) override;
	void DoDummyState(PointerWrap &p) override;

	const JitOptions &GetJitOptions() { return jo; }

	void RunLoopUntil(u64 globalticks) override;

	void Compile(u32 em_address) override;	// Compiles a block at current MIPS PC

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	// Not using a regular block cache.
	JitBlockCache *GetBlockCache() override { return nullptr; }
	MIPSOpcode GetOriginalOp(MIPSOpcode op) override;

	std::vector<u32> SaveAndClearEmuHackOps() override { return blocks_.SaveAndClearEmuHackOps(); }
	void RestoreSavedEmuHackOps(std::vector<u32> saved) override { blocks_.RestoreSavedEmuHackOps(saved); }

	void ClearCache() override;
	void InvalidateCache() override;
	void InvalidateCacheAt(u32 em_address, int length = 4) override;

	const u8 *GetDispatcher() const override { return nullptr; }

	void LinkBlock(u8 *exitPoint, const u8 *checkedEntry) override;
	void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) override;

private:
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

