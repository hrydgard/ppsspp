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

#include <algorithm>
#include <string>
#include <vector>
#include <map>

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemMap.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/KernelWaitHelpers.h"

const int TLSPL_NUM_INDEXES = 16;

//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
BlockAllocator userMemory(256);
BlockAllocator kernelMemory(256);

static int vplWaitTimer = -1;
static int fplWaitTimer = -1;
static bool tlsplUsedIndexes[TLSPL_NUM_INDEXES];
// STATE END
//////////////////////////////////////////////////////////////////////////

#define SCE_KERNEL_HASCOMPILEDSDKVERSION 0x1000
#define SCE_KERNEL_HASCOMPILERVERSION    0x2000

int flags_ = 0;
int sdkVersion_;
int compilerVersion_;

struct FplWaitingThread
{
	SceUID threadID;
	u32 addrPtr;
	u64 pausedTimeout;

	bool operator ==(const SceUID &otherThreadID) const
	{
		return threadID == otherThreadID;
	}
};

struct NativeFPL
{
	u32_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	u32_le attr;

	s32_le blocksize;
	s32_le numBlocks;
	s32_le numFreeBlocks;
	s32_le numWaitThreads;
};

//FPL - Fixed Length Dynamic Memory Pool - every item has the same length
struct FPL : public KernelObject
{
	FPL() : blocks(NULL), nextBlock(0) {}
	~FPL() {
		if (blocks != NULL) {
			delete [] blocks;
		}
	}
	const char *GetName() {return nf.name;}
	const char *GetTypeName() {return "FPL";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_FPLID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Fpl; }
	int GetIDType() const { return SCE_KERNEL_TMID_Fpl; }

	int findFreeBlock() {
		for (int i = 0; i < nf.numBlocks; i++) {
			int b = nextBlock++ % nf.numBlocks;
			if (!blocks[b]) {
				return b;
			}
		}
		return -1;
	}

	int allocateBlock() {
		int block = findFreeBlock();
		if (block >= 0)
			blocks[block] = true;
		return block;
	}
	
	bool freeBlock(int b) {
		if (blocks[b]) {
			blocks[b] = false;
			return true;
		}
		return false;
	}

	virtual void DoState(PointerWrap &p)
	{
		auto s = p.Section("FPL", 1);
		if (!s)
			return;

		p.Do(nf);
		if (p.mode == p.MODE_READ)
			blocks = new bool[nf.numBlocks];
		p.DoArray(blocks, nf.numBlocks);
		p.Do(address);
		p.Do(alignedSize);
		p.Do(nextBlock);
		FplWaitingThread dv = {0};
		p.Do(waitingThreads, dv);
		p.Do(pausedWaits);
	}

	NativeFPL nf;
	bool *blocks;
	u32 address;
	int alignedSize;
	int nextBlock;
	std::vector<FplWaitingThread> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, FplWaitingThread> pausedWaits;
};

struct VplWaitingThread
{
	SceUID threadID;
	u32 addrPtr;
	u64 pausedTimeout;

	bool operator ==(const SceUID &otherThreadID) const
	{
		return threadID == otherThreadID;
	}
};

struct SceKernelVplInfo
{
	SceSize_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	SceUInt_le attr;
	s32_le poolSize;
	s32_le freeSize;
	s32_le numWaitThreads;
};

struct SceKernelVplBlock
{
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

	void Init(u32 ptr, u32 size) {
		startPtr_ = ptr;
		startPtr2_ = ptr;
		sentinel_ = ptr + 7;
		sizeMinus8_ = size - 8;
		allocatedInBlocks_ = 0;
		nextFreeBlock_ = FirstBlockPtr();

		firstBlock_.next = LastBlockPtr();
		// Includes its own header, which is one block.
		firstBlock_.sizeInBlocks = (size - 0x28) / 8 + 1;

		auto lastBlock = LastBlock();
		lastBlock->next = FirstBlockPtr();
		lastBlock->sizeInBlocks = 0;
	}

	u32 Allocate(u32 size) {
		u32 allocBlocks = ((size + 7) / 8) + 1;
		auto prev = nextFreeBlock_;
		do {
			auto b = prev->next;
			if (b->sizeInBlocks > allocBlocks) {
				if (nextFreeBlock_ == b) {
					nextFreeBlock_ = prev;
				}
				prev = b;
				b = SplitBlock(b, allocBlocks);
			}

			if (b->sizeInBlocks == allocBlocks) {
				UnlinkFreeBlock(b, prev);
				return b.ptr + 8;
			}

			prev = b;
		} while (prev.IsValid() && prev != nextFreeBlock_);

		return (u32)-1;
	}

	bool Free(u32 ptr) {
		auto b = PSPPointer<SceKernelVplBlock>::Create(ptr - 8);
		// Is it even in the right range?  Can't be the last block, which is always 0.
		if (!b.IsValid() || ptr < FirstBlockPtr() || ptr >= LastBlockPtr()) {
			return false;
		}
		// Great, let's check if it matches our magic.
		if (b->next.ptr != SentinelPtr() || b->sizeInBlocks > allocatedInBlocks_) {
			return false;
		}

		auto prev = LastBlock();
		do {
			auto next = prev->next;
			// Already free.
			if (next == b) {
				return false;
			} else if (next > b) {
				LinkFreeBlock(b, prev, next);
				return true;
			}

			prev = next;
		} while (prev.IsValid() && prev != LastBlock());

		// TODO: Log?
		return false;
	}

	u32 FreeSize() {
		// Size less the header and number of allocated bytes.
		return sizeMinus8_ + 8 - 0x20 - allocatedInBlocks_ * 8;
	}

	bool LinkFreeBlock(PSPPointer<SceKernelVplBlock> b, PSPPointer<SceKernelVplBlock> prev, PSPPointer<SceKernelVplBlock> next) {
		allocatedInBlocks_ -= b->sizeInBlocks;
		nextFreeBlock_ = prev;

		// Make sure we don't consider it free later by erasing the magic.
		b->next = next.ptr;
		const auto afterB = b + b->sizeInBlocks;
		if (afterB == next && next->sizeInBlocks != 0) {
			b = MergeBlocks(b, next);
		}

		const auto afterPrev = prev + prev->sizeInBlocks;
		if (afterPrev == b) {
			b = MergeBlocks(prev, b);
		} else {
			prev->next = b.ptr;
		}

		return true;
	}

	void UnlinkFreeBlock(PSPPointer<SceKernelVplBlock> b, PSPPointer<SceKernelVplBlock> prev) {
		allocatedInBlocks_ += b->sizeInBlocks;
		prev->next = b->next;
		if (nextFreeBlock_ == b) {
			nextFreeBlock_ = prev;
		}
		b->next = SentinelPtr();
	}

	PSPPointer<SceKernelVplBlock> SplitBlock(PSPPointer<SceKernelVplBlock> b, u32 allocBlocks) {
		u32 prev = b->next.ptr;
		b->sizeInBlocks -= allocBlocks;
		b->next = b + b->sizeInBlocks;

		b += b->sizeInBlocks;
		b->sizeInBlocks = allocBlocks;
		b->next = prev;

		return b;
	}

	inline void Validate() {
		auto lastBlock = LastBlock();
		_dbg_assert_msg_(SCEKERNEL, nextFreeBlock_->next.ptr != SentinelPtr(), "Next free block should not be allocated.");
		_dbg_assert_msg_(SCEKERNEL, nextFreeBlock_->next.ptr != sentinel_, "Next free block should not point to sentinel.");
		_dbg_assert_msg_(SCEKERNEL, lastBlock->sizeInBlocks == 0, "Last block should have size of 0.");
		_dbg_assert_msg_(SCEKERNEL, lastBlock->next.ptr != SentinelPtr(), "Last block should not be allocated.");
		_dbg_assert_msg_(SCEKERNEL, lastBlock->next.ptr != sentinel_, "Last block should not point to sentinel.");

		auto b = PSPPointer<SceKernelVplBlock>::Create(FirstBlockPtr());
		bool sawFirstFree = false;
		while (b.ptr < lastBlock.ptr) {
			bool isFree = b->next.ptr != SentinelPtr();
			if (isFree) {
				if (!sawFirstFree) {
					_dbg_assert_msg_(SCEKERNEL, lastBlock->next.ptr == b.ptr, "Last block should point to first free block.");
					sawFirstFree = true;
				}
				_dbg_assert_msg_(SCEKERNEL, b->next.ptr != SentinelPtr(), "Free blocks should only point to other free blocks.");
				_dbg_assert_msg_(SCEKERNEL, b->next.ptr > b.ptr, "Free blocks should be in order.");
				_dbg_assert_msg_(SCEKERNEL, b + b->sizeInBlocks < b->next || b->next.ptr == lastBlock.ptr, "Two free blocks should not be next to each other.");
			} else {
				_dbg_assert_msg_(SCEKERNEL, b->next.ptr == SentinelPtr(), "Allocated blocks should point to the sentinel.");
			}
			_dbg_assert_msg_(SCEKERNEL, b->sizeInBlocks != 0, "Only the last block should have a size of 0.");
			b += b->sizeInBlocks;
		}
		if (!sawFirstFree) {
			_dbg_assert_msg_(SCEKERNEL, lastBlock->next.ptr == lastBlock.ptr, "Last block should point to itself when full.");
		}
		_dbg_assert_msg_(SCEKERNEL, b.ptr == lastBlock.ptr, "Blocks should not extend outside vpl.");
	}

	void ListBlocks() {
		auto b = PSPPointer<SceKernelVplBlock>::Create(FirstBlockPtr());
		auto lastBlock = LastBlock();
		while (b.ptr < lastBlock.ptr) {
			bool isFree = b->next.ptr != SentinelPtr();
			if (nextFreeBlock_ == b && isFree) {
				NOTICE_LOG(HLE, "NEXT:  %x -> %x (size %x)", b.ptr - startPtr_, b->next.ptr - startPtr_, b->sizeInBlocks * 8);
			} else if (isFree) {
				NOTICE_LOG(HLE, "FREE:  %x -> %x (size %x)", b.ptr - startPtr_, b->next.ptr - startPtr_, b->sizeInBlocks * 8);
			} else {
				NOTICE_LOG(HLE, "BLOCK: %x (size %x)", b.ptr - startPtr_, b->sizeInBlocks * 8);
			}
			b += b->sizeInBlocks;
		}
		NOTICE_LOG(HLE, "LAST:  %x -> %x (size %x)", lastBlock.ptr - startPtr_, lastBlock->next.ptr - startPtr_, lastBlock->sizeInBlocks * 8);
	}

	PSPPointer<SceKernelVplBlock> MergeBlocks(PSPPointer<SceKernelVplBlock> first, PSPPointer<SceKernelVplBlock> second) {
		first->sizeInBlocks += second->sizeInBlocks;
		first->next = second->next;
		return first;
	}

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

struct VPL : public KernelObject
{
	const char *GetName() {return nv.name;}
	const char *GetTypeName() {return "VPL";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VPLID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Vpl; }
	int GetIDType() const { return SCE_KERNEL_TMID_Vpl; }

	VPL() : alloc(8) {
		header = 0;
	}

	virtual void DoState(PointerWrap &p) {
		auto s = p.Section("VPL", 1, 2);
		if (!s) {
			return;
		}

		p.Do(nv);
		p.Do(address);
		VplWaitingThread dv = {0};
		p.Do(waitingThreads, dv);
		alloc.DoState(p);
		p.Do(pausedWaits);

		if (s >= 2) {
			p.Do(header);
		}
	}

	SceKernelVplInfo nv;
	u32 address;
	std::vector<VplWaitingThread> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, VplWaitingThread> pausedWaits;
	BlockAllocator alloc;
	PSPPointer<SceKernelVplHeader> header;
};

void __KernelVplTimeout(u64 userdata, int cyclesLate);
void __KernelFplTimeout(u64 userdata, int cyclesLate);

void __KernelVplBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelVplEndCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelFplBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelFplEndCallback(SceUID threadID, SceUID prevCallbackId);

void __KernelMemoryInit()
{
	kernelMemory.Init(PSP_GetKernelMemoryBase(), PSP_GetKernelMemoryEnd()-PSP_GetKernelMemoryBase());
	userMemory.Init(PSP_GetUserMemoryBase(), PSP_GetUserMemoryEnd()-PSP_GetUserMemoryBase());
	INFO_LOG(SCEKERNEL, "Kernel and user memory pools initialized");

	vplWaitTimer = CoreTiming::RegisterEvent("VplTimeout", __KernelVplTimeout);
	fplWaitTimer = CoreTiming::RegisterEvent("FplTimeout", __KernelFplTimeout);

	flags_ = 0;
	sdkVersion_ = 0;
	compilerVersion_ = 0;
	memset(tlsplUsedIndexes, 0, sizeof(tlsplUsedIndexes));

	__KernelRegisterWaitTypeFuncs(WAITTYPE_VPL, __KernelVplBeginCallback, __KernelVplEndCallback);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_FPL, __KernelFplBeginCallback, __KernelFplEndCallback);
}

void __KernelMemoryDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelMemory", 1);
	if (!s)
		return;

	kernelMemory.DoState(p);
	userMemory.DoState(p);

	p.Do(vplWaitTimer);
	CoreTiming::RestoreRegisterEvent(vplWaitTimer, "VplTimeout", __KernelVplTimeout);
	p.Do(fplWaitTimer);
	CoreTiming::RestoreRegisterEvent(fplWaitTimer, "FplTimeout", __KernelFplTimeout);
	p.Do(flags_);
	p.Do(sdkVersion_);
	p.Do(compilerVersion_);
	p.DoArray(tlsplUsedIndexes, ARRAY_SIZE(tlsplUsedIndexes));
}

void __KernelMemoryShutdown()
{
#ifdef _DEBUG
	INFO_LOG(SCEKERNEL,"Shutting down user memory pool: ");
	userMemory.ListBlocks();
#endif
	userMemory.Shutdown();
#ifdef _DEBUG
	INFO_LOG(SCEKERNEL,"Shutting down \"kernel\" memory pool: ");
	kernelMemory.ListBlocks();
#endif
	kernelMemory.Shutdown();
}

enum SceKernelFplAttr
{
	PSP_FPL_ATTR_FIFO     = 0x0000,
	PSP_FPL_ATTR_PRIORITY = 0x0100,
	PSP_FPL_ATTR_HIGHMEM  = 0x4000,
	PSP_FPL_ATTR_KNOWN    = PSP_FPL_ATTR_FIFO | PSP_FPL_ATTR_PRIORITY | PSP_FPL_ATTR_HIGHMEM,
};

bool __KernelUnlockFplForThread(FPL *fpl, FplWaitingThread &threadInfo, u32 &error, int result, bool &wokeThreads)
{
	const SceUID threadID = threadInfo.threadID;
	if (!HLEKernel::VerifyWait(threadID, WAITTYPE_FPL, fpl->GetUID()))
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0)
		{
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, threadInfo.addrPtr);
		}
		else
			return false;
	}

	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0 && fplWaitTimer != -1)
	{
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(fplWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
}

void __KernelFplBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitBeginCallback<FPL, WAITTYPE_FPL, FplWaitingThread>(threadID, prevCallbackId, fplWaitTimer);
	if (result == HLEKernel::WAIT_CB_SUCCESS)
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateFplCB: Suspending fpl wait for callback")
	else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
		ERROR_LOG_REPORT(SCEKERNEL, "sceKernelAllocateFplCB: wait not found to pause for callback")
	else
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelAllocateFplCB: beginning callback with bad wait id?");
}

void __KernelFplEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitEndCallback<FPL, WAITTYPE_FPL, FplWaitingThread>(threadID, prevCallbackId, fplWaitTimer, __KernelUnlockFplForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(SCEKERNEL, "sceKernelReceiveMbxCB: Resuming mbx wait from callback");
}

bool __FplThreadSortPriority(FplWaitingThread thread1, FplWaitingThread thread2)
{
	return __KernelThreadSortPriority(thread1.threadID, thread2.threadID);
}

bool __KernelClearFplThreads(FPL *fpl, int reason)
{
	u32 error;
	bool wokeThreads = false;
	for (auto iter = fpl->waitingThreads.begin(), end = fpl->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockFplForThread(fpl, *iter, error, reason, wokeThreads);
	fpl->waitingThreads.clear();

	return wokeThreads;
}

void __KernelSortFplThreads(FPL *fpl)
{
	// Remove any that are no longer waiting.
	SceUID uid = fpl->GetUID();
	HLEKernel::CleanupWaitingThreads(WAITTYPE_FPL, uid, fpl->waitingThreads);

	if ((fpl->nf.attr & PSP_FPL_ATTR_PRIORITY) != 0)
		std::stable_sort(fpl->waitingThreads.begin(), fpl->waitingThreads.end(), __FplThreadSortPriority);
}

int sceKernelCreateFpl(const char *name, u32 mpid, u32 attr, u32 blockSize, u32 numBlocks, u32 optPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid name", SCE_KERNEL_ERROR_NO_MEMORY);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}
	if (mpid < 1 || mpid > 9 || mpid == 7)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, mpid);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (mpid != 2 && mpid != 6)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_PERM, mpid);
		return SCE_KERNEL_ERROR_ILLEGAL_PERM;
	}
	if (((attr & ~PSP_FPL_ATTR_KNOWN) & ~0xFF) != 0)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}
	// There's probably a simpler way to get this same basic formula...
	// This is based on results from a PSP.
	bool illegalMemSize = blockSize == 0 || numBlocks == 0;
	if (!illegalMemSize && (u64) blockSize > ((0x100000000ULL / (u64) numBlocks) - 4ULL))
		illegalMemSize = true;
	if (!illegalMemSize && (u64) numBlocks >= 0x100000000ULL / (((u64) blockSize + 3ULL) & ~3ULL))
		illegalMemSize = true;
	if (illegalMemSize)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid blockSize/count", SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
	}

	int alignment = 4;
	if (optPtr != 0)
	{
		u32 size = Memory::Read_U32(optPtr);
		if (size > 8)
			WARN_LOG_REPORT(SCEKERNEL, "sceKernelCreateFpl(): unsupported extra options, size = %d", size);
		if (size >= 4)
			alignment = Memory::Read_U32(optPtr + 4);
		// Must be a power of 2 to be valid.
		if ((alignment & (alignment - 1)) != 0)
		{
			WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateFpl(): invalid alignment %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, alignment);
			return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
		}
	}

	if (alignment < 4)
		alignment = 4;

	int alignedSize = ((int)blockSize + alignment - 1) & ~(alignment - 1);
	u32 totalSize = alignedSize * numBlocks;
	bool atEnd = (attr & PSP_FPL_ATTR_HIGHMEM) != 0;
	u32 address = userMemory.Alloc(totalSize, atEnd, "FPL");
	if (address == (u32)-1)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelCreateFpl(\"%s\", partition=%i, attr=%08x, bsize=%i, nb=%i) FAILED - out of ram", 
			name, mpid, attr, blockSize, numBlocks);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	FPL *fpl = new FPL;
	SceUID id = kernelObjects.Create(fpl);

	strncpy(fpl->nf.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	fpl->nf.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	fpl->nf.attr = attr;
	fpl->nf.size = sizeof(fpl->nf);
	fpl->nf.blocksize = blockSize;
	fpl->nf.numBlocks = numBlocks;
	fpl->nf.numFreeBlocks = numBlocks;
	fpl->nf.numWaitThreads = 0;

	fpl->blocks = new bool[fpl->nf.numBlocks];
	memset(fpl->blocks, 0, fpl->nf.numBlocks * sizeof(bool));
	fpl->address = address;
	fpl->alignedSize = alignedSize;

	DEBUG_LOG(SCEKERNEL, "%i=sceKernelCreateFpl(\"%s\", partition=%i, attr=%08x, bsize=%i, nb=%i)", 
		id, name, mpid, attr, blockSize, numBlocks);

	return id;
}

int sceKernelDeleteFpl(SceUID uid)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelDeleteFpl(%i)", uid);

		bool wokeThreads = __KernelClearFplThreads(fpl, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("fpl deleted");

		userMemory.Free(fpl->address);
		return kernelObjects.Destroy<FPL>(uid);
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelDeleteFpl(%i): invalid fpl", uid);
		return error;
	}
}

void __KernelFplTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID) userdata;
	HLEKernel::WaitExecTimeout<FPL, WAITTYPE_FPL>(threadID);
}

void __KernelSetFplTimeout(u32 timeoutPtr)
{
	if (timeoutPtr == 0 || fplWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// TODO: test for fpls.
	// This happens to be how the hardware seems to time things.
	if (micro <= 5)
		micro = 10;
	// Yes, this 7 is reproducible.  6 is (a lot) longer than 7.
	else if (micro == 7)
		micro = 15;
	else if (micro <= 215)
		micro = 250;

	CoreTiming::ScheduleEvent(usToCycles(micro), fplWaitTimer, __KernelGetCurThread());
}

int sceKernelAllocateFpl(SceUID uid, u32 blockPtrAddr, u32 timeoutPtr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateFpl(%i, %08x, %08x)", uid, blockPtrAddr, timeoutPtr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
		} else {
			SceUID threadID = __KernelGetCurThread();
			HLEKernel::RemoveWaitingThread(fpl->waitingThreads, threadID);
			FplWaitingThread waiting = {threadID, blockPtrAddr};
			fpl->waitingThreads.push_back(waiting);

			__KernelSetFplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_FPL, uid, 0, timeoutPtr, false, "fpl waited");
		}

		return 0;
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateFpl(%i, %08x, %08x): invalid fpl", uid, blockPtrAddr, timeoutPtr);
		return error;
	}
}

int sceKernelAllocateFplCB(SceUID uid, u32 blockPtrAddr, u32 timeoutPtr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateFplCB(%i, %08x, %08x)", uid, blockPtrAddr, timeoutPtr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
		} else {
			SceUID threadID = __KernelGetCurThread();
			HLEKernel::RemoveWaitingThread(fpl->waitingThreads, threadID);
			FplWaitingThread waiting = {threadID, blockPtrAddr};
			fpl->waitingThreads.push_back(waiting);

			__KernelSetFplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_FPL, uid, 0, timeoutPtr, true, "fpl waited");
		}

		return 0;
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateFplCB(%i, %08x, %08x): invalid fpl", uid, blockPtrAddr, timeoutPtr);
		return error;
	}
}

int sceKernelTryAllocateFpl(SceUID uid, u32 blockPtrAddr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelTryAllocateFpl(%i, %08x)", uid, blockPtrAddr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			return 0;
		} else {
			return SCE_KERNEL_ERROR_NO_MEMORY;
		}
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelTryAllocateFpl(%i, %08x): invalid fpl", uid, blockPtrAddr);
		return error;
	}
}

int sceKernelFreeFpl(SceUID uid, u32 blockPtr)
{
	if (blockPtr > PSP_GetUserMemoryEnd()) {
		WARN_LOG(SCEKERNEL, "%08x=sceKernelFreeFpl(%i, %08x): invalid address", SCE_KERNEL_ERROR_ILLEGAL_ADDR, uid, blockPtr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl) {
		int blockNum = (blockPtr - fpl->address) / fpl->alignedSize;
		if (blockNum < 0 || blockNum >= fpl->nf.numBlocks) {
			DEBUG_LOG(SCEKERNEL, "sceKernelFreeFpl(%i, %08x): bad block ptr", uid, blockPtr);
			return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
		} else {
			if (fpl->freeBlock(blockNum)) {
				DEBUG_LOG(SCEKERNEL, "sceKernelFreeFpl(%i, %08x)", uid, blockPtr);
				__KernelSortFplThreads(fpl);

				bool wokeThreads = false;
retry:
				for (auto iter = fpl->waitingThreads.begin(), end = fpl->waitingThreads.end(); iter != end; ++iter)
				{
					if (__KernelUnlockFplForThread(fpl, *iter, error, 0, wokeThreads))
					{
						fpl->waitingThreads.erase(iter);
						goto retry;
					}
				}

				if (wokeThreads)
					hleReSchedule("fpl freed");
				return 0;
			} else {
				DEBUG_LOG(SCEKERNEL, "sceKernelFreeFpl(%i, %08x): already free", uid, blockPtr);
				return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
			}
		}
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelFreeFpl(%i, %08x): invalid fpl", uid, blockPtr);
		return error;
	}
}

int sceKernelCancelFpl(SceUID uid, u32 numWaitThreadsPtr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelCancelFpl(%i, %08x)", uid, numWaitThreadsPtr);
		fpl->nf.numWaitThreads = (int) fpl->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(fpl->nf.numWaitThreads, numWaitThreadsPtr);

		bool wokeThreads = __KernelClearFplThreads(fpl, SCE_KERNEL_ERROR_WAIT_CANCEL);
		if (wokeThreads)
			hleReSchedule("fpl canceled");
		return 0;
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelCancelFpl(%i, %08x): invalid fpl", uid, numWaitThreadsPtr);
		return error;
	}
}

int sceKernelReferFplStatus(SceUID uid, u32 statusPtr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelReferFplStatus(%i, %08x)", uid, statusPtr);
		// Refresh waiting threads and free block count.
		__KernelSortFplThreads(fpl);
		fpl->nf.numWaitThreads = (int) fpl->waitingThreads.size();
		fpl->nf.numFreeBlocks = 0;
		for (int i = 0; i < (int)fpl->nf.numBlocks; ++i)
		{
			if (!fpl->blocks[i])
				++fpl->nf.numFreeBlocks;
		}
		if (Memory::Read_U32(statusPtr) != 0)
			Memory::WriteStruct(statusPtr, &fpl->nf);
		return 0;
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelReferFplStatus(%i, %08x): invalid fpl", uid, statusPtr);
		return error;
	}
}



//////////////////////////////////////////////////////////////////////////
// ALLOCATIONS
//////////////////////////////////////////////////////////////////////////
//00:49:12 <TyRaNiD> ector, well the partitions are 1 = kernel, 2 = user, 3 = me, 4 = kernel mirror :)

enum MemblockType
{
	PSP_SMEM_Low = 0,
	PSP_SMEM_High = 1,
	PSP_SMEM_Addr = 2,
	PSP_SMEM_LowAligned = 3,
	PSP_SMEM_HighAligned = 4,
};

class PartitionMemoryBlock : public KernelObject
{
public:
	const char *GetName() {return name;}
	const char *GetTypeName() {return "MemoryPart";}
	void GetQuickInfo(char *ptr, int size)
	{
		int sz = alloc->GetBlockSizeFromAddress(address);
		sprintf(ptr, "MemPart: %08x - %08x	size: %08x", address, address + sz, sz);
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_UID; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_PMB; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_PMB; }

	PartitionMemoryBlock(BlockAllocator *_alloc, const char *_name, u32 size, MemblockType type, u32 alignment)
	{
		alloc = _alloc;
		strncpy(name, _name, 32);
		name[31] = '\0';

		// 0 is used for save states to wake up.
		if (size != 0)
		{
			if (type == PSP_SMEM_Addr)
			{
				alignment &= ~0xFF;
				address = alloc->AllocAt(alignment, size, name);
			}
			else if (type == PSP_SMEM_LowAligned || type == PSP_SMEM_HighAligned)
				address = alloc->AllocAligned(size, 0x100, alignment, type == PSP_SMEM_HighAligned, name);
			else
				address = alloc->Alloc(size, type == PSP_SMEM_High, name);
#ifdef _DEBUG
			alloc->ListBlocks();
#endif
		}
	}
	~PartitionMemoryBlock()
	{
		if (address != (u32)-1)
			alloc->Free(address);
	}
	bool IsValid() {return address != (u32)-1;}
	BlockAllocator *alloc;

	virtual void DoState(PointerWrap &p)
	{
		auto s = p.Section("PMB", 1);
		if (!s)
			return;

		p.Do(address);
		p.DoArray(name, sizeof(name));
	}

	u32 address;
	char name[32];
};


u32 sceKernelMaxFreeMemSize() 
{
	u32 retVal = userMemory.GetLargestFreeBlockSize();
	DEBUG_LOG(SCEKERNEL, "%08x (dec %i)=sceKernelMaxFreeMemSize()", retVal, retVal);
	return retVal;
}

u32 sceKernelTotalFreeMemSize()
{
	u32 retVal = userMemory.GetTotalFreeBytes();
	DEBUG_LOG(SCEKERNEL, "%08x (dec %i)=sceKernelTotalFreeMemSize()", retVal, retVal);
	return retVal;
}

int sceKernelAllocPartitionMemory(int partition, const char *name, int type, u32 size, u32 addr)
{
	if (name == NULL)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (size == 0)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid size %x", SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED, size);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid partition %x", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 5 && partition != 6)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid partition %x", SCE_KERNEL_ERROR_ILLEGAL_PARTITION, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_PARTITION;
	}
	if (type < PSP_SMEM_Low || type > PSP_SMEM_HighAligned)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid type %x", SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE, type);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE;
	}
	// Alignment is only allowed for powers of 2.
	if ((type == PSP_SMEM_LowAligned || type == PSP_SMEM_HighAligned) && ((addr & (addr - 1)) != 0 || addr == 0))
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelAllocPartitionMemory(): invalid alignment %x", SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE, addr);
		return SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE;
	}

	PartitionMemoryBlock *block = new PartitionMemoryBlock(&userMemory, name, size, (MemblockType)type, addr);
	if (!block->IsValid())
	{
		delete block;
		ERROR_LOG(SCEKERNEL, "sceKernelAllocPartitionMemory(partition = %i, %s, type= %i, size= %i, addr= %08x): allocation failed", partition, name, type, size, addr);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	SceUID uid = kernelObjects.Create(block);

	DEBUG_LOG(SCEKERNEL,"%i = sceKernelAllocPartitionMemory(partition = %i, %s, type= %i, size= %i, addr= %08x)",
		uid, partition, name, type, size, addr);

	return uid;
}

int sceKernelFreePartitionMemory(SceUID id)
{
	DEBUG_LOG(SCEKERNEL,"sceKernelFreePartitionMemory(%d)",id);

	return kernelObjects.Destroy<PartitionMemoryBlock>(id);
}

u32 sceKernelGetBlockHeadAddr(SceUID id)
{
	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(id, error);
	if (block)
	{
		DEBUG_LOG(SCEKERNEL,"%08x = sceKernelGetBlockHeadAddr(%i)", block->address, id);
		return block->address;
	}
	else
	{
		ERROR_LOG(SCEKERNEL,"sceKernelGetBlockHeadAddr failed(%i)", id);
		return 0;
	}
}


int sceKernelPrintf(const char *formatString)
{
	if (formatString == NULL)
		return -1;

	bool supported = true;
	int param = 1;
	char tempStr[24];
	char tempFormat[24] = {'%'};
	std::string result, format = formatString;

	// Each printf is a separate line already in the log, so don't double space.
	// This does mean we break up strings, unfortunately.
	if (!format.empty() && format[format.size() - 1] == '\n')
		format.resize(format.size() - 1);

	for (size_t i = 0, n = format.size(); supported && i < n; )
	{
		size_t next = format.find('%', i);
		if (next == format.npos)
		{
			result += format.substr(i);
			break;
		}
		else if (next != i)
			result += format.substr(i, next - i);

		i = next + 1;
		if (i >= n)
		{
			supported = false;
			break;
		}

		const char *s;
		switch (format[i])
		{
		case '%':
			result += '%';
			++i;
			break;

		case 's':
			s = Memory::GetCharPointer(PARAM(param++));
			result += s ? s : "(null)";
			++i;
			break;

		case 'd':
		case 'i':
		case 'x':
		case 'X':
		case 'u':
			tempFormat[1] = format[i];
			tempFormat[2] = '\0';
			snprintf(tempStr, sizeof(tempStr), tempFormat, PARAM(param++));
			result += tempStr;
			++i;
			break;

		case '0':
			if (i + 3 > n || format[i + 1] != '8' || (format[i + 2] != 'x' && format[i + 2] != 'X'))
				supported = false;
			else
			{
				// These are the '0', '8', and 'x' or 'X' respectively.
				tempFormat[1] = format[i];
				tempFormat[2] = format[i + 1];
				tempFormat[3] = format[i + 2];
				tempFormat[4] = '\0';
				snprintf(tempStr, sizeof(tempStr), tempFormat, PARAM(param++));
				result += tempStr;
				i += 3;
			}
			break;

		case 'p':
			snprintf(tempStr, sizeof(tempStr), "%08x", PARAM(param++));
			result += tempStr;
			++i;
			break;

		default:
			supported = false;
			break;
		}

		if (param > 6)
			supported = false;
	}

	// Just in case there were embedded strings that had \n's.
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);

	if (supported)
		INFO_LOG(SCEKERNEL, "sceKernelPrintf: %s", result.c_str())
	else
		ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelPrintf(%s, %08x, %08x, %08x)", format.c_str(), PARAM(1), PARAM(2), PARAM(3));
	return 0;
}

int sceKernelSetCompiledSdkVersion(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	bool validSDK = false;
	switch (sdkMainVersion) {
	case 0x01000000:
	case 0x01050000:
	case 0x02000000:
	case 0x02050000:
	case 0x02060000:
	case 0x02070000:
	case 0x02080000:
	case 0x03000000:
	case 0x03010000:
	case 0x03030000:
	case 0x03040000:
	case 0x03050000:
	case 0x03060000:
		validSDK = true;
		break;
	default:
		validSDK = false;
		break;
	}

	if (!validSDK) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion370(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x03070000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion370 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion370(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion380_390(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x03080000 && sdkMainVersion != 0x03090000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion380_390 unknown SDK: %x", sdkVersion);
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion380_390(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion395(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFFFF00;
	if (sdkMainVersion != 0x04000000
			&& sdkMainVersion != 0x04000100
			&& sdkMainVersion != 0x04000500
			&& sdkMainVersion != 0x03090500
			&& sdkMainVersion != 0x03090600) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion395 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion395(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion600_602(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06010000
			&& sdkMainVersion != 0x06000000
			&& sdkMainVersion != 0x06020000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion600_602 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion600_602(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion500_505(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x05000000
			&& sdkMainVersion != 0x05050000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion500_505 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion500_505(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion401_402(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x04010000
			&& sdkMainVersion != 0x04020000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion401_402 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion401_402(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion507(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x05070000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion507 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion507(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion603_605(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06040000
			&& sdkMainVersion != 0x06030000
			&& sdkMainVersion != 0x06050000) {
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion603_605 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion603_605(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelSetCompiledSdkVersion606(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06060000) {
		ERROR_LOG_REPORT(SCEKERNEL, "sceKernelSetCompiledSdkVersion606 unknown SDK: %x (would crash)", sdkVersion);
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompiledSdkVersion606(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelGetCompiledSdkVersion() {
	if (!(flags_ & SCE_KERNEL_HASCOMPILEDSDKVERSION))
		return 0;
	return sdkVersion_;
}

int sceKernelSetCompilerVersion(int version) {
	DEBUG_LOG(SCEKERNEL, "sceKernelSetCompilerVersion(%08x)", version);
	compilerVersion_ = version;
	flags_ |= SCE_KERNEL_HASCOMPILERVERSION;
	return 0;
}

KernelObject *__KernelMemoryFPLObject()
{
	return new FPL;
}

KernelObject *__KernelMemoryVPLObject()
{
	return new VPL;
}

KernelObject *__KernelMemoryPMBObject()
{
	// TODO: We could theoretically handle kernelMemory too, but we don't support that now anyway.
	return new PartitionMemoryBlock(&userMemory, "", 0, PSP_SMEM_Low, 0);
}

// VPL = variable length memory pool

enum SceKernelVplAttr
{
	PSP_VPL_ATTR_FIFO       = 0x0000,
	PSP_VPL_ATTR_PRIORITY   = 0x0100,
	PSP_VPL_ATTR_SMALLEST   = 0x0200,
	PSP_VPL_ATTR_MASK_ORDER = 0x0300,

	PSP_VPL_ATTR_HIGHMEM    = 0x4000,
	PSP_VPL_ATTR_KNOWN      = PSP_VPL_ATTR_FIFO | PSP_VPL_ATTR_PRIORITY | PSP_VPL_ATTR_SMALLEST | PSP_VPL_ATTR_HIGHMEM,
};

bool __KernelUnlockVplForThread(VPL *vpl, VplWaitingThread &threadInfo, u32 &error, int result, bool &wokeThreads) {
	const SceUID threadID = threadInfo.threadID;
	if (!HLEKernel::VerifyWait(threadID, WAITTYPE_VPL, vpl->GetUID())) {
		return true;
	}

	// If result is an error code, we're just letting it go.
	if (result == 0) {
		int size = (int) __KernelGetWaitValue(threadID, error);

		// An older savestate may have an invalid header, use the block allocator in that case.
		u32 addr;
		if (vpl->header.IsValid()) {
			addr = vpl->header->Allocate(size);
		} else {
			// Padding (normally used to track the allocation.)
			u32 allocSize = size + 8;
			addr = vpl->alloc.Alloc(allocSize, true);
		}
		if (addr != (u32) -1) {
			Memory::Write_U32(addr, threadInfo.addrPtr);
		} else {
			return false;
		}
	}

	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0 && vplWaitTimer != -1) {
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(vplWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
}

void __KernelVplBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitBeginCallback<VPL, WAITTYPE_VPL, VplWaitingThread>(threadID, prevCallbackId, vplWaitTimer);
	if (result == HLEKernel::WAIT_CB_SUCCESS)
		DEBUG_LOG(SCEKERNEL, "sceKernelAllocateVplCB: Suspending vpl wait for callback")
	else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
		ERROR_LOG_REPORT(SCEKERNEL, "sceKernelAllocateVplCB: wait not found to pause for callback")
	else
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelAllocateVplCB: beginning callback with bad wait id?");
}

void __KernelVplEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitEndCallback<VPL, WAITTYPE_VPL, VplWaitingThread>(threadID, prevCallbackId, vplWaitTimer, __KernelUnlockVplForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(SCEKERNEL, "sceKernelReceiveMbxCB: Resuming mbx wait from callback");
}

bool __VplThreadSortPriority(VplWaitingThread thread1, VplWaitingThread thread2)
{
	return __KernelThreadSortPriority(thread1.threadID, thread2.threadID);
}

bool __KernelClearVplThreads(VPL *vpl, int reason)
{
	u32 error;
	bool wokeThreads = false;
	for (auto iter = vpl->waitingThreads.begin(), end = vpl->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockVplForThread(vpl, *iter, error, reason, wokeThreads);
	vpl->waitingThreads.clear();

	return wokeThreads;
}

void __KernelSortVplThreads(VPL *vpl)
{
	// Remove any that are no longer waiting.
	SceUID uid = vpl->GetUID();
	HLEKernel::CleanupWaitingThreads(WAITTYPE_VPL, uid, vpl->waitingThreads);

	if ((vpl->nv.attr & PSP_VPL_ATTR_PRIORITY) != 0)
		std::stable_sort(vpl->waitingThreads.begin(), vpl->waitingThreads.end(), __VplThreadSortPriority);
}

SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 6)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_PERM, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_PERM;
	}
	if (((attr & ~PSP_VPL_ATTR_KNOWN) & ~0xFF) != 0)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}
	if (vplSize == 0)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): invalid size", SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
	}
	// Block Allocator seems to A-OK this, let's stop it here.
	if (vplSize >= 0x80000000)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateVpl(): way too big size", SCE_KERNEL_ERROR_NO_MEMORY);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	// Can't have that little space in a Vpl, sorry.
	if (vplSize <= 0x30)
		vplSize = 0x1000;
	vplSize = (vplSize + 7) & ~7;

	// We ignore the upalign to 256 and do it ourselves by 8.
	u32 allocSize = vplSize;
	u32 memBlockPtr = userMemory.Alloc(allocSize, (attr & PSP_VPL_ATTR_HIGHMEM) != 0, "VPL");
	if (memBlockPtr == (u32)-1)
	{
		ERROR_LOG(SCEKERNEL, "sceKernelCreateVpl(): Failed to allocate %i bytes of pool data", vplSize);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	VPL *vpl = new VPL;
	SceUID id = kernelObjects.Create(vpl);

	strncpy(vpl->nv.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	vpl->nv.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	vpl->nv.attr = attr;
	vpl->nv.size = sizeof(vpl->nv);
	vpl->nv.poolSize = vplSize - 0x20;
	vpl->nv.numWaitThreads = 0;
	vpl->nv.freeSize = vpl->nv.poolSize;

	// A vpl normally has accounting stuff in the first 32 bytes.
	vpl->address = memBlockPtr + 0x20;
	vpl->alloc.Init(vpl->address, vpl->nv.poolSize);

	vpl->header = PSPPointer<SceKernelVplHeader>::Create(memBlockPtr);
	vpl->header->Init(memBlockPtr, vplSize);

	DEBUG_LOG(SCEKERNEL, "%x=sceKernelCreateVpl(\"%s\", block=%i, attr=%i, size=%i)", 
		id, name, partition, vpl->nv.attr, vpl->nv.poolSize);

	if (optPtr != 0)
	{
		u32 size = Memory::Read_U32(optPtr);
		if (size > 4)
			WARN_LOG_REPORT(SCEKERNEL, "sceKernelCreateVpl(): unsupported options parameter, size = %d", size);
	}

	return id;
}

int sceKernelDeleteVpl(SceUID uid)
{
	DEBUG_LOG(SCEKERNEL, "sceKernelDeleteVpl(%i)", uid);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		bool wokeThreads = __KernelClearVplThreads(vpl, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("vpl deleted");

		userMemory.Free(vpl->address);
		kernelObjects.Destroy<VPL>(uid);
		return 0;
	}
	else
		return error;
}

// Returns false for invalid parameters (e.g. don't check callbacks, etc.)
// Successful allocation is indicated by error == 0.
bool __KernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 &error, bool trying, const char *funcname) {
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl) {
		if (size == 0 || size > (u32) vpl->nv.poolSize) {
			WARN_LOG(SCEKERNEL, "%s(vpl=%i, size=%i, ptrout=%08x): invalid size", funcname, uid, size, addrPtr);
			error = SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
			return false;
		}

		VERBOSE_LOG(SCEKERNEL, "%s(vpl=%i, size=%i, ptrout=%08x)", funcname, uid, size, addrPtr);

		// For some reason, try doesn't follow the same rules...
		if (!trying && (vpl->nv.attr & PSP_VPL_ATTR_MASK_ORDER) == PSP_VPL_ATTR_FIFO)
		{
			__KernelSortVplThreads(vpl);
			if (!vpl->waitingThreads.empty())
			{
				// Can't allocate, blocked by FIFO queue.
				error = SCE_KERNEL_ERROR_NO_MEMORY;
				return true;
			}
		}

		// Allocate using the header only for newer vpls (older come from savestates.)
		u32 addr;
		if (vpl->header.IsValid()) {
			addr = vpl->header->Allocate(size);
		} else {
			// Padding (normally used to track the allocation.)
			u32 allocSize = size + 8;
			addr = vpl->alloc.Alloc(allocSize, true);
		}
		if (addr != (u32) -1) {
			Memory::Write_U32(addr, addrPtr);
			error =  0;
		} else {
			error = SCE_KERNEL_ERROR_NO_MEMORY;
		}

		return true;
	}

	return false;
}

void __KernelVplTimeout(u64 userdata, int cyclesLate) {
	SceUID threadID = (SceUID) userdata;
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, WAITTYPE_VPL, error);

	HLEKernel::WaitExecTimeout<VPL, WAITTYPE_VPL>(threadID);

	// If in FIFO mode, that may have cleared another thread to wake up.
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl && (vpl->nv.attr & PSP_VPL_ATTR_MASK_ORDER) == PSP_VPL_ATTR_FIFO) {
		bool wokeThreads;
		std::vector<VplWaitingThread>::iterator iter = vpl->waitingThreads.begin();
		// Unlock every waiting thread until the first that must still wait.
		while (iter != vpl->waitingThreads.end() && __KernelUnlockVplForThread(vpl, *iter, error, 0, wokeThreads)) {
			vpl->waitingThreads.erase(iter);
			iter = vpl->waitingThreads.begin();
		}
	}
}

void __KernelSetVplTimeout(u32 timeoutPtr)
{
	if (timeoutPtr == 0 || vplWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 5)
		micro = 10;
	// Yes, this 7 is reproducible.  6 is (a lot) longer than 7.
	else if (micro == 7)
		micro = 15;
	else if (micro <= 215)
		micro = 250;

	CoreTiming::ScheduleEvent(usToCycles(micro), vplWaitTimer, __KernelGetCurThread());
}

int sceKernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr)
{
	u32 error, ignore;
	if (__KernelAllocateVpl(uid, size, addrPtr, error, false, __FUNCTION__))
	{
		VPL *vpl = kernelObjects.Get<VPL>(uid, ignore);
		if (error == SCE_KERNEL_ERROR_NO_MEMORY)
		{
			if (timeoutPtr != 0 && Memory::Read_U32(timeoutPtr) == 0)
				return SCE_KERNEL_ERROR_WAIT_TIMEOUT;

			if (vpl)
			{
				SceUID threadID = __KernelGetCurThread();
				HLEKernel::RemoveWaitingThread(vpl->waitingThreads, threadID);
				VplWaitingThread waiting = {threadID, addrPtr};
				vpl->waitingThreads.push_back(waiting);
			}

			__KernelSetVplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_VPL, uid, size, timeoutPtr, false, "vpl waited");
		}
		// If anyone else was waiting, the allocation causes a delay.
		else if (error == 0 && !vpl->waitingThreads.empty())
			return hleDelayResult(error, "vpl allocated", 50);
	}
	return error;
}

int sceKernelAllocateVplCB(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr)
{
	u32 error, ignore;
	if (__KernelAllocateVpl(uid, size, addrPtr, error, false, __FUNCTION__))
	{
		hleCheckCurrentCallbacks();

		VPL *vpl = kernelObjects.Get<VPL>(uid, ignore);
		if (error == SCE_KERNEL_ERROR_NO_MEMORY)
		{
			if (timeoutPtr != 0 && Memory::Read_U32(timeoutPtr) == 0)
				return SCE_KERNEL_ERROR_WAIT_TIMEOUT;

			if (vpl)
			{
				SceUID threadID = __KernelGetCurThread();
				HLEKernel::RemoveWaitingThread(vpl->waitingThreads, threadID);
				VplWaitingThread waiting = {threadID, addrPtr};
				vpl->waitingThreads.push_back(waiting);
			}

			__KernelSetVplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_VPL, uid, size, timeoutPtr, true, "vpl waited");
		}
		// If anyone else was waiting, the allocation causes a delay.
		else if (error == 0 && !vpl->waitingThreads.empty())
			return hleDelayResult(error, "vpl allocated", 50);
	}
	return error;
}

int sceKernelTryAllocateVpl(SceUID uid, u32 size, u32 addrPtr)
{
	u32 error;
	__KernelAllocateVpl(uid, size, addrPtr, error, true, __FUNCTION__);
	return error;
}

int sceKernelFreeVpl(SceUID uid, u32 addr) {
	if (addr && !Memory::IsValidAddress(addr)) {
		WARN_LOG(SCEKERNEL, "%08x=sceKernelFreeVpl(%i, %08x): Invalid address", SCE_KERNEL_ERROR_ILLEGAL_ADDR, uid, addr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	VERBOSE_LOG(SCEKERNEL, "sceKernelFreeVpl(%i, %08x)", uid, addr);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl) {
		bool freed;
		// Free using the header for newer vpls (not old savestates.)
		if (vpl->header.IsValid()) {
			freed = vpl->header->Free(addr);
		} else {
			freed = vpl->alloc.FreeExact(addr);
		}

		if (freed) {
			__KernelSortVplThreads(vpl);

			bool wokeThreads = false;
retry:
			for (auto iter = vpl->waitingThreads.begin(), end = vpl->waitingThreads.end(); iter != end; ++iter) {
				if (__KernelUnlockVplForThread(vpl, *iter, error, 0, wokeThreads)) {
					vpl->waitingThreads.erase(iter);
					goto retry;
				}
				// In FIFO, we stop at the first one that can't wake.
				else if ((vpl->nv.attr & PSP_VPL_ATTR_MASK_ORDER) == PSP_VPL_ATTR_FIFO)
					break;
			}

			if (wokeThreads) {
				hleReSchedule("vpl freed");
			}

			return 0;
		} else {
			WARN_LOG(SCEKERNEL, "%08x=sceKernelFreeVpl(%i, %08x): Unable to free", SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK, uid, addr);
			return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
		}
	} else {
		return error;
	}
}

int sceKernelCancelVpl(SceUID uid, u32 numWaitThreadsPtr)
{
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelCancelVpl(%i, %08x)", uid, numWaitThreadsPtr);
		vpl->nv.numWaitThreads = (int) vpl->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(vpl->nv.numWaitThreads, numWaitThreadsPtr);

		bool wokeThreads = __KernelClearVplThreads(vpl, SCE_KERNEL_ERROR_WAIT_CANCEL);
		if (wokeThreads)
			hleReSchedule("vpl canceled");

		return 0;
	}
	else
	{
		DEBUG_LOG(SCEKERNEL, "sceKernelCancelVpl(%i, %08x): invalid vpl", uid, numWaitThreadsPtr);
		return error;
	}
}

int sceKernelReferVplStatus(SceUID uid, u32 infoPtr) {
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl) {
		DEBUG_LOG(SCEKERNEL, "sceKernelReferVplStatus(%i, %08x)", uid, infoPtr);

		__KernelSortVplThreads(vpl);
		vpl->nv.numWaitThreads = (int) vpl->waitingThreads.size();
		if (vpl->header.IsValid()) {
			vpl->nv.freeSize = vpl->header->FreeSize();
		} else {
			vpl->nv.freeSize = vpl->alloc.GetTotalFreeBytes();
		}
		if (Memory::IsValidAddress(infoPtr) && Memory::Read_U32(infoPtr) != 0) {
			Memory::WriteStruct(infoPtr, &vpl->nv);
		}
		return 0;
	} else {
		return error;
	}
}

u32 AllocMemoryBlock(const char *pname, u32 type, u32 size, u32 paramsAddr) {
	if (Memory::IsValidAddress(paramsAddr) && Memory::Read_U32(paramsAddr) != 4) {
		ERROR_LOG_REPORT(SCEKERNEL, "AllocMemoryBlock(%s): unsupported params size %d", pname, Memory::Read_U32(paramsAddr));
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	if (type != PSP_SMEM_High && type != PSP_SMEM_Low) {
		ERROR_LOG_REPORT(SCEKERNEL, "AllocMemoryBlock(%s): unsupported type %d", pname, type);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE;
	}
	if (size == 0) {
		WARN_LOG_REPORT(SCEKERNEL, "AllocMemoryBlock(%s): invalid size %x", pname, size);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	if (pname == NULL) {
		ERROR_LOG_REPORT(SCEKERNEL, "AllocMemoryBlock(): NULL name");
		return SCE_KERNEL_ERROR_ERROR;
	}

	PartitionMemoryBlock *block = new PartitionMemoryBlock(&userMemory, pname, size, (MemblockType)type, 0);
	if (!block->IsValid())
	{
		delete block;
		ERROR_LOG(SCEKERNEL, "AllocMemoryBlock(%s, %i, %08x, %08x): allocation failed", pname, type, size, paramsAddr);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	SceUID uid = kernelObjects.Create(block);

	INFO_LOG(SCEKERNEL,"%08x=AllocMemoryBlock(SysMemUserForUser_FE707FDF)(%s, %i, %08x, %08x)", uid, pname, type, size, paramsAddr);
	return uid;
}

u32 FreeMemoryBlock(u32 uid) {
	INFO_LOG(SCEKERNEL, "FreeMemoryBlock(%08x)", uid);
	return kernelObjects.Destroy<PartitionMemoryBlock>(uid);
}

u32 GetMemoryBlockPtr(u32 uid, u32 addr) {
	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(uid, error);
	if (block)
	{
		INFO_LOG(SCEKERNEL, "GetMemoryBlockPtr(%08x, %08x) = %08x", uid, addr, block->address);
		Memory::Write_U32(block->address, addr);
		return 0;
	}
	else
	{
		ERROR_LOG(SCEKERNEL, "GetMemoryBlockPtr(%08x, %08x) failed", uid, addr);
		return 0;
	}
}

u32 SysMemUserForUser_D8DE5C1E() {
	// Called by Evangelion Jo and return 0 here to go in-game.
	ERROR_LOG(SCEKERNEL,"UNIMPL SysMemUserForUser_D8DE5C1E()");
	return 0; 
}

u32 SysMemUserForUser_ACBD88CA() {
	ERROR_LOG_REPORT_ONCE(SysMemUserForUser_ACBD88CA, SCEKERNEL, "UNIMPL SysMemUserForUser_ACBD88CA()");
	return 0; 
}

u32 SysMemUserForUser_945E45DA() {
	// Called by Evangelion Jo and expected return 0 here.
	ERROR_LOG_REPORT_ONCE(SysMemUserForUser945E45DA, SCEKERNEL, "UNIMPL SysMemUserForUser_945E45DA()");
	return 0; 
}

enum
{
	PSP_ERROR_UNKNOWN_TLSPL_ID = 0x800201D0,
	PSP_ERROR_TOO_MANY_TLSPL   = 0x800201D1,
};

enum
{
	// TODO: Complete untested guesses.
	PSP_TLSPL_ATTR_FIFO     = 0,
	PSP_TLSPL_ATTR_PRIORITY = 0x100,
	PSP_TLSPL_ATTR_HIGHMEM  = 0x4000,
	PSP_TLSPL_ATTR_KNOWN    = PSP_TLSPL_ATTR_HIGHMEM | PSP_TLSPL_ATTR_PRIORITY | PSP_TLSPL_ATTR_FIFO,
};

struct NativeTlspl
{
	SceSize_le size;
	char name[32];
	SceUInt_le attr;
	s32_le index;
	u32_le blockSize;
	u32_le totalBlocks;
	u32_le freeBlocks;
	u32_le numWaitThreads;
};

struct TLSPL : public KernelObject
{
	const char *GetName() {return ntls.name;}
	const char *GetTypeName() {return "TLS";}
	static u32 GetMissingErrorCode() { return PSP_ERROR_UNKNOWN_TLSPL_ID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Tlspl; }
	int GetIDType() const { return SCE_KERNEL_TMID_Tlspl; }

	TLSPL() : next(0) {}

	virtual void DoState(PointerWrap &p)
	{
		auto s = p.Section("TLS", 1);
		if (!s)
			return;

		p.Do(ntls);
		p.Do(address);
		p.Do(waitingThreads);
		p.Do(next);
		p.Do(usage);
	}

	NativeTlspl ntls;
	u32 address;
	std::vector<SceUID> waitingThreads;
	int next;
	std::vector<SceUID> usage;
};

KernelObject *__KernelTlsplObject()
{
	return new TLSPL;
}

SceUID sceKernelCreateTlspl(const char *name, u32 partition, u32 attr, u32 blockSize, u32 count, u32 optionsPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): invalid name", SCE_KERNEL_ERROR_NO_MEMORY);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}
	if ((attr & ~PSP_TLSPL_ATTR_KNOWN) >= 0x100)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 6)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_PERM, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_PERM;
	}

	// There's probably a simpler way to get this same basic formula...
	// This is based on results from a PSP.
	bool illegalMemSize = blockSize == 0 || count == 0;
	if (!illegalMemSize && (u64) blockSize > ((0x100000000ULL / (u64) count) - 4ULL))
		illegalMemSize = true;
	if (!illegalMemSize && (u64) count >= 0x100000000ULL / (((u64) blockSize + 3ULL) & ~3ULL))
		illegalMemSize = true;
	if (illegalMemSize)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): invalid blockSize/count", SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
	}

	int index = -1;
	for (int i = 0; i < TLSPL_NUM_INDEXES; ++i)
		if (tlsplUsedIndexes[i] == false)
		{
			index = i;
			break;
		}

	if (index == -1)
	{
		WARN_LOG_REPORT(SCEKERNEL, "%08x=sceKernelCreateTlspl(): ran out of indexes for TLS pools", PSP_ERROR_TOO_MANY_TLSPL);
		return PSP_ERROR_TOO_MANY_TLSPL;
	}

	u32 totalSize = blockSize * count;
	u32 blockPtr = userMemory.Alloc(totalSize, (attr & PSP_TLSPL_ATTR_HIGHMEM) != 0, name);
#ifdef _DEBUG
	userMemory.ListBlocks();
#endif

	if (blockPtr == (u32) -1)
	{
		ERROR_LOG(SCEKERNEL, "%08x=sceKernelCreateTlspl(%s, %d, %08x, %d, %d, %08x): failed to allocate memory", SCE_KERNEL_ERROR_NO_MEMORY, name, partition, attr, blockSize, count, optionsPtr);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	TLSPL *tls = new TLSPL();
	SceUID id = kernelObjects.Create(tls);

	tls->ntls.size = sizeof(tls->ntls);
	strncpy(tls->ntls.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	tls->ntls.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	tls->ntls.attr = attr;
	tls->ntls.index = index;
	tlsplUsedIndexes[index] = true;
	tls->ntls.blockSize = blockSize;
	tls->ntls.totalBlocks = count;
	tls->ntls.freeBlocks = count;
	tls->ntls.numWaitThreads = 0;
	tls->address = blockPtr;
	tls->usage.resize(count, 0);

	WARN_LOG(SCEKERNEL, "%08x=sceKernelCreateTlspl(%s, %d, %08x, %d, %d, %08x)", id, name, partition, attr, blockSize, count, optionsPtr);

	// TODO: just alignment?
	if (optionsPtr != 0)
	{
		u32 size = Memory::Read_U32(optionsPtr);
		if (size > 4)
			WARN_LOG_REPORT(SCEKERNEL, "sceKernelCreateTlspl(%s) unsupported options parameter, size = %d", name, size);
	}
	if ((attr & PSP_TLSPL_ATTR_PRIORITY) != 0)
		WARN_LOG_REPORT(SCEKERNEL, "sceKernelCreateTlspl(%s) unsupported attr parameter: %08x", name, attr);

	return id;
}

// Parameters are an educated guess.
int sceKernelDeleteTlspl(SceUID uid)
{
	WARN_LOG(SCEKERNEL, "sceKernelDeleteTlspl(%08x)", uid);
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		// TODO: Wake waiting threads, probably?
		userMemory.Free(tls->address);
		tlsplUsedIndexes[tls->ntls.index] = false;
		kernelObjects.Destroy<TLSPL>(uid);
	}
	return error;
}

int sceKernelGetTlsAddr(SceUID uid)
{
	// TODO: Allocate downward if PSP_TLSPL_ATTR_HIGHMEM?
	DEBUG_LOG(SCEKERNEL, "sceKernelGetTlsAddr(%08x)", uid);

	if (!__KernelIsDispatchEnabled() || __IsInInterrupt())
		return 0;

	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		SceUID threadID = __KernelGetCurThread();
		int allocBlock = -1;

		// If the thread already has one, return it.
		for (size_t i = 0; i < tls->ntls.totalBlocks && allocBlock == -1; ++i)
		{
			if (tls->usage[i] == threadID)
				allocBlock = (int) i;
		}

		if (allocBlock == -1)
		{
			for (size_t i = 0; i < tls->ntls.totalBlocks && allocBlock == -1; ++i)
			{
				// The PSP doesn't give the same block out twice in a row, even if freed.
				if (tls->usage[tls->next] == 0)
					allocBlock = tls->next;
				tls->next = (tls->next + 1) % tls->ntls.totalBlocks;
			}

			if (allocBlock != -1)
			{
				tls->usage[allocBlock] = threadID;
				--tls->ntls.freeBlocks;
			}
		}

		if (allocBlock == -1)
		{
			tls->waitingThreads.push_back(threadID);
			__KernelWaitCurThread(WAITTYPE_TLSPL, uid, 1, 0, false, "allocate tls");
			return -1;
		}

		return tls->address + allocBlock * tls->ntls.blockSize;
	}
	else
		return error;
}

// Parameters are an educated guess.
int sceKernelFreeTlspl(SceUID uid)
{
	WARN_LOG(SCEKERNEL, "UNIMPL sceKernelFreeTlspl(%08x)", uid);
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		SceUID threadID = __KernelGetCurThread();

		// Find the current thread's block.
		int freeBlock = -1;
		for (size_t i = 0; i < tls->ntls.totalBlocks; ++i)
		{
			if (tls->usage[i] == threadID)
			{
				freeBlock = (int) i;
				break;
			}
		}

		if (freeBlock != -1)
		{
			while (!tls->waitingThreads.empty())
			{
				// TODO: What order do they wake in?
				SceUID waitingThreadID = tls->waitingThreads[0];
				tls->waitingThreads.erase(tls->waitingThreads.begin());

				// This thread must've been woken up.
				if (!HLEKernel::VerifyWait(waitingThreadID, WAITTYPE_TLSPL, uid))
					continue;

				// Otherwise, if there was a thread waiting, we were full, so this newly freed one is theirs.
				// TODO: Is the block wiped or anything?
				tls->usage[freeBlock] = waitingThreadID;
				__KernelResumeThreadFromWait(waitingThreadID, freeBlock);
				// No need to continue or free it, we're done.
				return 0;
			}

			// No one was waiting, so now we can really free it.
			tls->usage[freeBlock] = 0;
			++tls->ntls.freeBlocks;
			return 0;
		}
		// TODO: Correct error code.
		else
			return -1;
	}
	else
		return error;
}

// Parameters are an educated guess.
int sceKernelReferTlsplStatus(SceUID uid, u32 infoPtr)
{
	DEBUG_LOG(SCEKERNEL, "sceKernelReferTlsplStatus(%08x, %08x)", uid, infoPtr);
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		// TODO: Check size.
		Memory::WriteStruct(infoPtr, &tls->ntls);
		return 0;
	}
	else
		return error;
}

const HLEFunction SysMemUserForUser[] = {
	{0xA291F107,WrapU_V<sceKernelMaxFreeMemSize>,"sceKernelMaxFreeMemSize"},
	{0xF919F628,WrapU_V<sceKernelTotalFreeMemSize>,"sceKernelTotalFreeMemSize"},
	{0x3FC9AE6A,WrapU_V<sceKernelDevkitVersion>,"sceKernelDevkitVersion"},
	{0x237DBD4F,WrapI_ICIUU<sceKernelAllocPartitionMemory>,"sceKernelAllocPartitionMemory"},	//(int size) ?
	{0xB6D61D02,WrapI_I<sceKernelFreePartitionMemory>,"sceKernelFreePartitionMemory"},	 //(void *ptr) ?
	{0x9D9A5BA1,WrapU_I<sceKernelGetBlockHeadAddr>,"sceKernelGetBlockHeadAddr"},			//(void *ptr) ?
	{0x13a5abef,WrapI_C<sceKernelPrintf>,"sceKernelPrintf"},
	{0x7591c7db,&WrapI_I<sceKernelSetCompiledSdkVersion>,"sceKernelSetCompiledSdkVersion"},
	{0x342061E5,&WrapI_I<sceKernelSetCompiledSdkVersion370>,"sceKernelSetCompiledSdkVersion370"},
	{0x315AD3A0,&WrapI_I<sceKernelSetCompiledSdkVersion380_390>,"sceKernelSetCompiledSdkVersion380_390"},
	{0xEBD5C3E6,&WrapI_I<sceKernelSetCompiledSdkVersion395>,"sceKernelSetCompiledSdkVersion395"},
	{0x057E7380,&WrapI_I<sceKernelSetCompiledSdkVersion401_402>,"sceKernelSetCompiledSdkVersion401_402"},
	{0xf77d77cb,&WrapI_I<sceKernelSetCompilerVersion>,"sceKernelSetCompilerVersion"},
	{0x91de343c,&WrapI_I<sceKernelSetCompiledSdkVersion500_505>,"sceKernelSetCompiledSdkVersion500_505"},
	{0x7893f79a,&WrapI_I<sceKernelSetCompiledSdkVersion507>,"sceKernelSetCompiledSdkVersion507"},
	{0x35669d4c,&WrapI_I<sceKernelSetCompiledSdkVersion600_602>,"sceKernelSetCompiledSdkVersion600_602"},  //??
	{0x1b4217bc,&WrapI_I<sceKernelSetCompiledSdkVersion603_605>,"sceKernelSetCompiledSdkVersion603_605"},
	{0x358ca1bb,&WrapI_I<sceKernelSetCompiledSdkVersion606>,"sceKernelSetCompiledSdkVersion606"},
	{0xfc114573,&WrapI_V<sceKernelGetCompiledSdkVersion>,"sceKernelGetCompiledSdkVersion"},
	{0x2a3e5280,0,"sceKernelQueryMemoryInfo"},
	{0xacbd88ca,WrapU_V<SysMemUserForUser_ACBD88CA>,"SysMemUserForUser_ACBD88CA"},
	{0x945e45da,WrapU_V<SysMemUserForUser_945E45DA>,"SysMemUserForUser_945E45DA"},
	{0xa6848df8,0,"sceKernelSetUsersystemLibWork"},
	{0x6231a71d,0,"sceKernelSetPTRIG"},
	{0x39f49610,0,"sceKernelGetPTRIG"}, 
	// Obscure raw block API
	{0xDB83A952,WrapU_UU<GetMemoryBlockPtr>,"SysMemUserForUser_DB83A952"},  // GetMemoryBlockAddr
	{0x50F61D8A,WrapU_U<FreeMemoryBlock>,"SysMemUserForUser_50F61D8A"},  // FreeMemoryBlock
	{0xFE707FDF,WrapU_CUUU<AllocMemoryBlock>,"SysMemUserForUser_FE707FDF"},  // AllocMemoryBlock
	{0xD8DE5C1E,WrapU_V<SysMemUserForUser_D8DE5C1E>,"SysMemUserForUser_D8DE5C1E"},
};


void Register_SysMemUserForUser()
{
	RegisterModule("SysMemUserForUser", ARRAY_SIZE(SysMemUserForUser), SysMemUserForUser);
}
