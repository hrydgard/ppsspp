// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once


// There's a good description of the thread scheduling rules in:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/ThreadManForUser.java

#include "sceKernelModule.h"
#include "HLE.h"


void sceKernelChangeThreadPriority();
void sceKernelCreateThread();
void sceKernelDelayThread();
void sceKernelDelayThreadCB();
void sceKernelDeleteThread();
void sceKernelExitDeleteThread();
void sceKernelExitThread();
void _sceKernelExitThread();
void sceKernelGetThreadId();
void sceKernelStartThread();
void sceKernelWaitThreadEnd();
void sceKernelReferThreadStatus();
void sceKernelChangeCurrentThreadAttr();
void sceKernelRotateThreadReadyQueue();
void sceKernelCheckThreadStack();
void sceKernelSuspendThread();
void sceKernelResumeThread();
void sceKernelWakeupThread();
void sceKernelTerminateDeleteThread();
void sceKernelWaitThreadEndCB();
void sceKernelGetThreadExitStatus();
void sceKernelGetThreadmanIdType();


enum WaitType //probably not the real values
{
	WAITTYPE_NONE = 0,
	WAITTYPE_SLEEP = 1,
	WAITTYPE_DELAY = 2,
	WAITTYPE_SEMA  = 3,
	WAITTYPE_EVENTFLAG = 4,
	WAITTYPE_MBX = 5,
	WAITTYPE_VPL = 6,
	WAITTYPE_FPL = 7,
  //
	WAITTYPE_THREADEND = 9,
	WAITTYPE_AUDIOCHANNEL = 10, // this is fake, should be replaced with 8 eventflags   ( ?? )
	WAITTYPE_UMD = 11,           // this is fake, should be replaced with 1 eventflag    ( ?? )
	WAITTYPE_VBLANK = 12,           // fake
  WAITTYPE_MUTEX = 13,
};


struct ThreadContext
{
  void reset();
  u32 r[32];
  float f[32];
  float v[128];
  u32 vfpuCtrl[16];

  u32 hi;
  u32 lo;
  u32 pc;
  u32 fpcond;

  u32 fcr0;
  u32 fcr31;
};


// Internal API, used by implementations of kernel functions

void __KernelThreadingInit();
void __KernelThreadingShutdown();

void __KernelScheduleWakeup(int usFromNow, int threadnumber);
SceUID __KernelGetCurThread();

void __KernelSaveContext(ThreadContext *ctx);
void __KernelLoadContext(ThreadContext *ctx);

// TODO: Replace this with __KernelResumeThread over time as it's misguided.
bool __KernelTriggerWait(WaitType type, int id, bool dontSwitch = false);
u32 __KernelResumeThread(SceUID threadID);  // can return an error value

u32 __KernelGetWaitValue(SceUID threadID, u32 &error);
void __KernelWaitCurThread(WaitType type, SceUID waitId, u32 waitValue, int timeout, bool processCallbacks);
void __KernelReSchedule(const char *reason = "no reason");
void __KernelNotifyCallback(SceUID threadID, SceUID cbid, u32 arg);


SceUID __KernelGetCurThread();
void __KernelSetupRootThread(SceUID moduleId, int args, const char *argp, int prio, int stacksize, int attr); //represents the real PSP elf loader, run before execution
void __KernelStartIdleThreads();

void _sceKernelReturnFromThread();
void _sceKernelIdle();

u32 __KernelCallbackReturnAddress();
u32 __KernelInterruptReturnAddress();
