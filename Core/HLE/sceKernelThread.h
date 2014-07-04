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

#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/HLE/sceKernel.h"

// There's a good description of the thread scheduling rules in:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/ThreadManForUser.java

int sceKernelChangeThreadPriority(SceUID threadID, int priority);
SceUID __KernelCreateThreadInternal(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr);
int __KernelCreateThread(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr);
int sceKernelCreateThread(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr);
int sceKernelDelayThread(u32 usec);
int sceKernelDelayThreadCB(u32 usec);
int sceKernelDelaySysClockThread(u32 sysclockAddr);
int sceKernelDelaySysClockThreadCB(u32 sysclockAddr);
void __KernelStopThread(SceUID threadID, int exitStatus, const char *reason);
u32 __KernelDeleteThread(SceUID threadID, int exitStatus, const char *reason);
int sceKernelDeleteThread(int threadHandle);
void sceKernelExitDeleteThread(int exitStatus);
void sceKernelExitThread(int exitStatus);
void _sceKernelExitThread(int exitStatus);
SceUID sceKernelGetThreadId();
void sceKernelGetThreadCurrentPriority();
int __KernelStartThread(SceUID threadToStartID, int argSize, u32 argBlockPtr, bool forceArgs = false);
int sceKernelStartThread(SceUID threadToStartID, int argSize, u32 argBlockPtr);
u32 sceKernelSuspendDispatchThread();
u32 sceKernelResumeDispatchThread(u32 suspended);
int sceKernelWaitThreadEnd(SceUID threadID, u32 timeoutPtr);
u32 sceKernelReferThreadStatus(u32 uid, u32 statusPtr);
u32 sceKernelReferThreadRunStatus(u32 uid, u32 statusPtr);
int sceKernelReleaseWaitThread(SceUID threadID);
int sceKernelChangeCurrentThreadAttr(u32 clearAttr, u32 setAttr);
int sceKernelRotateThreadReadyQueue(int priority);
int sceKernelCheckThreadStack();
int sceKernelSuspendThread(SceUID threadID);
int sceKernelResumeThread(SceUID threadID);
int sceKernelWakeupThread(SceUID threadID);
int sceKernelCancelWakeupThread(SceUID threadID);
int sceKernelSleepThread();
int sceKernelSleepThreadCB();
int sceKernelTerminateDeleteThread(int threadno);
int sceKernelTerminateThread(SceUID threadID);
int sceKernelWaitThreadEndCB(SceUID threadID, u32 timeoutPtr);
int sceKernelGetThreadExitStatus(SceUID threadID);
u32 sceKernelGetThreadmanIdType(u32);
u32 sceKernelGetThreadmanIdList(u32 type, u32 readBufPtr, u32 readBufSize, u32 idCountPtr);
u32 sceKernelExtendThreadStack(u32 size, u32 entryAddr, u32 entryParameter);

struct SceKernelSysClock {
	u32_le lo;
	u32_le hi;
};


// TODO: Map these to PSP wait types.  Most of these are wrong.
// remember to update the waitTypeNames array in sceKernelThread.cpp when changing these
enum WaitType
{
	WAITTYPE_NONE         = 0,
	WAITTYPE_SLEEP        = 1,
	WAITTYPE_DELAY        = 2,
	WAITTYPE_SEMA         = 3,
	WAITTYPE_EVENTFLAG    = 4,
	WAITTYPE_MBX          = 5,
	WAITTYPE_VPL          = 6,
	WAITTYPE_FPL          = 7,
	WAITTYPE_MSGPIPE      = 8,  // fake
	WAITTYPE_THREADEND    = 9,
	WAITTYPE_AUDIOCHANNEL = 10, // this is fake, should be replaced with 8 eventflags   ( ?? )
	WAITTYPE_UMD          = 11, // this is fake, should be replaced with 1 eventflag    ( ?? )
	WAITTYPE_VBLANK       = 12, // fake
	WAITTYPE_MUTEX        = 13,
	WAITTYPE_LWMUTEX      = 14,
	WAITTYPE_CTRL         = 15,
	WAITTYPE_IO           = 16,
	WAITTYPE_GEDRAWSYNC   = 17,
	WAITTYPE_GELISTSYNC   = 18,
	WAITTYPE_MODULE       = 19,
	WAITTYPE_HLEDELAY     = 20,
	WAITTYPE_TLSPL        = 21,
	WAITTYPE_VMEM         = 22,
	WAITTYPE_ASYNCIO      = 23,

	NUM_WAITTYPES
};

const char *getWaitTypeName(WaitType type);

// Suspend wait and timeout while a thread enters a callback.
typedef void (* WaitBeginCallbackFunc)(SceUID threadID, SceUID prevCallbackId);
// Resume wait and timeout as a thread exits a callback.
typedef void (* WaitEndCallbackFunc)(SceUID threadID, SceUID prevCallbackId);

void __KernelRegisterWaitTypeFuncs(WaitType type, WaitBeginCallbackFunc beginFunc, WaitEndCallbackFunc endFunc);

struct ThreadContext
{
	void reset();

	// r must be followed by f.
	u32 r[32];
	union {
		float f[32];
		u32 fi[32];
		int fs[32];
	};
	union {
		float v[128];
		u32 vi[128];
	};
	u32 vfpuCtrl[16];

	union {
		struct {
			u32 pc;

			u32 hi;
			u32 lo;

			u32 fcr31;
			u32 fpcond;
		};
		u32 other[6];
	};
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
u32 __KernelGetCurThreadStack();
const char *__KernelGetThreadName(SceUID threadID);

void __KernelSaveContext(ThreadContext *ctx, bool vfpuEnabled);
void __KernelLoadContext(ThreadContext *ctx, bool vfpuEnabled);

u32 __KernelResumeThreadFromWait(SceUID threadID, u32 retval); // can return an error value
u32 __KernelResumeThreadFromWait(SceUID threadID, u64 retval);

inline u32 __KernelResumeThreadFromWait(SceUID threadID, int retval)
{
	return __KernelResumeThreadFromWait(threadID, (u32)retval);
}

inline u32 __KernelResumeThreadFromWait(SceUID threadID, s64 retval)
{
	return __KernelResumeThreadFromWait(threadID, (u64)retval);
}

u32 __KernelGetWaitValue(SceUID threadID, u32 &error);
u32 __KernelGetWaitTimeoutPtr(SceUID threadID, u32 &error);
SceUID __KernelGetWaitID(SceUID threadID, WaitType type, u32 &error);
SceUID __KernelGetCurrentCallbackID(SceUID threadID, u32 &error);
void __KernelWaitCurThread(WaitType type, SceUID waitId, u32 waitValue, u32 timeoutPtr, bool processCallbacks, const char *reason);
void __KernelWaitCallbacksCurThread(WaitType type, SceUID waitID, u32 waitValue, u32 timeoutPtr);
void __KernelReSchedule(const char *reason = "no reason");
void __KernelReSchedule(bool doCallbacks, const char *reason);

SceUID __KernelGetCurThread();
SceUID __KernelGetCurThreadModuleId();
SceUID __KernelSetupRootThread(SceUID moduleId, int args, const char *argp, int prio, int stacksize, int attr); //represents the real PSP elf loader, run before execution
void __KernelStartIdleThreads(SceUID moduleId);
void __KernelReturnFromThread();  // Called as HLE function
u32 __KernelGetThreadPrio(SceUID id);
bool __KernelThreadSortPriority(SceUID thread1, SceUID thread2);
bool __KernelIsDispatchEnabled();
void __KernelReturnFromExtendStack();

void __KernelIdle();

u32 __KernelMipsCallReturnAddress();
u32 __KernelInterruptReturnAddress();  // TODO: remove

SceUID sceKernelCreateCallback(const char *name, u32 entrypoint, u32 signalArg);
int sceKernelDeleteCallback(SceUID cbId);
int sceKernelNotifyCallback(SceUID cbId, int notifyArg);
int sceKernelCancelCallback(SceUID cbId);
int sceKernelGetCallbackCount(SceUID cbId);
void sceKernelCheckCallback();
int sceKernelReferCallbackStatus(SceUID cbId, u32 statusAddr);

class Action;

// Not an official Callback object, just calls a mips function on the current thread.
void __KernelDirectMipsCall(u32 entryPoint, Action *afterAction, u32 args[], int numargs, bool reschedAfter);

void __KernelReturnFromMipsCall();  // Called as HLE function
bool __KernelInCallback();

// Should be called by (nearly) all ...CB functions.
bool __KernelCheckCallbacks();
bool __KernelForceCallbacks();
bool __KernelCurHasReadyCallbacks();
class Thread;
void __KernelSwitchContext(Thread *target, const char *reason);
bool __KernelExecutePendingMipsCalls(Thread *currentThread, bool reschedAfter);
void __KernelNotifyCallback(SceUID cbId, int notifyArg);

// Switch to an idle / non-user thread, if not already on one.
// Returns whether a switch occurred.
bool __KernelSwitchOffThread(const char *reason);
bool __KernelSwitchToThread(SceUID threadID, const char *reason);

// Set a thread's return address to a specific FakeSyscall nid.
// Discards old RA.  Only useful for special threads that do special things on exit.
u32 __KernelSetThreadRA(SceUID threadID, u32 nid);

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
	u32 savedRa;
	u32 savedPc;
	u32 savedV0;
	u32 savedV1;
	std::string tag;
	u32 savedId;
	bool reschedAfter;

	void DoState(PointerWrap &p);
	void setReturnValue(u32 value);
	void setReturnValue(u64 value);
	inline void setReturnValue(int value)
	{
		setReturnValue((u32)value);
	}
	inline void setReturnValue(s64 value)
	{
		setReturnValue((u64)value);
	}
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
	THREADSTATUS_READY   = 2,
	THREADSTATUS_WAIT    = 4,
	THREADSTATUS_SUSPEND = 8,
	THREADSTATUS_DORMANT = 16,
	THREADSTATUS_DEAD    = 32,

	THREADSTATUS_WAITSUSPEND = THREADSTATUS_WAIT | THREADSTATUS_SUSPEND
};

void __KernelChangeThreadState(Thread *thread, ThreadStatus newStatus);

typedef void (*ThreadCallback)(SceUID threadID);
void __KernelListenThreadEnd(ThreadCallback callback);

struct DebugThreadInfo
{
	SceUID id;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	u32 status;
	u32 curPC;
	u32 entrypoint;
	u32 initialStack;
	int stackSize;
	int priority;
	WaitType waitType;
	bool isCurrent;
};

std::vector<DebugThreadInfo> GetThreadsInfo();
void __KernelChangeThreadState(SceUID threadId, ThreadStatus newStatus);

int LoadExecForUser_362A956B();
int sceKernelRegisterExitCallback(SceUID cbId);
