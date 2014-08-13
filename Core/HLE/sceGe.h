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

#include "GPU/GPUInterface.h"
#include "Core/MemMap.h"

#define SCE_GE_LIST_COMPLETED  0
#define SCE_GE_LIST_QUEUED     1
#define SCE_GE_LIST_DRAWING    2
#define SCE_GE_LIST_STALLING   3
#define SCE_GE_LIST_PAUSED     4

typedef int SceUID;
typedef u32_le SceSize_le;
// typedef void (*PspGeCallback)(int id, void *arg);

struct PspGeCallbackData
{
	u32_le signal_func;
	u32_le signal_arg; //ptr
	u32_le finish_func;  // PspGeCallback
	u32_le finish_arg;
};

struct PspGeListArgs
{
	SceSize_le size;
	PSPPointer<u32_le> context;
	u32_le numStacks;
	u32_le stackAddr;
};

void Register_sceGe_user();

void __GeInit();
void __GeDoState(PointerWrap &p);
void __GeShutdown();
bool __GeTriggerSync(GPUSyncType waitType, int id, u64 atTicks);
bool __GeTriggerInterrupt(int listid, u32 pc, u64 atTicks);
void __GeWaitCurrentThread(GPUSyncType type, SceUID waitId, const char *reason);
bool __GeTriggerWait(GPUSyncType type, SceUID waitId);


// Export functions for use by Util/PPGe
u32 sceGeRestoreContext(u32 ctxAddr);
u32 sceGeSaveContext(u32 ctxAddr);
int sceGeBreak(u32 mode);
int sceGeContinue();
int sceGeListSync(u32 displayListID, u32 mode);

u32 sceGeListEnQueue(u32 listAddress, u32 stallAddress, int callbackId, u32 optParamAddr);
u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, int callbackId, u32 optParamAddr);
