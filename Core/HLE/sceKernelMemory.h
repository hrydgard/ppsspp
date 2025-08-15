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

#include <vector>
#include <map>

#include "Core/Util/BlockAllocator.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"

enum MemblockType {
	PSP_SMEM_Low = 0,
	PSP_SMEM_High = 1,
	PSP_SMEM_Addr = 2,
	PSP_SMEM_LowAligned = 3,
	PSP_SMEM_HighAligned = 4,
};

extern BlockAllocator userMemory;
extern BlockAllocator kernelMemory;

void __KernelMemoryInit();
void __KernelMemoryDoState(PointerWrap &p);
void __KernelMemoryShutdown();
KernelObject *__KernelMemoryFPLObject();
KernelObject *__KernelMemoryVPLObject();
KernelObject *__KernelMemoryPMBObject();
KernelObject *__KernelTlsplObject();

BlockAllocator *BlockAllocatorFromID(int id);
int BlockAllocatorToID(const BlockAllocator *alloc);
BlockAllocator *BlockAllocatorFromAddr(u32 addr);

struct VplWaitingThread {
	SceUID threadID;
	u32 addrPtr;
	u64 pausedTimeout;

	bool operator ==(const SceUID &otherThreadID) const
	{
		return threadID == otherThreadID;
	}
};

struct SceKernelVplInfo {
	SceSize_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	SceUInt_le attr;
	s32_le poolSize;
	s32_le freeSize;
	s32_le numWaitThreads;
};

struct SceKernelVplBlock {
	PSPPointer<SceKernelVplBlock> next;
	// Includes this info (which is 1 block / 8 bytes.)
	u32_le sizeInBlocks;
};

struct SceKernelVplHeader {
	u32_le startPtr_;
	// TODO: Why twice?  Is there a case it changes?
	u32_le startPtr2_;
	u32_le sentinel_;
	u32_le sizeMinus8_;
	u32_le allocatedInBlocks_;
	PSPPointer<SceKernelVplBlock> nextFreeBlock_;
	SceKernelVplBlock firstBlock_;

	void Init(u32 ptr, u32 size);
	u32 Allocate(u32 size);
	bool Free(u32 ptr);

	u32 FreeSize() const {
		// Size less the header and number of allocated bytes.
		return sizeMinus8_ + 8 - 0x20 - allocatedInBlocks_ * 8;
	}

	bool LinkFreeBlock(PSPPointer<SceKernelVplBlock> b, PSPPointer<SceKernelVplBlock> prev, PSPPointer<SceKernelVplBlock> next);
	void UnlinkFreeBlock(PSPPointer<SceKernelVplBlock> b, PSPPointer<SceKernelVplBlock> prev);
	PSPPointer<SceKernelVplBlock> SplitBlock(PSPPointer<SceKernelVplBlock> b, u32 allocBlocks);
	void Validate();
	void ListBlocks();
	PSPPointer<SceKernelVplBlock> MergeBlocks(PSPPointer<SceKernelVplBlock> first, PSPPointer<SceKernelVplBlock> second);

	u32 FirstBlockPtr() const {
		return startPtr_ + 0x18;
	}

	u32 LastBlockPtr() const {
		return startPtr_ + sizeMinus8_;
	}

	PSPPointer<SceKernelVplBlock> LastBlock() {
		return PSPPointer<SceKernelVplBlock>::Create(LastBlockPtr());
	}

	u32 SentinelPtr() const {
		return startPtr_ + 8;
	}
};


struct VPL : public KernelObject {
	const char *GetName() override { return nv.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "VPL"; }
	static u32 GetMissingErrorCode();
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Vpl; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Vpl; }

	VPL() : alloc(8) {}

	void DoState(PointerWrap &p) override;

	SceKernelVplInfo nv{};
	u32 address = 0;
	std::vector<VplWaitingThread> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, VplWaitingThread> pausedWaits;
	BlockAllocator alloc;
	PSPPointer<SceKernelVplHeader> header{};
};


SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr);
int sceKernelDeleteVpl(SceUID uid);
int sceKernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelAllocateVplCB(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelTryAllocateVpl(SceUID uid, u32 size, u32 addrPtr);
int sceKernelFreeVpl(SceUID uid, u32 addr);
int sceKernelCancelVpl(SceUID uid, u32 numWaitThreadsPtr);
int sceKernelReferVplStatus(SceUID uid, u32 infoPtr);

struct FplWaitingThread {
	SceUID threadID;
	u32 addrPtr;
	u64 pausedTimeout;

	bool operator ==(const SceUID &otherThreadID) const {
		return threadID == otherThreadID;
	}
};

struct NativeFPL {
	u32_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32_le attr;

	s32_le blocksize;
	s32_le numBlocks;
	s32_le numFreeBlocks;
	s32_le numWaitThreads;
};

//FPL - Fixed Length Dynamic Memory Pool - every item has the same length
struct FPL : public KernelObject {
	~FPL() {
		delete[] blocks;
	}
	const char *GetName() override { return nf.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "FPL"; }
	static u32 GetMissingErrorCode();
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Fpl; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Fpl; }

	int FindFreeBlock();
	int AllocateBlock();
	bool FreeBlock(int b);

	void DoState(PointerWrap &p) override;

	NativeFPL nf{};
	bool *blocks = nullptr;
	u32 address = 0;
	int alignedSize = 0;
	int nextBlock = 0;
	std::vector<FplWaitingThread> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, FplWaitingThread> pausedWaits;
};

int sceKernelCreateFpl(const char *name, u32 mpid, u32 attr, u32 blocksize, u32 numBlocks, u32 optPtr);
int sceKernelDeleteFpl(SceUID uid);
int sceKernelAllocateFpl(SceUID uid, u32 blockPtrAddr, u32 timeoutPtr);
int sceKernelAllocateFplCB(SceUID uid, u32 blockPtrAddr, u32 timeoutPtr);
int sceKernelTryAllocateFpl(SceUID uid, u32 blockPtrAddr);
int sceKernelFreeFpl(SceUID uid, u32 blockPtr);
int sceKernelCancelFpl(SceUID uid, u32 numWaitThreadsPtr);
int sceKernelReferFplStatus(SceUID uid, u32 statusPtr);

int sceKernelGetCompiledSdkVersion();

SceUID sceKernelCreateTlspl(const char *name, u32 partitionid, u32 attr, u32 size, u32 count, u32 optionsPtr);
int sceKernelDeleteTlspl(SceUID uid);
int sceKernelGetTlsAddr(SceUID uid);
int sceKernelFreeTlspl(SceUID uid);
int sceKernelReferTlsplStatus(SceUID uid, u32 infoPtr);

void Register_SysMemUserForUser();

int sceKernelAllocPartitionMemory(int partition, const char *name, int type, u32 size, u32 addr);
int sceKernelFreePartitionMemory(SceUID id);
u32 sceKernelGetBlockHeadAddr(SceUID id);
