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


// There's a good description of the thread scheduling rules in:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/ThreadManForUser.java

#include "sceKernelModule.h"
#include "HLE.h"


void sceKernelChangeThreadPriority();
int __KernelCreateThread(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr);
int sceKernelCreateThread(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr);
void sceKernelDelayThread();
void sceKernelDelayThreadCB();
void sceKernelDelaySysClockThread();
void sceKernelDelaySysClockThreadCB();
int sceKernelDeleteThread(int threadHandle);
void sceKernelExitDeleteThread();
void sceKernelExitThread();
void _sceKernelExitThread();
void sceKernelGetThreadId();
void sceKernelGetThreadCurrentPriority();
int sceKernelStartThread(SceUID threadToStartID, u32 argSize, u32 argBlockPtr);
u32 sceKernelSuspendDispatchThread();
u32 sceKernelResumeDispatchThread(u32 suspended);
int sceKernelWaitThreadEnd(SceUID threadID, u32 timeoutPtr);
u32 sceKernelReferThreadStatus(u32 uid, u32 statusPtr);
u32 sceKernelReferThreadRunStatus(u32 uid, u32 statusPtr);
int sceKernelReleaseWaitThread(SceUID threadID);
void sceKernelChangeCurrentThreadAttr();
void sceKernelRotateThreadReadyQueue();
void sceKernelCheckThreadStack();
void sceKernelSuspendThread();
void sceKernelResumeThread();
void sceKernelWakeupThread();
void sceKernelCancelWakeupThread();
int sceKernelTerminateDeleteThread(int threadno);
int sceKernelTerminateThread(u32 threadID);
int sceKernelWaitThreadEndCB(SceUID threadID, u32 timeoutPtr);
void sceKernelGetThreadExitStatus();
u32 sceKernelGetThreadmanIdType(u32);
u32 sceKernelGetThreadmanIdList(u32 type, u32 readBufPtr, u32 readBufSize, u32 idCountPtr);
u32 sceKernelExtendThreadStack(u32 cpu, u32 size, u32 entryAddr, u32 entryParameter);

struct SceKernelSysClock {
	u32 lo;
	u32 hi;
};


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
	WAITTYPE_MSGPIPE = 8, // fake
	WAITTYPE_THREADEND = 9,
	WAITTYPE_AUDIOCHANNEL = 10, // this is fake, should be replaced with 8 eventflags   ( ?? )
	WAITTYPE_UMD = 11,           // this is fake, should be replaced with 1 eventflag    ( ?? )
	WAITTYPE_VBLANK = 12,           // fake
	WAITTYPE_MUTEX = 13,
	WAITTYPE_LWMUTEX = 14,
	WAITTYPE_CTRL = 15,
};


struct ThreadContext
{
	void reset();
	u32 r[32];
	float f[32];
	float v[128];
	u32 vfpuCtrl[16];

	u32 pc;

	u32 hi;
	u32 lo;

	u32 fcr0;
	u32 fcr31;
	u32 fpcond;
};

// Internal API, used by implementations of kernel functions

void __KernelThreadingInit();
void __KernelThreadingDoState(PointerWrap &p);
void __KernelThreadingDoStateLate(PointerWrap &p);
void __KernelThreadingShutdown();
KernelObject *__KernelThreadObject();
KernelObject *__KernelCallbackObject();

void __KernelScheduleWakeup(int threadnumber, s64 usFromNow);
SceUID __KernelGetCurThread();

void __KernelSaveContext(ThreadContext *ctx);
void __KernelLoadContext(ThreadContext *ctx);

// TODO: Replace this with __KernelResumeThreadFromWait over time as it's misguided.
// It's better that each subsystem keeps track of the list of waiting threads
// and resumes them manually one by one using __KernelResumeThreadFromWait.
bool __KernelTriggerWait(WaitType type, int id, const char *reason, bool dontSwitch = false);
bool __KernelTriggerWait(WaitType type, int id, int retVal, const char *reason, bool dontSwitch);
u32 __KernelResumeThreadFromWait(SceUID threadID); // can return an error value
u32 __KernelResumeThreadFromWait(SceUID threadID, int retval);

u32 __KernelGetWaitValue(SceUID threadID, u32 &error);
u32 __KernelGetWaitTimeoutPtr(SceUID threadID, u32 &error);
SceUID __KernelGetWaitID(SceUID threadID, WaitType type, u32 &error);
void __KernelWaitCurThread(WaitType type, SceUID waitId, u32 waitValue, u32 timeoutPtr, bool processCallbacks, const char *reason);
void __KernelReSchedule(const char *reason = "no reason");
void __KernelReSchedule(bool doCallbacks, const char *reason);

// Registered callback types
enum RegisteredCallbackType {
	THREAD_CALLBACK_UMD = 0,
	THREAD_CALLBACK_IO = 1,
	THREAD_CALLBACK_MEMORYSTICK = 2,
	THREAD_CALLBACK_MEMORYSTICK_FAT = 3,
	THREAD_CALLBACK_POWER = 4,
	THREAD_CALLBACK_EXIT = 5,
	THREAD_CALLBACK_USER_DEFINED = 6,
	THREAD_CALLBACK_SIZE = 7,
	THREAD_CALLBACK_NUM_TYPES = 8,
};

// These operate on the current thread
u32 __KernelRegisterCallback(RegisteredCallbackType type, SceUID cbId);
u32 __KernelUnregisterCallback(RegisteredCallbackType type, SceUID cbId);

// If cbId == -1, all the callbacks of the type on all the threads get notified.
// If not, only this particular callback gets notified.
u32 __KernelNotifyCallbackType(RegisteredCallbackType type, SceUID cbId, int notifyArg);

SceUID __KernelGetCurThread();
SceUID __KernelGetCurThreadModuleId();
void __KernelSetupRootThread(SceUID moduleId, int args, const char *argp, int prio, int stacksize, int attr); //represents the real PSP elf loader, run before execution
void __KernelStartIdleThreads();
void __KernelReturnFromThread();  // Called as HLE function
u32 __KernelGetThreadPrio(SceUID id);
bool __KernelThreadSortPriority(SceUID thread1, SceUID thread2);

void __KernelIdle();

u32 __KernelMipsCallReturnAddress();
u32 __KernelInterruptReturnAddress();  // TODO: remove

// Internal access - used by sceSetGeCallback
u32 __KernelCreateCallback(const char *name, u32 entrypoint, u32 signalArg);

void sceKernelCreateCallback();
void sceKernelDeleteCallback();
void sceKernelNotifyCallback();
void sceKernelCancelCallback();
void sceKernelGetCallbackCount();
void sceKernelCheckCallback();
void sceKernelGetCallbackCount();
void sceKernelReferCallbackStatus();

class Action;

// Not an official Callback object, just calls a mips function on the current thread.
void __KernelDirectMipsCall(u32 entryPoint, Action *afterAction, u32 args[], int numargs, bool reschedAfter);

void __KernelReturnFromMipsCall();  // Called as HLE function
bool __KernelInCallback();

// Should be called by (nearly) all ...CB functions.
bool __KernelCheckCallbacks();
bool __KernelForceCallbacks();
class Thread;
void __KernelSwitchContext(Thread *target, const char *reason);
bool __KernelExecutePendingMipsCalls(bool reschedAfter);
void __KernelNotifyCallback(RegisteredCallbackType type, SceUID cbId, int notifyArg);

// Switch to an idle / non-user thread, if not already on one.
// Returns whether a switch occurred.
bool __KernelSwitchOffThread(const char *reason);

// A call into game code. These can be pending on a thread.
// Similar to Callback-s (NOT CallbackInfos) in JPCSP.
typedef Action *(*ActionCreator)();
Action *__KernelCreateAction(int actionType);
int __KernelRegisterActionType(ActionCreator creator);
void __KernelRestoreActionType(int actionType, ActionCreator creator);

struct MipsCall {
	MipsCall()
	{
		doAfter = NULL;
	}

	u32 entryPoint;
	u32 cbId;
	u32 args[6];
	int numArgs;
	Action *doAfter;
	u32 savedIdRegister;
	u32 savedRa;
	u32 savedPc;
	u32 savedV0;
	u32 savedV1;
	std::string tag;
	u32 savedId;
	bool reschedAfter;

	void DoState(PointerWrap &p);
	void setReturnValue(u32 value);
};

class Action
{
public:
	virtual ~Action() {}
	virtual void run(MipsCall &call) = 0;
	virtual void DoState(PointerWrap &p) = 0;
	int actionTypeID;
};

enum ThreadStatus
{
	THREADSTATUS_RUNNING = 1,
	THREADSTATUS_READY = 2,
	THREADSTATUS_WAIT = 4,
	THREADSTATUS_SUSPEND = 8,
	THREADSTATUS_DORMANT = 16,
	THREADSTATUS_DEAD = 32,

	THREADSTATUS_WAITSUSPEND = THREADSTATUS_WAIT | THREADSTATUS_SUSPEND
};

void __KernelChangeThreadState(Thread *thread, ThreadStatus newStatus);

typedef void (*ThreadCallback)(SceUID threadID);
void __KernelListenThreadEnd(ThreadCallback callback);
