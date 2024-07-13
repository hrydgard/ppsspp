#include <string>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelHeap.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/Reporting.h"
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
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");

	// TODO: This should probably actually use flags?  Name?
	u32 addr = allocator->Alloc(allocSize, g_fromBottom, StringFromFormat("KernelHeap/%s", Name).c_str());
	if (addr == (u32)-1) {
		// TODO: Validate error code.
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "fFailed to allocate %d bytes of memory", size);
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
	return hleLogSuccessInfoX(Log::sceKernel, uid);
}

static int sceKernelAllocHeapMemory(int heapId, int size) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "sceKernelAllocHeapMemory(%d): invalid heapId", heapId);

	// There's 8 bytes at the end of every block, reserved.
	u32 memSize = KERNEL_HEAP_BLOCK_HEADER_SIZE + size;
	u32 addr = heap->alloc.Alloc(memSize, true);
	return hleLogSuccessInfoX(Log::sceKernel, addr);
}

static int sceKernelDeleteHeap(int heapId) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "sceKernelDeleteHeap(%d): invalid heapId", heapId);

	// Not using heap->partitionId here for backwards compatibility with old save states.
	BlockAllocator *allocator = BlockAllocatorFromAddr(heap->address);
	if (allocator)
		allocator->Free(heap->address);
	kernelObjects.Destroy<KernelHeap>(heap->uid);
	return hleLogSuccessInfoX(Log::sceKernel, 0);
}

static u32 sceKernelPartitionTotalFreeMemSize(int partitionId) {
	BlockAllocator *allocator = BlockAllocatorFromID(partitionId);
	// TODO: Validate error code.
	if (!allocator)
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");
	return hleLogWarning(Log::sceKernel, allocator->GetTotalFreeBytes());
}

static u32 sceKernelPartitionMaxFreeMemSize(int partitionId) {
	BlockAllocator *allocator = BlockAllocatorFromID(partitionId);
	// TODO: Validate error code.
	if (!allocator)
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition");
	return hleLogWarning(Log::sceKernel, allocator->GetLargestFreeBlockSize());
}

static u32 sceKernelGetUidmanCB()
{
	ERROR_LOG_REPORT(Log::sceKernel, "UNIMP sceKernelGetUidmanCB");
	return 0;
}

static int sceKernelFreeHeapMemory(int heapId, u32 block) {
	u32 error;
	KernelHeap* heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "sceKernelFreeHeapMemory(%d): invalid heapId", heapId);
	if (block == 0) {
		return hleLogSuccessInfoI(Log::sceKernel, 0, "sceKernelFreeHeapMemory(%d): heapId,0: block", heapId);
	}
	if (!heap->alloc.FreeExact(block)) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_INVALID_POINTER, "invalid pointer %08x", block);
	}
	return hleLogSuccessInfoI(Log::sceKernel, 0, "sceKernelFreeHeapMemory(%d): heapId, block", heapId, block);

}

static int sceKernelAllocHeapMemoryWithOption(int heapId, u32 memSize, u32 paramsPtr) {
	u32 error;
	KernelHeap* heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "sceKernelFreeHeapMemory(%d): invalid heapId", heapId);
	u32 grain = 4;
	// 0 is ignored.
	if (paramsPtr != 0) {
		u32 size = Memory::Read_U32(paramsPtr);
		if (size < 8)
			return hleLogError(Log::sceKernel, 0, "invalid param size");
		if (size > 8)
			WARN_LOG(Log::HLE, "sceKernelAllocHeapMemoryWithOption(): unexpected param size %d", size);
		grain = Memory::Read_U32(paramsPtr + 4);
	}
	INFO_LOG(Log::HLE, "sceKernelAllocHeapMemoryWithOption(%08x, %08x, %08x)", heapId, memSize, paramsPtr);
	// There's 8 bytes at the end of every block, reserved.
	memSize += 8;
	u32 addr = heap->alloc.AllocAligned(memSize, grain, grain, true);
	return addr;
}

const HLEFunction SysMemForKernel[] = {
	{ 0X636C953B, &WrapI_II<sceKernelAllocHeapMemory>,             "sceKernelAllocHeapMemory",           'x', "ii",    HLE_KERNEL_SYSCALL },
	{ 0XC9805775, &WrapI_I<sceKernelDeleteHeap>,                   "sceKernelDeleteHeap",                'i', "i" ,    HLE_KERNEL_SYSCALL },
	{ 0X1C1FBFE7, &WrapI_IIIC<sceKernelCreateHeap>,                "sceKernelCreateHeap",                'i', "iixs",  HLE_KERNEL_SYSCALL },
	{ 0X237DBD4F, &WrapI_ICIUU<sceKernelAllocPartitionMemory>,     "sceKernelAllocPartitionMemory",      'i', "isixx", HLE_KERNEL_SYSCALL },
	{ 0XB6D61D02, &WrapI_I<sceKernelFreePartitionMemory>,          "sceKernelFreePartitionMemory",       'i', "i",     HLE_KERNEL_SYSCALL },
	{ 0X9D9A5BA1, &WrapU_I<sceKernelGetBlockHeadAddr>,             "sceKernelGetBlockHeadAddr",          'x', "i",     HLE_KERNEL_SYSCALL },
	{ 0x9697CD32, &WrapU_I<sceKernelPartitionTotalFreeMemSize>,    "sceKernelPartitionTotalFreeMemSize", 'x', "i" ,    HLE_KERNEL_SYSCALL },
	{ 0xE6581468, &WrapU_I<sceKernelPartitionMaxFreeMemSize>,      "sceKernelPartitionMaxFreeMemSize",   'x', "i" ,    HLE_KERNEL_SYSCALL },
	{ 0X3FC9AE6A, &WrapU_V<sceKernelDevkitVersion>,                "sceKernelDevkitVersion",             'x', "" ,     HLE_KERNEL_SYSCALL },
	{ 0X536AD5E1, &WrapU_V<sceKernelGetUidmanCB>,                  "sceKernelGetUidmanCB",               'i', "i" ,    HLE_KERNEL_SYSCALL },
	{ 0X7B749390, &WrapI_IU<sceKernelFreeHeapMemory>,              "sceKernelFreeHeapMemory",            'i', "ix" ,   HLE_KERNEL_SYSCALL },
	{ 0XEB7A74DB, &WrapI_IUU<sceKernelAllocHeapMemoryWithOption>,  "sceKernelAllocHeapMemoryWithOption", 'i', "ixp" ,  HLE_KERNEL_SYSCALL },
};

void Register_SysMemForKernel() {
	RegisterModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}
