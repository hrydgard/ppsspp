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
#include <sstream>

#include "Common/Thread/ParallelLoop.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ThreadPools.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"

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
BlockAllocator volatileMemory(256);

static int vplWaitTimer = -1;
static int fplWaitTimer = -1;
static bool tlsplUsedIndexes[TLSPL_NUM_INDEXES];

// Thread -> TLSPL uids for thread end.
typedef std::multimap<SceUID, SceUID> TlsplMap;
static TlsplMap tlsplThreadEndChecks;
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
		delete [] blocks;
	}
	const char *GetName() override { return nf.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "FPL"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_FPLID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Fpl; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Fpl; }

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

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("FPL", 1);
		if (!s)
			return;

		Do(p, nf);
		if (p.mode == p.MODE_READ)
			blocks = new bool[nf.numBlocks];
		DoArray(p, blocks, nf.numBlocks);
		Do(p, address);
		Do(p, alignedSize);
		Do(p, nextBlock);
		FplWaitingThread dv = {0};
		Do(p, waitingThreads, dv);
		Do(p, pausedWaits);
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

	u32 FreeSize() const {
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
		nextFreeBlock_ = prev;
		b->next = SentinelPtr();
	}

	PSPPointer<SceKernelVplBlock> SplitBlock(PSPPointer<SceKernelVplBlock> b, u32 allocBlocks) {
		u32 prev = b.ptr;
		b->sizeInBlocks -= allocBlocks;

		b += b->sizeInBlocks;
		b->sizeInBlocks = allocBlocks;
		b->next = prev;

		return b;
	}

	inline void Validate() {
		auto lastBlock = LastBlock();
		_dbg_assert_msg_(nextFreeBlock_->next.ptr != SentinelPtr(), "Next free block should not be allocated.");
		_dbg_assert_msg_(nextFreeBlock_->next.ptr != sentinel_, "Next free block should not point to sentinel.");
		_dbg_assert_msg_(lastBlock->sizeInBlocks == 0, "Last block should have size of 0.");
		_dbg_assert_msg_(lastBlock->next.ptr != SentinelPtr(), "Last block should not be allocated.");
		_dbg_assert_msg_(lastBlock->next.ptr != sentinel_, "Last block should not point to sentinel.");

		auto b = PSPPointer<SceKernelVplBlock>::Create(FirstBlockPtr());
		bool sawFirstFree = false;
		while (b.ptr < lastBlock.ptr) {
			bool isFree = b->next.ptr != SentinelPtr();
			if (isFree) {
				if (!sawFirstFree) {
					_dbg_assert_msg_(lastBlock->next.ptr == b.ptr, "Last block should point to first free block.");
					sawFirstFree = true;
				}
				_dbg_assert_msg_(b->next.ptr != SentinelPtr(), "Free blocks should only point to other free blocks.");
				_dbg_assert_msg_(b->next.ptr > b.ptr, "Free blocks should be in order.");
				_dbg_assert_msg_(b + b->sizeInBlocks < b->next || b->next.ptr == lastBlock.ptr, "Two free blocks should not be next to each other.");
			} else {
				_dbg_assert_msg_(b->next.ptr == SentinelPtr(), "Allocated blocks should point to the sentinel.");
			}
			_dbg_assert_msg_(b->sizeInBlocks != 0, "Only the last block should have a size of 0.");
			b += b->sizeInBlocks;
		}
		if (!sawFirstFree) {
			_dbg_assert_msg_(lastBlock->next.ptr == lastBlock.ptr, "Last block should point to itself when full.");
		}
		_dbg_assert_msg_(b.ptr == lastBlock.ptr, "Blocks should not extend outside vpl.");
	}

	void ListBlocks() {
		auto b = PSPPointer<SceKernelVplBlock>::Create(FirstBlockPtr());
		auto lastBlock = LastBlock();
		while (b.ptr < lastBlock.ptr) {
			bool isFree = b->next.ptr != SentinelPtr();
			if (nextFreeBlock_ == b && isFree) {
				NOTICE_LOG(Log::sceKernel, "NEXT:  %x -> %x (size %x)", b.ptr - startPtr_, b->next.ptr - startPtr_, b->sizeInBlocks * 8);
			} else if (isFree) {
				NOTICE_LOG(Log::sceKernel, "FREE:  %x -> %x (size %x)", b.ptr - startPtr_, b->next.ptr - startPtr_, b->sizeInBlocks * 8);
			} else {
				NOTICE_LOG(Log::sceKernel, "BLOCK: %x (size %x)", b.ptr - startPtr_, b->sizeInBlocks * 8);
			}
			b += b->sizeInBlocks;
		}
		NOTICE_LOG(Log::sceKernel, "LAST:  %x -> %x (size %x)", lastBlock.ptr - startPtr_, lastBlock->next.ptr - startPtr_, lastBlock->sizeInBlocks * 8);
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
	const char *GetName() override { return nv.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "VPL"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VPLID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Vpl; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Vpl; }

	VPL() : alloc(8) {
		header = 0;
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("VPL", 1, 2);
		if (!s) {
			return;
		}

		Do(p, nv);
		Do(p, address);
		VplWaitingThread dv = {0};
		Do(p, waitingThreads, dv);
		alloc.DoState(p);
		Do(p, pausedWaits);

		if (s >= 2) {
			Do(p, header);
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
void __KernelTlsplThreadEnd(SceUID threadID);

void __KernelVplBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelVplEndCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelFplBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelFplEndCallback(SceUID threadID, SceUID prevCallbackId);

void __KernelMemoryInit()
{
	MemBlockInfoInit();
	kernelMemory.Init(PSP_GetKernelMemoryBase(), PSP_GetKernelMemoryEnd() - PSP_GetKernelMemoryBase(), false);
	userMemory.Init(PSP_GetUserMemoryBase(), PSP_GetUserMemoryEnd() - PSP_GetUserMemoryBase(), false);
	volatileMemory.Init(PSP_GetVolatileMemoryStart(), PSP_GetVolatileMemoryEnd() - PSP_GetVolatileMemoryStart(), false);
	ParallelMemset(&g_threadManager, Memory::GetPointerWrite(PSP_GetKernelMemoryBase()), 0, PSP_GetUserMemoryEnd() - PSP_GetKernelMemoryBase(), TaskPriority::HIGH);
	NotifyMemInfo(MemBlockFlags::WRITE, PSP_GetKernelMemoryBase(), PSP_GetUserMemoryEnd() - PSP_GetKernelMemoryBase(), "MemInit");
	INFO_LOG(Log::sceKernel, "Kernel and user memory pools initialized");

	vplWaitTimer = CoreTiming::RegisterEvent("VplTimeout", __KernelVplTimeout);
	fplWaitTimer = CoreTiming::RegisterEvent("FplTimeout", __KernelFplTimeout);

	flags_ = 0;
	sdkVersion_ = 0;
	compilerVersion_ = 0;
	memset(tlsplUsedIndexes, 0, sizeof(tlsplUsedIndexes));

	__KernelListenThreadEnd(&__KernelTlsplThreadEnd);

	__KernelRegisterWaitTypeFuncs(WAITTYPE_VPL, __KernelVplBeginCallback, __KernelVplEndCallback);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_FPL, __KernelFplBeginCallback, __KernelFplEndCallback);

	// The kernel statically allocates this memory, which has some code in it.
	// It appears this is used for some common funcs in Kernel_Library (memcpy, lwmutex, suspend intr, etc.)
	// Allocating this block is necessary to have the same memory semantics as real firmware.
	userMemory.AllocAt(PSP_GetUserMemoryBase(), 0x4000, "usersystemlib");
}

void __KernelMemoryDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelMemory", 1, 3);
	if (!s)
		return;

	kernelMemory.DoState(p);
	userMemory.DoState(p);
	if (s >= 3)
		volatileMemory.DoState(p);

	Do(p, vplWaitTimer);
	CoreTiming::RestoreRegisterEvent(vplWaitTimer, "VplTimeout", __KernelVplTimeout);
	Do(p, fplWaitTimer);
	CoreTiming::RestoreRegisterEvent(fplWaitTimer, "FplTimeout", __KernelFplTimeout);
	Do(p, flags_);
	Do(p, sdkVersion_);
	Do(p, compilerVersion_);
	DoArray(p, tlsplUsedIndexes, ARRAY_SIZE(tlsplUsedIndexes));
	if (s >= 2) {
		Do(p, tlsplThreadEndChecks);
	}

	MemBlockInfoDoState(p);
}

void __KernelMemoryShutdown()
{
#ifdef _DEBUG
	INFO_LOG(Log::sceKernel, "Shutting down volatile memory pool: ");
	volatileMemory.ListBlocks();
#endif
	volatileMemory.Shutdown();
#ifdef _DEBUG
	INFO_LOG(Log::sceKernel,"Shutting down user memory pool: ");
	userMemory.ListBlocks();
#endif
	userMemory.Shutdown();
#ifdef _DEBUG
	INFO_LOG(Log::sceKernel,"Shutting down \"kernel\" memory pool: ");
	kernelMemory.ListBlocks();
#endif
	kernelMemory.Shutdown();
	tlsplThreadEndChecks.clear();
	MemBlockInfoShutdown();
}

BlockAllocator *BlockAllocatorFromID(int id) {
	switch (id) {
	case 1:
	case 3:
	case 4:
		if (hleIsKernelMode())
			return &kernelMemory;
		return nullptr;

	case 2:
	case 6:
		return &userMemory;

	case 8:
	case 10:
		if (hleIsKernelMode())
			return &userMemory;
		return nullptr;

	case 5:
		return &volatileMemory;

	default:
		break;
	}

	return nullptr;
}

int BlockAllocatorToID(const BlockAllocator *alloc) {
	if (alloc == &kernelMemory)
		return 1;
	if (alloc == &userMemory)
		return 2;
	if (alloc == &volatileMemory)
		return 5;
	return 0;
}

BlockAllocator *BlockAllocatorFromAddr(u32 addr) {
	addr &= 0x3FFFFFFF;
	if (Memory::IsKernelAndNotVolatileAddress(addr))
		return &kernelMemory;
	if (Memory::IsKernelAddress(addr))
		return &volatileMemory;
	if (Memory::IsRAMAddress(addr))
		return &userMemory;
	return nullptr;
}

enum SceKernelFplAttr
{
	PSP_FPL_ATTR_FIFO     = 0x0000,
	PSP_FPL_ATTR_PRIORITY = 0x0100,
	PSP_FPL_ATTR_HIGHMEM  = 0x4000,
	PSP_FPL_ATTR_KNOWN    = PSP_FPL_ATTR_FIFO | PSP_FPL_ATTR_PRIORITY | PSP_FPL_ATTR_HIGHMEM,
};

static bool __KernelUnlockFplForThread(FPL *fpl, FplWaitingThread &threadInfo, u32 &error, int result, bool &wokeThreads)
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
			NotifyMemInfo(MemBlockFlags::SUB_ALLOC, blockPtr, fpl->alignedSize, "FplAllocate");
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
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFplCB: Suspending fpl wait for callback");
	else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
		ERROR_LOG_REPORT(Log::sceKernel, "sceKernelAllocateFplCB: wait not found to pause for callback");
	else
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelAllocateFplCB: beginning callback with bad wait id?");
}

void __KernelFplEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitEndCallback<FPL, WAITTYPE_FPL, FplWaitingThread>(threadID, prevCallbackId, fplWaitTimer, __KernelUnlockFplForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFplCB: Resuming mbx wait from callback");
}

static bool __FplThreadSortPriority(FplWaitingThread thread1, FplWaitingThread thread2)
{
	return __KernelThreadSortPriority(thread1.threadID, thread2.threadID);
}

static bool __KernelClearFplThreads(FPL *fpl, int reason)
{
	u32 error;
	bool wokeThreads = false;
	for (auto iter = fpl->waitingThreads.begin(), end = fpl->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockFplForThread(fpl, *iter, error, reason, wokeThreads);
	fpl->waitingThreads.clear();

	return wokeThreads;
}

static void __KernelSortFplThreads(FPL *fpl)
{
	// Remove any that are no longer waiting.
	SceUID uid = fpl->GetUID();
	HLEKernel::CleanupWaitingThreads(WAITTYPE_FPL, uid, fpl->waitingThreads);

	if ((fpl->nf.attr & PSP_FPL_ATTR_PRIORITY) != 0)
		std::stable_sort(fpl->waitingThreads.begin(), fpl->waitingThreads.end(), __FplThreadSortPriority);
}

int sceKernelCreateFpl(const char *name, u32 mpid, u32 attr, u32 blockSize, u32 numBlocks, u32 optPtr) {
	if (!name)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "invalid name");
	if (mpid < 1 || mpid > 9 || mpid == 7)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition %d", mpid);

	BlockAllocator *allocator = BlockAllocatorFromID(mpid);
	if (allocator == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_PERM, "invalid partition %d", mpid);
	if (((attr & ~PSP_FPL_ATTR_KNOWN) & ~0xFF) != 0)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter: %08x", attr);

	// There's probably a simpler way to get this same basic formula...
	// This is based on results from a PSP.
	bool illegalMemSize = blockSize == 0 || numBlocks == 0;
	if (!illegalMemSize && (u64) blockSize > ((0x100000000ULL / (u64) numBlocks) - 4ULL))
		illegalMemSize = true;
	if (!illegalMemSize && (u64) numBlocks >= 0x100000000ULL / (((u64) blockSize + 3ULL) & ~3ULL))
		illegalMemSize = true;
	if (illegalMemSize)
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE, "invalid blockSize/count");

	int alignment = 4;
	if (Memory::IsValidRange(optPtr, 4)) {
		u32 size = Memory::ReadUnchecked_U32(optPtr);
		if (size >= 4)
			alignment = Memory::Read_U32(optPtr + 4);
		// Must be a power of 2 to be valid.
		if ((alignment & (alignment - 1)) != 0)
			return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid alignment %d", alignment);
	}

	if (alignment < 4)
		alignment = 4;

	int alignedSize = ((int)blockSize + alignment - 1) & ~(alignment - 1);
	u32 totalSize = alignedSize * numBlocks;
	bool atEnd = (attr & PSP_FPL_ATTR_HIGHMEM) != 0;
	u32 address = allocator->Alloc(totalSize, atEnd, StringFromFormat("FPL/%s", name).c_str());
	if (address == (u32)-1) {
		DEBUG_LOG(Log::sceKernel, "sceKernelCreateFpl(\"%s\", partition=%i, attr=%08x, bsize=%i, nb=%i) FAILED - out of ram", 
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

	DEBUG_LOG(Log::sceKernel, "%i=sceKernelCreateFpl(\"%s\", partition=%i, attr=%08x, bsize=%i, nb=%i)", 
		id, name, mpid, attr, blockSize, numBlocks);

	return id;
}

int sceKernelDeleteFpl(SceUID uid)
{
	hleEatCycles(600);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelDeleteFpl(%i)", uid);

		bool wokeThreads = __KernelClearFplThreads(fpl, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("fpl deleted");

		BlockAllocator *alloc = BlockAllocatorFromAddr(fpl->address);
		_assert_msg_(alloc != nullptr, "Should always have a valid allocator/address");
		if (alloc)
			alloc->Free(fpl->address);
		return kernelObjects.Destroy<FPL>(uid);
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelDeleteFpl(%i): invalid fpl", uid);
		return error;
	}
}

void __KernelFplTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID) userdata;
	HLEKernel::WaitExecTimeout<FPL, WAITTYPE_FPL>(threadID);
}

static void __KernelSetFplTimeout(u32 timeoutPtr)
{
	if (timeoutPtr == 0 || fplWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// TODO: test for fpls.
	// This happens to be how the hardware seems to time things.
	if (micro <= 5)
		micro = 20;
	// Yes, this 7 is reproducible.  6 is (a lot) longer than 7.
	else if (micro == 7)
		micro = 25;
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
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFpl(%i, %08x, %08x)", uid, blockPtrAddr, timeoutPtr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			NotifyMemInfo(MemBlockFlags::SUB_ALLOC, blockPtr, fpl->alignedSize, "FplAllocate");
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
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFpl(%i, %08x, %08x): invalid fpl", uid, blockPtrAddr, timeoutPtr);
		return error;
	}
}

int sceKernelAllocateFplCB(SceUID uid, u32 blockPtrAddr, u32 timeoutPtr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFplCB(%i, %08x, %08x)", uid, blockPtrAddr, timeoutPtr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			NotifyMemInfo(MemBlockFlags::SUB_ALLOC, blockPtr, fpl->alignedSize, "FplAllocate");
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
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateFplCB(%i, %08x, %08x): invalid fpl", uid, blockPtrAddr, timeoutPtr);
		return error;
	}
}

int sceKernelTryAllocateFpl(SceUID uid, u32 blockPtrAddr)
{
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelTryAllocateFpl(%i, %08x)", uid, blockPtrAddr);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			NotifyMemInfo(MemBlockFlags::SUB_ALLOC, blockPtr, fpl->alignedSize, "FplAllocate");
			return 0;
		} else {
			return SCE_KERNEL_ERROR_NO_MEMORY;
		}
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelTryAllocateFpl(%i, %08x): invalid fpl", uid, blockPtrAddr);
		return error;
	}
}

int sceKernelFreeFpl(SceUID uid, u32 blockPtr)
{
	if (blockPtr > PSP_GetUserMemoryEnd()) {
		WARN_LOG(Log::sceKernel, "%08x=sceKernelFreeFpl(%i, %08x): invalid address", SCE_KERNEL_ERROR_ILLEGAL_ADDR, uid, blockPtr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl) {
		int blockNum = (blockPtr - fpl->address) / fpl->alignedSize;
		if (blockNum < 0 || blockNum >= fpl->nf.numBlocks) {
			DEBUG_LOG(Log::sceKernel, "sceKernelFreeFpl(%i, %08x): bad block ptr", uid, blockPtr);
			return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
		} else {
			if (fpl->freeBlock(blockNum)) {
				u32 blockPtr = fpl->address + fpl->alignedSize * blockNum;
				NotifyMemInfo(MemBlockFlags::SUB_FREE, blockPtr, fpl->alignedSize, "FplFree");

				DEBUG_LOG(Log::sceKernel, "sceKernelFreeFpl(%i, %08x)", uid, blockPtr);
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
				DEBUG_LOG(Log::sceKernel, "sceKernelFreeFpl(%i, %08x): already free", uid, blockPtr);
				return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
			}
		}
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelFreeFpl(%i, %08x): invalid fpl", uid, blockPtr);
		return error;
	}
}

int sceKernelCancelFpl(SceUID uid, u32 numWaitThreadsPtr)
{
	hleEatCycles(600);

	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl)
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelCancelFpl(%i, %08x)", uid, numWaitThreadsPtr);
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
		DEBUG_LOG(Log::sceKernel, "sceKernelCancelFpl(%i, %08x): invalid fpl", uid, numWaitThreadsPtr);
		return error;
	}
}

int sceKernelReferFplStatus(SceUID uid, u32 statusPtr) {
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(uid, error);
	if (fpl) {
		// Refresh waiting threads and free block count.
		__KernelSortFplThreads(fpl);
		fpl->nf.numWaitThreads = (int) fpl->waitingThreads.size();
		fpl->nf.numFreeBlocks = 0;
		for (int i = 0; i < (int)fpl->nf.numBlocks; ++i) {
			if (!fpl->blocks[i])
				++fpl->nf.numFreeBlocks;
		}
		auto status = PSPPointer<NativeFPL>::Create(statusPtr);
		if (status.IsValid() && status->size != 0) {
			*status = fpl->nf;
			status.NotifyWrite("FplStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogError(Log::sceKernel, error, "invalid fpl");
	}
}



//////////////////////////////////////////////////////////////////////////
// ALLOCATIONS
//////////////////////////////////////////////////////////////////////////
//00:49:12 <TyRaNiD> ector, well the partitions are 1 = kernel, 2 = user, 3 = me, 4 = kernel mirror :)

class PartitionMemoryBlock : public KernelObject
{
public:
	const char *GetName() override { return name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "MemoryPart"; }
	void GetQuickInfo(char *ptr, int size) override
	{
		int sz = alloc->GetBlockSizeFromAddress(address);
		snprintf(ptr, size, "MemPart: %08x - %08x	size: %08x", address, address + sz, sz);
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_UID; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_PMB; }
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_PMB; }

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

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("PMB", 1, 2);
		if (!s)
			return;

		Do(p, address);
		DoArray(p, name, sizeof(name));
		if (s >= 2) {
			int allocType = BlockAllocatorToID(alloc);
			Do(p, allocType);
			alloc = BlockAllocatorFromID(allocType);
		}
	}

	BlockAllocator *alloc;
	u32 address;
	char name[32];
};


static u32 sceKernelMaxFreeMemSize()
{
	u32 retVal = userMemory.GetLargestFreeBlockSize();
	DEBUG_LOG(Log::sceKernel, "%08x (dec %i)=sceKernelMaxFreeMemSize()", retVal, retVal);
	return retVal;
}

static u32 sceKernelTotalFreeMemSize()
{
	u32 retVal = userMemory.GetTotalFreeBytes();
	DEBUG_LOG(Log::sceKernel, "%08x (dec %i)=sceKernelTotalFreeMemSize()", retVal, retVal);
	return retVal;
}

int sceKernelAllocPartitionMemory(int partition, const char *name, int type, u32 size, u32 addr) {
	if (type < PSP_SMEM_Low || type > PSP_SMEM_HighAligned)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE, "invalid type %x", type);
	// Alignment is only allowed for powers of 2.
	if (type == PSP_SMEM_LowAligned || type == PSP_SMEM_HighAligned) {
		if ((addr & (addr - 1)) != 0 || addr == 0)
			return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE, "invalid alignment %x", addr);
	}
	if (partition < 1 || partition > 9 || partition == 7)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition %x", partition);

	BlockAllocator *allocator = BlockAllocatorFromID(partition);
	if (allocator == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_PARTITION, "invalid partition %x", partition);

	if (name == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid name");
	if (size == 0)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED, "invalid size %x", size);

	PartitionMemoryBlock *block = new PartitionMemoryBlock(allocator, name, size, (MemblockType)type, addr);
	if (!block->IsValid()) {
		delete block;
		ERROR_LOG(Log::sceKernel, "sceKernelAllocPartitionMemory(partition = %i, %s, type= %i, size= %i, addr= %08x): allocation failed", partition, name, type, size, addr);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	SceUID uid = kernelObjects.Create(block);

	DEBUG_LOG(Log::sceKernel,"%i = sceKernelAllocPartitionMemory(partition = %i, %s, type= %i, size= %i, addr= %08x)",
		uid, partition, name, type, size, addr);

	return uid;
}

int sceKernelFreePartitionMemory(SceUID id)
{
	DEBUG_LOG(Log::sceKernel,"sceKernelFreePartitionMemory(%d)",id);

	return kernelObjects.Destroy<PartitionMemoryBlock>(id);
}

u32 sceKernelGetBlockHeadAddr(SceUID id)
{
	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(id, error);
	if (block)
	{
		DEBUG_LOG(Log::sceKernel,"%08x = sceKernelGetBlockHeadAddr(%i)", block->address, id);
		return block->address;
	}
	else
	{
		ERROR_LOG(Log::sceKernel,"sceKernelGetBlockHeadAddr failed(%i)", id);
		return 0;
	}
}


static int sceKernelPrintf(const char *formatString)
{
	if (formatString == NULL)
		return -1;

	bool supported = true;
	int param = 1;
	char tempStr[24];
	char tempFormat[24] = {'%'};
	std::string result, format = formatString;
	std::stringstream stream;
	float f_arg;

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

		case 'f':
			static_assert(sizeof(float) == 4, "sizeof(float) != sizeof(u32)!");

			// Maybe worth replacing with std::bit_cast when (if) we move to C++20
			std::memcpy(&f_arg, &PARAM(param++), sizeof(u32));
			stream << f_arg;
			result += stream.str();
			
			++i;
			stream.str(std::string()); // Reset the stream
			break;

		default:
			supported = false;
			break;
		}

		if (param > 6)
			supported = false;
	}

	// Scrub for beeps and other suspicious control characters.
	for (size_t i = 0; i < result.size(); i++) {
		switch (result[i]) {
		case 7:  // BEL
		case 8:  // Backspace
			result[i] = ' ';
			break;
		}
	}

	// Just in case there were embedded strings that had \n's.
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);

	if (supported)
		INFO_LOG(Log::Printf, "sceKernelPrintf: %s", result.c_str());
	else
		ERROR_LOG(Log::Printf, "UNIMPL sceKernelPrintf(%s, %08x, %08x, %08x)", format.c_str(), PARAM(1), PARAM(2), PARAM(3));
	return 0;
}

static int sceKernelSetCompiledSdkVersion(int sdkVersion) {
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
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion370(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x03070000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion370 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion370(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion380_390(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x03080000 && sdkMainVersion != 0x03090000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion380_390 unknown SDK: %x", sdkVersion);
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion380_390(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion395(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFFFF00;
	if (sdkMainVersion != 0x04000000
			&& sdkMainVersion != 0x04000100
			&& sdkMainVersion != 0x04000500
			&& sdkMainVersion != 0x03090500
			&& sdkMainVersion != 0x03090600) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion395 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion395(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion600_602(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06010000
			&& sdkMainVersion != 0x06000000
			&& sdkMainVersion != 0x06020000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion600_602 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion600_602(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion500_505(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x05000000
			&& sdkMainVersion != 0x05050000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion500_505 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion500_505(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion401_402(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x04010000
			&& sdkMainVersion != 0x04020000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion401_402 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion401_402(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion507(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x05070000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion507 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion507(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion603_605(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06040000
			&& sdkMainVersion != 0x06030000
			&& sdkMainVersion != 0x06050000) {
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion603_605 unknown SDK: %x", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion603_605(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

static int sceKernelSetCompiledSdkVersion606(int sdkVersion) {
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if (sdkMainVersion != 0x06060000) {
		ERROR_LOG_REPORT(Log::sceKernel, "sceKernelSetCompiledSdkVersion606 unknown SDK: %x (would crash)", sdkVersion);
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompiledSdkVersion606(%08x)", sdkVersion);
	sdkVersion_ = sdkVersion;
	flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	return 0;
}

int sceKernelGetCompiledSdkVersion() {
	if (!(flags_ & SCE_KERNEL_HASCOMPILEDSDKVERSION))
		return 0;
	return sdkVersion_;
}

static int sceKernelSetCompilerVersion(int version) {
	DEBUG_LOG(Log::sceKernel, "sceKernelSetCompilerVersion(%08x)", version);
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

static bool __KernelUnlockVplForThread(VPL *vpl, VplWaitingThread &threadInfo, u32 &error, int result, bool &wokeThreads) {
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
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateVplCB: Suspending vpl wait for callback");
	else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
		ERROR_LOG_REPORT(Log::sceKernel, "sceKernelAllocateVplCB: wait not found to pause for callback");
	else
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelAllocateVplCB: beginning callback with bad wait id?");
}

void __KernelVplEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitEndCallback<VPL, WAITTYPE_VPL, VplWaitingThread>(threadID, prevCallbackId, vplWaitTimer, __KernelUnlockVplForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(Log::sceKernel, "sceKernelAllocateVplCB: Resuming mbx wait from callback");
}

static bool __VplThreadSortPriority(VplWaitingThread thread1, VplWaitingThread thread2)
{
	return __KernelThreadSortPriority(thread1.threadID, thread2.threadID);
}

static bool __KernelClearVplThreads(VPL *vpl, int reason)
{
	u32 error;
	bool wokeThreads = false;
	for (auto iter = vpl->waitingThreads.begin(), end = vpl->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockVplForThread(vpl, *iter, error, reason, wokeThreads);
	vpl->waitingThreads.clear();

	return wokeThreads;
}

static void __KernelSortVplThreads(VPL *vpl)
{
	// Remove any that are no longer waiting.
	SceUID uid = vpl->GetUID();
	HLEKernel::CleanupWaitingThreads(WAITTYPE_VPL, uid, vpl->waitingThreads);

	if ((vpl->nv.attr & PSP_VPL_ATTR_PRIORITY) != 0)
		std::stable_sort(vpl->waitingThreads.begin(), vpl->waitingThreads.end(), __VplThreadSortPriority);
}

SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr) {
	if (!name)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid name");
	if (partition < 1 || partition > 9 || partition == 7)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition %d", partition);

	BlockAllocator *allocator = BlockAllocatorFromID(partition);
	if (allocator == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_PERM, "invalid partition %d", partition);

	if (((attr & ~PSP_VPL_ATTR_KNOWN) & ~0xFF) != 0)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter: %08x", attr);
	if (vplSize == 0)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE, "invalid size");
	// Block Allocator seems to A-OK this, let's stop it here.
	if (vplSize >= 0x80000000)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "way too big size");

	// Can't have that little space in a Vpl, sorry.
	if (vplSize <= 0x30)
		vplSize = 0x1000;
	vplSize = (vplSize + 7) & ~7;

	// We ignore the upalign to 256 and do it ourselves by 8.
	u32 allocSize = vplSize;
	u32 memBlockPtr = allocator->Alloc(allocSize, (attr & PSP_VPL_ATTR_HIGHMEM) != 0, StringFromFormat("VPL/%s", name).c_str());
	if (memBlockPtr == (u32)-1)
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "failed to allocate %i bytes of pool data", vplSize);

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
	vpl->alloc.Init(vpl->address, vpl->nv.poolSize, true);

	vpl->header = PSPPointer<SceKernelVplHeader>::Create(memBlockPtr);
	vpl->header->Init(memBlockPtr, vplSize);

	DEBUG_LOG(Log::sceKernel, "%x=sceKernelCreateVpl(\"%s\", block=%i, attr=%i, size=%i)", 
		id, name, partition, vpl->nv.attr, vpl->nv.poolSize);

	if (optPtr != 0)
	{
		u32 size = Memory::Read_U32(optPtr);
		if (size > 4)
			WARN_LOG_REPORT(Log::sceKernel, "sceKernelCreateVpl(): unsupported options parameter, size = %d", size);
	}

	return id;
}

int sceKernelDeleteVpl(SceUID uid)
{
	DEBUG_LOG(Log::sceKernel, "sceKernelDeleteVpl(%i)", uid);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		bool wokeThreads = __KernelClearVplThreads(vpl, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("vpl deleted");

		BlockAllocator *alloc = BlockAllocatorFromAddr(vpl->address);
		_assert_msg_(alloc != nullptr, "Should always have a valid allocator/address");
		if (alloc)
			alloc->Free(vpl->address);
		kernelObjects.Destroy<VPL>(uid);
		return 0;
	}
	else
		return error;
}

// Returns false for invalid parameters (e.g. don't check callbacks, etc.)
// Successful allocation is indicated by error == 0.
static bool __KernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 &error, bool trying, const char *funcname) {
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl) {
		if (size == 0 || size > (u32) vpl->nv.poolSize) {
			WARN_LOG(Log::sceKernel, "%s(vpl=%i, size=%i, ptrout=%08x): invalid size", funcname, uid, size, addrPtr);
			error = SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
			return false;
		}

		VERBOSE_LOG(Log::sceKernel, "%s(vpl=%i, size=%i, ptrout=%08x)", funcname, uid, size, addrPtr);

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
			addr = vpl->alloc.Alloc(allocSize, true, "VplAllocate");
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

static void __KernelSetVplTimeout(u32 timeoutPtr)
{
	if (timeoutPtr == 0 || vplWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 5)
		micro = 20;
	// Yes, this 7 is reproducible.  6 is (a lot) longer than 7.
	else if (micro == 7)
		micro = 25;
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
		WARN_LOG(Log::sceKernel, "%08x=sceKernelFreeVpl(%i, %08x): Invalid address", SCE_KERNEL_ERROR_ILLEGAL_ADDR, uid, addr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	VERBOSE_LOG(Log::sceKernel, "sceKernelFreeVpl(%i, %08x)", uid, addr);
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
			WARN_LOG(Log::sceKernel, "%08x=sceKernelFreeVpl(%i, %08x): Unable to free", SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK, uid, addr);
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
		DEBUG_LOG(Log::sceKernel, "sceKernelCancelVpl(%i, %08x)", uid, numWaitThreadsPtr);
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
		DEBUG_LOG(Log::sceKernel, "sceKernelCancelVpl(%i, %08x): invalid vpl", uid, numWaitThreadsPtr);
		return error;
	}
}

int sceKernelReferVplStatus(SceUID uid, u32 infoPtr) {
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl) {
		__KernelSortVplThreads(vpl);
		vpl->nv.numWaitThreads = (int) vpl->waitingThreads.size();
		if (vpl->header.IsValid()) {
			vpl->nv.freeSize = vpl->header->FreeSize();
		} else {
			vpl->nv.freeSize = vpl->alloc.GetTotalFreeBytes();
		}
		auto info = PSPPointer<SceKernelVplInfo>::Create(infoPtr);
		if (info.IsValid() && info->size != 0) {
			*info = vpl->nv;
			info.NotifyWrite("VplStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogError(Log::sceKernel, error, "invalid vpl");
	}
}

static u32 AllocMemoryBlock(const char *pname, u32 type, u32 size, u32 paramsAddr) {
	if (Memory::IsValidAddress(paramsAddr) && Memory::Read_U32(paramsAddr) != 4) {
		ERROR_LOG_REPORT(Log::sceKernel, "AllocMemoryBlock(%s): unsupported params size %d", pname, Memory::Read_U32(paramsAddr));
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	if (type != PSP_SMEM_High && type != PSP_SMEM_Low) {
		ERROR_LOG_REPORT(Log::sceKernel, "AllocMemoryBlock(%s): unsupported type %d", pname, type);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE;
	}
	if (size == 0) {
		WARN_LOG_REPORT(Log::sceKernel, "AllocMemoryBlock(%s): invalid size %x", pname, size);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	if (pname == NULL) {
		ERROR_LOG_REPORT(Log::sceKernel, "AllocMemoryBlock(): NULL name");
		return SCE_KERNEL_ERROR_ERROR;
	}

	PartitionMemoryBlock *block = new PartitionMemoryBlock(&userMemory, pname, size, (MemblockType)type, 0);
	if (!block->IsValid())
	{
		delete block;
		ERROR_LOG(Log::sceKernel, "AllocMemoryBlock(%s, %i, %08x, %08x): allocation failed", pname, type, size, paramsAddr);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	SceUID uid = kernelObjects.Create(block);

	INFO_LOG(Log::sceKernel,"%08x=AllocMemoryBlock(SysMemUserForUser_FE707FDF)(%s, %i, %08x, %08x)", uid, pname, type, size, paramsAddr);
	return uid;
}

static u32 FreeMemoryBlock(u32 uid) {
	INFO_LOG(Log::sceKernel, "FreeMemoryBlock(%08x)", uid);
	return kernelObjects.Destroy<PartitionMemoryBlock>(uid);
}

static u32 GetMemoryBlockPtr(u32 uid, u32 addr) {
	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(uid, error);
	if (block)
	{
		INFO_LOG(Log::sceKernel, "GetMemoryBlockPtr(%08x, %08x) = %08x", uid, addr, block->address);
		Memory::Write_U32(block->address, addr);
		return 0;
	}
	else
	{
		ERROR_LOG(Log::sceKernel, "GetMemoryBlockPtr(%08x, %08x) failed", uid, addr);
		return 0;
	}
}

static u32 SysMemUserForUser_D8DE5C1E() {
	// Called by Evangelion Jo and return 0 here to go in-game.
	ERROR_LOG(Log::sceKernel,"UNIMPL SysMemUserForUser_D8DE5C1E()");
	return 0; 
}

static u32 SysMemUserForUser_ACBD88CA() {
	ERROR_LOG_REPORT_ONCE(SysMemUserForUser_ACBD88CA, Log::sceKernel, "UNIMPL SysMemUserForUser_ACBD88CA()");
	return 0; 
}

static u32 SysMemUserForUser_945E45DA() {
	// Called by Evangelion Jo and expected return 0 here.
	ERROR_LOG_REPORT_ONCE(SysMemUserForUser945E45DA, Log::sceKernel, "UNIMPL SysMemUserForUser_945E45DA()");
	return 0; 
}

enum
{
	PSP_ERROR_UNKNOWN_TLSPL_ID = 0x800201D0,
	PSP_ERROR_TOO_MANY_TLSPL   = 0x800201D1,
	PSP_ERROR_TLSPL_IN_USE     = 0x800201D2,
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
	const char *GetName() override { return ntls.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "TLS"; }
	static u32 GetMissingErrorCode() { return PSP_ERROR_UNKNOWN_TLSPL_ID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Tlspl; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Tlspl; }

	TLSPL() : next(0) {}

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("TLS", 1, 2);
		if (!s)
			return;

		Do(p, ntls);
		Do(p, address);
		if (s >= 2)
			Do(p, alignment);
		else
			alignment = 4;
		Do(p, waitingThreads);
		Do(p, next);
		Do(p, usage);
	}

	NativeTlspl ntls;
	u32 address;
	u32 alignment;
	std::vector<SceUID> waitingThreads;
	int next;
	std::vector<SceUID> usage;
};

KernelObject *__KernelTlsplObject()
{
	return new TLSPL;
}

static void __KernelSortTlsplThreads(TLSPL *tls)
{
	// Remove any that are no longer waiting.
	SceUID uid = tls->GetUID();
	HLEKernel::CleanupWaitingThreads(WAITTYPE_TLSPL, uid, tls->waitingThreads);

	if ((tls->ntls.attr & PSP_FPL_ATTR_PRIORITY) != 0)
		std::stable_sort(tls->waitingThreads.begin(), tls->waitingThreads.end(), __KernelThreadSortPriority);
}

int __KernelFreeTls(TLSPL *tls, SceUID threadID)
{
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
		SceUID uid = tls->GetUID();

		u32 alignedSize = (tls->ntls.blockSize + tls->alignment - 1) & ~(tls->alignment - 1);
		u32 freedAddress = tls->address + freeBlock * alignedSize;
		NotifyMemInfo(MemBlockFlags::SUB_ALLOC, freedAddress, tls->ntls.blockSize, "TlsFree");

		// Whenever freeing a block, clear it (even if it's not going to wake anyone.)
		Memory::Memset(freedAddress, 0, tls->ntls.blockSize, "TlsFree");

		// First, let's remove the end check for the freeing thread.
		auto freeingLocked = tlsplThreadEndChecks.equal_range(threadID);
		for (TlsplMap::iterator iter = freeingLocked.first; iter != freeingLocked.second; ++iter)
		{
			if (iter->second == uid)
			{
				tlsplThreadEndChecks.erase(iter);
				break;
			}
		}

		__KernelSortTlsplThreads(tls);
		while (!tls->waitingThreads.empty())
		{
			SceUID waitingThreadID = tls->waitingThreads[0];
			tls->waitingThreads.erase(tls->waitingThreads.begin());

			// This thread must've been woken up.
			if (!HLEKernel::VerifyWait(waitingThreadID, WAITTYPE_TLSPL, uid))
				continue;

			// Otherwise, if there was a thread waiting, we were full, so this newly freed one is theirs.
			tls->usage[freeBlock] = waitingThreadID;
			__KernelResumeThreadFromWait(waitingThreadID, freedAddress);

			// Gotta watch the thread to quit as well, since they've allocated now.
			tlsplThreadEndChecks.emplace(waitingThreadID, uid);

			// No need to continue or free it, we're done.
			return 0;
		}

		// No one was waiting, so now we can really free it.
		tls->usage[freeBlock] = 0;
		++tls->ntls.freeBlocks;
		return 0;
	}
	// We say "okay" even though nothing was freed.
	else
		return 0;
}

void __KernelTlsplThreadEnd(SceUID threadID)
{
	u32 error;

	// It wasn't waiting, was it?
	SceUID waitingTlsID = __KernelGetWaitID(threadID, WAITTYPE_TLSPL, error);
	if (waitingTlsID)
	{
		TLSPL *tls = kernelObjects.Get<TLSPL>(waitingTlsID, error);
		if (tls)
			tls->waitingThreads.erase(std::remove(tls->waitingThreads.begin(), tls->waitingThreads.end(), threadID), tls->waitingThreads.end());
	}

	// Unlock all pools the thread had locked.
	auto locked = tlsplThreadEndChecks.equal_range(threadID);
	for (TlsplMap::iterator iter = locked.first; iter != locked.second; ++iter)
	{
		SceUID tlsID = iter->second;
		TLSPL *tls = kernelObjects.Get<TLSPL>(tlsID, error);

		if (tls)
		{
			__KernelFreeTls(tls, threadID);

			// Restart the loop, freeing mutated it.
			locked = tlsplThreadEndChecks.equal_range(threadID);
			iter = locked.first;
			if (locked.first == locked.second)
				break;
		}
	}
	tlsplThreadEndChecks.erase(locked.first, locked.second);
}

SceUID sceKernelCreateTlspl(const char *name, u32 partition, u32 attr, u32 blockSize, u32 count, u32 optionsPtr) {
	if (!name)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "invalid name");
	if ((attr & ~PSP_TLSPL_ATTR_KNOWN) >= 0x100)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter: %08x", attr);
	if (partition < 1 || partition > 9 || partition == 7)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition %d", partition);

	BlockAllocator *allocator = BlockAllocatorFromID(partition);
	if (allocator == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_PERM, "invalid partition %x", partition);

	// There's probably a simpler way to get this same basic formula...
	// This is based on results from a PSP.
	bool illegalMemSize = blockSize == 0 || count == 0;
	if (!illegalMemSize && (u64) blockSize > ((0x100000000ULL / (u64) count) - 4ULL))
		illegalMemSize = true;
	if (!illegalMemSize && (u64) count >= 0x100000000ULL / (((u64) blockSize + 3ULL) & ~3ULL))
		illegalMemSize = true;
	if (illegalMemSize)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE, "invalid blockSize/count");

	int index = -1;
	for (int i = 0; i < TLSPL_NUM_INDEXES; ++i) {
		if (tlsplUsedIndexes[i] == false) {
			index = i;
			break;
		}
	}

	if (index == -1)
		return hleLogWarning(Log::sceKernel, PSP_ERROR_TOO_MANY_TLSPL, "ran out of indexes for TLS pools");

	// Unless otherwise specified, we align to 4 bytes (a mips word.)
	u32 alignment = 4;
	if (Memory::IsValidRange(optionsPtr, 4)) {
		u32 size = Memory::ReadUnchecked_U32(optionsPtr);
		if (size >= 8)
			alignment = Memory::Read_U32(optionsPtr + 4);

		// Note that 0 intentionally is allowed.
		if ((alignment & (alignment - 1)) != 0)
			return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "alignment is not a power of 2: %d", alignment);
		// This goes for 0, 1, and 2.  Can't have less than 4 byte alignment.
		if (alignment < 4)
			alignment = 4;
	}

	// Upalign.  Strangely, the sceKernelReferTlsplStatus value is the original.
	u32 alignedSize = (blockSize + alignment - 1) & ~(alignment - 1);

	u32 totalSize = alignedSize * count;
	u32 blockPtr = allocator->Alloc(totalSize, (attr & PSP_TLSPL_ATTR_HIGHMEM) != 0, StringFromFormat("TLS/%s", name).c_str());
#ifdef _DEBUG
	allocator->ListBlocks();
#endif

	if (blockPtr == (u32)-1)
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "failed to allocate memory");

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
	tls->alignment = alignment;
	tls->usage.resize(count, 0);

	return hleLogSuccessInfoI(Log::sceKernel, id);
}

int sceKernelDeleteTlspl(SceUID uid)
{
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		bool inUse = false;
		for (SceUID threadID : tls->usage)
		{
			if (threadID != 0 && threadID != __KernelGetCurThread())
				inUse = true;
		}
		if (inUse)
		{
			error = PSP_ERROR_TLSPL_IN_USE;
			WARN_LOG(Log::sceKernel, "%08x=sceKernelDeleteTlspl(%08x): in use", error, uid);
			return error;
		}

		WARN_LOG(Log::sceKernel, "sceKernelDeleteTlspl(%08x)", uid);

		for (SceUID threadID : tls->waitingThreads)
			HLEKernel::ResumeFromWait(threadID, WAITTYPE_TLSPL, uid, 0);
		hleReSchedule("deleted tlspl");

		BlockAllocator *allocator = BlockAllocatorFromAddr(tls->address);
		_assert_msg_(allocator != nullptr, "Should always have a valid allocator/address");
		if (allocator)
			allocator->Free(tls->address);
		tlsplUsedIndexes[tls->ntls.index] = false;
		kernelObjects.Destroy<TLSPL>(uid);
	}
	else
		ERROR_LOG(Log::sceKernel, "%08x=sceKernelDeleteTlspl(%08x): bad tlspl", error, uid);
	return error;
}

struct FindTLSByIndexArg {
	int index;
	TLSPL *result = nullptr;
};

static bool FindTLSByIndex(TLSPL *possible, FindTLSByIndexArg *state) {
	if (possible->ntls.index == state->index) {
		state->result = possible;
		return false;
	}
	return true;
}

int sceKernelGetTlsAddr(SceUID uid) {
	if (!__KernelIsDispatchEnabled() || __IsInInterrupt())
		return hleLogWarning(Log::sceKernel, 0, "dispatch disabled");

	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (!tls) {
		if (uid < 0)
			return hleLogError(Log::sceKernel, 0, "tlspl not found");

		// There's this weird behavior where it looks up by index.  Maybe we shouldn't use uids...
		if (!tlsplUsedIndexes[(uid >> 3) & 15])
			return hleLogError(Log::sceKernel, 0, "tlspl not found");

		FindTLSByIndexArg state;
		state.index = (uid >> 3) & 15;
		kernelObjects.Iterate<TLSPL>(&FindTLSByIndex, &state);
		if (!state.result)
			return hleLogError(Log::sceKernel, 0, "tlspl not found");

		tls = state.result;
	}

	SceUID threadID = __KernelGetCurThread();
	int allocBlock = -1;
	bool needsClear = false;

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
			tlsplThreadEndChecks.emplace(threadID, uid);
			--tls->ntls.freeBlocks;
			needsClear = true;
		}
	}

	if (allocBlock == -1)
	{
		tls->waitingThreads.push_back(threadID);
		__KernelWaitCurThread(WAITTYPE_TLSPL, uid, 1, 0, false, "allocate tls");
		return hleLogDebug(Log::sceKernel, 0, "waiting for tls alloc");
	}

	u32 alignedSize = (tls->ntls.blockSize + tls->alignment - 1) & ~(tls->alignment - 1);
	u32 allocAddress = tls->address + allocBlock * alignedSize;
	NotifyMemInfo(MemBlockFlags::SUB_ALLOC, allocAddress, tls->ntls.blockSize, "TlsAddr");

	// We clear the blocks upon first allocation (and also when they are freed, both are necessary.)
	if (needsClear) {
		Memory::Memset(allocAddress, 0, tls->ntls.blockSize, "TlsAddr");
	}

	return hleLogDebug(Log::sceKernel, allocAddress);
}

// Parameters are an educated guess.
int sceKernelFreeTlspl(SceUID uid)
{
	WARN_LOG(Log::sceKernel, "UNIMPL sceKernelFreeTlspl(%08x)", uid);
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls)
	{
		SceUID threadID = __KernelGetCurThread();
		return __KernelFreeTls(tls, threadID);
	}
	else
		return error;
}

int sceKernelReferTlsplStatus(SceUID uid, u32 infoPtr) {
	u32 error;
	TLSPL *tls = kernelObjects.Get<TLSPL>(uid, error);
	if (tls) {
		// Update the waiting threads in case of deletions, etc.
		__KernelSortTlsplThreads(tls);
		tls->ntls.numWaitThreads = (int) tls->waitingThreads.size();

		auto info = PSPPointer<NativeTlspl>::Create(infoPtr);
		if (info.IsValid() && info->size != 0) {
			*info = tls->ntls;
			info.NotifyWrite("TlsplStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogError(Log::sceKernel, error, "invalid tlspl");
	}
}

const HLEFunction SysMemUserForUser[] = {
	{0XA291F107, &WrapU_V<sceKernelMaxFreeMemSize>,               "sceKernelMaxFreeMemSize",               'x', ""     },
	{0XF919F628, &WrapU_V<sceKernelTotalFreeMemSize>,             "sceKernelTotalFreeMemSize",             'x', ""     },
	{0X3FC9AE6A, &WrapU_V<sceKernelDevkitVersion>,                "sceKernelDevkitVersion",                'x', ""     },
	{0X237DBD4F, &WrapI_ICIUU<sceKernelAllocPartitionMemory>,     "sceKernelAllocPartitionMemory",         'i', "isixx"},
	{0XB6D61D02, &WrapI_I<sceKernelFreePartitionMemory>,          "sceKernelFreePartitionMemory",          'i', "i"    },
	{0X9D9A5BA1, &WrapU_I<sceKernelGetBlockHeadAddr>,             "sceKernelGetBlockHeadAddr",             'x', "i"    },
	{0X13A5ABEF, &WrapI_C<sceKernelPrintf>,                       "sceKernelPrintf",                       'i', "s"    },
	{0X7591C7DB, &WrapI_I<sceKernelSetCompiledSdkVersion>,        "sceKernelSetCompiledSdkVersion",        'i', "i"    },
	{0X342061E5, &WrapI_I<sceKernelSetCompiledSdkVersion370>,     "sceKernelSetCompiledSdkVersion370",     'i', "i"    },
	{0X315AD3A0, &WrapI_I<sceKernelSetCompiledSdkVersion380_390>, "sceKernelSetCompiledSdkVersion380_390", 'i', "i"    },
	{0XEBD5C3E6, &WrapI_I<sceKernelSetCompiledSdkVersion395>,     "sceKernelSetCompiledSdkVersion395",     'i', "i"    },
	{0X057E7380, &WrapI_I<sceKernelSetCompiledSdkVersion401_402>, "sceKernelSetCompiledSdkVersion401_402", 'i', "i"    },
	{0XF77D77CB, &WrapI_I<sceKernelSetCompilerVersion>,           "sceKernelSetCompilerVersion",           'i', "i"    },
	{0X91DE343C, &WrapI_I<sceKernelSetCompiledSdkVersion500_505>, "sceKernelSetCompiledSdkVersion500_505", 'i', "i"    },
	{0X7893F79A, &WrapI_I<sceKernelSetCompiledSdkVersion507>,     "sceKernelSetCompiledSdkVersion507",     'i', "i"    },
	{0X35669D4C, &WrapI_I<sceKernelSetCompiledSdkVersion600_602>, "sceKernelSetCompiledSdkVersion600_602", 'i', "i"    },  //??
	{0X1B4217BC, &WrapI_I<sceKernelSetCompiledSdkVersion603_605>, "sceKernelSetCompiledSdkVersion603_605", 'i', "i"    },
	{0X358CA1BB, &WrapI_I<sceKernelSetCompiledSdkVersion606>,     "sceKernelSetCompiledSdkVersion606",     'i', "i"    },
	{0XFC114573, &WrapI_V<sceKernelGetCompiledSdkVersion>,        "sceKernelGetCompiledSdkVersion",        'i', ""     },
	{0X2A3E5280, nullptr,                                         "sceKernelQueryMemoryInfo",              '?', ""     },
	{0XACBD88CA, &WrapU_V<SysMemUserForUser_ACBD88CA>,            "SysMemUserForUser_ACBD88CA",            'x', ""     },
	{0X945E45DA, &WrapU_V<SysMemUserForUser_945E45DA>,            "SysMemUserForUser_945E45DA",            'x', ""     },
	{0XA6848DF8, nullptr,                                         "sceKernelSetUsersystemLibWork",         '?', ""     },
	{0X6231A71D, nullptr,                                         "sceKernelSetPTRIG",                     '?', ""     },
	{0X39F49610, nullptr,                                         "sceKernelGetPTRIG",                     '?', ""     },
	// Obscure raw block API
	{0XDB83A952, &WrapU_UU<GetMemoryBlockPtr>,                    "SysMemUserForUser_DB83A952",            'x', "xx"   },  // GetMemoryBlockAddr
	{0X50F61D8A, &WrapU_U<FreeMemoryBlock>,                       "SysMemUserForUser_50F61D8A",            'x', "x"    },  // FreeMemoryBlock
	{0XFE707FDF, &WrapU_CUUU<AllocMemoryBlock>,                   "SysMemUserForUser_FE707FDF",            'x', "sxxx" },  // AllocMemoryBlock
	{0XD8DE5C1E, &WrapU_V<SysMemUserForUser_D8DE5C1E>,            "SysMemUserForUser_D8DE5C1E",            'x', ""     },
};

void Register_SysMemUserForUser() {
	RegisterModule("SysMemUserForUser", ARRAY_SIZE(SysMemUserForUser), SysMemUserForUser);
}
