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

#include <set>
#include <map>
#include <queue>
#include <algorithm>

#include "HLE.h"
#include "HLETables.h"
#include "../MIPS/MIPSInt.h"
#include "../MIPS/MIPSCodeUtils.h"
#include "../MIPS/MIPS.h"
#include "../../Core/CoreTiming.h"
#include "../../Core/MemMap.h"

#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"
#include "sceKernelModule.h"
#include "sceKernelInterrupt.h"


enum {
	ERROR_KERNEL_THREAD_ALREADY_DORMANT								 = 0x800201a2,
	ERROR_KERNEL_THREAD_ALREADY_SUSPEND								 = 0x800201a3,
	ERROR_KERNEL_THREAD_IS_NOT_DORMANT									= 0x800201a4,
	ERROR_KERNEL_THREAD_IS_NOT_SUSPEND									= 0x800201a5,
	ERROR_KERNEL_THREAD_IS_NOT_WAIT										 = 0x800201a6,
};

enum
{
	PSP_THREAD_ATTR_USER = 0x80000000,
	PSP_THREAD_ATTR_USBWLAN = 0xa0000000,
	PSP_THREAD_ATTR_VSH = 0xc0000000,
	PSP_THREAD_ATTR_KERNEL = 0x00001000,
	PSP_THREAD_ATTR_VFPU = 0x00004000,					 // TODO: Should not bother saving VFPU context except when switching between two thread that has this attribute
	PSP_THREAD_ATTR_SCRATCH_SRAM = 0x00008000,	 // Save/restore scratch as part of context???
	PSP_THREAD_ATTR_NO_FILLSTACK = 0x00100000,	 // TODO: No filling of 0xff
	PSP_THREAD_ATTR_CLEAR_STACK = 0x00200000,		// TODO: Clear thread stack when deleted
};

struct NativeCallback
{
	SceUInt size;
	char name[32];
	SceUID threadId;
	u32 entrypoint;
	u32 commonArgument;

	int notifyCount;
	int notifyArg;
};

class Callback : public KernelObject
{
public:
	const char *GetName() {return nc.name;}
	const char *GetTypeName() {return "CallBack";}

	void GetQuickInfo(char *ptr, int size)
	{
		sprintf(ptr, "thread=%i, argument= %08x",
			//hackAddress,
			nc.threadId,
			nc.commonArgument);
	}

	~Callback()
	{
	}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_CBID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Callback; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nc);
		p.Do(savedPC);
		p.Do(savedRA);
		p.Do(savedV0);
		p.Do(savedV1);
		p.Do(savedIdRegister);
		p.Do(forceDelete);
		p.DoMarker("Callback");
	}

	NativeCallback nc;

	u32 savedPC;
	u32 savedRA;
	u32 savedV0;
	u32 savedV1;
	u32 savedIdRegister;

	/*
	SceUInt 	attr;
	SceUInt 	initPattern;
	SceUInt 	currentPattern;
	int 		numWaitThreads;
	*/

	bool forceDelete;
};

// Real PSP struct, don't change the fields
struct NativeThread
{
	u32 nativeSize;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];

	// Threading stuff
	u32	attr;
	u32 status;
	u32 entrypoint;
	u32 initialStack;
	u32 stackSize;
	u32 gpreg;

	int initialPriority;
	int currentPriority;
	WaitType waitType;
	SceUID waitID;
	int wakeupCount;
	int exitStatus;
	SceKernelSysClock runForClocks;
	int numInterruptPreempts;
	int numThreadPreempts;
	int numReleases;
};

struct ThreadWaitInfo {
	u32 waitValue;
	u32 timeoutPtr;
};

// Owns outstanding MIPS calls and provides a way to get them by ID.
class MipsCallManager {
public:
	MipsCallManager() : idGen_(0) {}
	int add(MipsCall *call) {
		int id = genId();
		calls_.insert(std::pair<int, MipsCall *>(id, call));
		return id;
	}
	MipsCall *get(int id) {
		return calls_[id];
	}
	MipsCall *pop(int id) {
		MipsCall *temp = calls_[id];
		calls_.erase(id);
		return temp;
	}
	void clear() {
		std::map<int, MipsCall *>::iterator it, end;
		for (it = calls_.begin(), end = calls_.end(); it != end; ++it) {
			delete it->second;
		}
		calls_.clear();
		idGen_ = 0;
	}

	int registerActionType(ActionCreator creator) {
		types_.push_back(creator);
		return (int) types_.size() - 1;
	}

	void restoreActionType(int actionType, ActionCreator creator) {
		if (actionType >= (int) types_.size())
			types_.resize(actionType + 1, NULL);
		types_[actionType] = creator;
	}

	Action *createActionByType(int actionType) {
		if (actionType < (int) types_.size() && types_[actionType] != NULL) {
			Action *a = types_[actionType]();
			a->actionTypeID = actionType;
			return a;
		}
		return NULL;
	}

	void DoState(PointerWrap &p) {

		int n = (int) calls_.size();
		p.Do(n);

		if (p.mode == p.MODE_READ) {
			clear();
			for (int i = 0; i < n; ++i) {
				int k;
				p.Do(k);
				MipsCall *call = new MipsCall();
				call->DoState(p);
				calls_[k] = call;
			}
		} else {
			std::map<int, MipsCall *>::iterator it, end;
			for (it = calls_.begin(), end = calls_.end(); it != end; ++it) {
				p.Do(it->first);
				it->second->DoState(p);
			}
		}

		p.Do(idGen_);
		p.DoMarker("MipsCallManager");
	}

private:
	int genId() { return ++idGen_; }
	std::map<int, MipsCall *> calls_;
	std::vector<ActionCreator> types_;
	int idGen_;
};

class ActionAfterMipsCall : public Action
{
public:
	virtual void run(MipsCall &call);

	static Action *Create()
	{
		return new ActionAfterMipsCall;
	}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(threadID);
		p.Do(status);
		p.Do(waitType);
		p.Do(waitID);
		p.Do(waitInfo);
		p.Do(isProcessingCallbacks);

		p.DoMarker("ActionAfterMipsCall");

		int chainedActionType = 0;
		if (chainedAction != NULL)
			chainedActionType = chainedAction->actionTypeID;
		p.Do(chainedActionType);

		if (chainedActionType != 0)
		{
			if (p.mode == p.MODE_READ)
				chainedAction = __KernelCreateAction(chainedActionType);
			chainedAction->DoState(p);
		}
	}

	SceUID threadID;

	// Saved thread state
	int status;
	WaitType waitType;
	int waitID;
	ThreadWaitInfo waitInfo;
	bool isProcessingCallbacks;

	Action *chainedAction;
};

class ActionAfterCallback : public Action
{
public:
	ActionAfterCallback() {}
	virtual void run(MipsCall &call);

	static Action *Create()
	{
		return new ActionAfterCallback;
	}

	void setCallback(SceUID cbId_)
	{
		cbId = cbId_;
	}

	void DoState(PointerWrap &p)
	{
		p.Do(cbId);
		p.DoMarker("ActionAfterCallback");
	}

	SceUID cbId;
};

class Thread : public KernelObject
{
public:
	const char *GetName() {return nt.name;}
	const char *GetTypeName() {return "Thread";}
	void GetQuickInfo(char *ptr, int size)
	{
		sprintf(ptr, "pc= %08x sp= %08x %s %s %s %s %s %s (wt=%i wid=%i wv= %08x )",
			context.pc, context.r[MIPS_REG_SP],
			(nt.status & THREADSTATUS_RUNNING) ? "RUN" : "", 
			(nt.status & THREADSTATUS_READY) ? "READY" : "", 
			(nt.status & THREADSTATUS_WAIT) ? "WAIT" : "", 
			(nt.status & THREADSTATUS_SUSPEND) ? "SUSPEND" : "", 
			(nt.status & THREADSTATUS_DORMANT) ? "DORMANT" : "",
			(nt.status & THREADSTATUS_DEAD) ? "DEAD" : "",
			nt.waitType,
			nt.waitID,
			waitInfo.waitValue);
	}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_THID; }

	int GetIDType() const { return SCE_KERNEL_TMID_Thread; }

	bool AllocateStack(u32 &stackSize)
	{
		FreeStack();

		if (nt.attr & PSP_THREAD_ATTR_KERNEL)
		{
			// Allocate stacks for kernel threads (idle) in kernel RAM
			stackBlock = kernelMemory.Alloc(stackSize, true, (std::string("stack/") + nt.name).c_str());
		}
		else
		{
			stackBlock = userMemory.Alloc(stackSize, true, (std::string("stack/") + nt.name).c_str());
		}
		if (stackBlock == (u32)-1)
		{
			stackBlock = 0;
			ERROR_LOG(HLE, "Failed to allocate stack for thread");
			return false;
		}

		// Fill the stack.
		Memory::Memset(stackBlock, 0xFF, stackSize);
		context.r[MIPS_REG_SP] = stackBlock + stackSize;
		nt.initialStack = stackBlock;
		nt.stackSize = stackSize;
		// What's this 512?
		context.r[MIPS_REG_K0] = context.r[MIPS_REG_SP] - 256;
		context.r[MIPS_REG_SP] -= 512;
		u32 k0 = context.r[MIPS_REG_K0];
		Memory::Memset(k0, 0, 0x100);
		Memory::Write_U32(nt.initialStack, k0 + 0xc0);
		Memory::Write_U32(GetUID(),        k0 + 0xca);
		Memory::Write_U32(0xffffffff,      k0 + 0xf8);
		Memory::Write_U32(0xffffffff,      k0 + 0xfc);

		Memory::Write_U32(GetUID(), nt.initialStack);
		return true;
	}

	void FreeStack() {
		if (stackBlock != 0) {
			DEBUG_LOG(HLE, "Freeing thread stack %s", nt.name);
			if (nt.attr & PSP_THREAD_ATTR_KERNEL) {
				kernelMemory.Free(stackBlock);
			} else {
				userMemory.Free(stackBlock);
			}
			stackBlock = 0;
		}
	}

	Thread() : stackBlock(0)
	{
	}

	~Thread()
	{
		FreeStack();
	}

	ActionAfterMipsCall *getRunningCallbackAction();
	void setReturnValue(u32 retval);
	void resumeFromWait();
	bool isWaitingFor(WaitType type, int id);
	int getWaitID(WaitType type);
	ThreadWaitInfo getWaitInfo();

	// Utils
	bool isRunning() const { return (nt.status & THREADSTATUS_RUNNING) != 0; }
	bool isStopped() const { return (nt.status & THREADSTATUS_DORMANT) != 0; }
	bool isReady() const { return (nt.status & THREADSTATUS_READY) != 0; }
	bool isWaiting() const { return (nt.status & THREADSTATUS_WAIT) != 0; }
	bool isSuspended() const { return (nt.status & THREADSTATUS_SUSPEND) != 0; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nt);
		p.Do(waitInfo);
		p.Do(sleeping);
		p.Do(moduleId);
		p.Do(isProcessingCallbacks);
		p.Do(currentCallbackId);
		p.Do(context);

		u32 numCallbacks = THREAD_CALLBACK_NUM_TYPES;
		p.Do(numCallbacks);
		if (numCallbacks != THREAD_CALLBACK_NUM_TYPES)
			ERROR_LOG(HLE, "Unable to load state: different kernel object storage.");

		for (size_t i = 0; i < THREAD_CALLBACK_NUM_TYPES; ++i)
		{
			p.Do(registeredCallbacks[i]);
			p.Do(readyCallbacks[i]);
		}

		p.Do(pendingMipsCalls);
		p.Do(stackBlock);

		p.DoMarker("Thread");
	}

	NativeThread nt;

	ThreadWaitInfo waitInfo;
	bool sleeping;
	SceUID moduleId;

	bool isProcessingCallbacks;
	u32 currentCallbackId;

	ThreadContext context;

	std::set<SceUID> registeredCallbacks[THREAD_CALLBACK_NUM_TYPES];
	std::list<SceUID> readyCallbacks[THREAD_CALLBACK_NUM_TYPES];

	std::list<int> pendingMipsCalls;

	u32 stackBlock;
};

void __KernelExecuteMipsCallOnCurrentThread(int callId, bool reschedAfter);


Thread *__KernelCreateThread(SceUID &id, SceUID moduleID, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr);
void __KernelResetThread(Thread *t);
void __KernelCancelWakeup(SceUID threadID);
void __KernelCancelThreadEndTimeout(SceUID threadID);
bool __KernelCheckThreadCallbacks(Thread *thread, bool force);

//////////////////////////////////////////////////////////////////////////
//STATE BEGIN
//////////////////////////////////////////////////////////////////////////
int g_inCbCount = 0;
// Normally, the same as currentThread.  In an interrupt, remembers the callback's thread id.
SceUID currentCallbackThreadID = 0;
int readyCallbacksCount = 0;
SceUID currentThread;
u32 idleThreadHackAddr;
u32 threadReturnHackAddr;
u32 cbReturnHackAddr;
u32 intReturnHackAddr;
std::vector<SceUID> threadqueue;
std::vector<ThreadCallback> threadEndListeners;

SceUID threadIdleID[2];

int eventScheduledWakeup;
int eventThreadEndTimeout;

bool dispatchEnabled = true;

MipsCallManager mipsCalls;
int actionAfterCallback;
int actionAfterMipsCall;

// This seems nasty
SceUID curModule;

//////////////////////////////////////////////////////////////////////////
//STATE END
//////////////////////////////////////////////////////////////////////////

int __KernelRegisterActionType(ActionCreator creator)
{
	return mipsCalls.registerActionType(creator);
}

void __KernelRestoreActionType(int actionType, ActionCreator creator)
{
	mipsCalls.restoreActionType(actionType, creator);
}

Action *__KernelCreateAction(int actionType)
{
	return mipsCalls.createActionByType(actionType);
}

void MipsCall::DoState(PointerWrap &p)
{
	p.Do(entryPoint);
	p.Do(cbId);
	p.DoArray(args, ARRAY_SIZE(args));
	p.Do(numArgs);
	p.Do(savedIdRegister);
	p.Do(savedRa);
	p.Do(savedPc);
	p.Do(savedV0);
	p.Do(savedV1);
	p.Do(returnVoid);
	p.Do(tag);
	p.Do(savedId);
	p.Do(reschedAfter);

	p.DoMarker("MipsCall");

	int actionTypeID = 0;
	if (doAfter != NULL)
		actionTypeID = doAfter->actionTypeID;
	p.Do(actionTypeID);
	if (actionTypeID != 0)
	{
		if (p.mode == p.MODE_READ)
			doAfter = __KernelCreateAction(actionTypeID);
		doAfter->DoState(p);
	}
}

void MipsCall::setReturnValue(u32 value)
{
	savedV0 = value;
}

// TODO: Should move to this wrapper so we can keep the current thread as a SceUID instead
// of a dangerous raw pointer.
Thread *__GetCurrentThread() {
	u32 error;
	if (currentThread != 0)
		return kernelObjects.Get<Thread>(currentThread, error);
	else
		return NULL;
}

u32 __KernelMipsCallReturnAddress()
{
	return cbReturnHackAddr;
}

u32 __KernelInterruptReturnAddress()
{
	return intReturnHackAddr;
}

void hleScheduledWakeup(u64 userdata, int cyclesLate);
void hleThreadEndTimeout(u64 userdata, int cyclesLate);

void __KernelThreadingInit()
{
	u32 blockSize = 4 * 4 + 4 * 2 * 3;  // One 16-byte thread plus 3 8-byte "hacks"

	dispatchEnabled = true;

	g_inCbCount = 0;
	currentCallbackThreadID = 0;
	readyCallbacksCount = 0;
	idleThreadHackAddr = kernelMemory.Alloc(blockSize, false, "threadrethack");
	// Make sure it got allocated where we expect it... at the very start of kernel RAM
	//CHECK_EQ(idleThreadHackAddr & 0x3FFFFFFF, 0x08000000);

	// Yeah, this is straight out of JPCSP, I should be ashamed.
	Memory::Write_U32(MIPS_MAKE_ADDIU(MIPS_REG_A0, MIPS_REG_ZERO, 0), idleThreadHackAddr);
	Memory::Write_U32(MIPS_MAKE_LUI(MIPS_REG_RA, 0x0800), idleThreadHackAddr + 4);
	Memory::Write_U32(MIPS_MAKE_JR_RA(), idleThreadHackAddr + 8);
	//Memory::Write_U32(MIPS_MAKE_SYSCALL("ThreadManForUser", "sceKernelDelayThread"), idleThreadHackAddr + 12);
	Memory::Write_U32(MIPS_MAKE_SYSCALL("FakeSysCalls", "_sceKernelIdle"), idleThreadHackAddr + 12);
	Memory::Write_U32(MIPS_MAKE_BREAK(), idleThreadHackAddr + 16);

	threadReturnHackAddr = idleThreadHackAddr + 20;
	WriteSyscall("FakeSysCalls", NID_THREADRETURN, threadReturnHackAddr);

	cbReturnHackAddr = threadReturnHackAddr + 8;
	WriteSyscall("FakeSysCalls", NID_CALLBACKRETURN, cbReturnHackAddr);

	intReturnHackAddr = cbReturnHackAddr + 8;
	WriteSyscall("FakeSysCalls", NID_INTERRUPTRETURN, intReturnHackAddr);

	eventScheduledWakeup = CoreTiming::RegisterEvent("ScheduledWakeup", &hleScheduledWakeup);
	eventThreadEndTimeout = CoreTiming::RegisterEvent("ThreadEndTimeout", &hleThreadEndTimeout);
	actionAfterMipsCall = __KernelRegisterActionType(ActionAfterMipsCall::Create);
	actionAfterCallback = __KernelRegisterActionType(ActionAfterCallback::Create);

	// Create the two idle threads, as well. With the absolute minimal possible priority.
	// 4096 stack size - don't know what the right value is. Hm, if callbacks are ever to run on these threads...
	__KernelResetThread(__KernelCreateThread(threadIdleID[0], 0, "idle0", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL));
	__KernelResetThread(__KernelCreateThread(threadIdleID[1], 0, "idle1", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL));
	// These idle threads are later started in LoadExec, which calls __KernelStartIdleThreads below.

	__KernelListenThreadEnd(__KernelCancelWakeup);
	__KernelListenThreadEnd(__KernelCancelThreadEndTimeout);
}

void __KernelThreadingDoState(PointerWrap &p)
{
	p.Do(g_inCbCount);
	p.Do(currentCallbackThreadID);
	p.Do(readyCallbacksCount);
	p.Do(idleThreadHackAddr);
	p.Do(threadReturnHackAddr);
	p.Do(cbReturnHackAddr);
	p.Do(intReturnHackAddr);

	p.Do(currentThread);
	SceUID dv = 0;
	p.Do(threadqueue, dv);
	p.DoArray(threadIdleID, ARRAY_SIZE(threadIdleID));
	p.Do(dispatchEnabled);
	p.Do(curModule);

	p.Do(eventScheduledWakeup);
	CoreTiming::RestoreRegisterEvent(eventScheduledWakeup, "ScheduledWakeup", &hleScheduledWakeup);
	p.Do(eventThreadEndTimeout);
	CoreTiming::RestoreRegisterEvent(eventThreadEndTimeout, "ThreadEndTimeout", &hleThreadEndTimeout);
	p.Do(actionAfterMipsCall);
	__KernelRestoreActionType(actionAfterMipsCall, ActionAfterMipsCall::Create);
	p.Do(actionAfterCallback);
	__KernelRestoreActionType(actionAfterCallback, ActionAfterCallback::Create);

	p.DoMarker("sceKernelThread");
}

void __KernelThreadingDoStateLate(PointerWrap &p)
{
	// We do this late to give modules time to register actions.
	mipsCalls.DoState(p);
	p.DoMarker("sceKernelThread Late");
}

KernelObject *__KernelThreadObject()
{
	return new Thread;
}

KernelObject *__KernelCallbackObject()
{
	return new Callback;
}

void __KernelListenThreadEnd(ThreadCallback callback)
{
	threadEndListeners.push_back(callback);
}

void __KernelFireThreadEnd(SceUID threadID)
{
	for (auto iter = threadEndListeners.begin(), end = threadEndListeners.end(); iter != end; ++iter)
	{
		ThreadCallback cb = *iter;
		cb(threadID);
	}
}

void __KernelStartIdleThreads()
{
	for (int i = 0; i < 2; i++)
	{
		u32 error;
		Thread *t = kernelObjects.Get<Thread>(threadIdleID[i], error);
		t->nt.gpreg = __KernelGetModuleGP(curModule);
		t->context.r[MIPS_REG_GP] = t->nt.gpreg;
		//t->context.pc += 4;	// ADJUSTPC
		t->nt.status = THREADSTATUS_READY;
	}
}

bool __KernelSwitchOffThread(const char *reason)
{
	if (!reason)
		reason = "switch off thread";

	SceUID threadID = currentThread;

	if (threadID != threadIdleID[0] && threadID != threadIdleID[1])
	{
		u32 error;
		// Idle 0 chosen entirely arbitrarily.
		Thread *t = kernelObjects.Get<Thread>(threadIdleID[0], error);
		if (t)
		{
			__KernelSwitchContext(t, reason);
			return true;
		}
		else
			ERROR_LOG(HLE, "Unable to switch to idle thread.");
	}

	return false;
}

void __KernelIdle()
{
	CoreTiming::Idle();
	// Advance must happen between Idle and Reschedule, so that threads that were waiting for something
	// that was triggered at the end of the Idle period must get a chance to be scheduled.
	CoreTiming::Advance();

	// We must've exited a callback?
	if (__KernelInCallback())
	{
		u32 error;
		Thread *t = kernelObjects.Get<Thread>(currentCallbackThreadID, error);
		if (t)
			__KernelSwitchContext(t, "idle");
		else
		{
			WARN_LOG(HLE, "UNTESTED - Callback thread deleted during interrupt?");
			g_inCbCount = 0;
			currentCallbackThreadID = 0;
		}
	}

	// In Advance, we might trigger an interrupt such as vblank.
	// If we end up in an interrupt, we don't want to reschedule.
	// However, we have to reschedule... damn.
	__KernelReSchedule("idle");
}

void __KernelThreadingShutdown()
{
	kernelMemory.Free(threadReturnHackAddr);
	threadqueue.clear();
	threadEndListeners.clear();
	mipsCalls.clear();
	threadReturnHackAddr = 0;
	cbReturnHackAddr = 0;
	currentThread = 0;
	intReturnHackAddr = 0;
	curModule = 0;
}

const char *__KernelGetThreadName(SceUID threadID)
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
		return t->nt.name;
	return "ERROR";
}

u32 __KernelGetWaitValue(SceUID threadID, u32 &error)
{
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		return t->getWaitInfo().waitValue;
	}
	else
	{
		ERROR_LOG(HLE, "__KernelGetWaitValue ERROR: thread %i", threadID);
		return 0;
	}
}

u32 __KernelGetWaitTimeoutPtr(SceUID threadID, u32 &error)
{
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		return t->getWaitInfo().timeoutPtr;
	}
	else
	{
		ERROR_LOG(HLE, "__KernelGetWaitTimeoutPtr ERROR: thread %i", threadID);
		return 0;
	}
}

SceUID __KernelGetWaitID(SceUID threadID, WaitType type, u32 &error)
{
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		return t->getWaitID(type);
	}
	else
	{
		ERROR_LOG(HLE, "__KernelGetWaitID ERROR: thread %i", threadID);
		return 0;
	}
}

u32 sceKernelReferThreadStatus(u32 threadID, u32 statusPtr)
{
	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (!t)
	{
		ERROR_LOG(HLE,"sceKernelReferThreadStatus Error %08x", error);
		return error;
	}

	DEBUG_LOG(HLE,"sceKernelReferThreadStatus(%i, %08x)", threadID, statusPtr);
	u32 wantedSize = Memory::Read_U32(PARAM(1));
	u32 sz = sizeof(NativeThread);
	if (wantedSize) {
		t->nt.nativeSize = sz = std::min(sz, wantedSize);
	}
	Memory::Memcpy(statusPtr, &(t->nt), sz);
	return 0;
}

// Thanks JPCSP
u32 sceKernelReferThreadRunStatus(u32 threadID, u32 statusPtr)
{
	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (!t)
	{
		ERROR_LOG(HLE,"sceKernelReferThreadRunStatus Error %08x", error);
		return error;
	}

	DEBUG_LOG(HLE,"sceKernelReferThreadRunStatus(%i, %08x)", threadID, statusPtr);
	if (!Memory::IsValidAddress(statusPtr))
		return -1;

	Memory::Write_U32(t->nt.status, statusPtr);
	Memory::Write_U32(t->nt.currentPriority, statusPtr + 4);
	Memory::Write_U32(t->nt.waitType, statusPtr + 8);
	Memory::Write_U32(t->nt.waitID, statusPtr + 12);
	Memory::Write_U32(t->nt.wakeupCount, statusPtr + 16);
	Memory::Write_U32(t->nt.runForClocks.lo, statusPtr + 20);
	Memory::Write_U32(t->nt.runForClocks.hi, statusPtr + 24);
	Memory::Write_U32(t->nt.numInterruptPreempts, statusPtr + 28);
	Memory::Write_U32(t->nt.numThreadPreempts, statusPtr + 32);
	Memory::Write_U32(t->nt.numReleases, statusPtr + 36);

	return 0;
}

void sceKernelGetThreadExitStatus()
{
	SceUID threadID = PARAM(0);
	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (t->nt.status == THREADSTATUS_DORMANT) // TODO: can be dormant before starting, too, need to avoid that
		{
			DEBUG_LOG(HLE,"sceKernelGetThreadExitStatus(%i)", threadID);
			RETURN(t->nt.exitStatus);
		}
		else
		{
			RETURN(SCE_KERNEL_ERROR_NOT_DORMANT);
		}
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelGetThreadExitStatus Error %08x", error);
		RETURN(SCE_KERNEL_ERROR_UNKNOWN_THID);
	}
}

u32 sceKernelGetThreadmanIdType(u32 uid) {
	int type;
	if (kernelObjects.GetIDType(uid, &type)) {
		DEBUG_LOG(HLE, "%i=sceKernelGetThreadmanIdType(%i)", type, uid);
		return type;
	} else {
		ERROR_LOG(HLE, "sceKernelGetThreadmanIdType(%i) - FAILED", uid);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
}

u32 sceKernelGetThreadmanIdList(u32 type, u32 readBufPtr, u32 readBufSize, u32 idCountPtr)
{
	DEBUG_LOG(HLE, "sceKernelGetThreadmanIdList(%i, %08x, %i, %08x)",
		type, readBufPtr, readBufSize, idCountPtr);
	if (!Memory::IsValidAddress(readBufPtr))
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;

	if (type != SCE_KERNEL_TMID_Thread) {
		ERROR_LOG(HLE, "sceKernelGetThreadmanIdList only implemented for threads");
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}

	for (size_t i = 0; i < std::min((size_t)readBufSize, threadqueue.size()); i++)
	{
		Memory::Write_U32(threadqueue[i], readBufPtr + (u32)i * 4);
	}
	Memory::Write_U32((u32)threadqueue.size(), idCountPtr);
	return 0;
}

// Saves the current CPU context
void __KernelSaveContext(ThreadContext *ctx)
{
	memcpy(ctx->r, currentMIPS->r, sizeof(ctx->r));
	memcpy(ctx->f, currentMIPS->f, sizeof(ctx->f));

	// TODO: Make VFPU saving optional/delayed, only necessary between VFPU-attr-marked threads
	memcpy(ctx->v, currentMIPS->v, sizeof(ctx->v));
	memcpy(ctx->vfpuCtrl, currentMIPS->vfpuCtrl, sizeof(ctx->vfpuCtrl));

	ctx->pc = currentMIPS->pc;
	ctx->hi = currentMIPS->hi;
	ctx->lo = currentMIPS->lo;
	ctx->fcr0 = currentMIPS->fcr0;
	ctx->fcr31 = currentMIPS->fcr31;
	ctx->fpcond = currentMIPS->fpcond;
}

// Loads a CPU context
void __KernelLoadContext(ThreadContext *ctx)
{
	memcpy(currentMIPS->r, ctx->r, sizeof(ctx->r));
	memcpy(currentMIPS->f, ctx->f, sizeof(ctx->f));

	// TODO: Make VFPU saving optional/delayed, only necessary between VFPU-attr-marked threads
	memcpy(currentMIPS->v, ctx->v, sizeof(ctx->v));
	memcpy(currentMIPS->vfpuCtrl, ctx->vfpuCtrl, sizeof(ctx->vfpuCtrl));

	currentMIPS->pc = ctx->pc;
	currentMIPS->hi = ctx->hi;
	currentMIPS->lo = ctx->lo;
	currentMIPS->fcr0 = ctx->fcr0;
	currentMIPS->fcr31 = ctx->fcr31;
	currentMIPS->fpcond = ctx->fpcond;
}

u32 __KernelResumeThreadFromWait(SceUID threadID)
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		t->resumeFromWait();
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "__KernelResumeThreadFromWait(%d): bad thread: %08x", threadID, error);
		return error;
	}
}

u32 __KernelResumeThreadFromWait(SceUID threadID, int retval)
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		t->resumeFromWait();
		t->setReturnValue(retval);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "__KernelResumeThreadFromWait(%d): bad thread: %08x", threadID, error);
		return error;
	}
}

// Only run when you can safely accept a context switch
// Triggers a waitable event, that is, it wakes up all threads that waits for it
// If any changes were made, it will context switch after the syscall
bool __KernelTriggerWait(WaitType type, int id, bool useRetVal, int retVal, const char *reason, bool dontSwitch)
{
	bool doneAnything = false;

	u32 error;
	for (std::vector<SceUID>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++)
	{
		Thread *t = kernelObjects.Get<Thread>(*iter, error);
		if (t && t->isWaitingFor(type, id))
		{
			// This thread was waiting for the triggered object.
			t->resumeFromWait();
			if (useRetVal)
				t->setReturnValue(retVal);
			doneAnything = true;
		}
	}

//	if (doneAnything)     // lumines?
	{
		if (!dontSwitch)
		{
			// TODO: time waster
			hleReSchedule(reason);
		}
	}
	return true;
}

bool __KernelTriggerWait(WaitType type, int id, const char *reason, bool dontSwitch)
{
	return __KernelTriggerWait(type, id, false, 0, reason, dontSwitch);
}

bool __KernelTriggerWait(WaitType type, int id, int retVal, const char *reason, bool dontSwitch)
{
	return __KernelTriggerWait(type, id, true, retVal, reason, dontSwitch);
}

// makes the current thread wait for an event
void __KernelWaitCurThread(WaitType type, SceUID waitID, u32 waitValue, u32 timeoutPtr, bool processCallbacks, const char *reason)
{
	// TODO: Need to defer if in callback?
	if (g_inCbCount > 0)
		WARN_LOG(HLE, "UNTESTED - waiting within a callback, probably bad mojo.");

	Thread *thread = __GetCurrentThread();
	thread->nt.waitID = waitID;
	thread->nt.waitType = type;
	__KernelChangeThreadState(thread, THREADSTATUS_WAIT);
	thread->nt.numReleases++;
	thread->waitInfo.waitValue = waitValue;
	thread->waitInfo.timeoutPtr = timeoutPtr;

	// TODO: Remove this once all callers are cleaned up.
	RETURN(0); //pretend all went OK

	// TODO: time waster
	if (!reason)
		reason = "started wait";

	hleReSchedule(processCallbacks, reason);
	// TODO: Remove thread from Ready queue?
}

void hleScheduledWakeup(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;
	__KernelTriggerWait(WAITTYPE_DELAY, threadID, "thread delay finished", true);
}

void __KernelScheduleWakeup(SceUID threadID, s64 usFromNow)
{
	s64 cycles = usToCycles(usFromNow);
	CoreTiming::ScheduleEvent(cycles, eventScheduledWakeup, threadID);
}

void __KernelCancelWakeup(SceUID threadID)
{
	CoreTiming::UnscheduleEvent(eventScheduledWakeup, threadID);
}

void hleThreadEndTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID) userdata;
	SceUID waitID = (SceUID) (userdata >> 32);

	u32 error;
	// Just in case it was woken on its own.
	if (__KernelGetWaitID(threadID, WAITTYPE_THREADEND, error) == waitID)
	{
		u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
		if (Memory::IsValidAddress(timeoutPtr))
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
}

void __KernelScheduleThreadEndTimeout(SceUID threadID, SceUID waitID, s64 usFromNow)
{
	s64 cycles = usToCycles(usFromNow);
	CoreTiming::ScheduleEvent(cycles, eventThreadEndTimeout, (u64) threadID | ((u64) waitID << 32));
}

void __KernelCancelThreadEndTimeout(SceUID threadID)
{
	CoreTiming::UnscheduleEvent(eventThreadEndTimeout, threadID);
}

void __KernelRemoveFromThreadQueue(SceUID threadID)
{
	for (size_t i = 0; i < threadqueue.size(); i++)
	{
		if (threadqueue[i] == threadID)
		{
			DEBUG_LOG(HLE, "Deleted thread %08x (%i) from thread queue", threadID, threadID);
			threadqueue.erase(threadqueue.begin() + i);
			return;
		}
	}
}

u32 __KernelDeleteThread(SceUID threadID, int exitStatus, const char *reason, bool dontSwitch)
{
	__KernelFireThreadEnd(threadID);
	__KernelRemoveFromThreadQueue(threadID);
	__KernelTriggerWait(WAITTYPE_THREADEND, threadID, exitStatus, reason, dontSwitch);

	if (currentThread == threadID)
		currentThread = 0;
	if (currentCallbackThreadID == threadID)
	{
		currentCallbackThreadID = 0;
		g_inCbCount = 0;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		// TODO: Unless they should be run before deletion?
		for (int i = 0; i < THREAD_CALLBACK_NUM_TYPES; i++)
			readyCallbacksCount -= t->readyCallbacks[i].size();
	}

	return kernelObjects.Destroy<Thread>(threadID);
}

Thread *__KernelNextThread() {
	// round-robin scheduler
	// seems to work ?
	// not accurate!
	int bestthread = -1;
	int prio = 0xffffff;

	int next = 0;
	for (size_t i = 0; i < threadqueue.size(); i++)
	{
		if (currentThread == threadqueue[i])
		{
			next = (int)i;
			break;
		}
	}

	u32 error;
	for (size_t i = 0; i < threadqueue.size(); i++)
	{
		next = (next + 1) % threadqueue.size();

		Thread *t = kernelObjects.Get<Thread>(threadqueue[next], error);
		if (t && t->nt.currentPriority < prio)
		{
			if (t->nt.status & THREADSTATUS_READY)
			{
				bestthread = next;
				prio = t->nt.currentPriority;
			}
		}
	}

	if (bestthread != -1)
		return kernelObjects.Get<Thread>(threadqueue[bestthread], error);
	else
		return 0;
}

void __KernelReSchedule(const char *reason)
{
	// cancel rescheduling when in interrupt or callback, otherwise everything will be fucked up
	if (__IsInInterrupt() || __KernelInCallback())
	{
		reason = "In Interrupt Or Callback";
		return;
	}

	// This may get us running a callback, don't reschedule out of it.
	if (__KernelCheckCallbacks())
	{
		reason = "Began interrupt or callback.";
		return;
	}

	// Execute any pending events while we're doing scheduling.
	CoreTiming::Advance();
	if (__IsInInterrupt() || __KernelInCallback())
	{
		reason = "In Interrupt Or Callback";
		return;
	}

retry:
	Thread *nextThread = __KernelNextThread();

	if (nextThread)
	{
		__KernelSwitchContext(nextThread, reason);
		return;
	}
	else
	{
		// This shouldn't happen anymore now that we have idle threads.
		_dbg_assert_msg_(HLE,0,"No threads available to schedule! There should be at least one idle thread available.");
		CoreTiming::Idle();
		goto retry;
	}
}

void __KernelReSchedule(bool doCallbacks, const char *reason)
{
	Thread *thread = __GetCurrentThread();
	if (doCallbacks)
	{
		if (thread)
			thread->isProcessingCallbacks = doCallbacks;
	}
	__KernelReSchedule(reason);
	if (doCallbacks && thread != NULL && thread->GetUID() == currentThread) {
		if (thread->isRunning()) {
			thread->isProcessingCallbacks = false;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Thread Management
//////////////////////////////////////////////////////////////////////////
void sceKernelCheckThreadStack()
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(__KernelGetCurThread(), error);
	if (t) {
		u32 diff = abs((long)((s64)t->stackBlock - (s64)currentMIPS->r[MIPS_REG_SP]));
		WARN_LOG(HLE, "%i=sceKernelCheckThreadStack()", diff);
		RETURN(diff);
	} else {
		// WTF?
		ERROR_LOG(HLE, "sceKernelCheckThreadStack() - not on thread");
		RETURN(-1);
	}
}

void ThreadContext::reset()
{
	for (int i = 0; i<32; i++)
	{
		r[i] = 0;
		f[i] = 0.0f;
	}
	for (int i = 0; i<128; i++)
	{
		v[i] = 0.0f;
	}
	for (int i = 0; i<15; i++)
	{
		vfpuCtrl[i] = 0x00000000;
	}
	vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4; // neutral
	vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4; // neutral
	vfpuCtrl[VFPU_CTRL_DPREFIX] = 0x0;	// neutral
	vfpuCtrl[VFPU_CTRL_CC] = 0x3f;
	vfpuCtrl[VFPU_CTRL_INF4] = 0;
	vfpuCtrl[VFPU_CTRL_RCX0] = 0x3f800001;
	vfpuCtrl[VFPU_CTRL_RCX1] = 0x3f800002;
	vfpuCtrl[VFPU_CTRL_RCX2] = 0x3f800004;
	vfpuCtrl[VFPU_CTRL_RCX3] = 0x3f800008;
	vfpuCtrl[VFPU_CTRL_RCX4] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX5] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX6] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX7] = 0x3f800000;
	fpcond = 0;
	fcr0 = 0;
	fcr31 = 0;
	hi = 0;
	lo = 0;
}

void __KernelResetThread(Thread *t)
{
	t->context.reset();
	t->context.hi = 0;
	t->context.lo = 0;
	t->context.pc = t->nt.entrypoint;

	// TODO: Reset the priority?
	t->nt.waitType = WAITTYPE_NONE;
	t->nt.waitID = 0;
	memset(&t->waitInfo, 0, sizeof(t->waitInfo));

	t->nt.exitStatus = SCE_KERNEL_ERROR_NOT_DORMANT;
	t->isProcessingCallbacks = false;
	// TODO: Is this correct?
	t->pendingMipsCalls.clear();

	t->context.r[MIPS_REG_RA] = threadReturnHackAddr; //hack! TODO fix
	t->AllocateStack(t->nt.stackSize);  // can change the stacksize!
}

Thread *__KernelCreateThread(SceUID &id, SceUID moduleId, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr)
{
	Thread *t = new Thread;
	id = kernelObjects.Create(t);

	threadqueue.push_back(id);

	memset(&t->nt, 0xCD, sizeof(t->nt));

	t->nt.entrypoint = entryPoint;
	t->nt.nativeSize = sizeof(t->nt);
	t->nt.attr = attr;
	t->nt.initialPriority = t->nt.currentPriority = priority;
	t->nt.stackSize = stacksize;
	t->nt.status = THREADSTATUS_DORMANT;

	t->nt.numInterruptPreempts = 0;
	t->nt.numReleases = 0;
	t->nt.numThreadPreempts = 0;
	t->nt.runForClocks.lo = 0;
	t->nt.runForClocks.hi = 0;
	t->nt.wakeupCount = 0;
	t->nt.initialStack = 0;
	t->nt.waitID = 0;
	t->nt.exitStatus = SCE_KERNEL_ERROR_DORMANT;
	t->nt.waitType = WAITTYPE_NONE;

	if (moduleId)
		t->nt.gpreg = __KernelGetModuleGP(moduleId);
	else
		t->nt.gpreg = 0;  // sceKernelStartThread will take care of this.
	t->moduleId = moduleId;

	strncpy(t->nt.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	t->nt.name[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
	return t;
}

void __KernelSetupRootThread(SceUID moduleID, int args, const char *argp, int prio, int stacksize, int attr) 
{
	curModule = moduleID;
	//grab mips regs
	SceUID id;
	Thread *thread = __KernelCreateThread(id, moduleID, "root", currentMIPS->pc, prio, stacksize, attr);
	__KernelResetThread(thread);

	currentThread = id;
	thread->nt.status = THREADSTATUS_READY; // do not schedule

	strcpy(thread->nt.name, "root");

	__KernelLoadContext(&thread->context);
	mipsr4k.r[MIPS_REG_A0] = args;
	mipsr4k.r[MIPS_REG_SP] -= 256;
	u32 location = mipsr4k.r[MIPS_REG_SP];
	mipsr4k.r[MIPS_REG_A1] = location;
	for (int i = 0; i < args; i++)
		Memory::Write_U8(argp[i], location + i);
}


int sceKernelCreateThread(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr)
{
	if (threadName == NULL)
	{
		ERROR_LOG(HLE, "SCE_KERNEL_ERROR_ERROR = sceKernelCreateThread(): NULL name");
		return SCE_KERNEL_ERROR_ERROR;
	}

	// TODO: PSP actually fails for many of these cases, but trying for compat.
	if (stacksize < 0x200 || stacksize >= 0x20000000)
	{
		WARN_LOG(HLE, "sceKernelCreateThread(name=\"%s\"): bogus stack size %08x, using 0x4000", threadName, stacksize);
		stacksize = 0x4000;
	}
	if (prio < 0x08 || prio > 0x77)
		WARN_LOG(HLE, "sceKernelCreateThread(name=\"%s\"): bogus priority %08x", threadName, prio);
	if (!Memory::IsValidAddress(entry))
		WARN_LOG(HLE, "sceKernelCreateThread(name=\"%s\"): invalid entry %08x", threadName, entry);

	// We're assuming all threads created are user threads.
	if ((attr & PSP_THREAD_ATTR_KERNEL) == 0)
		attr |= PSP_THREAD_ATTR_USER;

	SceUID id;
	__KernelCreateThread(id, curModule, threadName, entry, prio, stacksize, attr);
	INFO_LOG(HLE, "%i = sceKernelCreateThread(name=\"%s\", entry=%08x, prio=%x, stacksize=%i)", id, threadName, entry, prio, stacksize);
	if (optionAddr != 0)
		WARN_LOG(HLE, "sceKernelCreateThread(name=\"%s\"): unsupported options parameter %08x", threadName, optionAddr);
	return id;
}


// int sceKernelStartThread(SceUID threadToStartID, SceSize argSize, void *argBlock)
int sceKernelStartThread(SceUID threadToStartID, u32 argSize, u32 argBlockPtr)
{
	if (threadToStartID != currentThread)
	{
		u32 error;
		Thread *startThread = kernelObjects.Get<Thread>(threadToStartID, error);
		if (startThread == 0)
		{
			ERROR_LOG(HLE,"%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr= %08x): thread does not exist!",
				error,threadToStartID,argSize,argBlockPtr)
			return error;
		}

		if (startThread->nt.status != THREADSTATUS_DORMANT)
		{
			//Not dormant, WTF?
			return ERROR_KERNEL_THREAD_IS_NOT_DORMANT;
		}

		INFO_LOG(HLE,"sceKernelStartThread(thread=%i, argSize=%i, argPtr= %08x )",
			threadToStartID,argSize,argBlockPtr);

		__KernelResetThread(startThread);

		startThread->nt.status = THREADSTATUS_READY;
		u32 sp = startThread->context.r[MIPS_REG_SP];
		if (argBlockPtr && argSize > 0)
		{
			startThread->context.r[MIPS_REG_A0] = argSize;
			startThread->context.r[MIPS_REG_A1] = sp;
		}
		else
		{
			startThread->context.r[MIPS_REG_A0] = 0;
			startThread->context.r[MIPS_REG_A1] = 0;
		}
		startThread->context.r[MIPS_REG_GP] = startThread->nt.gpreg;

		//now copy argument to stack
		for (int i = 0; i < (int)argSize; i++)
			Memory::Write_U8(argBlockPtr ? Memory::Read_U8(argBlockPtr + i) : 0, sp + i);

		if (!argBlockPtr && argSize > 0) {
			WARN_LOG(HLE,"sceKernelStartThread : had NULL arg");
		}

		hleReSchedule("thread started");
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"thread %i trying to start itself", threadToStartID);
		return -1;
	}
}

void sceKernelGetThreadStackFreeSize()
{
	SceUID threadID = PARAM(0);
	Thread *thread;

	INFO_LOG(HLE,"sceKernelGetThreadStackFreeSize(%i)", threadID);

	if (threadID == 0)
		thread = __GetCurrentThread();
	else
	{
		u32 error;
		thread = kernelObjects.Get<Thread>(threadID, error);
		if (thread == 0)
		{
			ERROR_LOG(HLE,"sceKernelGetThreadStackFreeSize: invalid thread id %i", threadID);
			RETURN(error);
			return;
		}
	}

	// Scan the stack for 0xFF
	int sz = 0;
	for (u32 addr = thread->stackBlock; addr < thread->stackBlock + thread->nt.stackSize; addr++)
	{
		if (Memory::Read_U8(addr) != 0xFF)
			break;
		sz++;
	}

	RETURN(sz & ~3);
}

// Internal function
void __KernelReturnFromThread()
{
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "Returned from a NULL thread.");

	INFO_LOG(HLE,"__KernelReturnFromThread : %s", thread->GetName());
	// TEMPORARY HACK: kill the stack of the root thread early:
	if (!strcmp(thread->GetName(), "root")) {
		thread->FreeStack();
	}

	thread->nt.exitStatus = currentMIPS->r[2];
	thread->nt.status = THREADSTATUS_DORMANT;
	__KernelFireThreadEnd(thread->GetUID());

	// TODO: Need to remove the thread from any ready queues.

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread returned", true);
	hleReSchedule("thread returned");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitThread()
{
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "Exited from a NULL thread.");

	ERROR_LOG(HLE,"sceKernelExitThread FAKED");
	thread->nt.status = THREADSTATUS_DORMANT;
	thread->nt.exitStatus = PARAM(0);
	__KernelFireThreadEnd(thread->GetUID());

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread exited", true);
	hleReSchedule("thread exited");

	// The stack will be deallocated when the thread is deleted.
}

void _sceKernelExitThread()
{
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "_Exited from a NULL thread.");

	ERROR_LOG(HLE,"_sceKernelExitThread FAKED");
	thread->nt.status = THREADSTATUS_DORMANT;
	thread->nt.exitStatus = PARAM(0);
	__KernelFireThreadEnd(thread->GetUID());

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread _exited", true);
	hleReSchedule("thread _exited");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitDeleteThread()
{
	int threadHandle = __KernelGetCurThread();
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadHandle, error);
	if (t)
	{
		INFO_LOG(HLE,"sceKernelExitDeleteThread()");
		t->nt.status = THREADSTATUS_DORMANT;
		t->nt.exitStatus = PARAM(0);
		error = __KernelDeleteThread(threadHandle, PARAM(0), "thread exited with delete", true);

		hleReSchedule("thread exited with delete");
		RETURN(error);
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelExitDeleteThread() ERROR - could not find myself!");
		RETURN(error);
	}
}

u32 sceKernelSuspendDispatchThread()
{
	u32 oldDispatchSuspended = !dispatchEnabled;
	dispatchEnabled = false;
	DEBUG_LOG(HLE,"%i=sceKernelSuspendDispatchThread()", oldDispatchSuspended);
	return oldDispatchSuspended;
}

u32 sceKernelResumeDispatchThread(u32 suspended)
{
	u32 oldDispatchSuspended = !dispatchEnabled;
	dispatchEnabled = !suspended;
	DEBUG_LOG(HLE,"%i=sceKernelResumeDispatchThread(%i)", oldDispatchSuspended, suspended);
	return oldDispatchSuspended;
}

void sceKernelRotateThreadReadyQueue()
{
	DEBUG_LOG(HLE,"sceKernelRotateThreadReadyQueue : rescheduling");
	hleReSchedule("rotatethreadreadyqueue");
}

int sceKernelDeleteThread(int threadHandle)
{
	if (threadHandle != currentThread)
	{
		//TODO: remove from threadqueue!
		DEBUG_LOG(HLE,"sceKernelDeleteThread(%i)",threadHandle);

		u32 error;
		Thread *t = kernelObjects.Get<Thread>(threadHandle, error);
		if (t)
		{
			// TODO: Should this reschedule ever?  Probably no?
			return __KernelDeleteThread(threadHandle, SCE_KERNEL_ERROR_THREAD_TERMINATED, "thread deleted", true);
		}

		// TODO: Error when doesn't exist?
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "Thread \"%s\" tries to delete itself! :(", __GetCurrentThread() ? __GetCurrentThread()->GetName() : "NULL");
		return -1;
	}
}

int sceKernelTerminateDeleteThread(int threadno)
{
	if (threadno != currentThread)
	{
		//TODO: remove from threadqueue!
		INFO_LOG(HLE, "sceKernelTerminateDeleteThread(%i)", threadno);

		u32 error;
		Thread *t = kernelObjects.Get<Thread>(threadno, error);
		if (t)
		{
			//TODO: should we really reschedule here?
			error = __KernelDeleteThread(threadno, SCE_KERNEL_ERROR_THREAD_TERMINATED, "thread terminated with delete", false);
			hleReSchedule("thread terminated with delete");

			return error;
		}

		// TODO: Error when doesn't exist?
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "Thread \"%s\" trying to delete itself! :(", __GetCurrentThread() ? __GetCurrentThread()->GetName() : "NULL");
		return -1;
	}
}

int sceKernelTerminateThread(u32 threadID)
{
	if (threadID != currentThread)
	{
		INFO_LOG(HLE, "sceKernelTerminateThread(%i)", threadID);

		u32 error;
		Thread *t = kernelObjects.Get<Thread>(threadID, error);
		if (t)
		{
			t->nt.exitStatus = SCE_KERNEL_ERROR_THREAD_TERMINATED;
			t->nt.status = THREADSTATUS_DORMANT;
			__KernelFireThreadEnd(threadID);
			// TODO: Should this really reschedule?
			__KernelTriggerWait(WAITTYPE_THREADEND, threadID, t->nt.exitStatus, "thread terminated", true);
		}
		// TODO: Return an error if it doesn't exist?
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "Thread \"%s\" trying to delete itself! :(", __GetCurrentThread() ? __GetCurrentThread()->GetName() : "NULL");
		return -1;
	}
}

SceUID __KernelGetCurThread()
{
	return currentThread;
}

SceUID __KernelGetCurThreadModuleId()
{
	Thread *t = __GetCurrentThread();
	if (t)
		return t->moduleId;
	return 0;
}


void sceKernelGetThreadId()
{
	u32 retVal = currentThread;
	// DEBUG_LOG(HLE,"%i = sceKernelGetThreadId()", retVal);
	RETURN(retVal);
}

void sceKernelGetThreadCurrentPriority()
{
	u32 retVal = __GetCurrentThread()->nt.currentPriority;
	DEBUG_LOG(HLE,"%i = sceKernelGetThreadCurrentPriority()", retVal);
	RETURN(retVal);
}

void sceKernelChangeCurrentThreadAttr()
{
	int clearAttr = PARAM(0);
	int setAttr = PARAM(1);
	DEBUG_LOG(HLE,"0 = sceKernelChangeCurrentThreadAttr(clear = %08x, set = %08x", clearAttr, setAttr);
	Thread *t = __GetCurrentThread();
	if (t)
		t->nt.attr = (t->nt.attr & ~clearAttr) | setAttr;
	else
		ERROR_LOG(HLE, "%s(): No current thread?", __FUNCTION__);
	RETURN(0);
}

void sceKernelChangeThreadPriority()
{
	int id = PARAM(0);
	if (id == 0) id = currentThread; //special

	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(id, error);
	if (thread)
	{
		DEBUG_LOG(HLE,"sceKernelChangeThreadPriority(%i, %i)", id, PARAM(1));
		thread->nt.currentPriority = PARAM(1);
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"%08x=sceKernelChangeThreadPriority(%i, %i) failed - no such thread", error, id, PARAM(1));
		RETURN(error);
	}
}

void sceKernelDelayThreadCB()
{
	u32 usec = PARAM(0);
	if (usec < 200) usec = 200;
	DEBUG_LOG(HLE,"sceKernelDelayThreadCB(%i usec)",usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, usec);
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, true, "thread delayed");
}

void sceKernelDelayThread()
{
	u32 usec = PARAM(0);
	if (usec < 200) usec = 200;
	DEBUG_LOG(HLE,"sceKernelDelayThread(%i usec)",usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, usec);
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, false, "thread delayed");
}

void sceKernelDelaySysClockThreadCB()
{
	u32 sysclockAddr = PARAM(0);
	if (!Memory::IsValidAddress(sysclockAddr)) {
		ERROR_LOG(HLE, "sceKernelDelaySysClockThread(%08x) - bad pointer", sysclockAddr);
		RETURN(-1);
		return;
	}
	SceKernelSysClock sysclock;
	Memory::ReadStruct(sysclockAddr, &sysclock);

	// TODO: Which unit?
	u64 usec = sysclock.lo | ((u64)sysclock.hi << 32);
	if (usec < 200) usec = 200;
	DEBUG_LOG(HLE, "sceKernelDelaySysClockThread(%08x (%llu))", sysclockAddr, usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, usec);
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, true, "thread delayed");
}

void sceKernelDelaySysClockThread()
{
	u32 sysclockAddr = PARAM(0);
	if (!Memory::IsValidAddress(sysclockAddr)) {
		ERROR_LOG(HLE, "sceKernelDelaySysClockThread(%08x) - bad pointer", sysclockAddr);
		RETURN(-1);
		return;
	}
	SceKernelSysClock sysclock;
	Memory::ReadStruct(sysclockAddr, &sysclock);

	// TODO: Which unit?
	u64 usec = sysclock.lo | ((u64)sysclock.hi << 32);
	if (usec < 200) usec = 200;
	DEBUG_LOG(HLE, "sceKernelDelaySysClockThread(%08x (%llu))", sysclockAddr, usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, usec);
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, false, "thread delayed");
}

u32 __KernelGetThreadPrio(SceUID id)
{
	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(id, error);
	if (thread)
		return thread->nt.currentPriority;
	return 0;
}

bool __KernelThreadSortPriority(SceUID thread1, SceUID thread2)
{
	return __KernelGetThreadPrio(thread1) < __KernelGetThreadPrio(thread2);
}

//////////////////////////////////////////////////////////////////////////
// WAIT/SLEEP ETC
//////////////////////////////////////////////////////////////////////////
void sceKernelWakeupThread()
{
	SceUID uid = PARAM(0);
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(uid, error);
	if (t)
	{
		if (t->nt.waitType != WAITTYPE_SLEEP) {
			t->nt.wakeupCount++;
			DEBUG_LOG(HLE,"sceKernelWakeupThread(%i) - wakeupCount incremented to %i", uid, t->nt.wakeupCount);
			RETURN(0);
		} else {
			__KernelResumeThreadFromWait(uid);
		}
	} 
	else {
		ERROR_LOG(HLE,"sceKernelWakeupThread(%i) - bad thread id", uid);
		RETURN(error);
	}
}

void sceKernelCancelWakeupThread()
{
	SceUID uid = PARAM(0);
	u32 error;
	if (uid == 0) uid = __KernelGetCurThread();
	Thread *t = kernelObjects.Get<Thread>(uid, error);
	if (t)
	{
		int wCount = t->nt.wakeupCount;
		t->nt.wakeupCount = 0;
		DEBUG_LOG(HLE,"sceKernelCancelWakeupThread(%i) - wakeupCount reset from %i", uid, wCount);
		RETURN(wCount);
	}
	else {
		ERROR_LOG(HLE,"sceKernelCancelWakeupThread(%i) - bad thread id", uid);
		RETURN(error);
	}
}

static void __KernelSleepThread(bool doCallbacks) {
	Thread *thread = __GetCurrentThread();
	if (!thread)
	{
		ERROR_LOG(HLE, "sceKernelSleepThread*(): bad current thread");
		return;
	}

	DEBUG_LOG(HLE,"sceKernelSleepThread() - wakeupCount decremented to %i", thread->nt.wakeupCount);
	if (thread->nt.wakeupCount > 0) {
		thread->nt.wakeupCount--;
		RETURN(0);
	} else {
		RETURN(0);
		__KernelWaitCurThread(WAITTYPE_SLEEP, 0, 0, 0, doCallbacks, "thread slept");
	}
}

void sceKernelSleepThread()
{
	__KernelSleepThread(false);
}

//the homebrew PollCallbacks
void sceKernelSleepThreadCB()
{
	DEBUG_LOG(HLE, "sceKernelSleepThreadCB()");
	__KernelSleepThread(true);
	__KernelCheckCallbacks();
}

int sceKernelWaitThreadEnd(SceUID threadID, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitThreadEnd(%i, %08x)", threadID, timeoutPtr);
	if (threadID == 0 || threadID == currentThread)
		return SCE_KERNEL_ERROR_ILLEGAL_THID;

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (t->nt.status != THREADSTATUS_DORMANT)
		{
			if (Memory::IsValidAddress(timeoutPtr))
				__KernelScheduleThreadEndTimeout(currentThread, threadID, Memory::Read_U32(timeoutPtr));
			__KernelWaitCurThread(WAITTYPE_THREADEND, threadID, 0, timeoutPtr, false, "thread wait end");
		}

		return t->nt.exitStatus;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelWaitThreadEnd - bad thread %i", threadID);
		return error;
	}
}

int sceKernelWaitThreadEndCB(SceUID threadID, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitThreadEndCB(%i, 0x%X)", threadID, timeoutPtr);
	if (threadID == 0 || threadID == currentThread)
		return SCE_KERNEL_ERROR_ILLEGAL_THID;

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		hleCheckCurrentCallbacks();
		if (t->nt.status != THREADSTATUS_DORMANT)
		{
			if (Memory::IsValidAddress(timeoutPtr))
				__KernelScheduleThreadEndTimeout(currentThread, threadID, Memory::Read_U32(timeoutPtr));
			__KernelWaitCurThread(WAITTYPE_THREADEND, threadID, 0, timeoutPtr, true, "thread wait end");
		}

		return t->nt.exitStatus;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelWaitThreadEndCB - bad thread %i", threadID);
		return error;
	}
}

int sceKernelReleaseWaitThread(SceUID threadID)
{
	DEBUG_LOG(HLE, "sceKernelReleaseWaitThread(%i)", threadID);
	if (__KernelInCallback())
		WARN_LOG(HLE, "UNTESTED sceKernelReleaseWaitThread() might not do the right thing in a callback");

	if (threadID == 0 || threadID == currentThread)
		return SCE_KERNEL_ERROR_ILLEGAL_THID;

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (!t->isWaiting())
			return SCE_KERNEL_ERROR_NOT_WAIT;

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_RELEASE_WAIT);
		hleReSchedule("thread released from wait");
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelReleaseWaitThread - bad thread %i", threadID);
		return error;
	}
}

void sceKernelSuspendThread()
{
	DEBUG_LOG(HLE,"UNIMPL sceKernelSuspendThread");
	RETURN(0);
}

void sceKernelResumeThread()
{
	DEBUG_LOG(HLE,"UNIMPL sceKernelResumeThread");
	RETURN(0);
}



//////////////////////////////////////////////////////////////////////////
// CALLBACKS
//////////////////////////////////////////////////////////////////////////


// Internal API
u32 __KernelCreateCallback(const char *name, u32 entrypoint, u32 commonArg)
{
	Callback *cb = new Callback;
	SceUID id = kernelObjects.Create(cb);

	cb->nc.size = sizeof(NativeCallback);
	strncpy(cb->nc.name, name, 32);

	cb->nc.entrypoint = entrypoint;
	cb->nc.threadId = __KernelGetCurThread();
	cb->nc.commonArgument = commonArg;
	cb->nc.notifyCount = 0;
	cb->nc.notifyArg = 0;

	cb->forceDelete = false;

	return id;
}

void sceKernelCreateCallback()
{
	u32 entrypoint = PARAM(1);
	u32 callbackArg = PARAM(2);

	const char *name = Memory::GetCharPointer(PARAM(0));

	u32 id = __KernelCreateCallback(name, entrypoint, callbackArg);

	DEBUG_LOG(HLE,"%i=sceKernelCreateCallback(name=%s,entry= %08x, callbackArg = %08x)", id, name, entrypoint, callbackArg);

	RETURN(id);
}

void sceKernelDeleteCallback()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelDeleteCallback(%i)", id);

	// TODO: Make sure it's gone from all threads first!

	RETURN(kernelObjects.Destroy<Callback>(id));
}

// Rarely used
void sceKernelNotifyCallback()
{
	SceUID cbId = PARAM(0);
	u32 arg = PARAM(1);
	DEBUG_LOG(HLE,"sceKernelNotifyCallback(%i, %i)", cbId, arg);

	__KernelNotifyCallback(THREAD_CALLBACK_USER_DEFINED, cbId, arg);
	RETURN(0);
}

void sceKernelCancelCallback()
{
	SceUID cbId = PARAM(0);
	ERROR_LOG(HLE,"sceKernelCancelCallback(%i) - BAD", cbId);
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (cb) {
		// This is what JPCSP does. Huh?
		cb->nc.notifyArg = 0;
		RETURN(0);
	} else {
		ERROR_LOG(HLE,"sceKernelCancelCallback(%i) - bad cbId", cbId);
		RETURN(error);
	}
	RETURN(0);
}

void sceKernelGetCallbackCount()
{
	SceUID cbId = PARAM(0);
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (cb) {
		RETURN(cb->nc.notifyCount);
	} else {
		ERROR_LOG(HLE,"sceKernelGetCallbackCount(%i) - bad cbId", cbId);
		RETURN(error);
	}
}

void sceKernelReferCallbackStatus()
{
	SceUID cbId = PARAM(0);
	u32 statusAddr = PARAM(1);
	u32 error;
	Callback *c = kernelObjects.Get<Callback>(cbId, error);
	if (c) {
		DEBUG_LOG(HLE,"sceKernelReferCallbackStatus(%i, %08x)", cbId, statusAddr);
		if (Memory::IsValidAddress(statusAddr)) {
			Memory::WriteStruct(statusAddr, &c->nc);
		} // else TODO
		RETURN(0);
	} else {
		ERROR_LOG(HLE,"sceKernelReferCallbackStatus(%i, %08x) - bad cbId", cbId, statusAddr);
		RETURN(error);
	}
}

void ActionAfterMipsCall::run(MipsCall &call) {
	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(threadID, error);
	if (thread) {
		thread->nt.status = status;
		thread->nt.waitType = waitType;
		thread->nt.waitID = waitID;
		thread->waitInfo = waitInfo;
		thread->isProcessingCallbacks = isProcessingCallbacks;
	}

	if (chainedAction) {
		chainedAction->run(call);
		delete chainedAction;
	}
}

ActionAfterMipsCall *Thread::getRunningCallbackAction()
{
	if (this->GetUID() == currentThread && g_inCbCount > 0)
	{
		MipsCall *call = mipsCalls.get(this->currentCallbackId);
		ActionAfterMipsCall *action = 0;
		if (call)
			action = dynamic_cast<ActionAfterMipsCall *>(call->doAfter);

		if (!call || !action)
		{
			ERROR_LOG(HLE, "Failed to access deferred info for thread: %s", this->nt.name);
			return NULL;
		}

		return action;
	}

	return NULL;
}

void Thread::setReturnValue(u32 retval)
{
	if (this->GetUID() == currentThread) {
		if (g_inCbCount) {
			int callId = this->currentCallbackId;
			MipsCall *call = mipsCalls.get(callId);
			if (call) {
				call->setReturnValue(retval);
			} else {
				ERROR_LOG(HLE, "Failed to inject return value %08x in thread", retval);
			}
		} else {
			currentMIPS->r[2] = retval;
		}
	} else {
		context.r[2] = retval;
	}
}

void Thread::resumeFromWait()
{
	// Do we need to "inject" it?
	ActionAfterMipsCall *action = getRunningCallbackAction();
	if (action)
	{
		action->status &= ~THREADSTATUS_WAIT;
		// TODO: What if DORMANT or DEAD?
		if (!(action->status & THREADSTATUS_WAITSUSPEND))
			action->status = THREADSTATUS_READY;

		// Non-waiting threads do not process callbacks.
		action->isProcessingCallbacks = false;
	}
	else
	{
		this->nt.status &= ~THREADSTATUS_WAIT;
		// TODO: What if DORMANT or DEAD?
		if (!(this->nt.status & THREADSTATUS_WAITSUSPEND))
			this->nt.status = THREADSTATUS_READY;

		// Non-waiting threads do not process callbacks.
		this->isProcessingCallbacks = false;
	}
}

bool Thread::isWaitingFor(WaitType type, int id)
{
	// Thread might be in a callback right now.
	ActionAfterMipsCall *action = getRunningCallbackAction();
	if (action)
	{
		if (action->status & THREADSTATUS_WAIT)
			return action->waitType == type && action->waitID == id;
		return false;
	}

	if (this->nt.status & THREADSTATUS_WAIT)
		return this->nt.waitType == type && this->nt.waitID == id;
	return false;
}

int Thread::getWaitID(WaitType type)
{
	// Thread might be in a callback right now.
	ActionAfterMipsCall *action = getRunningCallbackAction();
	if (action)
	{
		if (action->waitType == type)
			return action->waitID;
		return 0;
	}

	if (this->nt.waitType == type)
		return this->nt.waitID;
	return 0;
}

ThreadWaitInfo Thread::getWaitInfo()
{
	// Thread might be in a callback right now.
	ActionAfterMipsCall *action = getRunningCallbackAction();
	if (action)
		return action->waitInfo;

	return this->waitInfo;
}

void __KernelSwitchContext(Thread *target, const char *reason) 
{
	u32 oldPC = 0;
	u32 oldUID = 0;
	const char *oldName = "(none)";

	Thread *cur = __GetCurrentThread();
	if (cur)  // It might just have been deleted.
	{
		__KernelSaveContext(&cur->context);
		oldPC = currentMIPS->pc;
		oldUID = cur->GetUID();

		// Profile on Windows shows this takes time, skip it.
		if (DEBUG_LEVEL <= MAX_LOGLEVEL)
			oldName = cur->GetName();
	}
	currentThread = target->GetUID();
	__KernelLoadContext(&target->context);
	DEBUG_LOG(HLE,"Context switched: %s -> %s (%s) (%i - pc: %08x -> %i - pc: %08x)",
		oldName, target->GetName(),
		reason,
		oldUID, oldPC, target->GetUID(), currentMIPS->pc);

	// No longer waiting.
	target->nt.waitType = WAITTYPE_NONE;
	target->nt.waitID = 0;

	__KernelExecutePendingMipsCalls(true);
}

void __KernelChangeThreadState(Thread *thread, ThreadStatus newStatus) {
	if (!thread || thread->nt.status == newStatus)
		return;

	if (!dispatchEnabled && thread == __GetCurrentThread() && newStatus != THREADSTATUS_RUNNING) {
		ERROR_LOG(HLE, "Dispatching suspended, not changing thread state");
		return;
	}

	// TODO: JPSCP has many conditions here, like removing wait timeout actions etc.
	// if (thread->nt.status == THREADSTATUS_WAIT && newStatus != THREADSTATUS_WAITSUSPEND) {

	thread->nt.status = newStatus;

	if (newStatus == THREADSTATUS_WAIT) {
		if (thread->nt.waitType == WAITTYPE_NONE) {
			ERROR_LOG(HLE, "Waittype none not allowed here");
		}

		// Schedule deletion of stopped threads here.  if (thread->isStopped())
	}
}


bool __CanExecuteCallbackNow(Thread *thread) {
	return g_inCbCount == 0;
}

void __KernelCallAddress(Thread *thread, u32 entryPoint, Action *afterAction, bool returnVoid, std::vector<int> args, bool reschedAfter)
{
	if (thread) {
		ActionAfterMipsCall *after = (ActionAfterMipsCall *) __KernelCreateAction(actionAfterMipsCall);
		after->chainedAction = afterAction;
		after->threadID = thread->GetUID();
		after->status = thread->nt.status;
		after->waitType = thread->nt.waitType;
		after->waitID = thread->nt.waitID;
		after->waitInfo = thread->waitInfo;
		after->isProcessingCallbacks = thread->isProcessingCallbacks;

		afterAction = after;

		// Release thread from waiting
		thread->nt.waitType = WAITTYPE_NONE;

		__KernelChangeThreadState(thread, THREADSTATUS_READY);
	}

	MipsCall *call = new MipsCall();
	call->entryPoint = entryPoint;
	for (size_t i = 0; i < args.size(); i++) {
		call->args[i] = args[i];
	}
	call->numArgs = (int) args.size();
	call->doAfter = afterAction;
	call->tag = "callAddress";

	int callId = mipsCalls.add(call);

	bool called = false;
	if (!thread || thread == __GetCurrentThread()) {
		if (__CanExecuteCallbackNow(thread)) {
			thread = __GetCurrentThread();
			__KernelChangeThreadState(thread, THREADSTATUS_RUNNING);
			__KernelExecuteMipsCallOnCurrentThread(callId, reschedAfter);
			called = true;
		}
	}

	if (!called) {
		if (thread) {
			DEBUG_LOG(HLE, "Making mipscall pending on thread");
			thread->pendingMipsCalls.push_back(callId);
		} else {
			WARN_LOG(HLE, "Ignoring mispcall on NULL/deleted thread");
		}
	}
}

void __KernelDirectMipsCall(u32 entryPoint, Action *afterAction, bool returnVoid, u32 args[], int numargs, bool reschedAfter)
{
	// TODO: get rid of the vector
	std::vector<int> argsv;
	for (int i = 0; i < numargs; i++)
		argsv.push_back(args[i]);

	__KernelCallAddress(__GetCurrentThread(), entryPoint, afterAction, returnVoid, argsv, reschedAfter);
}

void __KernelExecuteMipsCallOnCurrentThread(int callId, bool reschedAfter)
{
	Thread *cur = __GetCurrentThread();
	if (cur == NULL)
	{
		ERROR_LOG(HLE, "__KernelExecuteMipsCallOnCurrentThread(): Bad current thread");
		return;
	}

	if (g_inCbCount > 0) {
		WARN_LOG(HLE, "__KernelExecuteMipsCallOnCurrentThread(): Already in a callback!");
	}
	DEBUG_LOG(HLE, "Executing mipscall %i", callId);
	MipsCall *call = mipsCalls.get(callId);

	// Save the few regs that need saving
	call->savedPc = currentMIPS->pc;
	call->savedRa = currentMIPS->r[MIPS_REG_RA];
	call->savedV0 = currentMIPS->r[MIPS_REG_V0];
	call->savedV1 = currentMIPS->r[MIPS_REG_V1];
	call->savedIdRegister = currentMIPS->r[MIPS_REG_CALL_ID];
	call->savedId = cur->currentCallbackId;
	call->returnVoid = false;
	call->reschedAfter = reschedAfter;

	// Set up the new state
	currentMIPS->pc = call->entryPoint;
	currentMIPS->r[MIPS_REG_RA] = __KernelMipsCallReturnAddress();
	// We put this two places in case the game overwrites it.
	// We may want it later to "inject" return values.
	currentMIPS->r[MIPS_REG_CALL_ID] = callId;
	cur->currentCallbackId = callId;
	for (int i = 0; i < call->numArgs; i++) {
		currentMIPS->r[MIPS_REG_A0 + i] = call->args[i];
	}

	g_inCbCount++;
	currentCallbackThreadID = currentThread;
}

void __KernelReturnFromMipsCall()
{
	Thread *cur = __GetCurrentThread();
	if (cur == NULL)
	{
		ERROR_LOG(HLE, "__KernelReturnFromMipsCall(): Bad current thread");
		return;
	}

	int callId = cur->currentCallbackId;
	if (currentMIPS->r[MIPS_REG_CALL_ID] != callId)
		WARN_LOG(HLE, "__KernelReturnFromMipsCall(): s0 is %08x != %08x", currentMIPS->r[MIPS_REG_CALL_ID], callId);

	MipsCall *call = mipsCalls.pop(callId);

	// Value returned by the callback function
	u32 retVal = currentMIPS->r[MIPS_REG_V0];
	DEBUG_LOG(HLE,"__KernelReturnFromMipsCall(), returned %08x", retVal);

	// Should also save/restore wait state here.
	if (call->doAfter)
	{
		call->doAfter->run(*call);
		delete call->doAfter;
	}

	currentMIPS->pc = call->savedPc;
	currentMIPS->r[MIPS_REG_RA] = call->savedRa;
	currentMIPS->r[MIPS_REG_V0] = call->savedV0;
	currentMIPS->r[MIPS_REG_V1] = call->savedV1;
	currentMIPS->r[MIPS_REG_CALL_ID] = call->savedIdRegister;
	cur->currentCallbackId = call->savedId;

	g_inCbCount--;
	currentCallbackThreadID = 0;

	// yeah! back in the real world, let's keep going. Should we process more callbacks?
	if (!__KernelExecutePendingMipsCalls(call->reschedAfter))
	{
		// Sometimes, we want to stay on the thread.
		int threadReady = cur->nt.status & (THREADSTATUS_READY | THREADSTATUS_RUNNING);
		if (call->reschedAfter || threadReady == 0)
			__KernelReSchedule("return from callback");
	}

	delete call;
}

bool __KernelExecutePendingMipsCalls(bool reschedAfter)
{
	Thread *thread = __GetCurrentThread();

	if (thread->pendingMipsCalls.empty()) {
		// Nothing to do
		return false;
	}

	if (__CanExecuteCallbackNow(thread))
	{
		// Pop off the first pending mips call
		int callId = thread->pendingMipsCalls.front();
		thread->pendingMipsCalls.pop_front();
		__KernelExecuteMipsCallOnCurrentThread(callId, reschedAfter);
		return true;
	}
	return false;
}

// Executes the callback, when it next is context switched to.
void __KernelRunCallbackOnThread(SceUID cbId, Thread *thread, bool reschedAfter)
{
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (!cb) {
		ERROR_LOG(HLE, "__KernelRunCallbackOnThread: Bad cbId %i", cbId);
		return;
	}

	DEBUG_LOG(HLE, "__KernelRunCallbackOnThread: Turning callback %i into pending mipscall", cbId);

	// Alright, we're on the right thread
	// Should save/restore wait state?

	std::vector<int> args;
	args.push_back(cb->nc.notifyCount);
	args.push_back(cb->nc.notifyArg);
	args.push_back(cb->nc.commonArgument);

	// Clear the notify count / arg
	cb->nc.notifyCount = 0;
	cb->nc.notifyArg = 0;

	ActionAfterCallback *action = (ActionAfterCallback *) __KernelCreateAction(actionAfterCallback);
	if (action != NULL)
		action->setCallback(cbId);
	else
		ERROR_LOG(HLE, "Something went wrong creating a restore action for a callback.");

	__KernelCallAddress(thread, cb->nc.entrypoint, action, false, args, reschedAfter);
}

void ActionAfterCallback::run(MipsCall &call) {
	if (cbId != -1) {
		u32 error;
		Callback *cb = kernelObjects.Get<Callback>(cbId, error);
		if (cb)
		{
			Thread *t = kernelObjects.Get<Thread>(cb->nc.threadId, error);
			if (t)
			{
				// Check for other callbacks to run (including ones this callback scheduled.)
				__KernelCheckThreadCallbacks(t, true);
			}

			DEBUG_LOG(HLE, "Left callback %i - %s", cbId, cb->nc.name);
			// Callbacks that don't return 0 are deleted. But should this be done here?
			if (currentMIPS->r[MIPS_REG_V0] != 0 || cb->forceDelete)
			{
				DEBUG_LOG(HLE, "ActionAfterCallback::run(): Callback returned non-zero, gets deleted!");
				kernelObjects.Destroy<Callback>(cbId);
			}
		}
	}
}

// Check callbacks on the current thread only.
// Returns true if any callbacks were processed on the current thread.
bool __KernelCheckThreadCallbacks(Thread *thread, bool force)
{
	if (!thread->isProcessingCallbacks && !force)
		return false;

	for (int i = 0; i < THREAD_CALLBACK_NUM_TYPES; i++) {
		if (thread->readyCallbacks[i].size()) {
			SceUID readyCallback = thread->readyCallbacks[i].front();
			thread->readyCallbacks[i].pop_front();
			readyCallbacksCount--;

			// If the callback was deleted, we're good.  Just skip it.
			if (kernelObjects.IsValid(readyCallback))
			{
				__KernelRunCallbackOnThread(readyCallback, thread, !force);   // makes pending
				return true;
			}
			else
			{
				WARN_LOG(HLE, "Ignoring deleted callback %08x", readyCallback);
			}
		}
	}
	return false;
}

// Checks for callbacks on all threads
bool __KernelCheckCallbacks() {
	// Let's not check every thread all the time, callbacks are fairly uncommon.
	if (readyCallbacksCount == 0) {
		return false;
	}
	if (readyCallbacksCount < 0) {
		ERROR_LOG(HLE, "readyCallbacksCount became negative: %i", readyCallbacksCount);
	}

	// SceUID currentThread = __KernelGetCurThread();
	// __GetCurrentThread()->isProcessingCallbacks = true;
	// do {
		bool processed = false;

		u32 error;
		for (std::vector<SceUID>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++) {
			Thread *thread = kernelObjects.Get<Thread>(*iter, error);
			if (thread && __KernelCheckThreadCallbacks(thread, false)) {
				processed = true;
			}
		}
	// } while (processed && currentThread == __KernelGetCurThread());

	if (processed)
		return __KernelExecutePendingMipsCalls(true);
	return processed;
}

bool __KernelForceCallbacks()
{
	Thread *curThread = __GetCurrentThread();	

	bool callbacksProcessed = __KernelCheckThreadCallbacks(curThread, true);
	if (callbacksProcessed)
		__KernelExecutePendingMipsCalls(false);

	return callbacksProcessed;
}

void sceKernelCheckCallback()
{
	// Start with yes.
	RETURN(1);

	bool callbacksProcessed = __KernelForceCallbacks();

	if (callbacksProcessed) {
		DEBUG_LOG(HLE,"sceKernelCheckCallback() - processed a callback.");
	} else {
		RETURN(0);
	}
}

bool __KernelInCallback()
{
	return (g_inCbCount != 0);
}


u32 __KernelRegisterCallback(RegisteredCallbackType type, SceUID cbId)
{
	Thread *t = __GetCurrentThread();
	if (cbId > 0 && t->registeredCallbacks[type].find(cbId) == t->registeredCallbacks[type].end()) {
		t->registeredCallbacks[type].insert(cbId);
		return 0;
	} else {
		return SCE_KERNEL_ERROR_INVAL;
	}
}

u32 __KernelUnregisterCallback(RegisteredCallbackType type, SceUID cbId)
{
	Thread *t = __GetCurrentThread();
	if (t->registeredCallbacks[type].find(cbId) != t->registeredCallbacks[type].end()) {
		t->registeredCallbacks[type].erase(cbId);
		return 0;
	} else {
		return 0x80010016;
	}
}

void __KernelNotifyCallback(RegisteredCallbackType type, SceUID cbId, int notifyArg)
{
	u32 error;

	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (!cb) {
		// Yeah, we're screwed, this shouldn't happen.
		ERROR_LOG(HLE, "__KernelNotifyCallback - invalid callback %08x", cbId);
		return;
	}
	cb->nc.notifyCount++;
	cb->nc.notifyArg = notifyArg;

	Thread *t = kernelObjects.Get<Thread>(cb->nc.threadId, error);
	std::list<SceUID> &readyCallbacks = t->readyCallbacks[type];
	auto iter = std::find(readyCallbacks.begin(), readyCallbacks.end(), cbId);
	if (iter == readyCallbacks.end())
	{
		t->readyCallbacks[type].push_back(cbId);
		readyCallbacksCount++;
	}
}

// TODO: If cbId == -1, notify the callback ID on all threads that have it.
u32 __KernelNotifyCallbackType(RegisteredCallbackType type, SceUID cbId, int notifyArg)
{
	u32 error;
	for (std::vector<SceUID>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++) {
		Thread *t = kernelObjects.Get<Thread>(*iter, error);
		if (!t)
			continue;

		for (std::set<SceUID>::iterator citer = t->registeredCallbacks[type].begin(); citer != t->registeredCallbacks[type].end(); citer++) {
			if (cbId == -1 || cbId == *citer) {
				__KernelNotifyCallback(type, *citer, notifyArg);
			}
		}
	}

	// checkCallbacks on other threads?
	return 0;
}

