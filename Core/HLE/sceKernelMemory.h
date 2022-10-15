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

#include "../Util/BlockAllocator.h"
#include "sceKernel.h"

enum MemblockType
{
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

SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr);
int sceKernelDeleteVpl(SceUID uid);
int sceKernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelAllocateVplCB(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelTryAllocateVpl(SceUID uid, u32 size, u32 addrPtr);
int sceKernelFreeVpl(SceUID uid, u32 addr);
int sceKernelCancelVpl(SceUID uid, u32 numWaitThreadsPtr);
int sceKernelReferVplStatus(SceUID uid, u32 infoPtr);

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
