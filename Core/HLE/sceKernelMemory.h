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


//todo: "real" memory block allocator, 
// have elf loader grab its memory block first to avoid overwriting,
// etc


extern BlockAllocator userMemory;
extern BlockAllocator kernelMemory;

void __KernelMemoryInit();
void __KernelMemoryDoState(PointerWrap &p);
void __KernelMemoryShutdown();
KernelObject *__KernelMemoryFPLObject();
KernelObject *__KernelMemoryVPLObject();
KernelObject *__KernelMemoryPMBObject();
KernelObject *__KernelTlsObject();

SceUID sceKernelCreateVpl(const char *name, int partition, u32 attr, u32 vplSize, u32 optPtr);
int sceKernelDeleteVpl(SceUID uid);
int sceKernelAllocateVpl(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelAllocateVplCB(SceUID uid, u32 size, u32 addrPtr, u32 timeoutPtr);
int sceKernelTryAllocateVpl(SceUID uid, u32 size, u32 addrPtr);
int sceKernelFreeVpl(SceUID uid, u32 addr);
int sceKernelCancelVpl(SceUID uid, u32 numWaitThreadsPtr);
int sceKernelReferVplStatus(SceUID uid, u32 infoPtr);

void sceKernelCreateFpl();
void sceKernelDeleteFpl();
void sceKernelAllocateFpl();
void sceKernelAllocateFplCB();
void sceKernelTryAllocateFpl();
void sceKernelFreeFpl();
void sceKernelCancelFpl();
void sceKernelReferFplStatus();

int sceKernelGetCompiledSdkVersion();

SceUID sceKernelCreateTls(const char *name, u32 partitionid, u32 attr, u32 size, u32 count, u32 optionsPtr);
int sceKernelDeleteTls(SceUID uid);
int sceKernelAllocateTls(SceUID uid);
int sceKernelFreeTls(SceUID uid);
int sceKernelReferTlsStatus(SceUID uid, u32 infoPtr);

void Register_SysMemUserForUser();
