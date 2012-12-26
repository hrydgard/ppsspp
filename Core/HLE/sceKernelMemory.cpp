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

#include "HLE.h"
#include "../System.h"
#include "../MIPS/MIPS.h"
#include "../MemMap.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"


//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
BlockAllocator userMemory(256);
BlockAllocator kernelMemory(256);
// STATE END
//////////////////////////////////////////////////////////////////////////


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
struct FPL : KernelObject
{
	const char *GetName() {return nf.name;}
	const char *GetTypeName() {return "FPL";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_FPLID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Fpl; }
	NativeFPL nf;
	bool *blocks;
	u32 address;

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

struct VPL : KernelObject
{
	const char *GetName() {return nv.name;}
	const char *GetTypeName() {return "VPL";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VPLID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Vpl; }
	SceKernelVplInfo nv;
	u32 size;
	bool *freeBlocks;
	u32 address;
	BlockAllocator alloc;
};

void __KernelMemoryInit()
{
	kernelMemory.Init(PSP_GetKernelMemoryBase(), PSP_GetKernelMemoryEnd()-PSP_GetKernelMemoryBase());
	userMemory.Init(PSP_GetUserMemoryBase(), PSP_GetUserMemoryEnd()-PSP_GetUserMemoryBase());
	INFO_LOG(HLE, "Kernel and user memory pools initialized");
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
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MPPID; }	/// ????
	int GetIDType() const { return 0; }

	PartitionMemoryBlock(BlockAllocator *_alloc, u32 size, bool fromEnd)
	{
		alloc = _alloc;
		address = alloc->Alloc(size, fromEnd, "PMB");
		alloc->ListBlocks();
	}
	~PartitionMemoryBlock()
	{
		alloc->Free(address);
	}
	bool IsValid() {return address != (u32)-1;}
	BlockAllocator *alloc;
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

void sceKernelAllocPartitionMemory()
{
	int partid = PARAM(0);
	const char *name = Memory::GetCharPointer(PARAM(1));
	int type = PARAM(2);
	u32 size = PARAM(3);
	int addr = PARAM(4); //only if type == addr

	PartitionMemoryBlock *block = new PartitionMemoryBlock(&userMemory, size, type==1);
	if (!block->IsValid())
	{
		ERROR_LOG(HLE, "ARGH! sceKernelAllocPartMem failed");
		RETURN(-1);
	}
	SceUID id = kernelObjects.Create(block);
	strncpy(block->name, name, 32);

	DEBUG_LOG(HLE,"%i = sceKernelAllocPartMem(partition = %i, %s, type= %i, size= %i, addr= %08x)",
		id, partid,name,type,size,addr);
	if (type == 2)
		ERROR_LOG(HLE, "ARGH! sceKernelAllocPartMem wants a specific address");

	RETURN(id); //for now
}

void sceKernelFreePartitionMemory()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelFreePartitionMemory(%d)",id);

	RETURN(kernelObjects.Destroy<PartitionMemoryBlock>(id));
}

void sceKernelGetBlockHeadAddr()
{
	SceUID id = PARAM(0);

	u32 error;
	PartitionMemoryBlock *block = kernelObjects.Get<PartitionMemoryBlock>(id, error);
	if (block)
	{
		DEBUG_LOG(HLE,"%08x = sceKernelGetBlockHeadAddr(%i)", block->address, id);
		RETURN(block->address);
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelGetBlockHeadAddr failed(%i)", id);
		RETURN(error);
	}
}


void sceKernelPrintf()
{
	const char *formatString = Memory::GetCharPointer(PARAM(0));

	ERROR_LOG(HLE,"UNIMPL sceKernelPrintf(%08x, %08x, %08x, %08x)", PARAM(0),PARAM(1),PARAM(2),PARAM(3));
	ERROR_LOG(HLE,"%s", formatString);
	RETURN(0);
}


// VPL = variable length memory pool


void sceKernelCreateVpl()
{
	const char *name = Memory::GetCharPointer(PARAM(0));

	u32 vplSize = PARAM(3);
	u32 memBlockPtr = userMemory.Alloc(vplSize, false, "VPL");
	if (memBlockPtr == -1) {
		ERROR_LOG(HLE, "sceKernelCreateVpl: Failed to allocate %i bytes of pool data", vplSize);
		RETURN(-1);
		return;
	}

	VPL *vpl = new VPL;
	SceUID id = kernelObjects.Create(vpl);

	strncpy(vpl->nv.name, name, 32);
	//vpl->nv.mpid = PARAM(1); //seems to be the standard memory partition (user, kernel etc)
	vpl->nv.attr = PARAM(2);
	vpl->size = vplSize;
	vpl->nv.poolSize = vpl->size;
	vpl->nv.size = sizeof(vpl->nv);
	vpl->nv.numWaitThreads = 0;
	vpl->nv.freeSize = vpl->nv.poolSize;
		
	vpl->address = memBlockPtr;
	vpl->alloc.Init(vpl->address, vpl->size);

	DEBUG_LOG(HLE,"sceKernelCreateVpl(\"%s\", block=%i, attr=%i, size=%i)", 
		name, PARAM(1), vpl->nv.attr, vpl->size);

	RETURN(id);
}

void sceKernelDeleteVpl()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelDeleteVpl(%i)", id);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(id, error);
	if (vpl)
	{
		userMemory.Free(vpl->address);
		kernelObjects.Destroy<VPL>(id);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

void sceKernelAllocateVpl()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelAllocateVpl()");
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(id, error);
	if (vpl)
	{
		u32 size = PARAM(1);
		int timeOut = PARAM(3);
		DEBUG_LOG(HLE,"sceKernelAllocateVpl(vpl=%i, size=%i, ptrout= %08x , timeout=%i)", id, size, PARAM(2), timeOut);
		u32 addr = vpl->alloc.Alloc(size);
		if (addr != (u32)-1)
		{
			Memory::Write_U32(addr, PARAM(2));
			RETURN(0);
		}
		else
		{
			ERROR_LOG(HLE, "FAILURE");
			RETURN(-1);
		}
	}
	else
	{
		RETURN(error);
	}
	RETURN(0);
}

void sceKernelAllocateVplCB()
{
	SceUID id = PARAM(0);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(id, error);
	if (vpl)
	{
		u32 size = PARAM(1);
		int timeOut = PARAM(3);
		DEBUG_LOG(HLE,"sceKernelAllocateVplCB(vpl=%i, size=%i, ptrout= %08x , timeout=%i)", id, size, PARAM(2), timeOut);
		u32 addr = vpl->alloc.Alloc(size);
		if (addr != (u32)-1)
		{
			Memory::Write_U32(addr, PARAM(2));
			RETURN(0);
		}
		else
		{
			ERROR_LOG(HLE, "sceKernelAllocateVplCB FAILURE");
			__KernelCheckCallbacks();
			RETURN(-1);
		}
	}
	else
	{
		RETURN(error);
	}
	RETURN(0);
}

void sceKernelTryAllocateVpl()
{
	SceUID id = PARAM(0);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(id, error);
	if (vpl)
	{
		u32 size = PARAM(1);
		int timeOut = PARAM(3);
		DEBUG_LOG(HLE,"sceKernelAllocateVplCB(vpl=%i, size=%i, ptrout= %08x , timeout=%i)", id, size, PARAM(2), timeOut);
		u32 addr = vpl->alloc.Alloc(size);
		if (addr != (u32)-1)
		{
			Memory::Write_U32(addr, PARAM(2));
			RETURN(0);
		}
		else
		{
			ERROR_LOG(HLE, "sceKernelTryAllocateVpl FAILURE");
			RETURN(-1);
		}
	}
	else
	{
		RETURN(error);
	}
	RETURN(0);
}

void sceKernelFreeVpl()
{
	SceUID id = PARAM(0);
	u32 blockPtr = PARAM(1);
	DEBUG_LOG(HLE,"sceKernelFreeVpl(%i, %08x)", id, blockPtr);
	u32 error;
	VPL *vpl = kernelObjects.Get<VPL>(id, error);
	if (vpl)
	{
		if (vpl->alloc.Free(blockPtr)) {
			RETURN(0);
			// Should trigger waiting threads
		} else {
			ERROR_LOG(HLE, "sceKernelFreeVpl: Error freeing %08x", blockPtr);
			RETURN(-1);
		}
	}
	else {
		RETURN(error);
	}
}

void sceKernelCancelVpl()
{
	ERROR_LOG(HLE,"UNIMPL: sceKernelCancelVpl()");
	RETURN(0);
}

void sceKernelReferVplStatus()
{
	SceUID id = PARAM(0);
	u32 error;
	VPL *v = kernelObjects.Get<VPL>(id, error);
	if (v)
	{
		DEBUG_LOG(HLE,"sceKernelReferVplStatus(%i, %08x)", id, PARAM(1));
		v->nv.freeSize = v->alloc.GetTotalFreeBytes();
		Memory::WriteStruct(PARAM(1), &v->nv);
	}
	else
	{
		ERROR_LOG(HLE,"Error %08x", error);
		RETURN(error);
	}
	RETURN(0);
}


// TODO: Make proper kernel objects for these instead of using the UID as a pointer.

u32 AllocMemoryBlock(const char *pname, u32 type, u32 size, u32 paramsAddr) {
	INFO_LOG(HLE,"AllocMemoryBlock(SysMemUserForUser_FE707FDF)(%s, %i, %i, %08x)", pname, type, size, paramsAddr);

	// Just support allocating a block in the user region.

	u32 blockPtr = userMemory.Alloc(size, false, pname);

	// Create a UID object??? Nah, let's just us the UID itself (hack!)

	return blockPtr;
}

u32 FreeMemoryBlock(u32 uid) {
	INFO_LOG(HLE, "FreeMemoryBlock(%i)", uid);
	userMemory.Free(uid);
	return 0;
}

u32 GetMemoryBlockPtr(u32 uid) {
	INFO_LOG(HLE, "GetMemoryBlockPtr(%i)", uid);
	return uid;
}

u32 sceKernelSetCompiledSdkVersion(u32 param) {
	// pretty sure this only takes one arg
	ERROR_LOG(HLE,"UNIMPL sceKernelSetCompiledSdkVersion(%08x)", param);
	return 0;
}

u32 sceKernelSetCompiledSdkVersion395(u32 param) {
	// pretty sure this only takes one arg
	ERROR_LOG(HLE,"UNIMPL sceKernelSetCompiledSdkVersion395(%08x)", param);
	return 0;
}

u32 sceKernelSetCompilerVersion(u32 a, u32 b, u32 c, u32 d) {
	ERROR_LOG(HLE,"UNIMPL sceKernelSetCompilerVersion(%08x, %08x, %08x, %08x)", a, b, c, d);
	return 0;
}

const HLEFunction SysMemUserForUser[] = {
	{0xA291F107,sceKernelMaxFreeMemSize,	"sceKernelMaxFreeMemSize"},
	{0xF919F628,sceKernelTotalFreeMemSize,"sceKernelTotalFreeMemSize"},
	{0x3FC9AE6A,sceKernelDevkitVersion,	 "sceKernelDevkitVersion"},
	{0x237DBD4F,sceKernelAllocPartitionMemory,"sceKernelAllocPartitionMemory"},	//(int size) ?
	{0xB6D61D02,sceKernelFreePartitionMemory,"sceKernelFreePartitionMemory"},	 //(void *ptr) ?
	{0x9D9A5BA1,sceKernelGetBlockHeadAddr,"sceKernelGetBlockHeadAddr"},			//(void *ptr) ?
	{0x13a5abef,sceKernelPrintf,"sceKernelPrintf 0x13a5abef"},
	{0x7591c7db,WrapU_U<sceKernelSetCompiledSdkVersion>,"sceKernelSetCompiledSdkVersion"},
	{0x342061E5,0,"sceKernelSetCompiledSdkVersion370"},
	{0x315AD3A0,0,"sceKernelSetCompiledSdkVersion380_390"},
	{0xEBD5C3E6,WrapU_U<sceKernelSetCompiledSdkVersion395>,"sceKernelSetCompiledSdkVersion395"},
	{0xf77d77cb,WrapU_UUUU<sceKernelSetCompilerVersion>,"sceKernelSetCompilerVersion"},
	{0x35669d4c,0,"sceKernelSetCompiledSdkVersion600_602"},  //??
	{0x1b4217bc,0,"sceKernelSetCompiledSdkVersion603_605"},
	{0x358ca1bb,0,"sceKernelSetCompiledSdkVersion606"},

	// Obscure raw block API
	{0xDB83A952,WrapU_U<GetMemoryBlockPtr>,"SysMemUserForUser_DB83A952"},  // GetMemoryBlockAddr
	{0x91DE343C,0,"SysMemUserForUser_91DE343C"},
	{0x50F61D8A,WrapU_U<FreeMemoryBlock>,"SysMemUserForUser_50F61D8A"},  // FreeMemoryBlock
	{0xFE707FDF,WrapU_CUUU<AllocMemoryBlock>,"SysMemUserForUser_FE707FDF"},  // AllocMemoryBlock
};


void Register_SysMemUserForUser()
{
	RegisterModule("SysMemUserForUser", ARRAY_SIZE(SysMemUserForUser), SysMemUserForUser);
}
