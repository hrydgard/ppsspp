#include <string>

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelHeap.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/Util/BlockAllocator.h"

static const u32 HEAP_BLOCK_HEADER_SIZE = 8;
static const bool g_fromBottom = false;

struct Heap : public KernelObject {
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

	void DoState(PointerWrap &p) override {
		p.Do(uid);
		p.Do(partitionId);
		p.Do(size);
		p.Do(flags);
		p.Do(address);
		p.Do(name);
		p.Do(alloc);
	}
};

static int sceKernelCreateHeap(int partitionId, int size, int flags, const char *Name) {
	u32 allocSize = (size + 3) & ~3;

	// TODO: partitionId should probably decide if we allocate from userMemory or kernel or whatever...
	u32 addr = userMemory.Alloc(allocSize, g_fromBottom, "SysMemForKernel-Heap");
	if (addr == (u32)-1) {
		ERROR_LOG(HLE, "sceKernelCreateHeap(partitionId=%d): Failed to allocate %d bytes memory", partitionId, size);
		return SCE_KERNEL_ERROR_NO_MEMORY;  // Blind guess
	}

	Heap *heap = new Heap();
	SceUID uid = kernelObjects.Create(heap);

	heap->partitionId = partitionId;
	heap->flags = flags;
	heap->name = Name ? Name : "";  // Not sure if this needs validation.
	heap->size = allocSize;
	heap->address = addr;
	heap->alloc.Init(heap->address + 128, heap->size - 128);
	heap->uid = uid;
	return hleLogSuccessInfoX(SCEKERNEL, uid);
}

static int sceKernelAllocHeapMemory(int heapId, int size) {
	u32 error;
	Heap *heap = kernelObjects.Get<Heap>(heapId, error);
	if (heap) {
		// There's 8 bytes at the end of every block, reserved.
		u32 memSize = HEAP_BLOCK_HEADER_SIZE + size;
		u32 addr = heap->alloc.Alloc(memSize, true);
		return hleLogSuccessInfoX(SCEKERNEL, addr);
	} else {
		return hleLogError(SCEKERNEL, error, "sceKernelAllocHeapMemory(%d): invalid heapId", heapId);
	}
}

static int sceKernelDeleteHeap(int heapId) {
	u32 error;
	Heap *heap = kernelObjects.Get<Heap>(heapId, error);
	if (heap) {
		userMemory.Free(heap->address);
		kernelObjects.Destroy<Heap>(heap->uid);
		return hleLogSuccessInfoX(SCEKERNEL, 0);
	} else {
		return hleLogError(SCEKERNEL, error, "sceKernelDeleteHeap(%d): invalid heapId", heapId);
	}
}

static u32 sceKernelPartitionTotalFreeMemSize(int partitionId) {
	ERROR_LOG(SCEKERNEL, "UNIMP sceKernelPartitionTotalFreeMemSize(%d)", partitionId);
	//Need more work #13021
	///We ignore partitionId for now
	return userMemory.GetTotalFreeBytes();
}

static u32 sceKernelPartitionMaxFreeMemSize(int partitionId) {
	ERROR_LOG(SCEKERNEL, "UNIMP sceKernelPartitionMaxFreeMemSize(%d)", partitionId);
	//Need more work #13021
	///We ignore partitionId for now
	return userMemory.GetLargestFreeBlockSize();
}

const HLEFunction SysMemForKernel[] = {
	{ 0X636C953B, &WrapI_II<sceKernelAllocHeapMemory>, "sceKernelAllocHeapMemory", 'x', "ii",                                  HLE_KERNEL_SYSCALL },
	{ 0XC9805775, &WrapI_I<sceKernelDeleteHeap>,       "sceKernelDeleteHeap",      'i', "i" ,                                  HLE_KERNEL_SYSCALL },
	{ 0X1C1FBFE7, &WrapI_IIIC<sceKernelCreateHeap>,    "sceKernelCreateHeap",      'i', "iixs",                                HLE_KERNEL_SYSCALL },
	{ 0X237DBD4F, &WrapI_ICIUU<sceKernelAllocPartitionMemory>,     "sceKernelAllocPartitionMemory",         'i', "isixx",      HLE_KERNEL_SYSCALL },
	{ 0XB6D61D02, &WrapI_I<sceKernelFreePartitionMemory>,          "sceKernelFreePartitionMemory",          'i', "i",          HLE_KERNEL_SYSCALL },
	{ 0X9D9A5BA1, &WrapU_I<sceKernelGetBlockHeadAddr>,             "sceKernelGetBlockHeadAddr",             'x', "i",          HLE_KERNEL_SYSCALL },
	{ 0x9697CD32, &WrapU_I<sceKernelPartitionTotalFreeMemSize>,           "sceKernelPartitionTotalFreeMemSize",       'x', "i",HLE_KERNEL_SYSCALL },
	{ 0xE6581468, &WrapU_I<sceKernelPartitionMaxFreeMemSize>,             "sceKernelPartitionMaxFreeMemSize",         'x', "i",HLE_KERNEL_SYSCALL },
};

void Register_SysMemForKernel() {
	RegisterModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}
