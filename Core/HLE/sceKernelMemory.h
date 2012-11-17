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


//todo: "real" memory block allocator, 
// have elf loader grab its memory block first to avoid overwriting,
// etc


extern BlockAllocator userMemory;
extern BlockAllocator kernelMemory;

void __KernelMemoryInit();
void __KernelMemoryShutdown();

void sceKernelCreateVpl();
void sceKernelDeleteVpl();
void sceKernelAllocateVpl();
void sceKernelAllocateVplCB();
void sceKernelTryAllocateVpl();
void sceKernelFreeVpl();
void sceKernelCancelVpl();
void sceKernelReferVplStatus();

void sceKernelCreateFpl();
void sceKernelDeleteFpl();
void sceKernelAllocateFpl();
void sceKernelAllocateFplCB();
void sceKernelTryAllocateFpl();
void sceKernelFreeFpl();
void sceKernelCancelFpl();
void sceKernelReferFplStatus();


void Register_SysMemUserForUser();
