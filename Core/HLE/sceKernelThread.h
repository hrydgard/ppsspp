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
#include <map>
#include <list>

#include "Common/CommonTypes.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/PSPThreadContext.h"
#include "Core/HLE/KernelThreadDebugInterface.h"

// There's a good description of the thread scheduling rules in:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/ThreadManForUser.java

class PSPThread;
class DebugInterface;
class BlockAllocator;

int sceKernelChangeThreadPriority(SceUID threadID, int priority);
SceUID __KernelCreateThreadInternal(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr);
int __KernelCreateThread(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr, bool allowKernel);
int sceKernelCreateThread(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr);
int sceKernelDelayThread(u32 usec);
int sceKernelDelayThreadCB(u32 usec);
int sceKernelDelaySysClockThread(u32 sysclockAddr);
int sceKernelDelaySysClockThreadCB(u32 sysclockAddr);
void __KernelStopThread(SceUID threadID, int exitStatus, const char *reason);
u32 __KernelDeleteThread(SceUID threadID, int exitStatus, const char *reason);
int sceKernelDeleteThread(int threadHandle);
int sceKernelExitDeleteThread(int exitStatus);
int sceKernelExitThread(int exitStatus);
void _sceKernelExitThread(int exitStatus);
SceUID sceKernelGetThreadId();
int sceKernelGetThreadCurrentPriority();
// Warning: will alter v0 in current MIPS state.
int __KernelStartThread(SceUID threadToStartID, int argSize, u32 argBlockPtr, bool forceArgs = false);
int __KernelStartThreadValidate(SceUID threadToStartID, int argSize, u32 argBlockPtr, bool forceArgs = false);
int __KernelGetThreadExitStatus(SceUID threadID);
int sceKernelStartThread(SceUID threadToStartID, int argSize, u32 argBlockPtr);
u32 sceKernelSuspendDispatchThread();
u32 sceKernelResumeDispatchThread(u32 suspended);
int sceKernelWaitThreadEnd(SceUID threadID, u32 timeoutPtr);
u32 sceKernelReferThreadStatus(u32 uid, u32 statusPtr);
u32 sceKernelReferThreadRunStatus(u32 uid, u32 statusPtr);
int sceKernelReleaseWaitThread(SceUID threadID);
int sceKernelChangeCurrentThreadAttr(u32 clearAttr, u32 setAttr);
int sceKernelRotateThreadReadyQueue(int priority);
int KernelRotateThreadReadyQueue(int priority);
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
enum WaitType : int {
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
	WAITTYPE_MICINPUT     = 24, // fake
	WAITTYPE_NET          = 25, // fake
	WAITTYPE_USB          = 26, // fake
	WAITTYPE_PLUGIN       = 27, // this is fake, for when LoadExec thread is waiting for plugins to finish loading

	NUM_WAITTYPES
};

const char *WaitTypeToString(WaitType type);

// Suspend wait and timeout while a thread enters a callback.
typedef void (* WaitBeginCallbackFunc)(SceUID threadID, SceUID prevCallbackId);
// Resume wait and timeout as a thread exits a callback.
typedef void (* WaitEndCallbackFunc)(SceUID threadID, SceUID prevCallbackId);

void __KernelRegisterWaitTypeFuncs(WaitType type, WaitBeginCallbackFunc beginFunc, WaitEndCallbackFunc endFunc);

#if COMMON_LITTLE_ENDIAN
typedef WaitType WaitType_le;
#else
typedef swap_struct_t<WaitType, swap_32_t<WaitType> > WaitType_le;
#endif

// Real PSP struct, don't change the fields.
struct SceKernelThreadRunStatus {
	SceSize_le size;
	u32_le status;
	s32_le currentPriority;
	WaitType_le waitType;
	SceUID_le waitID;
	s32_le wakeupCount;
	SceKernelSysClock runForClocks;
	s32_le numInterruptPreempts;
	s32_le numThreadPreempts;
	s32_le numReleases;
};

// Real PSP struct, don't change the fields.
struct NativeThread {
	u32_le nativeSize;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];

	// Threading stuff
	u32_le attr;
	u32_le status;
	u32_le entrypoint;
	u32_le initialStack;
	u32_le stackSize;
	u32_le gpreg;

	s32_le initialPriority;
	s32_le currentPriority;
	WaitType_le waitType;
	SceUID_le waitID;
	s32_le wakeupCount;
	s32_le exitStatus;
	SceKernelSysClock runForClocks;
	s32_le numInterruptPreempts;
	s32_le numThreadPreempts;
	s32_le numReleases;
};

struct ThreadWaitInfo {
	u32 waitValue;
	u32 timeoutPtr;
};

enum {
	PSP_THREAD_ATTR_KERNEL = 0x00001000,
	PSP_THREAD_ATTR_VFPU = 0x00004000,
	PSP_THREAD_ATTR_SCRATCH_SRAM = 0x00008000, // Save/restore scratch as part of context???
	PSP_THREAD_ATTR_NO_FILLSTACK = 0x00100000, // No filling of 0xff.
	PSP_THREAD_ATTR_CLEAR_STACK = 0x00200000, // Clear thread stack when deleted.
	PSP_THREAD_ATTR_LOW_STACK = 0x00400000, // Allocate stack from bottom not top.
	PSP_THREAD_ATTR_USER = 0x80000000,
	PSP_THREAD_ATTR_USBWLAN = 0xa0000000,
	PSP_THREAD_ATTR_VSH = 0xc0000000,

	// TODO: Support more, not even sure what all of these mean.
	PSP_THREAD_ATTR_USER_MASK = 0xf8f060ff,
	PSP_THREAD_ATTR_USER_ERASE = 0x78800000,
	PSP_THREAD_ATTR_SUPPORTED = (PSP_THREAD_ATTR_KERNEL | PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_NO_FILLSTACK | PSP_THREAD_ATTR_CLEAR_STACK | PSP_THREAD_ATTR_LOW_STACK | PSP_THREAD_ATTR_USER)
};

enum ThreadStatus : u32 {
	THREADSTATUS_RUNNING = 1,
	THREADSTATUS_READY = 2,
	THREADSTATUS_WAIT = 4,
	THREADSTATUS_SUSPEND = 8,
	THREADSTATUS_DORMANT = 16,
	THREADSTATUS_DEAD = 32,

	THREADSTATUS_WAITSUSPEND = THREADSTATUS_WAIT | THREADSTATUS_SUSPEND
};
const char *ThreadStatusToString(ThreadStatus status);

class PSPThread : public KernelObject {
public:
	PSPThread() : debug(&context) {}
	const char *GetName() override { return nt.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Thread"; }
	void GetQuickInfo(char *ptr, int size) override;

	static u32 GetMissingErrorCode();
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Thread; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Thread; }

	bool AllocateStack(u32 &stackSize);
	bool FillStack();
	void FreeStack();

	bool PushExtendedStack(u32 size);
	bool PopExtendedStack();

	// Can't use a destructor since savestates will call that too.
	void Cleanup();

	BlockAllocator &StackAllocator();

	void setReturnValue(u32 retval);
	void setReturnValue(u64 retval);
	void resumeFromWait();
	bool isWaitingFor(WaitType type, int id) const;
	int getWaitID(WaitType type) const;
	ThreadWaitInfo getWaitInfo() const;

	// Utils
	inline bool isRunning() const { return (nt.status & THREADSTATUS_RUNNING) != 0; }
	inline bool isStopped() const { return (nt.status & THREADSTATUS_DORMANT) != 0; }
	inline bool isReady() const { return (nt.status & THREADSTATUS_READY) != 0; }
	inline bool isWaiting() const { return (nt.status & THREADSTATUS_WAIT) != 0; }
	inline bool isSuspended() const { return (nt.status & THREADSTATUS_SUSPEND) != 0; }

	void DoState(PointerWrap &p) override;

	NativeThread nt{};

	ThreadWaitInfo waitInfo{};
	SceUID moduleId = -1;
	KernelThreadDebugInterface debug;

	bool isProcessingCallbacks = false;
	u32 currentMipscallId = -1;
	SceUID currentCallbackId = -1;

	PSPThreadContext context{};

	std::vector<SceUID> callbacks;

	// TODO: Should probably just be a vector.
	std::list<u32> pendingMipsCalls;

	struct StackInfo {
		u32 start;
		u32 end;
	};
	// This is a stack of... stacks, since sceKernelExtendThreadStack() can recurse.
	// These are stacks that aren't "active" right now, but will pop off once the func returns.
	std::vector<StackInfo> pushedStacks;

	StackInfo currentStack{};

	// For thread end.
	std::vector<SceUID> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, u64> pausedWaits;
};

// Internal API, used by implementations of kernel functions

void __KernelThreadingInit();
void __KernelThreadingDoState(PointerWrap &p);
void __KernelThreadingDoStateLate(PointerWrap &p);
void __KernelThreadingShutdown();

std::string __KernelThreadingSummary();

KernelObject *__KernelThreadObject();
KernelObject *__KernelCallbackObject();

SceUID __KernelGetCurThread();
int KernelCurThreadPriority();
bool KernelChangeThreadPriority(SceUID threadID, int priority);
u32 __KernelGetCurThreadStack();
u32 __KernelGetCurThreadStackStart();
const char *__KernelGetThreadName(SceUID threadID);
bool KernelIsThreadDormant(SceUID threadID);
bool KernelIsThreadWaiting(SceUID threadID);

void __KernelSaveContext(PSPThreadContext *ctx, bool vfpuEnabled);
void __KernelLoadContext(const PSPThreadContext *ctx, bool vfpuEnabled);

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

u32 HLEMipsCallReturnAddress();
u32 __KernelCallbackReturnAddress();
u32 __KernelInterruptReturnAddress();  // TODO: remove

SceUID sceKernelCreateCallback(const char *name, u32 entrypoint, u32 signalArg);
int sceKernelDeleteCallback(SceUID cbId);
int sceKernelNotifyCallback(SceUID cbId, int notifyArg);
int sceKernelCancelCallback(SceUID cbId);
int sceKernelGetCallbackCount(SceUID cbId);
void sceKernelCheckCallback();
int sceKernelReferCallbackStatus(SceUID cbId, u32 statusAddr);

class PSPAction;

// Not an official Callback object, just calls a mips function on the current thread.
// Takes ownership of afterAction.
void __KernelDirectMipsCall(u32 entryPoint, PSPAction *afterAction, u32 args[], int numargs, bool reschedAfter);

void __KernelReturnFromMipsCall();  // Called as HLE function
bool __KernelInCallback();

// Should be called by (nearly) all ...CB functions.
bool __KernelCheckCallbacks();
bool __KernelForceCallbacks();
bool __KernelCurHasReadyCallbacks();
void __KernelSwitchContext(PSPThread *target, const char *reason);
bool __KernelExecutePendingMipsCalls(PSPThread *currentThread, bool reschedAfter);
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
typedef PSPAction *(*ActionCreator)();
PSPAction *__KernelCreateAction(int actionType);
int __KernelRegisterActionType(ActionCreator creator);
void __KernelRestoreActionType(int actionType, ActionCreator creator);

struct MipsCall {
	MipsCall() {
		doAfter = nullptr;
	}

	u32 entryPoint;
	u32 cbId;
	u32 args[6];
	int numArgs;
	PSPAction *doAfter;
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

class PSPAction
{
public:
	virtual ~PSPAction() {}
	virtual void run(MipsCall &call) = 0;
	virtual void DoState(PointerWrap &p) = 0;
	int actionTypeID;
};

void __KernelChangeThreadState(PSPThread *thread, ThreadStatus newStatus);

typedef void (*ThreadCallback)(SceUID threadID);
void __KernelListenThreadEnd(ThreadCallback callback);

struct DebugThreadInfo
{
	SceUID id;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	ThreadStatus status;
	u32 curPC;
	u32 entrypoint;
	u32 initialStack;
	int stackSize;
	int priority;
	WaitType waitType;
	SceUID waitID;
	bool isCurrent;
};

std::vector<DebugThreadInfo> GetThreadsInfo();
DebugInterface *KernelDebugThread(SceUID threadID);
void __KernelChangeThreadState(SceUID threadId, ThreadStatus newStatus);

int LoadExecForUser_362A956B();
int sceKernelRegisterExitCallback(SceUID cbId);

KernelObject *__KernelThreadEventHandlerObject();
SceUID sceKernelRegisterThreadEventHandler(const char *name, SceUID threadID, u32 mask, u32 handlerPtr, u32 commonArg);
int sceKernelReleaseThreadEventHandler(SceUID uid);
int sceKernelReferThreadEventHandlerStatus(SceUID uid, u32 infoPtr);
