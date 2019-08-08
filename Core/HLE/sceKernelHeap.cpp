#pragma once

#include <string>

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelHeap.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/Util/BlockAllocator.h"

struct HeapInformation : public KernelObject {
	int uid = 0;
	int partitionId = 0;
	u32 size = 0;
	int flags = 0;
	u32 address = 0;
	std::string name;
	BlockAllocator alloc;

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_UID; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_Heap; }
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_Heap; }

	void DoState(PointerWrap &p) {
		p.Do(uid);
		p.Do(partitionId);
		p.Do(size);
		p.Do(flags);
		p.Do(address);
		p.Do(name);
		p.Do(alloc);
	}
};

std::map<u32, HeapInformation *> heapInformationList;

void __HeapInformationDoState(PointerWrap &p) {
	auto s = p.Section("kernelHeap", 1);
	if (!s)
		return;

	if (s >= 1) {
		p.Do(heapInformationList);
	}
}

static HeapInformation  *getHeap(u32 heapId) {
	auto found = heapInformationList.find(heapId);
	if (found == heapInformationList.end()) {
		return NULL;
	}
	return found->second;
}

const u32 HEAP_BLOCK_HEADER_SIZE = 8;
const bool frombottom = false;

static int sceKernelCreateHeap(int partitionId, int size, int flags, const char *Name) {
	HeapInformation *heap = new HeapInformation;
	heap->partitionId = partitionId;
	heap->flags = flags;
	heap->name = *Name;
	int allocSize = (size + 3) & ~3;
	heap->size = allocSize;
	u32 addr = userMemory.Alloc(heap->size, frombottom, "SysMemForKernel-Heap");
	if (addr == (u32)-1) {
		ERROR_LOG(HLE, "sceKernelCreateHeap(): Failed to allocate %i bytes memory", size);
		heap->uid = -1;
		delete heap;
	}
	heap->address = addr;
	heap->alloc.Init(heap->address + 128, heap->size - 128);
	SceUID uid = kernelObjects.Create(heap);
	heap->uid = uid;
	heapInformationList[uid] = heap;
	return hleLogSuccessInfoX(SCEKERNEL, uid, "");
}

static int sceKernelAllocHeapMemory(int heapId, int size) {
	HeapInformation *heap = getHeap(heapId);
	if (!heap) {
		ERROR_LOG(HLE, "sceKernelAllocHeapMemory cannot find heapId", heapId);
		return 0;
	}

	// There's 8 bytes at the end of every block, reserved.
	u32 memSize = HEAP_BLOCK_HEADER_SIZE + size;
	u32 addr = heap->alloc.Alloc(memSize, true);
	return hleLogSuccessInfoX(SCEKERNEL, addr, "");
}

static int sceKernelDeleteHeap(int heapId) {
	HeapInformation *heap = getHeap(heapId);
	if (!heap) {
		ERROR_LOG(HLE, "sceKernelDeleteHeap(%i): invalid heapId", heapId);
		return -1;
	}
	userMemory.Free(heap->address);
	kernelObjects.Destroy<HeapInformation>(heap->uid);
	heapInformationList.erase(heapId);
	return hleLogSuccessInfoX(SCEKERNEL, 0, "");
}

const HLEFunction SysMemForKernel[] =
{
	{ 0X636C953B, &WrapI_II<sceKernelAllocHeapMemory>,                "sceKernelAllocHeapMemory",                  'I', "ii" },
	{ 0XC9805775, &WrapI_I<sceKernelDeleteHeap>,                      "sceKernelDeleteHeap",                       'I', "i" },
	{ 0X1C1FBFE7, &WrapI_IIIC<sceKernelCreateHeap>,                   "sceKernelCreateHeap",                       'I', "iiis" },
};

void Register_SysMemForKernel() {
	RegisterModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}
