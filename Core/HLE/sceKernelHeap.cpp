#include <string>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelHeap.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/Util/BlockAllocator.h"

static const u32 KERNEL_HEAP_BLOCK_HEADER_SIZE = 8;
static const bool g_fromBottom = false;

// This object and the functions here are available for kernel code only, not game code.
// This differs from code like sceKernelMutex, which is available for games.
// This exists in PPSSPP mainly because certain game patches use these kernel modules.

struct KernelHeap : public KernelObject {
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
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Heap"; }

	void DoState(PointerWrap &p) override {
		Do(p, uid);
		Do(p, partitionId);
		Do(p, size);
		Do(p, flags);
		Do(p, address);
		Do(p, name);
		Do(p, alloc);
	}
};

static int sceKernelCreateHeap(int partitionId, int size, int flags, const char *Name) {
	u32 allocSize = (size + 3) & ~3;

	BlockAllocator *allocator = BlockAllocatorFromAddr(partitionId);
	// TODO: Validate error code.
	if (!allocator)
		return hleLogError(SCEKERNEL, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");

	// TODO: This should probably actually use flags?  Name?
	u32 addr = allocator->Alloc(allocSize, g_fromBottom, "SysMemForKernel-Heap");
	if (addr == (u32)-1) {
		// TODO: Validate error code.
		return hleLogError(SCEKERNEL, SCE_KERNEL_ERROR_NO_MEMORY, "fFailed to allocate %d bytes of memory", size);
	}

	KernelHeap *heap = new KernelHeap();
	SceUID uid = kernelObjects.Create(heap);

	heap->partitionId = partitionId;
	heap->flags = flags;
	heap->name = Name ? Name : "";  // Not sure if this needs validation.
	heap->size = allocSize;
	heap->address = addr;
	heap->alloc.Init(heap->address + 128, heap->size - 128, true);
	heap->uid = uid;
	return hleLogSuccessInfoX(SCEKERNEL, uid);
}

static int sceKernelAllocHeapMemory(int heapId, int size) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (heap) {
		// There's 8 bytes at the end of every block, reserved.
		u32 memSize = KERNEL_HEAP_BLOCK_HEADER_SIZE + size;
		u32 addr = heap->alloc.Alloc(memSize, true);
		return hleLogSuccessInfoX(SCEKERNEL, addr);
	} else {
		return hleLogError(SCEKERNEL, error, "invalid heapId");
	}
}

static int sceKernelDeleteHeap(int heapId) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (heap) {
		// Not using heap->partitionId here for backwards compatibility with old save states.
		BlockAllocator *allocator = BlockAllocatorFromAddr(heap->address);
		if (allocator)
			allocator->Free(heap->address);
		kernelObjects.Destroy<KernelHeap>(heap->uid);
		return hleLogSuccessInfoX(SCEKERNEL, 0);
	} else {
		return hleLogError(SCEKERNEL, error, "invalid heapId");
	}
}

static u32 sceKernelPartitionTotalFreeMemSize(int partitionId) {
	BlockAllocator *allocator = BlockAllocatorFromID(partitionId);
	// TODO: Validate error code.
	if (!allocator)
		return hleLogError(SCEKERNEL, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");
	return hleLogWarning(SCEKERNEL, allocator->GetTotalFreeBytes());
}

static u32 sceKernelPartitionMaxFreeMemSize(int partitionId) {
	BlockAllocator *allocator = BlockAllocatorFromID(partitionId);
	// TODO: Validate error code.
	if (!allocator)
		return hleLogError(SCEKERNEL, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");
	return hleLogWarning(SCEKERNEL, allocator->GetLargestFreeBlockSize());
}

static u32 SysMemForKernel_536AD5E1()
{
	ERROR_LOG(SCEKERNEL, "UNIMP SysMemForKernel_536AD5E1");
	return 0;
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
	{ 0X3FC9AE6A, &WrapU_V<sceKernelDevkitVersion>,                "sceKernelDevkitVersion",                'x', ""           ,HLE_KERNEL_SYSCALL },
	{ 0x536AD5E1, &WrapU_V<SysMemForKernel_536AD5E1>,       "SysMemForKernel_536AD5E1",      'i', "i"                         ,HLE_KERNEL_SYSCALL },
};

void Register_SysMemForKernel() {
	RegisterModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}
