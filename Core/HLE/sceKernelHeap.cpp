#include <string>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelHeap.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
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

	BlockAllocator *allocator = BlockAllocatorFromID(partitionId);
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
	return hleLogInfo(Log::sceKernel, uid);
}

static int sceKernelAllocHeapMemory(int heapId, int size) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap) {
		return hleLogError(Log::sceKernel, error, "invalid heapId");
	}

	// There's 8 bytes at the end of every block, reserved.
	u32 memSize = KERNEL_HEAP_BLOCK_HEADER_SIZE + size;
	u32 addr = heap->alloc.Alloc(memSize, true);
	return hleLogInfo(Log::sceKernel, addr);
}

static int sceKernelDeleteHeap(int heapId) {
	u32 error;
	KernelHeap *heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "invalid heapId");

	// Not using heap->partitionId here for backwards compatibility with old save states.
	BlockAllocator *allocator = BlockAllocatorFromAddr(heap->address);
	if (allocator)
		allocator->Free(heap->address);
	kernelObjects.Destroy<KernelHeap>(heap->uid);
	return hleLogInfo(Log::sceKernel, 0);
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
		return hleLogError(Log::sceKernel, error, "invalid heapId");
	if (block == 0) {
		return hleLogInfo(Log::sceKernel, 0, "heapId,0: block");
	}
	if (!heap->alloc.FreeExact(block)) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_INVALID_POINTER, "invalid pointer %08x", block);
	}
	return hleLogInfo(Log::sceKernel, 0, "heapId, block");
}

static int sceKernelAllocHeapMemoryWithOption(int heapId, u32 memSize, u32 paramsPtr) {
	u32 error;
	KernelHeap* heap = kernelObjects.Get<KernelHeap>(heapId, error);
	if (!heap)
		return hleLogError(Log::sceKernel, error, "invalid heapId");
	u32 grain = 4;
	// 0 is ignored.
	if (paramsPtr != 0) {
		if (!Memory::IsValid4AlignedRange(paramsPtr, 8))
			return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "invalid paramsPtr");
		u32 size = Memory::ReadUnchecked_U32(paramsPtr);  // size of the params struct
		if (size < 8)
			return hleLogError(Log::sceKernel, 0, "invalid param size");
		if (size > 8)
			WARN_LOG(Log::HLE, "sceKernelAllocHeapMemoryWithOption(): unexpected param size %d", size);
		grain = Memory::ReadUnchecked_U32(paramsPtr + 4);
	}
	INFO_LOG(Log::HLE, "sceKernelAllocHeapMemoryWithOption(%08x, %08x, %08x)", heapId, memSize, paramsPtr);
	// There's 8 bytes at the end of every block, reserved.
	memSize += 8;
	u32 addr = heap->alloc.AllocAligned(memSize, grain, grain, true);
	return addr;
}

static int sceKernelGetModel() {
	constexpr u32 model = 2;  // 2 = original slim.
	return hleLogWarning(Log::sceKernel, model - 1);
}

// Both configure things PPSSPP has no equivalent of - which kernel image a reboot would use, and
// whether the UMD read cache is on. Accepted and ignored; the VSH calls them once each during
// startup and only cares that they succeed.
static int sceKernelSetRebootKernel(u32 arg) {
	return hleLogWarning(Log::sceKernel, 0, "UNIMPL");
}

static int sceKernelSetUmdCacheOn(int on) {
	return hleLogWarning(Log::sceKernel, 0, "UNIMPL");
}

static int sceKernelMemset32(u32 destAddr, u32 value, int size) {
	if (size < 0 || (size & 3) != 0 || !Memory::IsValid4AlignedRange(destAddr, size)) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	for (int offset = 0; offset < size; offset += 4) {
		Memory::WriteUnchecked_U32(value, destAddr + offset);
	}
	return hleLogDebug(Log::sceKernel, 0);
}

static int SysMemForKernel_2A8B8B2D(u32 subscriptionValidity) {
	// Stores an eight-byte subscription field in SceKernelGameInfo. It is not
	// relevant to local impose rendering, but the native driver requires the
	// setter to exist during initialization.
	return hleLogDebug(Log::sceKernel, 0, "subscription=%08x", subscriptionValidity);
}

static u32 sceKernelPartitionTotalMemSize(int partitionId) {
	switch (partitionId) {
	case KERNEL_PARTITION_ID:
		return PSP_GetKernelMemoryEnd() - PSP_GetKernelMemoryBase();
	case USER_PARTITION_ID:
		return PSP_GetUserMemoryEnd() - PSP_GetUserMemoryBase();
	case VSHELL_PARTITION_ID:
		return PSP_GetVolatileMemoryEnd() - PSP_GetVolatileMemoryStart();
	case 8:  // SCE_KERNEL_EXTENDED_SC_KERNEL_PARTITION on PSP-2000+
		return Memory::g_PSPModel > 0 ? 0x00400000 : 0;
	default:
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT);
	}
}

static u32 sceKernelGetGameInfo() {
	static u32 gameInfoAddr = 0;
	const char *tag = gameInfoAddr ? kernelMemory.GetBlockTag(gameInfoAddr) : nullptr;
	bool allocated = false;
	if (!tag || strcmp(tag, "SceKernelGameInfo") != 0) {
		// Provisional 6.60/6.61 layout carried forward from the failed hybrid
		// attempt's comparison with Jpcsp's native-firmware implementation.
		u32 allocationSize = 220;
		gameInfoAddr = kernelMemory.Alloc(allocationSize, false, "SceKernelGameInfo");
		allocated = true;
	}
	constexpr u32 size = 220;
	if (gameInfoAddr == (u32)-1 || !Memory::IsValidRange(gameInfoAddr, size)) {
		return 0;
	}
	if (allocated) {
		Memory::Memset(gameInfoAddr, 0, size, "SceKernelGameInfo");
		Memory::WriteUnchecked_U32(size, gameInfoAddr + 0);
	}

	auto writeString = [](u32 address, u32 capacity, const std::string &value) {
		u8 *destination = Memory::GetPointerWriteUnchecked(address);
		memset(destination, 0, capacity);
		size_t length = value.size();
		if (length >= capacity)
			length = capacity - 1;
		memcpy(destination, value.data(), length);
	};

	// Offsets are the provisional SceKernelGameInfo 6.61 ABI described above.
	Memory::WriteUnchecked_U32(0x200, gameInfoAddr + 4);
	std::string discId = g_paramSFO.GetValueString("DISC_ID");
	if (discId.empty())
		discId = g_paramSFORaw.GetValueString("DISC_ID");
	writeString(gameInfoAddr + 68, 14, discId);
	writeString(gameInfoAddr + 88, 8, "6.61");
	Memory::WriteUnchecked_U32(0, gameInfoAddr + 96);
	Memory::WriteUnchecked_U32(sceKernelGetCompiledSdkVersion(), gameInfoAddr + 100);
	Memory::WriteUnchecked_U32(sceKernelGetCompilerVersion(), gameInfoAddr + 104);
	const std::string category = g_paramSFO.GetValueString("CATEGORY");
	Memory::WriteUnchecked_U32(category == "EG" || category == "MG" ? 0x7F : 0, gameInfoAddr + 112);
	writeString(gameInfoAddr + 180, 11, "");
	writeString(gameInfoAddr + 196, 8, "00.00");
	return hleLogDebug(Log::sceKernel, gameInfoAddr, "disc=%s category=%s sdk=%08x",
		discId.empty() ? "<empty>" : discId.c_str(), category.empty() ? "<empty>" : category.c_str(),
		sceKernelGetCompiledSdkVersion());
}

const HLEFunction SysMemForKernel[] = {
	{ 0X96A3CE2C, &WrapI_U<sceKernelSetRebootKernel>,              "sceKernelSetRebootKernel",           'i', "x",     HLE_KERNEL_SYSCALL },
	{ 0X1404C1AA, &WrapI_I<sceKernelSetUmdCacheOn>,                "sceKernelSetUmdCacheOn",             'i', "i",     HLE_KERNEL_SYSCALL },
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
	{ 0x6373995d, &WrapI_V<sceKernelGetModel>,                     "sceKernelGetModel",                  'i', "",      HLE_KERNEL_SYSCALL},  // 220
	{ 0x07C586A1, &WrapI_V<sceKernelGetModel>,                     "sceKernelGetModel",                  'i', "",      HLE_KERNEL_SYSCALL },  // 220
	{ 0X22A114DC, &WrapI_UUI<sceKernelMemset32>,                   "sceKernelMemset32",                  'i', "xxi",   HLE_KERNEL_SYSCALL },
	{ 0X2A8B8B2D, &WrapI_U<SysMemForKernel_2A8B8B2D>,             "SysMemForKernel_2A8B8B2D",          'i', "x",     HLE_KERNEL_SYSCALL },
	{ 0X53D50AC2, &WrapU_I<sceKernelPartitionTotalMemSize>,       "sceKernelPartitionTotalMemSize",    'x', "i",     HLE_KERNEL_SYSCALL },
	{ 0XEF29061C, &WrapU_V<sceKernelGetGameInfo>,                  "sceKernelGetGameInfo",               'x', "",      HLE_KERNEL_SYSCALL },
	{ 0XFAF29F34, &WrapI_UUU<sceKernelQueryMemoryInfo>,            "sceKernelQueryMemoryInfo",            'i', "xpp",   HLE_KERNEL_SYSCALL },
	{ 0X7158CE7E, &WrapI_ICIUU<sceKernelAllocPartitionMemory>,     "sceKernelAllocPartitionMemory",      'i', "isixx", HLE_KERNEL_SYSCALL },
	{ 0XC1A26C6F, &WrapI_I<sceKernelFreePartitionMemory>,          "sceKernelFreePartitionMemory",       'i', "i",     HLE_KERNEL_SYSCALL },
	{ 0XF12A62F7, &WrapU_I<sceKernelGetBlockHeadAddr>,             "sceKernelGetBlockHeadAddr",          'x', "i",     HLE_KERNEL_SYSCALL },
	{ 0X1AB50974, &WrapI_II<sceKernelJointMemoryBlock>,            "sceKernelJointMemoryBlock",         'i', "ii",    HLE_KERNEL_SYSCALL },
	{ 0XE860BE8F, &WrapI_IU<sceKernelQueryMemoryBlockInfo>,        "sceKernelQueryMemoryBlockInfo",     'i', "ip",    HLE_KERNEL_SYSCALL },
	{ 0XB4F00CB5, &WrapI_V<sceKernelGetCompiledSdkVersion>,        "sceKernelGetCompiledSdkVersion",    'i', "",      HLE_KERNEL_SYSCALL },
};

void Register_SysMemForKernel() {
	RegisterHLEModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}
