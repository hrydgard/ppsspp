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

int sceKernelCreateMutex(const char *name, u32 attr, int initialCount, u32 optionsPtr);
int sceKernelDeleteMutex(SceUID id);
int sceKernelLockMutex(SceUID id, int count, u32 timeoutPtr);
int sceKernelLockMutexCB(SceUID id, int count, u32 timeoutPtr);
int sceKernelTryLockMutex(SceUID id, int count);
int sceKernelUnlockMutex(SceUID id, int count);
int sceKernelReferMutexStatus(SceUID id, u32 infoAddr);

int sceKernelCreateLwMutex(u32 workareaPtr, const char *name, u32 attr, int initialCount, u32 optionsPtr);
int sceKernelDeleteLwMutex(u32 workareaPtr);
int sceKernelTryLockLwMutex(u32 workareaPtr, int count);
int sceKernelTryLockLwMutex_600(u32 workareaPtr, int count);
int sceKernelLockLwMutex(u32 workareaPtr, int count, u32 timeoutPtr);
int sceKernelLockLwMutexCB(u32 workareaPtr, int count, u32 timeoutPtr);
int sceKernelUnlockLwMutex(u32 workareaPtr, int count);
int sceKernelReferLwMutexStatusByID(SceUID uid, u32 infoPtr);
int sceKernelReferLwMutexStatus(u32 workareaPtr, u32 infoPtr);

void __KernelMutexTimeout(u64 userdata, int cyclesLate);
void __KernelLwMutexTimeout(u64 userdata, int cyclesLate);
void __KernelMutexThreadEnd(SceUID thread);

void __KernelMutexInit();
void __KernelMutexDoState(PointerWrap &p);
void __KernelMutexShutdown();
KernelObject *__KernelMutexObject();
KernelObject *__KernelLwMutexObject();
