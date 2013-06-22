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

#include "Common/LogManager.h"
#include "HLE.h"
#include "HLETables.h"
#include "../MIPS/MIPSInt.h"
#include "../MIPS/MIPSCodeUtils.h"
#include "../MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "ChunkFile.h"

#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"
#include "sceKernelModule.h"
#include "sceKernelInterrupt.h"


enum {
	PSP_THREAD_ATTR_KERNEL =           0x00001000,
	PSP_THREAD_ATTR_VFPU =             0x00004000,
	PSP_THREAD_ATTR_SCRATCH_SRAM =     0x00008000,   // Save/restore scratch as part of context???
	PSP_THREAD_ATTR_NO_FILLSTACK =     0x00100000,   // TODO: No filling of 0xff (only with PSP_THREAD_ATTR_LOW_STACK?)
	PSP_THREAD_ATTR_CLEAR_STACK =      0x00200000,   // TODO: Clear thread stack when deleted
	PSP_THREAD_ATTR_LOW_STACK =        0x00400000,   // TODO: Allocate stack from bottom not top.
	PSP_THREAD_ATTR_USER =             0x80000000,
	PSP_THREAD_ATTR_USBWLAN =          0xa0000000,
	PSP_THREAD_ATTR_VSH =              0xc0000000,
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
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Callback; }
	int GetIDType() const { return SCE_KERNEL_TMID_Callback; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nc);
		p.Do(savedPC);
		p.Do(savedRA);
		p.Do(savedV0);
		p.Do(savedV1);
		p.Do(savedIdRegister);
		p.DoMarker("Callback");
	}

	NativeCallback nc;

	u32 savedPC;
	u32 savedRA;
	u32 savedV0;
	u32 savedV1;
	u32 savedIdRegister;
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
	u32 add(MipsCall *call) {
		u32 id = genId();
		calls_.insert(std::pair<int, MipsCall *>(id, call));
		return id;
	}
	MipsCall *get(u32 id) {
		auto iter = calls_.find(id);
		if (iter == calls_.end())
			return NULL;
		return iter->second;
	}
	MipsCall *pop(u32 id) {
		MipsCall *temp = calls_[id];
		calls_.erase(id);
		return temp;
	}
	void clear() {
		for (auto it = calls_.begin(), end = calls_.end(); it != end; ++it) {
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
		p.Do(calls_);
		p.Do(idGen_);
		p.DoMarker("MipsCallManager");
	}

private:
	u32 genId() { return ++idGen_; }
	std::map<u32, MipsCall *> calls_;
	std::vector<ActionCreator> types_;
	u32 idGen_;
};

class ActionAfterMipsCall : public Action
{
	ActionAfterMipsCall()
	{
		chainedAction = NULL;
	}

public:
	virtual void run(MipsCall &call);

	static Action *Create()
	{
		return new ActionAfterMipsCall();
	}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(threadID);
		p.Do(status);
		p.Do(waitType);
		p.Do(waitID);
		p.Do(waitInfo);
		p.Do(isProcessingCallbacks);
		p.Do(currentCallbackId);

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
	SceUID currentCallbackId;

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
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Thread; }
	int GetIDType() const { return SCE_KERNEL_TMID_Thread; }

	bool AllocateStack(u32 &stackSize)
	{
		FreeStack();

		if (nt.attr & PSP_THREAD_ATTR_KERNEL)
		{
			// Allocate stacks for kernel threads (idle) in kernel RAM
			currentStack.start = kernelMemory.Alloc(stackSize, true, (std::string("stack/") + nt.name).c_str());
		}
		else
		{
			currentStack.start = userMemory.Alloc(stackSize, true, (std::string("stack/") + nt.name).c_str());
		}
		if (currentStack.start == (u32)-1)
		{
			currentStack.start = 0;
			ERROR_LOG(HLE, "Failed to allocate stack for thread");
			return false;
		}

		nt.initialStack = currentStack.start;
		nt.stackSize = stackSize;
		return true;
	}

	bool FillStack() {
		// Fill the stack.
		Memory::Memset(currentStack.start, 0xFF, nt.stackSize);
		context.r[MIPS_REG_SP] = currentStack.start + nt.stackSize;
		currentStack.end = context.r[MIPS_REG_SP];
		// The k0 section is 256 bytes at the top of the stack.
		context.r[MIPS_REG_SP] -= 256;
		context.r[MIPS_REG_K0] = context.r[MIPS_REG_SP];
		u32 k0 = context.r[MIPS_REG_K0];
		Memory::Memset(k0, 0, 0x100);
		Memory::Write_U32(GetUID(),        k0 + 0xc0);
		Memory::Write_U32(nt.initialStack, k0 + 0xc8);
		Memory::Write_U32(0xffffffff,      k0 + 0xf8);
		Memory::Write_U32(0xffffffff,      k0 + 0xfc);
		// After k0 comes the arguments, which is done by sceKernelStartThread().

		Memory::Write_U32(GetUID(), nt.initialStack);
		return true;
	}

	void FreeStack() {
		if (currentStack.start != 0) {
			DEBUG_LOG(HLE, "Freeing thread stack %s", nt.name);
			if (nt.attr & PSP_THREAD_ATTR_KERNEL) {
				kernelMemory.Free(currentStack.start);
			} else {
				userMemory.Free(currentStack.start);
			}
			currentStack.start = 0;
		}
	}

	bool PushExtendedStack(u32 size)
	{
		u32 stack = userMemory.Alloc(size, true, (std::string("extended/") + nt.name).c_str());
		if (stack == (u32)-1)
			return false;

		pushedStacks.push_back(currentStack);
		currentStack.start = stack;
		currentStack.end = stack + size;
		nt.initialStack = currentStack.start;
		nt.stackSize = currentStack.end - currentStack.start;

		// We still drop the threadID at the bottom and fill it, but there's no k0.
		Memory::Memset(currentStack.start, 0xFF, nt.stackSize);
		Memory::Write_U32(GetUID(), nt.initialStack);
		return true;
	}

	bool PopExtendedStack()
	{
		if (pushedStacks.size() == 0)
			return false;

		userMemory.Free(currentStack.start);
		currentStack = pushedStacks.back();
		pushedStacks.pop_back();
		nt.initialStack = currentStack.start;
		nt.stackSize = currentStack.end - currentStack.start;
		return true;
	}

	Thread()
	{
		currentStack.start = 0;
	}

	~Thread()
	{
		if (pushedStacks.size() != 0)
		{
			WARN_LOG_REPORT(HLE, "Thread ended within an extended stack");
			for (size_t i = 0; i < pushedStacks.size(); ++i)
				userMemory.Free(pushedStacks[i].start);
		}
		FreeStack();
	}

	ActionAfterMipsCall *getRunningCallbackAction();
	void setReturnValue(u32 retval);
	void setReturnValue(u64 retval);
	void resumeFromWait();
	bool isWaitingFor(WaitType type, int id);
	int getWaitID(WaitType type);
	ThreadWaitInfo getWaitInfo();

	// Utils
	inline bool isRunning() const { return (nt.status & THREADSTATUS_RUNNING) != 0; }
	inline bool isStopped() const { return (nt.status & THREADSTATUS_DORMANT) != 0; }
	inline bool isReady() const { return (nt.status & THREADSTATUS_READY) != 0; }
	inline bool isWaiting() const { return (nt.status & THREADSTATUS_WAIT) != 0; }
	inline bool isSuspended() const { return (nt.status & THREADSTATUS_SUSPEND) != 0; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nt);
		p.Do(waitInfo);
		p.Do(moduleId);
		p.Do(isProcessingCallbacks);
		p.Do(currentMipscallId);
		p.Do(currentCallbackId);
		p.Do(context);

		u32 numCallbacks = THREAD_CALLBACK_NUM_TYPES;
		p.Do(numCallbacks);
		if (numCallbacks != THREAD_CALLBACK_NUM_TYPES)
		{
			p.SetError(p.ERROR_FAILURE);
			ERROR_LOG(HLE, "Unable to load state: different thread callback storage.");
			return;
		}

		for (size_t i = 0; i < THREAD_CALLBACK_NUM_TYPES; ++i)
		{
			p.Do(registeredCallbacks[i]);
			p.Do(readyCallbacks[i]);
		}

		p.Do(pendingMipsCalls);
		p.Do(pushedStacks);
		p.Do(currentStack);

		p.DoMarker("Thread");
	}

	NativeThread nt;

	ThreadWaitInfo waitInfo;
	SceUID moduleId;

	bool isProcessingCallbacks;
	u32 currentMipscallId;
	SceUID currentCallbackId;

	ThreadContext context;

	std::set<SceUID> registeredCallbacks[THREAD_CALLBACK_NUM_TYPES];
	std::list<SceUID> readyCallbacks[THREAD_CALLBACK_NUM_TYPES];

	std::list<u32> pendingMipsCalls;

	struct StackInfo {
		u32 start;
		u32 end;
	};
	// This is a stack of... stacks, since sceKernelExtendThreadStack() can recurse.
	// These are stacks that aren't "active" right now, but will pop off once the func returns.
	std::vector<StackInfo> pushedStacks;

	StackInfo currentStack;
};

struct ThreadQueueList
{
	// Number of queues (number of priority levels starting at 0.)
	static const int NUM_QUEUES = 128;
	// Initial number of threads a single queue can handle.
	static const int INITIAL_CAPACITY = 32;

	struct Queue
	{
		// Next ever-been-used queue (worse priority.)
		Queue *next;
		// First valid item in data.
		int first;
		// One after last valid item in data.
		int end;
		// A too-large array with room on the front and end.
		SceUID *data;
		// Size of data array.
		int capacity;
	};

	ThreadQueueList()
	{
		memset(queues, 0, sizeof(queues));
		first = invalid();
	}

	~ThreadQueueList()
	{
		for (int i = 0; i < NUM_QUEUES; ++i)
		{
			if (queues[i].data != NULL)
				free(queues[i].data);
		}
	}

	inline SceUID pop_first()
	{
		Queue *cur = first;
		while (cur != invalid())
		{
			if (cur->end - cur->first > 0)
				return cur->data[cur->first++];
			cur = cur->next;
		}

		_dbg_assert_msg_(HLE, false, "ThreadQueueList should not be empty.");
		return 0;
	}

	inline SceUID pop_first_better(u32 priority)
	{
		Queue *cur = first;
		Queue *stop = &queues[priority];
		while (cur < stop)
		{
			if (cur->end - cur->first > 0)
				return cur->data[cur->first++];
			cur = cur->next;
		}

		return 0;
	}

	inline void push_front(u32 priority, const SceUID threadID)
	{
		Queue *cur = &queues[priority];
		cur->data[--cur->first] = threadID;
		if (cur->first == 0)
			rebalance(priority);
	}

	inline void push_back(u32 priority, const SceUID threadID)
	{
		Queue *cur = &queues[priority];
		cur->data[cur->end++] = threadID;
		if (cur->end == cur->capacity)
			rebalance(priority);
	}

	inline void remove(u32 priority, const SceUID threadID)
	{
		Queue *cur = &queues[priority];
		_dbg_assert_msg_(HLE, cur->next != NULL, "ThreadQueueList::Queue should already be linked up.");

		for (int i = cur->first; i < cur->end; ++i)
		{
			if (cur->data[i] == threadID)
			{
				int remaining = --cur->end - i;
				if (remaining > 0)
					memmove(&cur->data[i], &cur->data[i + 1], remaining * sizeof(SceUID));
				return;
			}
		}

		// Wasn't there.
	}

	inline void rotate(u32 priority)
	{
		Queue *cur = &queues[priority];
		_dbg_assert_msg_(HLE, cur->next != NULL, "ThreadQueueList::Queue should already be linked up.");

		if (cur->end - cur->first > 1)
		{
			cur->data[cur->end++] = cur->data[cur->first++];
			if (cur->end == cur->capacity)
				rebalance(priority);
		}
	}

	inline void clear()
	{
		for (int i = 0; i < NUM_QUEUES; ++i)
		{
			if (queues[i].data != NULL)
				free(queues[i].data);
		}
		memset(queues, 0, sizeof(queues));
		first = invalid();
	}

	inline bool empty(u32 priority) const
	{
		const Queue *cur = &queues[priority];
		return cur->first == cur->end;
	}

	inline void prepare(u32 priority)
	{
		Queue *cur = &queues[priority];
		if (cur->next == NULL)
			link(priority, INITIAL_CAPACITY);
	}

	void DoState(PointerWrap &p)
	{
		int numQueues = NUM_QUEUES;
		p.Do(numQueues);
		if (numQueues != NUM_QUEUES)
		{
			p.SetError(p.ERROR_FAILURE);
			ERROR_LOG(HLE, "Savestate loading error: invalid data");
			return;
		}

		if (p.mode == p.MODE_READ)
			clear();

		for (int i = 0; i < NUM_QUEUES; ++i)
		{
			Queue *cur = &queues[i];
			int size = cur->end - cur->first;
			p.Do(size);
			int capacity = cur->capacity;
			p.Do(capacity);

			if (capacity == 0)
				continue;

			if (p.mode == p.MODE_READ)
			{
				link(i, capacity);
				cur->first = (cur->capacity - size) / 2;
				cur->end = cur->first + size;
			}

			if (size != 0)
				p.DoArray(&cur->data[cur->first], size);
		}

		p.DoMarker("ThreadQueueList");
	}

private:
	Queue *invalid() const
	{
		return (Queue *) -1;
	}

	void link(u32 priority, int size)
	{
		_dbg_assert_msg_(HLE, queues[priority].data == NULL, "ThreadQueueList::Queue should only be initialized once.");

		if (size <= INITIAL_CAPACITY)
			size = INITIAL_CAPACITY;
		else
		{
			int goal = size;
			size = INITIAL_CAPACITY;
			while (size < goal)
				size *= 2;
		}
		Queue *cur = &queues[priority];
		cur->data = (SceUID *) malloc(sizeof(SceUID) * size);
		cur->capacity = size;
		cur->first = size / 2;
		cur->end = size / 2;

		for (int i = (int) priority - 1; i >= 0; --i)
		{
			if (queues[i].next != NULL)
			{
				cur->next = queues[i].next;
				queues[i].next = cur;
				return;
			}
		}

		cur->next = first;
		first = cur;
	}

	void rebalance(u32 priority)
	{
		Queue *cur = &queues[priority];
		int size = cur->end - cur->first;
		if (size >= cur->capacity - 2)
		{
			SceUID *new_data = (SceUID *)realloc(cur->data, cur->capacity * sizeof(SceUID));
			if (new_data != NULL)
			{
				cur->capacity *= 2;
				cur->data = new_data;
			}
		}

		int newFirst = (cur->capacity - size) / 2;
		if (newFirst != cur->first)
		{
			memmove(&cur->data[newFirst], &cur->data[cur->first], size * sizeof(SceUID));
			cur->first = newFirst;
			cur->end = newFirst + size;
		}
	}

	// The first queue that's ever been used.
	Queue *first;
	// The priority level queues of thread ids.
	Queue queues[NUM_QUEUES];
};

struct WaitTypeFuncs
{
	WaitBeginCallbackFunc beginFunc;
	WaitEndCallbackFunc endFunc;
};

void __KernelExecuteMipsCallOnCurrentThread(u32 callId, bool reschedAfter);

Thread *__KernelCreateThread(SceUID &id, SceUID moduleID, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr);
void __KernelResetThread(Thread *t, int lowestPriority);
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
u32 extendReturnHackAddr;
u32 moduleReturnHackAddr;
std::vector<ThreadCallback> threadEndListeners;

// Lists all thread ids that aren't deleted/etc.
std::vector<SceUID> threadqueue;

// Lists only ready thread ids.
ThreadQueueList threadReadyQueue;

SceUID threadIdleID[2];

int eventScheduledWakeup;
int eventThreadEndTimeout;

bool dispatchEnabled = true;

MipsCallManager mipsCalls;
int actionAfterCallback;
int actionAfterMipsCall;

// Doesn't need state saving.
WaitTypeFuncs waitTypeFuncs[NUM_WAITTYPES];

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

void MipsCall::setReturnValue(u64 value)
{
	savedV0 = value & 0xFFFFFFFF;
	savedV1 = (value >> 32) & 0xFFFFFFFF;
}

Thread *__GetCurrentThread() {
	if (currentThread != 0)
		return kernelObjects.GetFast<Thread>(currentThread);
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

u32 __KernelSetThreadRA(SceUID threadID, u32 nid)
{
	u32 newRA;
	switch (nid)
	{
	case NID_MODULERETURN:
		newRA = moduleReturnHackAddr;
		break;
	default:
		ERROR_LOG_REPORT(HLE, "__KernelSetThreadRA(): invalid RA address");
		return -1;
	}

	if (threadID == currentThread)
		currentMIPS->r[MIPS_REG_RA] = newRA;
	else
	{
		u32 error;
		Thread *thread = kernelObjects.Get<Thread>(threadID, error);
		if (!thread)
			return error;

		thread->context.r[MIPS_REG_RA] = newRA;
	}

	return 0;
}

void hleScheduledWakeup(u64 userdata, int cyclesLate);
void hleThreadEndTimeout(u64 userdata, int cyclesLate);

void __KernelWriteFakeSysCall(u32 nid, u32 *ptr, u32 &pos)
{
	*ptr = pos;
	pos += 8;
	WriteSyscall("FakeSysCalls", nid, *ptr);
}

void __KernelThreadingInit()
{
	struct ThreadHack
	{
		u32 nid;
		u32 *addr;
	};

	// Yeah, this is straight out of JPCSP, I should be ashamed.
	const static u32 idleThreadCode[] = {
		MIPS_MAKE_ADDIU(MIPS_REG_A0, MIPS_REG_ZERO, 0),
		MIPS_MAKE_LUI(MIPS_REG_RA, 0x0800),
		MIPS_MAKE_JR_RA(),
		//MIPS_MAKE_SYSCALL("ThreadManForUser", "sceKernelDelayThread"),
		MIPS_MAKE_SYSCALL("FakeSysCalls", "_sceKernelIdle"),
		MIPS_MAKE_BREAK(),
	};

	// If you add another func here, don't forget __KernelThreadingDoState() below.
	static ThreadHack threadHacks[] = {
		{NID_THREADRETURN, &threadReturnHackAddr},
		{NID_CALLBACKRETURN, &cbReturnHackAddr},
		{NID_INTERRUPTRETURN, &intReturnHackAddr},
		{NID_EXTENDRETURN, &extendReturnHackAddr},
		{NID_MODULERETURN, &moduleReturnHackAddr},
	};
	u32 blockSize = sizeof(idleThreadCode) + ARRAY_SIZE(threadHacks) * 2 * 4;  // The thread code above plus 8 bytes per "hack"

	dispatchEnabled = true;
	memset(waitTypeFuncs, 0, sizeof(waitTypeFuncs));

	currentThread = 0;
	g_inCbCount = 0;
	currentCallbackThreadID = 0;
	readyCallbacksCount = 0;
	idleThreadHackAddr = kernelMemory.Alloc(blockSize, false, "threadrethack");

	Memory::Memcpy(idleThreadHackAddr, idleThreadCode, sizeof(idleThreadCode));

	u32 pos = idleThreadHackAddr + sizeof(idleThreadCode);
	for (size_t i = 0; i < ARRAY_SIZE(threadHacks); ++i) {
		__KernelWriteFakeSysCall(threadHacks[i].nid, threadHacks[i].addr, pos);
	}

	eventScheduledWakeup = CoreTiming::RegisterEvent("ScheduledWakeup", &hleScheduledWakeup);
	eventThreadEndTimeout = CoreTiming::RegisterEvent("ThreadEndTimeout", &hleThreadEndTimeout);
	actionAfterMipsCall = __KernelRegisterActionType(ActionAfterMipsCall::Create);
	actionAfterCallback = __KernelRegisterActionType(ActionAfterCallback::Create);

	// Create the two idle threads, as well. With the absolute minimal possible priority.
	// 4096 stack size - don't know what the right value is. Hm, if callbacks are ever to run on these threads...
	__KernelResetThread(__KernelCreateThread(threadIdleID[0], 0, "idle0", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL), 0);
	__KernelResetThread(__KernelCreateThread(threadIdleID[1], 0, "idle1", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL), 0);
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
	p.Do(extendReturnHackAddr);
	p.Do(moduleReturnHackAddr);

	p.Do(currentThread);
	SceUID dv = 0;
	p.Do(threadqueue, dv);
	p.DoArray(threadIdleID, ARRAY_SIZE(threadIdleID));
	p.Do(dispatchEnabled);

	p.Do(threadReadyQueue);

	p.Do(eventScheduledWakeup);
	CoreTiming::RestoreRegisterEvent(eventScheduledWakeup, "ScheduledWakeup", &hleScheduledWakeup);
	p.Do(eventThreadEndTimeout);
	CoreTiming::RestoreRegisterEvent(eventThreadEndTimeout, "ThreadEndTimeout", &hleThreadEndTimeout);
	p.Do(actionAfterMipsCall);
	__KernelRestoreActionType(actionAfterMipsCall, ActionAfterMipsCall::Create);
	p.Do(actionAfterCallback);
	__KernelRestoreActionType(actionAfterCallback, ActionAfterCallback::Create);

	hleCurrentThreadName = __KernelGetThreadName(currentThread);

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

// TODO: Use __KernelChangeThreadState instead?  It has other affects...
void __KernelChangeReadyState(Thread *thread, SceUID threadID, bool ready)
{
	int prio = thread->nt.currentPriority;

	if (thread->isReady())
	{
		if (!ready)
			threadReadyQueue.remove(prio, threadID);
	}
	else if (ready)
	{
		if (thread->isRunning())
			threadReadyQueue.push_front(prio, threadID);
		else
			threadReadyQueue.push_back(prio, threadID);
		thread->nt.status = THREADSTATUS_READY;
	}
}

void __KernelChangeReadyState(SceUID threadID, bool ready)
{
	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(threadID, error);
	if (thread)
		__KernelChangeReadyState(thread, threadID, ready);
	else
		WARN_LOG(HLE, "Trying to change the ready state of an unknown thread?");
}

void __KernelStartIdleThreads(SceUID moduleId)
{
	for (int i = 0; i < 2; i++)
	{
		u32 error;
		Thread *t = kernelObjects.Get<Thread>(threadIdleID[i], error);
		t->nt.gpreg = __KernelGetModuleGP(moduleId);
		t->context.r[MIPS_REG_GP] = t->nt.gpreg;
		//t->context.pc += 4;	// ADJUSTPC
		threadReadyQueue.prepare(t->nt.currentPriority);
		__KernelChangeReadyState(t, threadIdleID[i], true);
	}
}

bool __KernelSwitchOffThread(const char *reason)
{
	if (!reason)
		reason = "switch off thread";

	SceUID threadID = currentThread;

	if (threadID != threadIdleID[0] && threadID != threadIdleID[1])
	{
		Thread *current = __GetCurrentThread();
		if (current && current->isRunning())
			__KernelChangeReadyState(current, threadID, true);

		// Idle 0 chosen entirely arbitrarily.
		Thread *t = kernelObjects.GetFast<Thread>(threadIdleID[0]);
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

bool __KernelSwitchToThread(SceUID threadID, const char *reason)
{
	if (!reason)
		reason = "switch to thread";

	if (currentThread != threadIdleID[0] && currentThread != threadIdleID[1])
	{
		ERROR_LOG_REPORT(HLE, "__KernelSwitchToThread used when already on a thread.");
		return false;
	}

	if (currentThread == threadID)
		return false;

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (!t)
		ERROR_LOG(HLE, "__KernelSwitchToThread: %x doesn't exist", threadID)
	else
	{
		Thread *current = __GetCurrentThread();
		if (current && current->isRunning())
			__KernelChangeReadyState(current, threadID, true);

		__KernelSwitchContext(t, reason);
		return true;
	}

	return false;
}

void __KernelIdle()
{
	CoreTiming::Idle();
	// Advance must happen between Idle and Reschedule, so that threads that were waiting for something
	// that was triggered at the end of the Idle period must get a chance to be scheduled.
	CoreTiming::AdvanceQuick();

	// We must've exited a callback?
	if (__KernelInCallback())
	{
		u32 error;
		Thread *t = kernelObjects.Get<Thread>(currentCallbackThreadID, error);
		if (t)
		{
			__KernelChangeReadyState(t, currentCallbackThreadID, false);
			t->nt.status = (t->nt.status | THREADSTATUS_RUNNING) & ~THREADSTATUS_READY;
			__KernelSwitchContext(t, "idle");
		}
		else
		{
			WARN_LOG_REPORT(HLE, "UNTESTED - Callback thread deleted during interrupt?");
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
	threadReadyQueue.clear();
	threadEndListeners.clear();
	mipsCalls.clear();
	threadReturnHackAddr = 0;
	cbReturnHackAddr = 0;
	currentThread = 0;
	intReturnHackAddr = 0;
	hleCurrentThreadName = NULL;
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

SceUID __KernelGetCurrentCallbackID(SceUID threadID, u32 &error)
{
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
		return t->currentCallbackId;
	else
	{
		ERROR_LOG(HLE, "__KernelGetCurrentCallbackID ERROR: thread %i", threadID);
		return 0;
	}
}

u32 sceKernelReferThreadStatus(u32 threadID, u32 statusPtr)
{
	static const u32 THREADINFO_SIZE = 104;
	static const u32 THREADINFO_SIZE_AFTER_260 = 108;

	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (!t)
	{
		ERROR_LOG(HLE, "%08x=sceKernelReferThreadStatus(%i, %08x): bad thread", error, threadID, statusPtr);
		return error;
	}

	u32 wantedSize = Memory::Read_U32(statusPtr);

	if (sceKernelGetCompiledSdkVersion() > 0x2060010)
	{
		if (wantedSize > THREADINFO_SIZE_AFTER_260)
		{
			ERROR_LOG(HLE, "%08x=sceKernelReferThreadStatus(%i, %08x): bad size %d", SCE_KERNEL_ERROR_ILLEGAL_SIZE, threadID, statusPtr, wantedSize);
			return SCE_KERNEL_ERROR_ILLEGAL_SIZE;
		}

		DEBUG_LOG(HLE, "sceKernelReferThreadStatus(%i, %08x)", threadID, statusPtr);

		t->nt.nativeSize = THREADINFO_SIZE_AFTER_260;
		if (wantedSize != 0)
			Memory::Memcpy(statusPtr, &t->nt, wantedSize);
		// TODO: What is this value?  Basic tests show 0...
		if (wantedSize > sizeof(t->nt))
			Memory::Memset(statusPtr + sizeof(t->nt), 0, wantedSize - sizeof(t->nt));
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReferThreadStatus(%i, %08x)", threadID, statusPtr);

		t->nt.nativeSize = THREADINFO_SIZE;
		u32 sz = std::min(THREADINFO_SIZE, wantedSize);
		if (sz != 0)
			Memory::Memcpy(statusPtr, &t->nt, sz);
	}

	hleEatCycles(1220);
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

int sceKernelGetThreadExitStatus(SceUID threadID)
{
	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (t->nt.status == THREADSTATUS_DORMANT) // TODO: can be dormant before starting, too, need to avoid that
		{
			DEBUG_LOG(HLE,"sceKernelGetThreadExitStatus(%i)", threadID);
			return t->nt.exitStatus;
		}
		else
		{
			return SCE_KERNEL_ERROR_NOT_DORMANT;
		}
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelGetThreadExitStatus Error %08x", error);
		return SCE_KERNEL_ERROR_UNKNOWN_THID;
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
		ERROR_LOG_REPORT(HLE, "sceKernelGetThreadmanIdList only implemented for threads");
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
void __KernelSaveContext(ThreadContext *ctx, bool vfpuEnabled)
{
	memcpy(ctx->r, currentMIPS->r, sizeof(ctx->r));
	memcpy(ctx->f, currentMIPS->f, sizeof(ctx->f));

	if (vfpuEnabled)
	{
		memcpy(ctx->v, currentMIPS->v, sizeof(ctx->v));
		memcpy(ctx->vfpuCtrl, currentMIPS->vfpuCtrl, sizeof(ctx->vfpuCtrl));
	}

	ctx->pc = currentMIPS->pc;
	ctx->hi = currentMIPS->hi;
	ctx->lo = currentMIPS->lo;
	ctx->fcr0 = currentMIPS->fcr0;
	ctx->fcr31 = currentMIPS->fcr31;
	ctx->fpcond = currentMIPS->fpcond;
}

// Loads a CPU context
void __KernelLoadContext(ThreadContext *ctx, bool vfpuEnabled)
{
	memcpy(currentMIPS->r, ctx->r, sizeof(ctx->r));
	memcpy(currentMIPS->f, ctx->f, sizeof(ctx->f));

	if (vfpuEnabled)
	{
		memcpy(currentMIPS->v, ctx->v, sizeof(ctx->v));
		memcpy(currentMIPS->vfpuCtrl, ctx->vfpuCtrl, sizeof(ctx->vfpuCtrl));
	}

	currentMIPS->pc = ctx->pc;
	currentMIPS->hi = ctx->hi;
	currentMIPS->lo = ctx->lo;
	currentMIPS->fcr0 = ctx->fcr0;
	currentMIPS->fcr31 = ctx->fcr31;
	currentMIPS->fpcond = ctx->fpcond;

	// Reset the llBit, the other thread may have touched memory.
	currentMIPS->llBit = 0;
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

u32 __KernelResumeThreadFromWait(SceUID threadID, u32 retval)
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

u32 __KernelResumeThreadFromWait(SceUID threadID, u64 retval)
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
				t->setReturnValue((u32)retVal);
			doneAnything = true;

			if (type == WAITTYPE_THREADEND)
				__KernelCancelThreadEndTimeout(*iter);
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
	return doneAnything;
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
	if (!dispatchEnabled)
	{
		WARN_LOG_REPORT(HLE, "Ignoring wait, dispatching disabled... right thing to do?");
		return;
	}

	// TODO: Need to defer if in callback?
	if (g_inCbCount > 0)
		WARN_LOG_REPORT(HLE, "UNTESTED - waiting within a callback, probably bad mojo.");

	Thread *thread = __GetCurrentThread();
	thread->nt.waitID = waitID;
	thread->nt.waitType = type;
	__KernelChangeThreadState(thread, ThreadStatus(THREADSTATUS_WAIT | (thread->nt.status & THREADSTATUS_SUSPEND)));
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

void __KernelWaitCallbacksCurThread(WaitType type, SceUID waitID, u32 waitValue, u32 timeoutPtr)
{
	if (!dispatchEnabled)
	{
		WARN_LOG_REPORT(HLE, "Ignoring wait, dispatching disabled... right thing to do?");
		return;
	}

	Thread *thread = __GetCurrentThread();
	thread->nt.waitID = waitID;
	thread->nt.waitType = type;
	__KernelChangeThreadState(thread, ThreadStatus(THREADSTATUS_WAIT | (thread->nt.status & THREADSTATUS_SUSPEND)));
	// TODO: Probably not...?
	thread->nt.numReleases++;
	thread->waitInfo.waitValue = waitValue;
	thread->waitInfo.timeoutPtr = timeoutPtr;

	__KernelForceCallbacks();
}

void hleScheduledWakeup(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;
	u32 error;
	if (__KernelGetWaitID(threadID, WAITTYPE_DELAY, error) == threadID)
		__KernelResumeThreadFromWait(threadID);
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

	u32 error;
	// Just in case it was woken on its own.
	if (__KernelGetWaitID(threadID, WAITTYPE_THREADEND, error) != 0)
	{
		u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
		if (Memory::IsValidAddress(timeoutPtr))
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
}

void __KernelScheduleThreadEndTimeout(SceUID threadID, SceUID waitForID, s64 usFromNow)
{
	s64 cycles = usToCycles(usFromNow);
	CoreTiming::ScheduleEvent(cycles, eventThreadEndTimeout, threadID);
}

void __KernelCancelThreadEndTimeout(SceUID threadID)
{
	CoreTiming::UnscheduleEvent(eventThreadEndTimeout, threadID);
}

void __KernelRemoveFromThreadQueue(SceUID threadID)
{
	int prio = __KernelGetThreadPrio(threadID);
	if (prio != 0)
		threadReadyQueue.remove(prio, threadID);

	threadqueue.erase(std::remove(threadqueue.begin(), threadqueue.end(), threadID), threadqueue.end());
}

u32 __KernelDeleteThread(SceUID threadID, int exitStatus, const char *reason, bool dontSwitch)
{
	__KernelFireThreadEnd(threadID);
	__KernelRemoveFromThreadQueue(threadID);
	__KernelTriggerWait(WAITTYPE_THREADEND, threadID, exitStatus, reason, dontSwitch);

	if (currentThread == threadID)
	{
		currentThread = 0;
		hleCurrentThreadName = NULL;
	}
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
			readyCallbacksCount -= (int)t->readyCallbacks[i].size();
	}

	return kernelObjects.Destroy<Thread>(threadID);
}

// Returns NULL if the current thread is fine.
Thread *__KernelNextThread() {
	SceUID bestThread;

	// If the current thread is running, it's a valid candidate.
	Thread *cur = __GetCurrentThread();
	if (cur && cur->isRunning())
	{
		bestThread = threadReadyQueue.pop_first_better(cur->nt.currentPriority);
		if (bestThread != 0)
			__KernelChangeReadyState(cur, currentThread, true);
	}
	else
		bestThread = threadReadyQueue.pop_first();

	// Assume threadReadyQueue has not become corrupt.
	if (bestThread != 0)
		return kernelObjects.GetFast<Thread>(bestThread);
	else
		return 0;
}

void __KernelReSchedule(const char *reason)
{
	// cancel rescheduling when in interrupt or callback, otherwise everything will be fucked up
	if (__IsInInterrupt() || __KernelInCallback() || !__KernelIsDispatchEnabled())
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
	CoreTiming::AdvanceQuick();
	if (__IsInInterrupt() || __KernelInCallback() || !__KernelIsDispatchEnabled())
	{
		reason = "In Interrupt Or Callback";
		return;
	}

	Thread *nextThread = __KernelNextThread();
	if (nextThread)
		__KernelSwitchContext(nextThread, reason);
	// Otherwise, no need to switch.
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

int sceKernelCheckThreadStack()
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(__KernelGetCurThread(), error);
	if (t) {
		u32 diff = labs((long)((s64)currentMIPS->r[MIPS_REG_SP] - (s64)t->currentStack.start));
		WARN_LOG(HLE, "%i=sceKernelCheckThreadStack()", diff);
		return diff;
	} else {
		ERROR_LOG_REPORT(HLE, "sceKernelCheckThreadStack() - not on thread");
		return -1;
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

void __KernelResetThread(Thread *t, int lowestPriority)
{
	t->context.reset();
	t->context.hi = 0;
	t->context.lo = 0;
	t->context.pc = t->nt.entrypoint;

	// If the thread would be better than lowestPriority, reset to its initial.  Yes, kinda odd...
	if (t->nt.currentPriority < lowestPriority)
		t->nt.currentPriority = t->nt.initialPriority;

	t->nt.waitType = WAITTYPE_NONE;
	t->nt.waitID = 0;
	memset(&t->waitInfo, 0, sizeof(t->waitInfo));

	t->nt.exitStatus = SCE_KERNEL_ERROR_NOT_DORMANT;
	t->isProcessingCallbacks = false;
	t->currentCallbackId = 0;
	t->currentMipscallId = 0;
	t->pendingMipsCalls.clear();

	t->context.r[MIPS_REG_RA] = threadReturnHackAddr; //hack! TODO fix
	// TODO: Not sure if it's reset here, but this makes sense.
	t->context.r[MIPS_REG_GP] = t->nt.gpreg;
	t->FillStack();
}

Thread *__KernelCreateThread(SceUID &id, SceUID moduleId, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr)
{
	Thread *t = new Thread;
	id = kernelObjects.Create(t);

	threadqueue.push_back(id);
	threadReadyQueue.prepare(priority);

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

	t->AllocateStack(t->nt.stackSize);  // can change the stacksize!
	return t;
}

SceUID __KernelSetupRootThread(SceUID moduleID, int args, const char *argp, int prio, int stacksize, int attr) 
{
	//grab mips regs
	SceUID id;
	Thread *thread = __KernelCreateThread(id, moduleID, "root", currentMIPS->pc, prio, stacksize, attr);
	if (thread->currentStack.start == 0)
		ERROR_LOG_REPORT(HLE, "Unable to allocate stack for root thread.");
	__KernelResetThread(thread, 0);

	Thread *prevThread = __GetCurrentThread();
	if (prevThread && prevThread->isRunning())
		__KernelChangeReadyState(currentThread, true);
	currentThread = id;
	hleCurrentThreadName = "root";
	thread->nt.status = THREADSTATUS_RUNNING; // do not schedule

	strcpy(thread->nt.name, "root");

	__KernelLoadContext(&thread->context, (attr & PSP_THREAD_ATTR_VFPU) != 0);
	mipsr4k.r[MIPS_REG_A0] = args;
	mipsr4k.r[MIPS_REG_SP] -= 256;
	u32 location = mipsr4k.r[MIPS_REG_SP];
	mipsr4k.r[MIPS_REG_A1] = location;
	for (int i = 0; i < args; i++)
		Memory::Write_U8(argp[i], location + i);

	return id;
}

int __KernelCreateThread(const char *threadName, SceUID moduleID, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr)
{
	if (threadName == NULL)
	{
		ERROR_LOG_REPORT(HLE, "SCE_KERNEL_ERROR_ERROR=sceKernelCreateThread(): NULL name");
		return SCE_KERNEL_ERROR_ERROR;
	}

	// TODO: PSP actually fails for many of these cases, but trying for compat.
	if (stacksize < 0x200 || stacksize >= 0x20000000)
	{
		WARN_LOG_REPORT(HLE, "sceKernelCreateThread(name=%s): bogus stack size %08x, using 0x4000", threadName, stacksize);
		stacksize = 0x4000;
	}
	if (prio < 0x08 || prio > 0x77)
	{
		WARN_LOG_REPORT(HLE, "sceKernelCreateThread(name=%s): bogus priority %08x", threadName, prio);
		prio = prio < 0x08 ? 0x08 : 0x77;
	}
	if (!Memory::IsValidAddress(entry))
	{
		ERROR_LOG_REPORT(HLE, "sceKernelCreateThread(name=%s): invalid entry %08x", threadName, entry);
		// The PSP firmware seems to allow NULL...?
		if (entry != 0)
			return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	// We're assuming all threads created are user threads.
	if ((attr & PSP_THREAD_ATTR_KERNEL) == 0)
		attr |= PSP_THREAD_ATTR_USER;

	SceUID id;
	Thread *newThread = __KernelCreateThread(id, moduleID, threadName, entry, prio, stacksize, attr);
	if (newThread->currentStack.start == 0)
	{
		ERROR_LOG_REPORT(HLE, "sceKernelCreateThread(name=%s): out of memory, %08x stack requested", threadName, stacksize);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	INFO_LOG(HLE, "%i=sceKernelCreateThread(name=%s, entry=%08x, prio=%x, stacksize=%i)", id, threadName, entry, prio, stacksize);
	if (optionAddr != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateThread(name=%s): unsupported options parameter %08x", threadName, optionAddr);
	return id;
}

int sceKernelCreateThread(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr)
{
	return __KernelCreateThread(threadName, __KernelGetCurThreadModuleId(), entry, prio, stacksize, attr, optionAddr);
}


// int sceKernelStartThread(SceUID threadToStartID, SceSize argSize, void *argBlock)
int sceKernelStartThread(SceUID threadToStartID, int argSize, u32 argBlockPtr)
{
	u32 error = 0;
	if (threadToStartID == 0)
	{
		error = SCE_KERNEL_ERROR_ILLEGAL_THID;
		ERROR_LOG_REPORT(HLE, "%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x): NULL thread", error, threadToStartID, argSize, argBlockPtr);
		return error;
	}
	if (argSize < 0 || argBlockPtr & 0x80000000)
	{
		error = SCE_KERNEL_ERROR_ILLEGAL_ADDR;
		ERROR_LOG_REPORT(HLE, "%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x): bad argument pointer/length", error, threadToStartID, argSize, argBlockPtr);
		return error;
	}

	Thread *startThread = kernelObjects.Get<Thread>(threadToStartID, error);
	if (startThread == 0)
	{
		ERROR_LOG_REPORT(HLE, "%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x): thread does not exist!", error, threadToStartID, argSize, argBlockPtr);
		return error;
	}

	if (startThread->nt.status != THREADSTATUS_DORMANT)
	{
		error = SCE_KERNEL_ERROR_NOT_DORMANT;
		WARN_LOG_REPORT(HLE, "%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x): thread already running", error, threadToStartID, argSize, argBlockPtr);
		return error;
	}

	INFO_LOG(HLE, "sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x)", threadToStartID, argSize, argBlockPtr);

	Thread *cur = __GetCurrentThread();
	__KernelResetThread(startThread, cur ? cur->nt.currentPriority : 0);

	u32 &sp = startThread->context.r[MIPS_REG_SP];
	if (argBlockPtr && argSize > 0)
	{
		// Make room for the arguments, always 0x10 aligned.
		sp -= (argSize + 0xf) & ~0xf;
		startThread->context.r[MIPS_REG_A0] = argSize;
		startThread->context.r[MIPS_REG_A1] = sp;
	}
	else
	{
		if (argSize > 0)
			WARN_LOG_REPORT(HLE, "%08x=sceKernelStartThread(thread=%i, argSize=%i, argPtr=%08x): NULL argument with size (should crash?)", error, threadToStartID, argSize, argBlockPtr);

		startThread->context.r[MIPS_REG_A0] = 0;
		startThread->context.r[MIPS_REG_A1] = 0;
	}

	// Now copy argument to stack.
	if (Memory::IsValidAddress(argBlockPtr))
		Memory::Memcpy(sp, Memory::GetPointer(argBlockPtr), argSize);

	// On the PSP, there's an extra 64 bytes of stack eaten after the args.
	// This could be stack overflow safety, or just stack eaten by the kernel entry func.
	sp -= 64;

	// Smaller is better for priority.  Only switch if the new thread is better.
	if (cur && cur->nt.currentPriority > startThread->nt.currentPriority)
	{
		// Starting a thread automatically resumes the dispatch thread.
		// TODO: Maybe this happens even for worse-priority started threads?
		dispatchEnabled = true;

		__KernelChangeReadyState(cur, currentThread, true);
		hleReSchedule("thread started");
	}
	else if (!dispatchEnabled)
		WARN_LOG_REPORT(HLE, "UNTESTED Dispatch disabled while starting worse-priority thread");

	__KernelChangeReadyState(startThread, threadToStartID, true);
	return 0;
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
	for (u32 addr = thread->currentStack.start; addr < thread->currentStack.start + thread->nt.stackSize; addr++)
	{
		if (Memory::Read_U8(addr) != 0xFF)
			break;
		sz++;
	}

	RETURN(sz & ~3);
}

void __KernelReturnFromThread()
{
	int exitStatus = currentMIPS->r[2];
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "Returned from a NULL thread.");

	INFO_LOG(HLE,"__KernelReturnFromThread: %d", exitStatus);

	thread->nt.exitStatus = exitStatus;
	__KernelChangeReadyState(thread, currentThread, false);
	thread->nt.status = THREADSTATUS_DORMANT;
	__KernelFireThreadEnd(currentThread);

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread returned", true);
	hleReSchedule("thread returned");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitThread(int exitStatus)
{
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "Exited from a NULL thread.");

	INFO_LOG(HLE, "sceKernelExitThread(%d)", exitStatus);
	__KernelChangeReadyState(thread, currentThread, false);
	thread->nt.status = THREADSTATUS_DORMANT;
	thread->nt.exitStatus = exitStatus;
	__KernelFireThreadEnd(currentThread);

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread exited", true);
	hleReSchedule("thread exited");

	// The stack will be deallocated when the thread is deleted.
}

void _sceKernelExitThread(int exitStatus)
{
	Thread *thread = __GetCurrentThread();
	_dbg_assert_msg_(HLE, thread != NULL, "_Exited from a NULL thread.");

	ERROR_LOG_REPORT(HLE, "_sceKernelExitThread(%d): should not be called directly", exitStatus);
	thread->nt.status = THREADSTATUS_DORMANT;
	thread->nt.exitStatus = exitStatus;
	__KernelFireThreadEnd(currentThread);

	__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread(), thread->nt.exitStatus, "thread _exited", true);
	hleReSchedule("thread _exited");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitDeleteThread(int exitStatus)
{
	Thread *thread = __GetCurrentThread();
	if (thread)
	{
		INFO_LOG(HLE,"sceKernelExitDeleteThread(%d)", exitStatus);
		__KernelChangeReadyState(thread, currentThread, false);
		thread->nt.status = THREADSTATUS_DORMANT;
		thread->nt.exitStatus = exitStatus;
		__KernelDeleteThread(currentThread, exitStatus, "thread exited with delete", true);

		hleReSchedule("thread exited with delete");
	}
	else
		ERROR_LOG_REPORT(HLE, "sceKernelExitDeleteThread(%d) ERROR - could not find myself!", exitStatus);
}

u32 sceKernelSuspendDispatchThread()
{
	if (!__InterruptsEnabled())
		return SCE_KERNEL_ERROR_CPUDI;

	u32 oldDispatchEnabled = dispatchEnabled;
	dispatchEnabled = false;
	DEBUG_LOG(HLE, "%i=sceKernelSuspendDispatchThread()", oldDispatchEnabled);
	return oldDispatchEnabled;
}

u32 sceKernelResumeDispatchThread(u32 enabled)
{
	if (!__InterruptsEnabled())
		return SCE_KERNEL_ERROR_CPUDI;

	u32 oldDispatchEnabled = dispatchEnabled;
	dispatchEnabled = enabled != 0;
	DEBUG_LOG(HLE, "sceKernelResumeDispatchThread(%i) - from %i", enabled, oldDispatchEnabled);
	hleReSchedule("dispatch resumed");
	return 0;
}

bool __KernelIsDispatchEnabled()
{
	// Dispatch can never be enabled when interrupts are disabled.
	return dispatchEnabled && __InterruptsEnabled();
}

int sceKernelRotateThreadReadyQueue(int priority)
{
	VERBOSE_LOG(HLE, "sceKernelRotateThreadReadyQueue(%x)", priority);

	Thread *cur = __GetCurrentThread();

	// 0 is special, it means "my current priority."
	if (priority == 0)
		priority = cur->nt.currentPriority;

	if (priority <= 0x07 || priority > 0x77)
		return SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;

	if (!threadReadyQueue.empty(priority))
	{
		// In other words, yield to everyone else.
		if (cur->nt.currentPriority == priority)
		{
			threadReadyQueue.push_back(priority, currentThread);
			cur->nt.status = (cur->nt.status & ~THREADSTATUS_RUNNING) | THREADSTATUS_READY;
		}
		// Yield the next thread of this priority to all other threads of same priority.
		else
			threadReadyQueue.rotate(priority);

		hleReSchedule("rotatethreadreadyqueue");
	}

	hleEatCycles(250);
	return 0;
}

int sceKernelDeleteThread(int threadID)
{
	if (threadID == 0 || threadID == currentThread)
	{
		ERROR_LOG(HLE, "sceKernelDeleteThread(%i): cannot delete current thread", threadID);
		return SCE_KERNEL_ERROR_NOT_DORMANT;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (!t->isStopped())
		{
			ERROR_LOG(HLE, "sceKernelDeleteThread(%i): thread not dormant", threadID);
			return SCE_KERNEL_ERROR_NOT_DORMANT;
		}

		DEBUG_LOG(HLE, "sceKernelDeleteThread(%i)", threadID);
		return __KernelDeleteThread(threadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "thread deleted", true);
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelDeleteThread(%i): thread doesn't exist", threadID);
		return error;
	}
}

int sceKernelTerminateDeleteThread(int threadID)
{
	if (threadID == 0 || threadID == currentThread)
	{
		ERROR_LOG(HLE, "sceKernelTerminateDeleteThread(%i): cannot terminate current thread", threadID);
		return SCE_KERNEL_ERROR_ILLEGAL_THID;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		INFO_LOG(HLE, "sceKernelTerminateDeleteThread(%i)", threadID);
		error = __KernelDeleteThread(threadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "thread terminated with delete", true);

		return error;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelTerminateDeleteThread(%i): thread doesn't exist", threadID);
		return error;
	}
}

int sceKernelTerminateThread(SceUID threadID)
{
	if (threadID == 0 || threadID == currentThread)
	{
		ERROR_LOG(HLE, "sceKernelTerminateThread(%i): cannot terminate current thread", threadID);
		return SCE_KERNEL_ERROR_ILLEGAL_THID;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (t->isStopped())
		{
			ERROR_LOG(HLE, "sceKernelTerminateThread(%i): already stopped", threadID);
			return SCE_KERNEL_ERROR_DORMANT;
		}

		INFO_LOG(HLE, "sceKernelTerminateThread(%i)", threadID);

		t->nt.exitStatus = SCE_KERNEL_ERROR_THREAD_TERMINATED;
		__KernelChangeReadyState(t, threadID, false);
		t->nt.status = THREADSTATUS_DORMANT;
		__KernelFireThreadEnd(threadID);
		// TODO: Should this really reschedule?
		__KernelTriggerWait(WAITTYPE_THREADEND, threadID, t->nt.exitStatus, "thread terminated", true);

		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelTerminateThread(%i): thread doesn't exist", threadID);
		return error;
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

u32 __KernelGetCurThreadStack()
{
	Thread *t = __GetCurrentThread();
	if (t)
		return t->currentStack.end;
	return 0;
}

SceUID sceKernelGetThreadId()
{
	VERBOSE_LOG(HLE, "%i = sceKernelGetThreadId()", currentThread);
	return currentThread;
}

void sceKernelGetThreadCurrentPriority()
{
	u32 retVal = __GetCurrentThread()->nt.currentPriority;
	DEBUG_LOG(HLE,"%i = sceKernelGetThreadCurrentPriority()", retVal);
	RETURN(retVal);
}

int sceKernelChangeCurrentThreadAttr(u32 clearAttr, u32 setAttr)
{
	// Seems like this is the only allowed attribute?
	if ((clearAttr & ~PSP_THREAD_ATTR_VFPU) != 0 || (setAttr & ~PSP_THREAD_ATTR_VFPU) != 0)
	{
		ERROR_LOG_REPORT(HLE, "0 = sceKernelChangeCurrentThreadAttr(clear = %08x, set = %08x): invalid attr", clearAttr, setAttr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	DEBUG_LOG(HLE, "0 = sceKernelChangeCurrentThreadAttr(clear = %08x, set = %08x)", clearAttr, setAttr);
	Thread *t = __GetCurrentThread();
	if (t)
		t->nt.attr = (t->nt.attr & ~clearAttr) | setAttr;
	else
		ERROR_LOG_REPORT(HLE, "%s(): No current thread?", __FUNCTION__);
	return 0;
}

int sceKernelChangeThreadPriority(SceUID threadID, int priority)
{
	if (threadID == 0)
		threadID = currentThread;
	// 0 means the current (running) thread's priority, not target's.
	if (priority == 0)
	{
		Thread *cur = __GetCurrentThread();
		if (!cur)
			ERROR_LOG_REPORT(HLE, "sceKernelChangeThreadPriority(%i, %i): no current thread?", threadID, priority)
		else
			priority = cur->nt.currentPriority;
	}

	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(threadID, error);
	if (thread)
	{
		if (thread->isStopped())
		{
			ERROR_LOG_REPORT(HLE, "sceKernelChangeThreadPriority(%i, %i): thread is dormant", threadID, priority);
			return SCE_KERNEL_ERROR_DORMANT;
		}

		if (priority < 0x08 || priority > 0x77)
		{
			ERROR_LOG_REPORT(HLE, "sceKernelChangeThreadPriority(%i, %i): bogus priority", threadID, priority);
			return SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;
		}

		DEBUG_LOG(HLE, "sceKernelChangeThreadPriority(%i, %i)", threadID, priority);

		int old = thread->nt.currentPriority;
		threadReadyQueue.remove(old, threadID);

		thread->nt.currentPriority = priority;
		threadReadyQueue.prepare(thread->nt.currentPriority);
		if (thread->isRunning())
			thread->nt.status = (thread->nt.status & ~THREADSTATUS_RUNNING) | THREADSTATUS_READY;
		if (thread->isReady())
			threadReadyQueue.push_back(thread->nt.currentPriority, threadID);

		hleReSchedule("change thread priority");
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "%08x=sceKernelChangeThreadPriority(%i, %i) failed - no such thread", error, threadID, priority);
		return error;
	}
}

s64 __KernelDelayThreadUs(u64 usec)
{
	// Seems to very based on clockrate / other things, but 0 delays less than 200us for sure.
	if (usec == 0)
		return 100;
	else if (usec < 200)
		return 200;
	return usec;
}

int sceKernelDelayThreadCB(u32 usec)
{
	DEBUG_LOG(HLE,"sceKernelDelayThreadCB(%i usec)",usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, __KernelDelayThreadUs(usec));
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, true, "thread delayed");
	return 0;
}

int sceKernelDelayThread(u32 usec)
{
	DEBUG_LOG(HLE,"sceKernelDelayThread(%i usec)",usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, __KernelDelayThreadUs(usec));
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, false, "thread delayed");
	return 0;
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
	DEBUG_LOG(HLE, "sceKernelDelaySysClockThread(%08x (%llu))", sysclockAddr, usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, __KernelDelayThreadUs(usec));
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
	DEBUG_LOG(HLE, "sceKernelDelaySysClockThread(%08x (%llu))", sysclockAddr, usec);

	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, __KernelDelayThreadUs(usec));
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
int sceKernelWakeupThread(SceUID uid)
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(uid, error);
	if (t)
	{
		if (!t->isWaitingFor(WAITTYPE_SLEEP, 1)) {
			t->nt.wakeupCount++;
			DEBUG_LOG(HLE,"sceKernelWakeupThread(%i) - wakeupCount incremented to %i", uid, t->nt.wakeupCount);
		} else {
			VERBOSE_LOG(HLE,"sceKernelWakeupThread(%i) - woke thread at %i", uid, t->nt.wakeupCount);
			__KernelResumeThreadFromWait(uid);
			hleReSchedule("thread woken up");
		}
		return 0;
	} 
	else {
		ERROR_LOG(HLE,"sceKernelWakeupThread(%i) - bad thread id", uid);
		return error;
	}
}

int sceKernelCancelWakeupThread(SceUID uid)
{
	u32 error;
	if (uid == 0) uid = __KernelGetCurThread();
	Thread *t = kernelObjects.Get<Thread>(uid, error);
	if (t)
	{
		int wCount = t->nt.wakeupCount;
		t->nt.wakeupCount = 0;
		DEBUG_LOG(HLE,"sceKernelCancelWakeupThread(%i) - wakeupCount reset from %i", uid, wCount);
		return wCount;
	}
	else {
		ERROR_LOG(HLE,"sceKernelCancelWakeupThread(%i) - bad thread id", uid);
		return error;
	}
}

static int __KernelSleepThread(bool doCallbacks) {
	Thread *thread = __GetCurrentThread();
	if (!thread) {
		ERROR_LOG(HLE, "sceKernelSleepThread*(): bad current thread");
		return -1;
	}

	if (thread->nt.wakeupCount > 0) {
		thread->nt.wakeupCount--;
		DEBUG_LOG(HLE, "sceKernelSleepThread() - wakeupCount decremented to %i", thread->nt.wakeupCount);
	} else {
		VERBOSE_LOG(HLE, "sceKernelSleepThread()");
		__KernelWaitCurThread(WAITTYPE_SLEEP, 1, 0, 0, doCallbacks, "thread slept");
	}
	return 0;
}

int sceKernelSleepThread()
{
	return __KernelSleepThread(false);
}

//the homebrew PollCallbacks
int sceKernelSleepThreadCB()
{
	VERBOSE_LOG(HLE, "sceKernelSleepThreadCB()");
	hleCheckCurrentCallbacks();
	return __KernelSleepThread(true);
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
		WARN_LOG_REPORT(HLE, "UNTESTED sceKernelReleaseWaitThread() might not do the right thing in a callback");

	if (threadID == 0 || threadID == currentThread)
		return SCE_KERNEL_ERROR_ILLEGAL_THID;

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (!t->isWaiting())
			return SCE_KERNEL_ERROR_NOT_WAIT;
		if (t->nt.waitType == WAITTYPE_HLEDELAY)
		{
			WARN_LOG_REPORT(HLE, "sceKernelReleaseWaitThread(): Refusing to wake HLE-delayed thread, right thing to do?");
			return SCE_KERNEL_ERROR_NOT_WAIT;
		}

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

int sceKernelSuspendThread(SceUID threadID)
{
	// TODO: What about interrupts/callbacks?
	if (threadID == 0 || threadID == currentThread)
	{
		ERROR_LOG(HLE, "sceKernelSuspendThread(%d): cannot suspend current thread", threadID);
		return SCE_KERNEL_ERROR_ILLEGAL_THID;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (t->isStopped())
		{
			ERROR_LOG(HLE, "sceKernelSuspendThread(%d): thread not running", threadID);
			return SCE_KERNEL_ERROR_DORMANT;
		}
		if (t->isSuspended())
		{
			ERROR_LOG(HLE, "sceKernelSuspendThread(%d): thread already suspended", threadID);
			return SCE_KERNEL_ERROR_SUSPEND;
		}

		WARN_LOG(HLE, "sceKernelSuspendThread(%d)", threadID);
		if (t->isReady())
			__KernelChangeReadyState(t, threadID, false);
		t->nt.status = (t->nt.status & ~THREADSTATUS_READY) | THREADSTATUS_SUSPEND;
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSuspendThread(%d): bad thread", threadID);
		return error;
	}
}

int sceKernelResumeThread(SceUID threadID)
{
	// TODO: What about interrupts/callbacks?
	if (threadID == 0 || threadID == currentThread)
	{
		ERROR_LOG(HLE, "sceKernelSuspendThread(%d): cannot suspend current thread", threadID);
		return SCE_KERNEL_ERROR_ILLEGAL_THID;
	}

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		if (!t->isSuspended())
		{
			ERROR_LOG(HLE, "sceKernelSuspendThread(%d): thread not suspended", threadID);
			return SCE_KERNEL_ERROR_NOT_SUSPEND;
		}
		WARN_LOG(HLE, "sceKernelResumeThread(%d)", threadID);
		t->nt.status &= ~THREADSTATUS_SUSPEND;

		// If it was dormant, waiting, etc. before we don't flip it's ready state.
		if (t->nt.status == 0)
			__KernelChangeReadyState(t, threadID, true);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelResumeThread(%d): bad thread", threadID);
		return error;
	}
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

	return id;
}

SceUID sceKernelCreateCallback(const char *name, u32 entrypoint, u32 signalArg)
{
	SceUID id = __KernelCreateCallback(name, entrypoint, signalArg);
	DEBUG_LOG(HLE, "%i=sceKernelCreateCallback(name=%s, entry=%08x, callbackArg=%08x)", id, name, entrypoint, signalArg);

	return id;
}

int sceKernelDeleteCallback(SceUID cbId)
{
	DEBUG_LOG(HLE, "sceKernelDeleteCallback(%i)", cbId);

	// TODO: Make sure it's gone from all threads first!

	return kernelObjects.Destroy<Callback>(cbId);
}

// Generally very rarely used, but Numblast uses it like candy.
int sceKernelNotifyCallback(SceUID cbId, int notifyArg)
{
	DEBUG_LOG(HLE,"sceKernelNotifyCallback(%i, %i)", cbId, notifyArg);
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (cb) {
		// TODO: Should this notify other existing callbacks too?
		__KernelNotifyCallback(THREAD_CALLBACK_USER_DEFINED, cbId, notifyArg);
		return 0;
	} else {
		ERROR_LOG(HLE, "sceKernelCancelCallback(%i) - bad cbId", cbId);
		return error;
	}
}

int sceKernelCancelCallback(SceUID cbId)
{
	DEBUG_LOG(HLE, "sceKernelCancelCallback(%i)", cbId);
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (cb) {
		// This just resets the notify count.
		cb->nc.notifyArg = 0;
		return 0;
	} else {
		ERROR_LOG(HLE, "sceKernelCancelCallback(%i) - bad cbId", cbId);
		return error;
	}
}

int sceKernelGetCallbackCount(SceUID cbId)
{
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (cb) {
		return cb->nc.notifyCount;
	} else {
		ERROR_LOG(HLE, "sceKernelGetCallbackCount(%i) - bad cbId", cbId);
		return error;
	}
}

int sceKernelReferCallbackStatus(SceUID cbId, u32 statusAddr)
{
	u32 error;
	Callback *c = kernelObjects.Get<Callback>(cbId, error);
	if (c) {
		DEBUG_LOG(HLE, "sceKernelReferCallbackStatus(%i, %08x)", cbId, statusAddr);
		// TODO: Maybe check size parameter?
		if (Memory::IsValidAddress(statusAddr)) {
			Memory::WriteStruct(statusAddr, &c->nc);
		} // else TODO
		return 0;
	} else {
		ERROR_LOG(HLE, "sceKernelReferCallbackStatus(%i, %08x) - bad cbId", cbId, statusAddr);
		return error;
	}
}

u32 sceKernelExtendThreadStack(u32 size, u32 entryAddr, u32 entryParameter)
{
	if (size < 512)
	{
		ERROR_LOG_REPORT(HLE, "sceKernelExtendThreadStack(%08x, %08x, %08x) - stack size too small", size, entryAddr, entryParameter);
		return SCE_KERNEL_ERROR_ILLEGAL_STACK_SIZE;
	}

	Thread *thread = __GetCurrentThread();
	if (!thread)
	{
		ERROR_LOG_REPORT(HLE, "sceKernelExtendThreadStack(%08x, %08x, %08x) - not on a thread?", size, entryAddr, entryParameter);
		return -1;
	}

	if (!thread->PushExtendedStack(size))
	{
		ERROR_LOG_REPORT(HLE, "sceKernelExtendThreadStack(%08x, %08x, %08x) - could not allocate new stack", size, entryAddr, entryParameter);
		return SCE_KERNEL_ERROR_NO_MEMORY;
	}

	// The stack has been changed now, so it's do or die time.
	DEBUG_LOG(HLE, "sceKernelExtendThreadStack(%08x, %08x, %08x)", size, entryAddr, entryParameter);

	// Push the old SP, RA, and PC onto the stack (so we can restore them later.)
	Memory::Write_U32(currentMIPS->r[MIPS_REG_RA], thread->currentStack.end - 4);
	Memory::Write_U32(currentMIPS->r[MIPS_REG_SP], thread->currentStack.end - 8);
	Memory::Write_U32(currentMIPS->pc, thread->currentStack.end - 12);

	currentMIPS->pc = entryAddr;
	currentMIPS->r[MIPS_REG_A0] = entryParameter;
	currentMIPS->r[MIPS_REG_RA] = extendReturnHackAddr;
	// Stack should stay aligned even though we saved only 3 regs.
	currentMIPS->r[MIPS_REG_SP] = thread->currentStack.end - 0x10;

	return 0;
}

void __KernelReturnFromExtendStack()
{
	Thread *thread = __GetCurrentThread();
	if (!thread)
	{
		ERROR_LOG_REPORT(HLE, "__KernelReturnFromExtendStack() - not on a thread?");
		return;
	}

	// Grab the saved regs at the top of the stack.
	u32 restoreRA = Memory::Read_U32(thread->currentStack.end - 4);
	u32 restoreSP = Memory::Read_U32(thread->currentStack.end - 8);
	u32 restorePC = Memory::Read_U32(thread->currentStack.end - 12);

	if (!thread->PopExtendedStack())
	{
		ERROR_LOG_REPORT(HLE, "__KernelReturnFromExtendStack() - no stack to restore?");
		return;
	}

	DEBUG_LOG(HLE, "__KernelReturnFromExtendStack()");
	currentMIPS->r[MIPS_REG_RA] = restoreRA;
	currentMIPS->r[MIPS_REG_SP] = restoreSP;
	currentMIPS->pc = restorePC;

	// We retain whatever is in v0/v1, it gets passed on to the caller of sceKernelExtendThreadStack().
}

void ActionAfterMipsCall::run(MipsCall &call) {
	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(threadID, error);
	if (thread) {
		__KernelChangeReadyState(thread, threadID, (status & THREADSTATUS_READY) != 0);
		thread->nt.status = status;
		thread->nt.waitType = waitType;
		thread->nt.waitID = waitID;
		thread->waitInfo = waitInfo;
		thread->isProcessingCallbacks = isProcessingCallbacks;
		thread->currentCallbackId = currentCallbackId;
	}

	if (chainedAction) {
		chainedAction->run(call);
		delete chainedAction;
	}
}

ActionAfterMipsCall *Thread::getRunningCallbackAction()
{
	if (this->GetUID() == currentThread && g_inCbCount > 0) 	{
		MipsCall *call = mipsCalls.get(this->currentMipscallId);
		ActionAfterMipsCall *action = 0;
		if (call)
			action = static_cast<ActionAfterMipsCall *>(call->doAfter);

		// We don't have rtti, so check manually.
		if (!call || !action || action->actionTypeID != actionAfterMipsCall) {
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
			u32 callId = this->currentMipscallId;
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

void Thread::setReturnValue(u64 retval)
{
	if (this->GetUID() == currentThread) {
		if (g_inCbCount) {
			u32 callId = this->currentMipscallId;
			MipsCall *call = mipsCalls.get(callId);
			if (call) {
				call->setReturnValue(retval);
			} else {
				ERROR_LOG(HLE, "Failed to inject return value %08llx in thread", retval);
			}
		} else {
			currentMIPS->r[2] = retval & 0xFFFFFFFF;
			currentMIPS->r[3] = (retval >> 32) & 0xFFFFFFFF;
		}
	} else {
		context.r[2] = retval & 0xFFFFFFFF;
		context.r[3] = (retval >> 32) & 0xFFFFFFFF;
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
			__KernelChangeReadyState(this, this->GetUID(), true);

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
	SceUID oldUID = 0;
	const char *oldName = "(none)";

	Thread *cur = __GetCurrentThread();
	if (cur)  // It might just have been deleted.
	{
		__KernelSaveContext(&cur->context, (cur->nt.attr & PSP_THREAD_ATTR_VFPU) != 0);
		oldPC = currentMIPS->pc;
		oldUID = cur->GetUID();

		// Profile on Windows shows this takes time, skip it.
		if (DEBUG_LEVEL <= MAX_LOGLEVEL)
			oldName = cur->GetName();

		// Normally this is taken care of in __KernelNextThread().
		if (cur->isRunning())
			__KernelChangeReadyState(cur, oldUID, true);
	}

	if (target)
	{
		currentThread = target->GetUID();
		hleCurrentThreadName = target->nt.name;
		__KernelChangeReadyState(target, currentThread, false);
		target->nt.status = (target->nt.status | THREADSTATUS_RUNNING) & ~THREADSTATUS_READY;

		__KernelLoadContext(&target->context, (target->nt.attr & PSP_THREAD_ATTR_VFPU) != 0);
	}
	else
	{
		currentThread = 0;
		hleCurrentThreadName = NULL;
	}

	bool fromIdle = oldUID == threadIdleID[0] || oldUID == threadIdleID[1];
	bool toIdle = currentThread == threadIdleID[0] || currentThread == threadIdleID[1];
	if (!(fromIdle && toIdle))
	{
		DEBUG_LOG(HLE,"Context switched: %s -> %s (%s) (%i - pc: %08x -> %i - pc: %08x)",
			oldName, hleCurrentThreadName,
			reason,
			oldUID, oldPC, currentThread, currentMIPS->pc);
	}

	if (target)
	{
		// No longer waiting.
		target->nt.waitType = WAITTYPE_NONE;
		target->nt.waitID = 0;

		__KernelExecutePendingMipsCalls(target, true);
	}
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

	__KernelChangeReadyState(thread, thread->GetUID(), (newStatus & THREADSTATUS_READY) != 0);
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

void __KernelCallAddress(Thread *thread, u32 entryPoint, Action *afterAction, const u32 args[], int numargs, bool reschedAfter, SceUID cbId)
{
	_dbg_assert_msg_(HLE, numargs <= 6, "MipsCalls can only take 6 args.");

	if (thread) {
		ActionAfterMipsCall *after = (ActionAfterMipsCall *) __KernelCreateAction(actionAfterMipsCall);
		after->chainedAction = afterAction;
		after->threadID = thread->GetUID();
		after->status = thread->nt.status;
		after->waitType = thread->nt.waitType;
		after->waitID = thread->nt.waitID;
		after->waitInfo = thread->waitInfo;
		after->isProcessingCallbacks = thread->isProcessingCallbacks;
		after->currentCallbackId = thread->currentCallbackId;

		afterAction = after;

		if (thread->nt.waitType != WAITTYPE_NONE) {
			// If it's a callback, tell the wait to stop.
			if (waitTypeFuncs[thread->nt.waitType].beginFunc != NULL && cbId > 0) {
				waitTypeFuncs[thread->nt.waitType].beginFunc(after->threadID, thread->currentCallbackId);
			}

			// Release thread from waiting
			thread->nt.waitType = WAITTYPE_NONE;
		}

		__KernelChangeThreadState(thread, THREADSTATUS_READY);
	}

	MipsCall *call = new MipsCall();
	call->entryPoint = entryPoint;
	for (int i = 0; i < numargs; i++) {
		call->args[i] = args[i];
	}
	call->numArgs = (int) numargs;
	call->doAfter = afterAction;
	call->tag = "callAddress";
	call->cbId = cbId;

	u32 callId = mipsCalls.add(call);

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

void __KernelDirectMipsCall(u32 entryPoint, Action *afterAction, u32 args[], int numargs, bool reschedAfter)
{
	__KernelCallAddress(__GetCurrentThread(), entryPoint, afterAction, args, numargs, reschedAfter, 0);
}

void __KernelExecuteMipsCallOnCurrentThread(u32 callId, bool reschedAfter)
{
	Thread *cur = __GetCurrentThread();
	if (cur == NULL)
	{
		ERROR_LOG(HLE, "__KernelExecuteMipsCallOnCurrentThread(): Bad current thread");
		return;
	}

	if (g_inCbCount > 0) {
		WARN_LOG_REPORT(HLE, "__KernelExecuteMipsCallOnCurrentThread(): Already in a callback!");
	}
	DEBUG_LOG(HLE, "Executing mipscall %i", callId);
	MipsCall *call = mipsCalls.get(callId);

	// Save the few regs that need saving
	call->savedPc = currentMIPS->pc;
	call->savedRa = currentMIPS->r[MIPS_REG_RA];
	call->savedV0 = currentMIPS->r[MIPS_REG_V0];
	call->savedV1 = currentMIPS->r[MIPS_REG_V1];
	call->savedIdRegister = currentMIPS->r[MIPS_REG_CALL_ID];
	call->savedId = cur->currentMipscallId;
	call->reschedAfter = reschedAfter;

	// Set up the new state
	currentMIPS->pc = call->entryPoint;
	currentMIPS->r[MIPS_REG_RA] = __KernelMipsCallReturnAddress();
	// We put this two places in case the game overwrites it.
	// We may want it later to "inject" return values.
	currentMIPS->r[MIPS_REG_CALL_ID] = callId;
	cur->currentMipscallId = callId;
	for (int i = 0; i < call->numArgs; i++) {
		currentMIPS->r[MIPS_REG_A0 + i] = call->args[i];
	}

	if (call->cbId != 0)
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

	u32 callId = cur->currentMipscallId;
	if (currentMIPS->r[MIPS_REG_CALL_ID] != callId)
		WARN_LOG_REPORT(HLE, "__KernelReturnFromMipsCall(): s0 is %08x != %08x", currentMIPS->r[MIPS_REG_CALL_ID], callId);

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
	cur->currentMipscallId = call->savedId;

	if (call->cbId != 0)
		g_inCbCount--;
	currentCallbackThreadID = 0;

	if (cur->nt.waitType != WAITTYPE_NONE)
	{
		if (waitTypeFuncs[cur->nt.waitType].endFunc != NULL && call->cbId > 0)
			waitTypeFuncs[cur->nt.waitType].endFunc(cur->GetUID(), cur->currentCallbackId, currentMIPS->r[MIPS_REG_V0]);
	}

	// yeah! back in the real world, let's keep going. Should we process more callbacks?
	if (!__KernelExecutePendingMipsCalls(cur, call->reschedAfter))
	{
		// Sometimes, we want to stay on the thread.
		int threadReady = cur->nt.status & (THREADSTATUS_READY | THREADSTATUS_RUNNING);
		if (call->reschedAfter || threadReady == 0)
			__KernelReSchedule("return from callback");
	}

	delete call;
}

// First arg must be current thread, passed to avoid perf cost of a lookup.
bool __KernelExecutePendingMipsCalls(Thread *thread, bool reschedAfter)
{
	_dbg_assert_msg_(HLE, thread->GetUID() == __KernelGetCurThread(), "__KernelExecutePendingMipsCalls() should be called only with the current thread.");

	if (thread->pendingMipsCalls.empty()) {
		// Nothing to do
		return false;
	}

	if (__CanExecuteCallbackNow(thread))
	{
		// Pop off the first pending mips call
		u32 callId = thread->pendingMipsCalls.front();
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

	const u32 args[] = {(u32) cb->nc.notifyCount, (u32) cb->nc.notifyArg, cb->nc.commonArgument};

	// Clear the notify count / arg
	cb->nc.notifyCount = 0;
	cb->nc.notifyArg = 0;

	ActionAfterCallback *action = (ActionAfterCallback *) __KernelCreateAction(actionAfterCallback);
	if (action != NULL)
		action->setCallback(cbId);
	else
		ERROR_LOG(HLE, "Something went wrong creating a restore action for a callback.");

	__KernelCallAddress(thread, cb->nc.entrypoint, action, args, 3, reschedAfter, cbId);
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
			if (currentMIPS->r[MIPS_REG_V0] != 0)
			{
				DEBUG_LOG(HLE, "ActionAfterCallback::run(): Callback returned non-zero, gets deleted!");
				kernelObjects.Destroy<Callback>(cbId);
			}
		}
	}
}

bool __KernelCurHasReadyCallbacks() {
	if (readyCallbacksCount == 0)
		return false;

	Thread *thread = __GetCurrentThread();
	for (int i = 0; i < THREAD_CALLBACK_NUM_TYPES; i++) {
		if (thread->readyCallbacks[i].size()) {
			return true;
		}
	}

	return false;
}

// Check callbacks on the current thread only.
// Returns true if any callbacks were processed on the current thread.
bool __KernelCheckThreadCallbacks(Thread *thread, bool force)
{
	if (!thread || (!thread->isProcessingCallbacks && !force))
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
		ERROR_LOG_REPORT(HLE, "readyCallbacksCount became negative: %i", readyCallbacksCount);
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
		return __KernelExecutePendingMipsCalls(__GetCurrentThread(), true);
	return processed;
}

bool __KernelForceCallbacks()
{
	// Let's not check every thread all the time, callbacks are fairly uncommon.
	if (readyCallbacksCount == 0) {
		return false;
	}
	if (readyCallbacksCount < 0) {
		ERROR_LOG_REPORT(HLE, "readyCallbacksCount became negative: %i", readyCallbacksCount);
	}

	Thread *curThread = __GetCurrentThread();	

	bool callbacksProcessed = __KernelCheckThreadCallbacks(curThread, true);
	if (callbacksProcessed)
		__KernelExecutePendingMipsCalls(curThread, false);

	return callbacksProcessed;
}

// Not wrapped because it has special return logic.
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

void __KernelRegisterWaitTypeFuncs(WaitType type, WaitBeginCallbackFunc beginFunc, WaitEndCallbackFunc endFunc)
{
	waitTypeFuncs[type].beginFunc = beginFunc;
	waitTypeFuncs[type].endFunc = endFunc;
}

std::vector<DebugThreadInfo> GetThreadsInfo()
{
	std::vector<DebugThreadInfo> threadList;

	u32 error;
	for (std::vector<SceUID>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++)
	{
		Thread *t = kernelObjects.Get<Thread>(*iter, error);
		if (!t)
			continue;

		DebugThreadInfo info;
		info.id = *iter;
		strncpy(info.name,t->GetName(),KERNELOBJECT_MAX_NAME_LENGTH);
		info.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
		info.status = t->nt.status;
		info.entrypoint = t->nt.entrypoint;
		if(*iter == currentThread)
			info.curPC = currentMIPS->pc;
		else
			info.curPC = t->context.pc;
		info.isCurrent = (*iter == currentThread);
		threadList.push_back(info);
	}

	return threadList;
}

void __KernelChangeThreadState(SceUID threadId, ThreadStatus newStatus)
{
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadId, error);
	if (!t)
		return;

	__KernelChangeThreadState(t, newStatus);
}

int hleLoadExecForUser_362A956B()
{
	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(registeredExitCbId, error);
	if (!cb) {
		WARN_LOG(HLE, "LoadExecForUser_362A956B() : registeredExitCbId not found 0x%x", registeredExitCbId);
		return SCE_KERNEL_ERROR_UNKNOWN_CBID;
	}
	int cbArg = cb->nc.commonArgument;
	if (!Memory::IsValidAddress(cbArg)) {
		WARN_LOG(HLE, "LoadExecForUser_362A956B() : invalid address for cbArg (0x%08X)", cbArg);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}
	int unknown1 = Memory::Read_U32(cbArg - 8);
	if (unknown1 < 0 || unknown1 >= 4) {
		WARN_LOG(HLE, "LoadExecForUser_362A956B() : invalid value unknown1 (0x%08X)", unknown1);
		return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
	}
	int parameterArea = Memory::Read_U32(cbArg - 4);
	if (!Memory::IsValidAddress(parameterArea)) {
		WARN_LOG(HLE, "LoadExecForUser_362A956B() : invalid address for parameterArea on userMemory  (0x%08X)", parameterArea);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}
	
	int size = Memory::Read_U32(parameterArea);
	if (size < 12) {
		WARN_LOG(HLE, "LoadExecForUser_362A956B() : invalid parameterArea size %d", size);
		return SCE_KERNEL_ERROR_ILLEGAL_SIZE;
	}
	Memory::Write_U32(0, parameterArea + 4);
	Memory::Write_U32(-1, parameterArea + 8);
	return 0;
}
