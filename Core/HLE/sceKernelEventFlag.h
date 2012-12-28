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

int sceKernelCreateEventFlag(const char *name, u32 flag_attr, u32 flag_initPattern, u32 optPtr);
u32 sceKernelClearEventFlag(SceUID id, u32 bits);
u32 sceKernelDeleteEventFlag(SceUID uid);
u32 sceKernelSetEventFlag(SceUID id, u32 bitsToSet);
int sceKernelWaitEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr);
int sceKernelWaitEventFlagCB(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr);
int sceKernelPollEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr);
u32 sceKernelReferEventFlagStatus(SceUID id, u32 statusPtr);
u32 sceKernelCancelEventFlag(SceUID uid, u32 pattern, u32 numWaitThreadsPtr);

void __KernelEventFlagInit();
void __KernelEventFlagDoState(PointerWrap &p);
KernelObject *__KernelEventFlagObject();
