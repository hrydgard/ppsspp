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
#include "HLE.h"
#include "../System.h"
#include "../MIPS/MIPS.h"
#include "../MemMap.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"

const int TLS_NUM_INDEXES = 16;

//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
BlockAllocator userMemory(256);
BlockAllocator kernelMemory(256);

static int vplWaitTimer = -1;
static bool tlsUsedIndexes[TLS_NUM_INDEXES];
// STATE END
//////////////////////////////////////////////////////////////////////////

#define SCE_KERNEL_HASCOMPILEDSDKVERSION 	0x1000
#define SCE_KERNEL_HASCOMPILERVERSION		0x2000

int flags_ = 0;
int sdkVersion_;
int compilerVersion_;

struct NativeFPL
{
	u32 size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	SceUID mpid;
	u32 attr;

	int blocksize;
	int numBlocks;
	int numFreeBlocks;
	int numWaitThreads;
};

//FPL - Fixed Length Dynamic Memory Pool - every item has the same length
struct FPL : public KernelObject
{
	FPL() : blocks(NULL) {}
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
			if (!blocks[i])
				return i;
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
		p.Do(nf);
		if (p.mode == p.MODE_READ)
			blocks = new bool[nf.numBlocks];
		p.DoArray(blocks, nf.numBlocks);
		p.Do(address);
		p.DoMarker("FPL");
	}

	NativeFPL nf;
	bool *blocks;
	u32 address;
};

struct VplWaitingThread
{
	SceUID threadID;
	u32 addrPtr;
};

struct SceKernelVplInfo
{
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	SceUInt attr;
	int poolSize;
	int freeSize;
	int numWaitThreads;
};

struct VPL : public KernelObject
{
	const char *GetName() {return nv.name;}
	const char *GetTypeName() {return "VPL";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VPLID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Vpl; }
	int GetIDType() const { return SCE_KERNEL_TMID_Vpl; }

	VPL() : alloc(8) {}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nv);
		p.Do(address);
		VplWaitingThread dv = {0};
		p.Do(waitingThreads, dv);
		alloc.DoState(p);
		p.DoMarker("VPL");
	}

	SceKernelVplInfo nv;
	u32 address;
	std::vector<VplWaitingThread> waitingThreads;
	BlockAllocator alloc;
};

void __KernelVplTimeout(u64 userdata, int cyclesLate);

void __KernelMemoryInit()
{
	kernelMemory.Init(PSP_GetKernelMemoryBase(), PSP_GetKernelMemoryEnd()-PSP_GetKernelMemoryBase());
	userMemory.Init(PSP_GetUserMemoryBase(), PSP_GetUserMemoryEnd()-PSP_GetUserMemoryBase());
	INFO_LOG(HLE, "Kernel and user memory pools initialized");

	vplWaitTimer = CoreTiming::RegisterEvent("VplTimeout", __KernelVplTimeout);

	flags_ = 0;
	sdkVersion_ = 0;
	compilerVersion_ = 0;
	memset(tlsUsedIndexes, 0, sizeof(tlsUsedIndexes));
}

void __KernelMemoryDoState(PointerWrap &p)
{
	kernelMemory.DoState(p);
	userMemory.DoState(p);

	p.Do(vplWaitTimer);
	CoreTiming::RestoreRegisterEvent(vplWaitTimer, "VplTimeout", __KernelVplTimeout);
	p.Do(flags_);
	p.Do(sdkVersion_);
	p.Do(compilerVersion_);
	p.DoArray(tlsUsedIndexes, ARRAY_SIZE(tlsUsedIndexes));
	p.DoMarker("sceKernelMemory");
}

void __KernelMemoryShutdown()
{
	INFO_LOG(HLE,"Shutting down user memory pool: ");
	userMemory.ListBlocks();
	userMemory.Shutdown();
	INFO_LOG(HLE,"Shutting down \"kernel\" memory pool: ");
	kernelMemory.ListBlocks();
	kernelMemory.Shutdown();
}

//sceKernelCreateFpl(const char *name, SceUID mpid, SceUint attr, SceSize blocksize, int numBlocks, optparam)
void sceKernelCreateFpl()
{
	const char *name = Memory::GetCharPointer(PARAM(0));

	u32 mpid = PARAM(1);
	u32 attr = PARAM(2);
	u32 blockSize = PARAM(3);
	u32 numBlocks = PARAM(4);

	u32 totalSize = blockSize * numBlocks;

	bool atEnd = false;   // attr can change this I think

	u32 address = userMemory.Alloc(totalSize, atEnd, "FPL");
	if (address == (u32)-1)
	{
		DEBUG_LOG(HLE,"sceKernelCreateFpl(\"%s\", partition=%i, attr=%i, bsize=%i, nb=%i) FAILED - out of ram", 
			name, mpid, attr, blockSize, numBlocks);
		RETURN(SCE_KERNEL_ERROR_NO_MEMORY);
		return;
	}

	FPL *fpl = new FPL;
	SceUID id = kernelObjects.Create(fpl);
	strncpy(fpl->nf.name, name, 32);

	fpl->nf.size = sizeof(fpl->nf);
	fpl->nf.mpid = mpid;  // partition
	fpl->nf.attr = attr;
	fpl->nf.blocksize = blockSize;
	fpl->nf.numBlocks = numBlocks;
	fpl->nf.numWaitThreads = 0;
	fpl->blocks = new bool[fpl->nf.numBlocks];
	memset(fpl->blocks, 0, fpl->nf.numBlocks * sizeof(bool));
	fpl->address = address;

	DEBUG_LOG(HLE,"%i=sceKernelCreateFpl(\"%s\", partition=%i, attr=%i, bsize=%i, nb=%i)", 
		id, name, mpid, attr, blockSize, numBlocks);

	RETURN(id);
}

void sceKernelDeleteFpl()
{
	SceUID id = PARAM(0);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		userMemory.Free(fpl->address);
		DEBUG_LOG(HLE,"sceKernelDeleteFpl(%i)", id);
		RETURN(kernelObjects.Destroy<FPL>(id));
	}
	else
	{
		RETURN(error);
	}
}

void sceKernelAllocateFpl()
{
	SceUID id = PARAM(0);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		u32 blockPtrAddr = PARAM(1);
		int timeOut = PARAM(2);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->nf.blocksize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			RETURN(0);
		} else {
			// TODO: Should block!
			RETURN(0);
		}

		DEBUG_LOG(HLE,"sceKernelAllocateFpl(%i, %08x, %i)", id, blockPtrAddr, timeOut);
		RETURN(0);
	}
	else
	{
		DEBUG_LOG(HLE,"ERROR: sceKernelAllocateFpl(%i)", id);
		RETURN(error);
	}
}

void sceKernelAllocateFplCB()
{
	SceUID id = PARAM(0);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		u32 blockPtrAddr = PARAM(1);
		int timeOut = PARAM(2);

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->nf.blocksize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			RETURN(0);
		} else {
			// TODO: Should block and process callbacks!
			__KernelCheckCallbacks();
		}

		DEBUG_LOG(HLE,"sceKernelAllocateFpl(%i, %08x, %i)", id, PARAM(1), timeOut);
	}
	else
	{
		DEBUG_LOG(HLE,"ERROR: sceKernelAllocateFplCB(%i)", id);
		RETURN(error);
	}
}

void sceKernelTryAllocateFpl()
{
	SceUID id = PARAM(0);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		u32 blockPtrAddr = PARAM(1);
		DEBUG_LOG(HLE,"sceKernelTryAllocateFpl(%i, %08x)", id, PARAM(1));

		int blockNum = fpl->allocateBlock();
		if (blockNum >= 0) {
			u32 blockPtr = fpl->address + fpl->nf.blocksize * blockNum;
			Memory::Write_U32(blockPtr, blockPtrAddr);
			RETURN(0);
		} else {
			RETURN(SCE_KERNEL_ERROR_NO_MEMORY);
		}
	}
	else
	{
		DEBUG_LOG(HLE,"sceKernelTryAllocateFpl(%i) - bad UID", id);
		RETURN(error);
	}
}

void sceKernelFreeFpl()
{
	SceUID id = PARAM(0);
	u32 blockAddr = PARAM(1);

	DEBUG_LOG(HLE,"sceKernelFreeFpl(%i, %08x)", id, blockAddr);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl) {
		int blockNum = (blockAddr - fpl->address) / fpl->nf.blocksize;
		if (blockNum < 0 || blockNum >= fpl->nf.numBlocks) {
			RETURN(SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK);
		} else {
			if (fpl->freeBlock(blockNum)) {
				// TODO: If there are waiting threads, wake them up
			}
			RETURN(0);
		}
	}
	else
	{
		RETURN(error);
	}
}

void sceKernelCancelFpl()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"UNIMPL: sceKernelCancelFpl(%i)", id);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

void sceKernelReferFplStatus()
{
	SceUID id = PARAM(0);
	u32 statusAddr = PARAM(1);
	DEBUG_LOG(HLE,"sceKernelReferFplStatus(%i, %08x)", id, statusAddr);
	u32 error;
	FPL *fpl = kernelObjects.Get<FPL>(id, error);
	if (fpl)
	{
		Memory::WriteStruct(statusAddr, &fpl->nf);
		RETURN(0);
	}
	else
	{
		RETURN(error);
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
			alloc->ListBlocks();
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
		p.Do(address);
		p.DoArray(name, sizeof(name));
		p.DoMarker("PMB");
	}

	u32 address;
	char name[32];
};


void sceKernelMaxFreeMemSize() 
{
	// TODO: Fudge factor improvement
	u32 retVal = userMemory.GetLargestFreeBlockSize()-0x40000;
	DEBUG_LOG(HLE,"%08x (dec %i)=sceKernelMaxFreeMemSize",retVal,retVal);
	RETURN(retVal);
}

void sceKernelTotalFreeMemSize()
{
	u32 retVal = userMemory.GetLargestFreeBlockSize()-0x8000;
	DEBUG_LOG(HLE,"%08x (dec %i)=sceKernelTotalFreeMemSize",retVal,retVal);
	RETURN(retVal);
}

int sceKernelAllocPartitionMemory(int partition, const char *name, int type, u32 size, u32 addr)
{
	if (name == NULL)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (size == 0)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid size %x", SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED, size);
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid partition %x", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 5 && partition != 6)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid partition %x", SCE_KERNEL_ERROR_ILLEGAL_PARTITION, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_PARTITION;
	}
	if (type < PSP_SMEM_Low || type > PSP_SMEM_HighAligned)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid type %x", SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE, type);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE;
	}
	// Alignment is only allowed for powers of 2.
	if ((type == PSP_SMEM_LowAligned || type == PSP_SMEM_HighAligned) && ((addr & (addr - 1)) != 0 || addr == 0))
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelAllocPartitionMemory(): invalid alignment %x", SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE, addr);
		return SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE;
	}

	PartitionMemoryBlock *block = new PartitionMemoryBlock(&userMemory, name, size, (MemblockType)type, addr);
	if (!block->IsValid())
	{
		delete block;
		ERROR_LOG(HLE, "ARGH! sceKernelAllocPartitionMemory failed");
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	SceUID uid = kernelObjects.Create(block);

	DEBUG_LOG(HLE,"%i = sceKernelAllocPartitionMemory(partition = %i, %s, type= %i, size= %i, addr= %08x)",
		uid, partition, name, type, size, addr);

	return uid;
}

int sceKernelFreePartitionMemory(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelFreePartitionMemory(%d)",id);

	return kernelObjects.Destroy<PartitionMemoryBlock>(id);
}

u32 sceKernelGetBlockHeadAddr(SceUID id)
{
	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(id, error);
	if (block)
	{
		DEBUG_LOG(HLE,"%08x = sceKernelGetBlockHeadAddr(%i)", block->address, id);
		return block->address;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelGetBlockHeadAddr failed(%i)", id);
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

		switch (format[i])
		{
		case '%':
			result += '%';
			++i;
			break;

		case 's':
			result += Memory::GetCharPointer(PARAM(param++));
			++i;
			break;

		case 'd':
		case 'i':
		case 'x':
		case 'u':
			tempFormat[1] = format[i];
			tempFormat[2] = '\0';
			snprintf(tempStr, sizeof(tempStr), tempFormat, PARAM(param++));
			result += tempStr;
			++i;
			break;

		case '0':
			if (i + 3 >= n || format[i + 1] != '8' || format[i + 2] != 'x')
				supported = false;
			else
			{
				snprintf(tempFormat + 1, sizeof(tempFormat) - 1, "08x");
				snprintf(tempStr, sizeof(tempStr), tempFormat, PARAM(param++));
				result += tempStr;
				i += 3;
			}
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
		INFO_LOG(HLE, "sceKernelPrintf: %s", result.c_str())
	else
		ERROR_LOG(HLE, "UNIMPL sceKernelPrintf(%s, %08x, %08x, %08x)", format.c_str(), PARAM(1), PARAM(2), PARAM(3));
	return 0;
}

void sceKernelSetCompiledSdkVersion(int sdkVersion)
{
/*	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	bool valiSDK = false;
	switch(sdkMainVersion)
	{
	case 0x1000000:
	case 0x1050000:
	case 0x2000000:
	case 0x2050000:
	case 0x2060000:
	case 0x2070000:
	case 0x2080000:
	case 0x3000000:
	case 0x3010000:
	case 0x3030000:
	case 0x3040000:
	case 0x3050000:
	case 0x3060000:
		valiSDK = true;
		break;
	default:
		valiSDK = false;
		break;
	}

	if(valiSDK)
	{*/
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
/*	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion unknown SDK : %x\n",sdkVersion);
	}
	return;*/
}

void sceKernelSetCompiledSdkVersion370(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x3070000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion370 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion380_390(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x3080000 || sdkMainVersion == 0x3090000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion380_390 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion395(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFFFF00;
	if(sdkMainVersion == 0x4000000
			|| sdkMainVersion == 0x4000100
			|| sdkMainVersion == 0x4000500
			|| sdkMainVersion == 0x3090500
			|| sdkMainVersion == 0x3090600)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion395 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion600_602(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x6010000
			|| sdkMainVersion == 0x6000000
			|| sdkMainVersion == 0x6020000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion600_602 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion500_505(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x5000000
			|| sdkMainVersion == 0x5050000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion500_505 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion401_402(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x4010000
			|| sdkMainVersion == 0x4020000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion401_402 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion507(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x5070000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion507 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion603_605(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x6040000
			|| sdkMainVersion == 0x6030000
			|| sdkMainVersion == 0x6050000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion603_605 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

void sceKernelSetCompiledSdkVersion606(int sdkVersion)
{
	int sdkMainVersion = sdkVersion & 0xFFFF0000;
	if(sdkMainVersion == 0x6060000)
	{
		sdkVersion_ = sdkVersion;
		flags_ |=  SCE_KERNEL_HASCOMPILEDSDKVERSION;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelSetCompiledSdkVersion606 unknown SDK : %x\n",sdkVersion);
	}
	return;
}

int sceKernelGetCompiledSdkVersion()
{
	if(!(flags_ & SCE_KERNEL_HASCOMPILEDSDKVERSION))
		return 0;
	return sdkVersion_;
}

void sceKernelSetCompilerVersion(int version)
{
	compilerVersion_ = version;
	flags_ |= SCE_KERNEL_HASCOMPILERVERSION;
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
	PSP_VPL_ATTR_FIFO = 0x0000,
	PSP_VPL_ATTR_PRIORITY = 0x0100,
	PSP_VPL_ATTR_SMALLEST = 0x0200,
	PSP_VPL_ATTR_HIGHMEM = 0x4000,
	PSP_VPL_ATTR_KNOWN = PSP_VPL_ATTR_FIFO | PSP_VPL_ATTR_PRIORITY | PSP_VPL_ATTR_SMALLEST | PSP_VPL_ATTR_HIGHMEM,
};

bool __KernelUnlockVplForThread(VPL *vpl, VplWaitingThread &threadInfo, u32 &error, int result, bool &wokeThreads)
{
	const SceUID threadID = threadInfo.threadID;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_VPL, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

	// The waitID may be different after a timeout.
	if (waitID != vpl->GetUID())
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		int size = (int) __KernelGetWaitValue(threadID, error);

		// Padding (normally used to track the allocation.)
		u32 allocSize = size + 8;
		u32 addr = vpl->alloc.Alloc(allocSize, true);
		if (addr != (u32) -1)
			Memory::Write_U32(addr, threadInfo.addrPtr);
		else
			return false;
	}

	if (timeoutPtr != 0 && vplWaitTimer != -1)
	{
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(vplWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
}

void __KernelVplRemoveThread(VPL *vpl, SceUID threadID)
{
	for (size_t i = 0; i < vpl->waitingThreads.size(); i++)
	{
		VplWaitingThread *t = &vpl->waitingThreads[i];
		if (t->threadID == threadID)
		{
			vpl->waitingThreads.erase(vpl->waitingThreads.begin() + i);
			break;
		}
	}
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

SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 6)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_PERM, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_PERM;
	}
	if (((attr & ~PSP_VPL_ATTR_KNOWN) & ~0xFF) != 0)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}
	if (vplSize == 0)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): invalid size", SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
	}
	// Block Allocator seems to A-OK this, let's stop it here.
	if (vplSize >= 0x80000000)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateVpl(): way too big size", SCE_KERNEL_ERROR_NO_MEMORY);
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
		ERROR_LOG(HLE, "sceKernelCreateVpl: Failed to allocate %i bytes of pool data", vplSize);
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

	DEBUG_LOG(HLE, "%x=sceKernelCreateVpl(\"%s\", block=%i, attr=%i, size=%i)", 
		id, name, partition, vpl->nv.attr, vpl->nv.poolSize);

	return id;
}

int sceKernelDeleteVpl(SceUID uid)
{
	DEBUG_LOG(HLE, "sceKernelDeleteVpl(%i)", uid);
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
bool __KernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 &error, const char *funcname)
{
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		if (size == 0 || size > (u32) vpl->nv.poolSize)
		{
			WARN_LOG(HLE, "%s(vpl=%i, size=%i, ptrout=%08x): invalid size", funcname, uid, size, addrPtr);
			error = SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
			return false;
		}

		VERBOSE_LOG(HLE, "%s(vpl=%i, size=%i, ptrout=%08x)", funcname, uid, size, addrPtr);
		// Padding (normally used to track the allocation.)
		u32 allocSize = size + 8;
		u32 addr = vpl->alloc.Alloc(allocSize, true);
		if (addr != (u32) -1)
		{
			Memory::Write_U32(addr, addrPtr);
			error =  0;
		}
		else
			error = SCE_KERNEL_ERROR_NO_MEMORY;

		return true;
	}

	return false;
}

void __KernelVplTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID) userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID uid = __KernelGetWaitID(threadID, WAITTYPE_VPL, error);
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
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
	if (__KernelAllocateVpl(uid, size, addrPtr, error, __FUNCTION__))
	{
		VPL *vpl = kernelObjects.Get<VPL>(uid, ignore);
		if (error == SCE_KERNEL_ERROR_NO_MEMORY)
		{
			if (vpl)
			{
				SceUID threadID = __KernelGetCurThread();
				__KernelVplRemoveThread(vpl, threadID);
				VplWaitingThread waiting = {threadID, addrPtr};
				vpl->waitingThreads.push_back(waiting);
			}

			__KernelSetVplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_VPL, uid, size, timeoutPtr, false, "vpl waited");
		}
	}
	return error;
}

int sceKernelAllocateVplCB(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr)
{
	u32 error, ignore;
	if (__KernelAllocateVpl(uid, size, addrPtr, error, __FUNCTION__))
	{
		hleCheckCurrentCallbacks();

		VPL *vpl = kernelObjects.Get<VPL>(uid, ignore);
		if (error == SCE_KERNEL_ERROR_NO_MEMORY)
		{
			if (vpl)
			{
				vpl->nv.numWaitThreads++;

				SceUID threadID = __KernelGetCurThread();
				__KernelVplRemoveThread(vpl, threadID);
				VplWaitingThread waiting = {threadID, addrPtr};
				vpl->waitingThreads.push_back(waiting);
			}

			__KernelSetVplTimeout(timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_VPL, uid, size, timeoutPtr, true, "vpl waited");
		}
	}
	return error;
}

int sceKernelTryAllocateVpl(SceUID uid, u32 size, u32 addrPtr)
{
	u32 error;
	__KernelAllocateVpl(uid, size, addrPtr, error, __FUNCTION__);
	return error;
}

int sceKernelFreeVpl(SceUID uid, u32 addr)
{
	if (addr && !Memory::IsValidAddress(addr))
	{
		WARN_LOG(HLE, "%08x=sceKernelFreeVpl(%i, %08x): Invalid address", SCE_KERNEL_ERROR_ILLEGAL_ADDR, uid, addr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	VERBOSE_LOG(HLE, "sceKernelFreeVpl(%i, %08x)", uid, addr);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		if (vpl->alloc.FreeExact(addr))
		{
			// TODO: smallest priority
			if ((vpl->nv.attr & PSP_VPL_ATTR_PRIORITY) != 0)
				std::stable_sort(vpl->waitingThreads.begin(), vpl->waitingThreads.end(), __VplThreadSortPriority);

			bool wokeThreads = false;
retry:
			for (auto iter = vpl->waitingThreads.begin(), end = vpl->waitingThreads.end(); iter != end; ++iter)
			{
				if (__KernelUnlockVplForThread(vpl, *iter, error, 0, wokeThreads))
				{
					vpl->waitingThreads.erase(iter);
					goto retry;
				}
			}

			if (wokeThreads)
				hleReSchedule("vpl freed");

			return 0;
		}
		else
		{
			WARN_LOG(HLE, "%08x=sceKernelFreeVpl(%i, %08x): Unable to free", SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK, uid, addr);
			return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK;
		}
	}
	else
		return error;
}

int sceKernelCancelVpl(SceUID uid, u32 numWaitThreadsPtr)
{
	DEBUG_LOG(HLE, "sceKernelCancelVpl(%i, %08x)", uid, numWaitThreadsPtr);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		vpl->nv.numWaitThreads = (int) vpl->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(vpl->nv.numWaitThreads, numWaitThreadsPtr);

		bool wokeThreads = __KernelClearVplThreads(vpl, SCE_KERNEL_ERROR_WAIT_CANCEL);
		if (wokeThreads)
			hleReSchedule("vpl canceled");

		return 0;
	}
	else
		return error;
}

int sceKernelReferVplStatus(SceUID uid, u32 infoPtr)
{
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(uid, error);
	if (vpl)
	{
		DEBUG_LOG(HLE, "sceKernelReferVplStatus(%i, %08x)", uid, infoPtr);

		u32 error;
		for (auto iter = vpl->waitingThreads.begin(); iter != vpl->waitingThreads.end(); ++iter)
		{
			SceUID waitID = __KernelGetWaitID(iter->threadID, WAITTYPE_VPL, error);
			// The thread is no longer waiting for this, clean it up.
			if (waitID != uid)
				vpl->waitingThreads.erase(iter--);
		}

		vpl->nv.numWaitThreads = (int) vpl->waitingThreads.size();
		vpl->nv.freeSize = vpl->alloc.GetTotalFreeBytes();
		if (Memory::IsValidAddress(infoPtr) && Memory::Read_U32(infoPtr))
			Memory::WriteStruct(infoPtr, &vpl->nv);
		return 0;
	}
	else
		return error;
}


// TODO: Make proper kernel objects for these instead of using the UID as a pointer.



u32 AllocMemoryBlock(const char *pname, u32 type, u32 size, u32 paramsAddr) {

	// Just support allocating a block in the user region.
	if (paramsAddr) {
		u32 length = Memory::Read_U32(paramsAddr);
		if (length != 4) {
			WARN_LOG(HLE, "AllockMemoryBlock(SysMemUserForUser_FE707FDF) : unknown parameters with length %d", length);
		}
	}
	if (type < 0 || type > 1) {
		return SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE;
	}

	u32 blockPtr = userMemory.Alloc(size, type == 1, pname);
	if (!blockPtr) {
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	INFO_LOG(HLE,"%08x=AllocMemoryBlock(SysMemUserForUser_FE707FDF)(%s, %i, %08x, %08x)", blockPtr, pname, type, size, paramsAddr);

	// Create a UID object??? Nah, let's just us the UID itself (hack!)

	return blockPtr;
}

u32 FreeMemoryBlock(u32 uid) {
	INFO_LOG(HLE, "FreeMemoryBlock(%08x)", uid);
	u32 blockPtr = userMemory.GetBlockStartFromAddress(uid);
	if (!blockPtr) {
		return SCE_KERNEL_ERROR_UNKNOWN_UID;
	}
	userMemory.Free(blockPtr);
	return 0;
}

u32 GetMemoryBlockPtr(u32 uid, u32 addr) {
	INFO_LOG(HLE, "GetMemoryBlockPtr(%08x, %08x)", uid, addr);
	u32 blockPtr = userMemory.GetBlockStartFromAddress(uid);
	if (!blockPtr) {
		return SCE_KERNEL_ERROR_UNKNOWN_UID;
	}
	Memory::Write_U32(blockPtr, addr);
	return 0;
}

u32 SysMemUserForUser_D8DE5C1E(){
	ERROR_LOG(HLE,"HACKIMPL SysMemUserForUser_D8DE5C1E Returning 0");
	return 0; //according to jpcsp always returns 0
}
// These aren't really in sysmem, but they are memory related?

enum
{
	PSP_ERROR_UNKNOWN_TLS_ID = 0x800201D0,
	PSP_ERROR_TOO_MANY_TLS = 0x800201D1,
};

enum
{
	// TODO: Complete untested guesses.
	PSP_TLS_ATTR_FIFO = 0,
	PSP_TLS_ATTR_PRIORITY = 0x100,
	PSP_TLS_ATTR_HIGHMEM = 0x4000,
	PSP_TLS_ATTR_KNOWN = PSP_TLS_ATTR_HIGHMEM | PSP_TLS_ATTR_PRIORITY | PSP_TLS_ATTR_FIFO,
};

struct NativeTls
{
	SceSize size;
	char name[32];
	SceUInt attr;
	int index;
	u32 blockSize;
	u32 totalBlocks;
	u32 freeBlocks;
	u32 numWaitThreads;
};

struct TLS : public KernelObject
{
	const char *GetName() {return ntls.name;}
	const char *GetTypeName() {return "TLS";}
	static u32 GetMissingErrorCode() { return PSP_ERROR_UNKNOWN_TLS_ID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Tls; }
	int GetIDType() const { return SCE_KERNEL_TMID_Tls; }

	TLS() : next(0) {}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(ntls);
		p.Do(address);
		p.Do(next);
		p.Do(usage);
		p.DoMarker("TLS");
	}

	NativeTls ntls;
	u32 address;
	// TODO: Waiting threads.
	int next;
	std::vector<SceUID> usage;
};

KernelObject *__KernelTlsObject()
{
	return new TLS;
}

SceUID sceKernelCreateTls(const char *name, u32 partition, u32 attr, u32 blockSize, u32 count, u32 optionsPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): invalid name", SCE_KERNEL_ERROR_NO_MEMORY);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}
	if ((attr & ~PSP_TLS_ATTR_KNOWN) >= 0x100)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}
	if (partition < 1 || partition > 9 || partition == 7)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, partition);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	// We only support user right now.
	if (partition != 2 && partition != 6)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): invalid partition %d", SCE_KERNEL_ERROR_ILLEGAL_PERM, partition);
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
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): invalid blockSize/count", SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE);
		return SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE;
	}

	int index = -1;
	for (int i = 0; i < TLS_NUM_INDEXES; ++i)
		if (tlsUsedIndexes[i] == false)
		{
			index = i;
			break;
		}

	if (index == -1)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateTls(): ran out of indexes for TLS objects", PSP_ERROR_TOO_MANY_TLS);
		return PSP_ERROR_TOO_MANY_TLS;
	}

	u32 totalSize = blockSize * count;
	u32 blockPtr = userMemory.Alloc(totalSize, (attr & PSP_TLS_ATTR_HIGHMEM) != 0, name);
	userMemory.ListBlocks();

	if (blockPtr == (u32) -1)
	{
		ERROR_LOG(HLE, "%08x=sceKernelCreateTls(%s, %d, %08x, %d, %d, %08x): failed to allocate memory", SCE_KERNEL_ERROR_NO_MEMORY, name, partition, attr, blockSize, count, optionsPtr);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	TLS *tls = new TLS();
	SceUID id = kernelObjects.Create(tls);

	tls->ntls.size = sizeof(tls->ntls);
	strncpy(tls->ntls.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	tls->ntls.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	tls->ntls.attr = attr;
	tls->ntls.index = index;
	tlsUsedIndexes[index] = true;
	tls->ntls.blockSize = blockSize;
	tls->ntls.totalBlocks = count;
	tls->ntls.freeBlocks = count;
	tls->ntls.numWaitThreads = 0;
	tls->address = blockPtr;
	tls->usage.resize(count, 0);

	WARN_LOG(HLE, "%08x=sceKernelCreateTls(%s, %d, %08x, %d, %d, %08x)", id, name, partition, attr, blockSize, count, optionsPtr);

	// TODO: just alignment?
	if (optionsPtr != 0)
		WARN_LOG(HLE, "sceKernelCreateTls(%s) unsupported options parameter: %08x", name, optionsPtr);
	if ((attr & PSP_TLS_ATTR_PRIORITY) != 0)
		WARN_LOG(HLE, "sceKernelCreateTls(%s) unsupported attr parameter: %08x", name, attr);

	return id;
}

// Parameters are an educated guess.
int sceKernelDeleteTls(SceUID uid)
{
	WARN_LOG(HLE, "sceKernelDeleteTls(%08x)", uid);
	u32 error;
	TLS *tls = kernelObjects.Get<TLS>(uid, error);
	if (tls)
	{
		// TODO: Wake waiting threads, probably?
		userMemory.Free(tls->address);
		tlsUsedIndexes[tls->ntls.index] = false;
		kernelObjects.Destroy<TLS>(uid);
	}
	return error;
}

int sceKernelAllocateTls(SceUID uid)
{
	// TODO: Allocate downward if PSP_TLS_ATTR_HIGHMEM?
	DEBUG_LOG(HLE, "sceKernelAllocateTls(%08x)", uid);
	u32 error;
	TLS *tls = kernelObjects.Get<TLS>(uid, error);
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
				tls->next = (tls->next + 1) % tls->ntls.blockSize;
			}

			if (allocBlock != -1)
			{
				tls->usage[allocBlock] = threadID;
				--tls->ntls.freeBlocks;
			}
		}

		if (allocBlock == -1)
		{
			// TODO: Wait here, wake when one is free.
			ERROR_LOG_REPORT(HLE, "sceKernelAllocateTls: should wait");
			return -1;
		}

		return tls->address + allocBlock * tls->ntls.blockSize;
	}
	else
		return error;
}

// Parameters are an educated guess.
int sceKernelFreeTls(SceUID uid)
{
	WARN_LOG(HLE, "UNIMPL sceKernelFreeTls(%08x)", uid);
	u32 error;
	TLS *tls = kernelObjects.Get<TLS>(uid, error);
	if (tls)
	{
		SceUID threadID = __KernelGetCurThread();

		// If the thread already has one, return it.
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
			// TODO: Free anyone waiting for a free block.
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
int sceKernelReferTlsStatus(SceUID uid, u32 infoPtr)
{
	DEBUG_LOG(HLE, "sceKernelReferTlsStatus(%08x, %08x)", uid, infoPtr);
	u32 error;
	TLS *tls = kernelObjects.Get<TLS>(uid, error);
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
	{0xA291F107,sceKernelMaxFreeMemSize,"sceKernelMaxFreeMemSize"},
	{0xF919F628,sceKernelTotalFreeMemSize,"sceKernelTotalFreeMemSize"},
	{0x3FC9AE6A,WrapU_V<sceKernelDevkitVersion>,"sceKernelDevkitVersion"},
	{0x237DBD4F,WrapI_ICIUU<sceKernelAllocPartitionMemory>,"sceKernelAllocPartitionMemory"},	//(int size) ?
	{0xB6D61D02,WrapI_I<sceKernelFreePartitionMemory>,"sceKernelFreePartitionMemory"},	 //(void *ptr) ?
	{0x9D9A5BA1,WrapU_I<sceKernelGetBlockHeadAddr>,"sceKernelGetBlockHeadAddr"},			//(void *ptr) ?
	{0x13a5abef,WrapI_C<sceKernelPrintf>,"sceKernelPrintf"},
	{0x7591c7db,&WrapV_I<sceKernelSetCompiledSdkVersion>,"sceKernelSetCompiledSdkVersion"},
	{0x342061E5,&WrapV_I<sceKernelSetCompiledSdkVersion370>,"sceKernelSetCompiledSdkVersion370"},
	{0x315AD3A0,&WrapV_I<sceKernelSetCompiledSdkVersion380_390>,"sceKernelSetCompiledSdkVersion380_390"},
	{0xEBD5C3E6,&WrapV_I<sceKernelSetCompiledSdkVersion395>,"sceKernelSetCompiledSdkVersion395"},
	{0x057E7380,&WrapV_I<sceKernelSetCompiledSdkVersion401_402>,"sceKernelSetCompiledSdkVersion401_402"},
	{0xf77d77cb,&WrapV_I<sceKernelSetCompilerVersion>,"sceKernelSetCompilerVersion"},
	{0x91de343c,&WrapV_I<sceKernelSetCompiledSdkVersion500_505>,"sceKernelSetCompiledSdkVersion500_505"},
	{0x7893f79a,&WrapV_I<sceKernelSetCompiledSdkVersion507>,"sceKernelSetCompiledSdkVersion507"},
	{0x35669d4c,&WrapV_I<sceKernelSetCompiledSdkVersion600_602>,"sceKernelSetCompiledSdkVersion600_602"},  //??
	{0x1b4217bc,&WrapV_I<sceKernelSetCompiledSdkVersion603_605>,"sceKernelSetCompiledSdkVersion603_605"},
	{0x358ca1bb,&WrapV_I<sceKernelSetCompiledSdkVersion606>,"sceKernelSetCompiledSdkVersion606"},
	{0xfc114573,&WrapI_V<sceKernelGetCompiledSdkVersion>,"sceKernelGetCompiledSdkVersion"},
	// Obscure raw block API
	{0xDB83A952,WrapU_UU<GetMemoryBlockPtr>,"SysMemUserForUser_DB83A952"},  // GetMemoryBlockAddr
	{0x50F61D8A,WrapU_U<FreeMemoryBlock>,"SysMemUserForUser_50F61D8A"},  // FreeMemoryBlock
	{0xFE707FDF,WrapU_CUUU<AllocMemoryBlock>,"SysMemUserForUser_FE707FDF"},  // AllocMemoryBlock
	{0xD8DE5C1E,WrapU_V<SysMemUserForUser_D8DE5C1E>,"SysMemUserForUser_D8DE5C1E"}
};


void Register_SysMemUserForUser()
{
	RegisterModule("SysMemUserForUser", ARRAY_SIZE(SysMemUserForUser), SysMemUserForUser);
}
