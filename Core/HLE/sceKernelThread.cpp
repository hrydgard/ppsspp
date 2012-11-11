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

#include "HLE.h"
#include "HLETables.h"
#include "../MIPS/MIPSInt.h"
#include "../MIPS/MIPSCodeUtils.h"
#include "../MIPS/MIPS.h"
#include "../../Core/CoreTiming.h"
#include "../../Core/MemMap.h"
#include "../../Common/Action.h"

#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"
#include "sceKernelModule.h"
#include "sceKernelInterrupt.h"


enum {
  ERROR_KERNEL_THREAD_ALREADY_DORMANT                 = 0x800201a2,
  ERROR_KERNEL_THREAD_ALREADY_SUSPEND                 = 0x800201a3,
  ERROR_KERNEL_THREAD_IS_NOT_DORMANT                  = 0x800201a4,
  ERROR_KERNEL_THREAD_IS_NOT_SUSPEND                  = 0x800201a5,
  ERROR_KERNEL_THREAD_IS_NOT_WAIT                     = 0x800201a6,
};

enum
{
  PSP_THREAD_ATTR_USER = 0x80000000,
  PSP_THREAD_ATTR_USBWLAN = 0xa0000000,        
  PSP_THREAD_ATTR_VSH = 0xc0000000,
  PSP_THREAD_ATTR_KERNEL = 0x00001000,
  PSP_THREAD_ATTR_VFPU = 0x00004000,           // TODO: Should not bother saving VFPU context except when switching between two thread that has this attribute
  PSP_THREAD_ATTR_SCRATCH_SRAM = 0x00008000,   // Save/restore scratch as part of context???
  PSP_THREAD_ATTR_NO_FILLSTACK = 0x00100000,   // TODO: No filling of 0xff
  PSP_THREAD_ATTR_CLEAR_STACK = 0x00200000,    // TODO: Clear thread stack when deleted
};

const char *waitTypeStrings[] = 
{
  "NONE",
  "Sleep",
  "Delay",
  "Sema",
  "EventFlag",
  "Mbx",
  "Vpl",
  "Fpl",
  "",
  "ThreadEnd",   // These are nonstandard wait types
  "AudioChannel",
  "Umd",
  "Vblank",
  "Mutex",
};

struct SceKernelSysClock {
	u32 low;
	u32 hi;
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
      ERROR_LOG(HLE, "Failed to allocate stack for thread");
      return false;
    }
    // Fill the stack.
    Memory::Memset(stackBlock, 0xFF, stackSize);
		context.r[MIPS_REG_SP] = stackBlock + stackSize;
		nt.initialStack = context.r[MIPS_REG_SP];
    nt.stackSize = stackSize;
    // What's this 512?
		context.r[MIPS_REG_K0] = context.r[MIPS_REG_SP] - 512;
		context.r[MIPS_REG_SP] -= 512;
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

	~Thread()
	{
		FreeStack();
	}

	void setReturnValue(u32 retval);

	// Utils
	bool isRunning() const { return (nt.status & THREADSTATUS_RUNNING) != 0; }
	bool isStopped() const { return (nt.status & THREADSTATUS_DORMANT) != 0; }
	bool isReady() const { return (nt.status & THREADSTATUS_DORMANT) != 0; }
	bool isWaiting() const { return (nt.status & THREADSTATUS_WAIT) != 0; }
	bool isSuspended() const { return (nt.status & THREADSTATUS_SUSPEND) != 0; }
	
	NativeThread nt;

	ThreadWaitInfo waitInfo;
	bool sleeping;

	bool isProcessingCallbacks;

	ThreadContext context;

	std::set<SceUID> registeredCallbacks[THREAD_CALLBACK_NUM_TYPES];
	std::list<SceUID> readyCallbacks[THREAD_CALLBACK_NUM_TYPES];
	
	std::list<int> pendingMipsCalls;

	u32 stackBlock;
};

void __KernelExecuteMipsCallOnCurrentThread(int callId);


int g_inCbCount = 0;

Thread *__KernelCreateThread(SceUID &id, SceUID moduleID, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr);

//////////////////////////////////////////////////////////////////////////
//STATE BEGIN
//////////////////////////////////////////////////////////////////////////
Thread *currentThread;
u32 idleThreadHackAddr;
u32 threadReturnHackAddr;
u32 cbReturnHackAddr;
u32 intReturnHackAddr;
std::vector<Thread *> threadqueue; //Change to SceUID

SceUID threadIdleID[2];

int eventScheduledWakeup;

bool dispatchEnabled = true;


// This seems nasty
SceUID curModule;

//////////////////////////////////////////////////////////////////////////
//STATE END
//////////////////////////////////////////////////////////////////////////


// TODO: Should move to this wrapper so we can keep the current thread as a SceUID instead
// of a dangerous raw pointer.
Thread *__GetCurrentThread() {
	return currentThread;
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

void __KernelThreadingInit()
{
  u32 blockSize = 4 * 4 + 4 * 2 * 3;  // One 16-byte thread plus 3 8-byte "hacks"

	dispatchEnabled = true;

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

  // Create the two idle threads, as well. With the absolute minimal possible priority.
  // 4096 stack size - don't know what the right value is. Hm, if callbacks are ever to run on these threads...
  __KernelCreateThread(threadIdleID[0], 0, "idle0", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL);
  __KernelCreateThread(threadIdleID[1], 0, "idle1", idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL);
  // These idle threads are later started in LoadExec, which calls __KernelStartIdleThreads below.
}

void __KernelStartIdleThreads()
{
  for (int i = 0; i < 2; i++)
  {
    u32 error;
    Thread *t = kernelObjects.Get<Thread>(threadIdleID[i], error);
    t->nt.gpreg = __KernelGetModuleGP(curModule);
    t->context.r[MIPS_REG_GP] = t->nt.gpreg;
    //t->context.pc += 4;  // ADJUSTPC
    t->nt.status = THREADSTATUS_READY;
  }
}

void _sceKernelIdle()
{
  CoreTiming::Idle();
  // Advance must happen between Idle and Reschedule, so that threads that were waiting for something
  // that was triggered at the end of the Idle period must get a chance to be scheduled.
  // get a chance to be rescheduled.
  CoreTiming::Advance();

  // In Advance, we might trigger an interrupt such as vblank.
  // If we end up in an interrupt, we don't want to reschedule.
  // However, we have to reschedule... damn.
  __KernelReSchedule("idle");
}

void __KernelThreadingShutdown()
{
	kernelMemory.Free(threadReturnHackAddr);
	threadReturnHackAddr = 0;
  cbReturnHackAddr = 0;
	currentThread = 0;
	intReturnHackAddr = 0;
	threadqueue.clear();
}

u32 __KernelGetWaitValue(SceUID threadID, u32 &error)
{
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		return t->waitInfo.waitValue;
	}
	else
	{
		ERROR_LOG(HLE, "__KernelGetWaitValue ERROR: thread %i", threadID);
		return 0;
	}
}

void sceKernelReferThreadStatus()
{
	SceUID threadID = PARAM(0);
	if (threadID == 0)
		threadID = __KernelGetCurThread();

	u32 error;
	Thread *t = kernelObjects.Get<Thread>(threadID, error);
	if (t)
	{
		DEBUG_LOG(HLE,"sceKernelReferThreadStatus(%i, %08x)", threadID, PARAM(1));
		void *outptr = (void*)Memory::GetPointer(PARAM(1));
		u32 wantedSize = *(u32 *)outptr;
		u32 sz = sizeof(NativeThread);
		if (wantedSize) {
			t->nt.nativeSize = sz = std::min(sz, wantedSize);
		}
		memcpy(outptr, &(t->nt), sz);
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelReferThreadStatus Error %08x", error);
		RETURN(error);
	}
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
    if (t->nt.status == THREADSTATUS_DORMANT)  // TODO: can be dormant before starting, too, need to avoid that
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

void sceKernelGetThreadmanIdType()
{
  SceUID uid = PARAM(0);
  int type;
  if (kernelObjects.GetIDType(uid, &type))
  {
    RETURN(type);
    DEBUG_LOG(HLE, "%i=sceKernelGetThreadmanIdType(%i)", type, uid);
  }
  else
  {
    ERROR_LOG(HLE, "sceKernelGetThreadmanIdType(%i) - FAILED", uid);
    RETURN(SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT);
  }
}

// Saves the current CPU context
void __KernelSaveContext(ThreadContext *ctx)
{
	for (int i = 0; i < 32; i++)
	{
		ctx->r[i] = currentMIPS->r[i];
		ctx->f[i] = currentMIPS->f[i];
	}
	for (int i=0; i<128; i++)
	{
		ctx->v[i] = currentMIPS->v[i];
	}
	for (int i=0; i<16; i++)
	{
		ctx->vfpuCtrl[i] = currentMIPS->vfpuCtrl[i];
	}
	ctx->hi = currentMIPS->hi;
	ctx->lo = currentMIPS->lo;
	ctx->pc = currentMIPS->pc;
	ctx->fpcond = currentMIPS->fpcond;
  // ctx->fcr0 = currentMIPS->fcr0;
  // ctx->fcr31 = currentMIPS->fcr31;

  // TODO: Make VFPU saving optional/delayed, only necessary between VFPU-attr-marked threads
}

// Loads a CPU context
void __KernelLoadContext(ThreadContext *ctx)
{
	for (int i=0; i<32; i++)
	{
		currentMIPS->r[i] = ctx->r[i];
		currentMIPS->f[i] = ctx->f[i];
	}
	for (int i=0; i<128; i++)
	{
		currentMIPS->v[i] = ctx->v[i];
	}
	for (int i=0; i<15; i++)
	{
		currentMIPS->vfpuCtrl[i] = ctx->vfpuCtrl[i];
	}
	currentMIPS->hi = ctx->hi;
	currentMIPS->lo = ctx->lo;
	currentMIPS->pc = ctx->pc;
	currentMIPS->fpcond = ctx->fpcond;
  // currentMIPS->fcr0 = ctx->fcr0;
  // currentMIPS->fcr31 = ctx->fcr31;
}

// DANGEROUS
// Only run when you can safely accept a context switch
// Triggers a waitable event, that is, it wakes up all threads that waits for it
// If any changes were made, it will context switch
bool __KernelTriggerWait(WaitType type, int id, bool dontSwitch)
{
	bool doneAnything = false;

	for (std::vector<Thread *>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++)
	{
		Thread *t = *iter;
		if (t->nt.status & THREADSTATUS_WAIT)
		{
			if (t->nt.waitType == type && t->nt.waitID == id)
			{
				// This threads is waiting for the triggered object
				t->nt.status &= ~THREADSTATUS_WAIT;
				if (t->nt.status == 0)
        {
					t->nt.status = THREADSTATUS_READY;
        }
				// Non-waiting threads do not process callbacks.
				t->isProcessingCallbacks = false;
				doneAnything = true;
			}
		}
	}

//  if (doneAnything)     // lumines?
  {
    if (!dontSwitch)
    {
      // TODO: time waster
      char temp[256];
      sprintf(temp, "resumed from wait %s", waitTypeStrings[(int)type]);
      __KernelReSchedule(temp);
    }
  }
	return true;
}

u32 __KernelResumeThreadFromWait(SceUID threadID)
{
  u32 error;
  Thread *t = kernelObjects.Get<Thread>(threadID, error);
  if (t)
  {
    t->nt.status &= ~THREADSTATUS_WAIT;
    if (!(t->nt.status & (THREADSTATUS_SUSPEND | THREADSTATUS_WAIT)))
      t->nt.status |= THREADSTATUS_READY;
		t->isProcessingCallbacks = false;
    return 0;
  }
  else
  {
    ERROR_LOG(HLE, "__KernelResumeThreadFromWait(%d): bad thread: %08x", threadID, error);
    return error;
  }
}

// makes the current thread wait for an event
void __KernelWaitCurThread(WaitType type, SceUID waitID, u32 waitValue, int timeout, bool processCallbacks)
{
	currentThread->nt.waitID = waitID;
	currentThread->nt.waitType = type;
	__KernelChangeThreadState(currentThread, THREADSTATUS_WAIT);
	currentThread->nt.numReleases++;
	currentThread->waitInfo.waitValue = waitValue;
  if (timeout)
  {
    // TODO:
  }

	RETURN(0); //pretend all went OK

  // TODO: time waster
  char temp[256];
  sprintf(temp, "started wait %s", waitTypeStrings[(int)type]);
  
	__KernelReSchedule(processCallbacks, temp);
	// TODO: Remove thread from Ready queue?
}

void hleScheduledWakeup(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;
	__KernelTriggerWait(WAITTYPE_DELAY, threadID, true);
}

void __KernelScheduleWakeup(SceUID threadID, int usFromNow)
{
	CoreTiming::ScheduleEvent(usToCycles(usFromNow), eventScheduledWakeup, threadID);
}

void __KernelRemoveFromThreadQueue(Thread *t)
{
  for (size_t i = 0; i < threadqueue.size(); i++)
  {
    if (threadqueue[i] == t)
    {
			DEBUG_LOG(HLE, "Deleted thread %p (%i) from thread queue", t, t->GetUID());
      threadqueue.erase(threadqueue.begin() + i);
      return;
    }
  }
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

	for (size_t i = 0; i < threadqueue.size(); i++)
	{
		next = (next + 1) % threadqueue.size();

		Thread *t = threadqueue[next];
		if (t->nt.currentPriority < prio)
		{
			if (t->nt.status & THREADSTATUS_READY)
			{
				bestthread = next;
				prio = t->nt.currentPriority;
			}
		}
	}

	if (bestthread != -1)
		return threadqueue[bestthread];
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
	Thread *thread = currentThread;
	if (doCallbacks)
	{
		if (thread)
			thread->isProcessingCallbacks = doCallbacks;
		__KernelCheckCallbacks();
	}
	__KernelReSchedule(reason);
	if (doCallbacks && thread == currentThread) {
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
  u32 diff = (u32)abs((s64)t->stackBlock - (s64)currentMIPS->r[MIPS_REG_SP]);
  ERROR_LOG(HLE, "%i=sceKernelCheckThreadStack()", diff);
	RETURN(diff); //Blatant lie
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
  vfpuCtrl[VFPU_CTRL_DPREFIX] = 0x0;
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

Thread *__KernelCreateThread(SceUID &id, SceUID moduleID, const char *name, u32 entryPoint, u32 priority, int stacksize, u32 attr)
{
	Thread *t = new Thread;
	id = kernelObjects.Create(t);

	threadqueue.push_back(t);

  t->context.reset();

	t->context.hi = 0;
	t->context.lo = 0;
	t->context.pc = entryPoint;
	memset(&t->nt, 0xCD, sizeof(t->nt));

  t->nt.entrypoint = entryPoint;
  t->nt.nativeSize = sizeof(t->nt);
  t->nt.attr = attr;
  t->nt.initialPriority = t->nt.currentPriority = priority;
	t->nt.stackSize = stacksize;
	t->nt.status = THREADSTATUS_DORMANT;
	t->nt.waitType = WAITTYPE_NONE;
	t->nt.waitID = 0;
	memset(&t->waitInfo, 0, sizeof(t->waitInfo));
	t->nt.exitStatus = 0;
	t->nt.numInterruptPreempts = 0;
	t->nt.numReleases = 0;
	t->nt.numThreadPreempts = 0;
	t->nt.runForClocks.low = 0;
	t->nt.runForClocks.hi = 0;
	t->nt.wakeupCount = 0;
	t->isProcessingCallbacks = false;
  if (moduleID)
    t->nt.gpreg = __KernelGetModuleGP(moduleID); 
  else
    t->nt.gpreg = 0;  // sceKernelStartThread will take care of this.

	strncpy(t->nt.name, name, 32);
	t->context.r[MIPS_REG_RA] = threadReturnHackAddr; //hack! TODO fix
	t->AllocateStack(t->nt.stackSize);  // can change the stacksize!
	return t;
}

void __KernelSetupRootThread(SceUID moduleID, int args, const char *argp, int prio, int stacksize, int attr) 
{
	curModule = moduleID;
	//grab mips regs
	SceUID id;
	currentThread = __KernelCreateThread(id, moduleID, "root", currentMIPS->pc, prio, stacksize, attr);
	currentThread->nt.status = THREADSTATUS_READY; // do not schedule

	strcpy(currentThread->nt.name, "root");

	__KernelLoadContext(&currentThread->context);
	mipsr4k.r[MIPS_REG_A0] = args;
	mipsr4k.r[MIPS_REG_SP] -= 256;
	u32 location = mipsr4k.r[MIPS_REG_SP];
	mipsr4k.r[MIPS_REG_A1] = location;
	for (int i=0; i<args; i++)
		Memory::Write_U8(argp[i], location+i); 
}


void sceKernelCreateThread()
{
	u32 nameAddr = PARAM(0);
	const char *threadName = Memory::GetCharPointer(nameAddr);
	u32 entry = PARAM(1);
	u32 prio  = PARAM(2);
	int stacksize = PARAM(3);
	u32 attr  = PARAM(4);
	//ignore PARAM(5) 

  // HACK! Kill super big stacks.
  // if (stacksize > 0x4000) stacksize = 0x4000;

	SceUID id;
	__KernelCreateThread(id, curModule, threadName, entry, prio, stacksize, attr);
	INFO_LOG(HLE,"%i = sceKernelCreateThread(name=\"%s\", entry= %08x, stacksize=%i )", id, threadName, entry, stacksize);
	RETURN(id);
}


u32 sceKernelStartThread()
{
	int threadToStartID = PARAM(0);
	u32 argSize = PARAM(1);
	u32 argBlockPtr = PARAM(2);

	if (threadToStartID != currentThread->GetUID())
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

		startThread->nt.status = THREADSTATUS_READY;
		u32 sp = startThread->context.r[MIPS_REG_SP];
		if (argBlockPtr)
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
  INFO_LOG(HLE,"sceKernelGetThreadStackFreeSize(%i)", threadID);
  u32 error;
  Thread *thread = kernelObjects.Get<Thread>(threadID, error);

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
	INFO_LOG(HLE,"__KernelReturnFromThread : %s", currentThread->GetName());
	// TEMPORARY HACK: kill the stack of the root thread early:
	if (!strcmp(currentThread->GetName(), "root")) {
		currentThread->FreeStack();
	}

	currentThread->nt.exitStatus = currentThread->context.r[2];
	currentThread->nt.status = THREADSTATUS_DORMANT;

	// TODO: Need to remove the thread from any ready queues.

	// Find threads that waited for me
	// Wake them
	if (!__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread()))
		__KernelReSchedule("return from thread");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitThread()
{
	ERROR_LOG(HLE,"sceKernelExitThread FAKED");
  currentThread->nt.status = THREADSTATUS_DORMANT;
  currentThread->nt.exitStatus = PARAM(0);
	//Find threads that waited for me
	// Wake them
	if (!__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread()))
		__KernelReSchedule("exited thread");

	// The stack will be deallocated when the thread is deleted.
}

void _sceKernelExitThread()
{
  ERROR_LOG(HLE,"_sceKernelExitThread FAKED");
  currentThread->nt.status = THREADSTATUS_DORMANT;
  currentThread->nt.exitStatus = PARAM(0);
  //Find threads that waited for this one
  // Wake them
  if (!__KernelTriggerWait(WAITTYPE_THREADEND, __KernelGetCurThread()))
    __KernelReSchedule("exit-deleted thread");

	// The stack will be deallocated when the thread is deleted.
}

void sceKernelExitDeleteThread()
{
  int threadHandle = __KernelGetCurThread();
  u32 error;
  Thread *t = kernelObjects.Get<Thread>(threadHandle, error);
  if (t)
  {
    ERROR_LOG(HLE,"sceKernelExitDeleteThread()");
    currentThread->nt.status = THREADSTATUS_DORMANT;
    currentThread->nt.exitStatus = PARAM(0);
		//userMemory.Free(currentThread->stackBlock);
		currentThread->stackBlock = -1;

    __KernelRemoveFromThreadQueue(t);
    currentThread = 0;

    RETURN(kernelObjects.Destroy<Thread>(threadHandle));

    __KernelTriggerWait(WAITTYPE_THREADEND, threadHandle);
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
	__KernelReSchedule("rotatethreadreadyqueue");
}

void sceKernelDeleteThread()
{
	int threadHandle = PARAM(0);
	if (threadHandle != currentThread->GetUID())
	{
		//TODO: remove from threadqueue!
		DEBUG_LOG(HLE,"sceKernelDeleteThread(%i)",threadHandle);
		
    u32 error;
    Thread *t = kernelObjects.Get<Thread>(threadHandle, error);
    if (t)
    {
      __KernelRemoveFromThreadQueue(t);

      RETURN(kernelObjects.Destroy<Thread>(threadHandle));

      __KernelTriggerWait(WAITTYPE_THREADEND, threadHandle);

      //TODO: should we really reschedule here?
      //if (!__KernelTriggerWait(WAITTYPE_THREADEND, threadHandle))
      //  __KernelReSchedule("thread deleted");
    }
	}
	else
	{
		ERROR_LOG(HLE, "Thread \"%s\" tries to delete itself! :(",currentThread->GetName());
		RETURN(-1);
	}
}

void sceKernelTerminateDeleteThread()
{
	int threadno = PARAM(0);
	if (threadno != currentThread->GetUID())
	{
		//TODO: remove from threadqueue!
		INFO_LOG(HLE,"sceKernelTerminateDeleteThread(%i)",threadno);
		RETURN(0); //kernelObjects.Destroy<Thread>(threadno));

    //TODO: should we really reschedule here?
		if (!__KernelTriggerWait(WAITTYPE_THREADEND, threadno))
			__KernelReSchedule("termdeletethread");
	}
	else
	{
		ERROR_LOG(HLE, "Thread \"%s\" tries to delete itself! :(",currentThread->GetName());
		RETURN(-1);
	}
}

SceUID __KernelGetCurThread()
{
	return currentThread->GetUID();
}

void sceKernelGetThreadId()
{
	u32 retVal = currentThread->GetUID();
	DEBUG_LOG(HLE,"%i = sceKernelGetThreadId()", retVal);
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
	currentThread->nt.attr = (currentThread->nt.attr & ~clearAttr) | setAttr;
	RETURN(0);
}

void sceKernelChangeThreadPriority()
{
	int id = PARAM(0);
	if (id == 0) id = currentThread->GetUID(); //special

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
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, true);
	if (__KernelCheckCallbacks())
		__KernelExecutePendingMipsCalls();
}

void sceKernelDelayThread()
{
	u32 usec = PARAM(0);
	if (usec < 200) usec = 200;
	DEBUG_LOG(HLE,"sceKernelDelayThread(%i usec)",usec);
	SceUID curThread = __KernelGetCurThread();
	__KernelScheduleWakeup(curThread, usec);
	__KernelWaitCurThread(WAITTYPE_DELAY, curThread, 0, 0, false);
}

u32 __KernelGetThreadPrio(SceUID id)
{
	u32 error;
	Thread *thread = kernelObjects.Get<Thread>(id, error);
	if (thread)
		return thread->nt.currentPriority;
	return 0;
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
		RETURN(error);
	}
}

static void __KernelSleepThread(bool doCallbacks) {
	DEBUG_LOG(HLE,"sceKernelSleepThread() - wakeupCount decremented to %i", currentThread->nt.wakeupCount);
	if (currentThread->nt.wakeupCount > 0) {
		currentThread->nt.wakeupCount--;
		RETURN(0);
	} else {
		RETURN(0);
		__KernelWaitCurThread(WAITTYPE_SLEEP, 0, 0, 0, doCallbacks);
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
	__KernelExecutePendingMipsCalls();
}

void sceKernelWaitThreadEnd()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelWaitThreadEnd(%i)",id);
	u32 error;
	Thread *t = kernelObjects.Get<Thread>(id, error);
	if (t)
	{
		if (t->nt.status != THREADSTATUS_DORMANT) {
			__KernelWaitCurThread(WAITTYPE_THREADEND, id, 0, 0, false);
		} else {
			DEBUG_LOG(HLE,"sceKernelWaitThreadEnd - thread %i already ended. Doing nothing.", id);
		}
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelWaitThreadEnd - bad thread %i", id);
	}
	RETURN(0);
}

void sceKernelWaitThreadEndCB()
{
  SceUID id = PARAM(0);
  DEBUG_LOG(HLE,"sceKernelWaitThreadEnd(%i)",id);
  u32 error;
  Thread *t = kernelObjects.Get<Thread>(id, error);
  if (t)
  {
    if (t->nt.status != THREADSTATUS_DORMANT) {
      __KernelWaitCurThread(WAITTYPE_THREADEND, id, 0, 0, true);
    } else {
			DEBUG_LOG(HLE,"sceKernelWaitThreadEnd - thread %i already ended. Doing nothing.", id);
		}
		__KernelCheckCallbacks();
  }
  else
  {
    ERROR_LOG(HLE,"sceKernelWaitThreadEnd - bad thread %i", id);
  }
  RETURN(0);
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
	strcpy(cb->nc.name, name);

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

	__KernelNotifyCallback(THREAD_CALLBACK_USER_DEFINED, __KernelGetCurThread(), cbId, arg);
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

// Owns outstanding MIPS calls and provides a way to get them by ID.
// TODO: MipsCall structs are kinda big, try to cut down on the copying by owning pointers instead.
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

private:
	int genId() { return ++idGen_; }
	std::map<int, MipsCall *> calls_;
	int idGen_;
};

MipsCallManager mipsCalls;


class ActionAfterMipsCall : public Action
{
public:
	virtual void run();
	Thread *thread;

	// Saved thread state
	int status;
	WaitType waitType;
	int waitId;
	ThreadWaitInfo waitInfo;
	bool isProcessingCallbacks;

	Action *chainedAction;
};

void ActionAfterMipsCall::run() {
	thread->nt.status = status;
	thread->nt.waitType = waitType;
	thread->nt.waitID = waitId;
	thread->waitInfo = waitInfo;
	thread->isProcessingCallbacks = isProcessingCallbacks;

	if (chainedAction) {
		chainedAction->run();
		delete chainedAction;
	}
}


void Thread::setReturnValue(u32 retval)
{
	if (this == currentThread) {
		if (g_inCbCount) {
			int callId = currentMIPS->r[MIPS_REG_CALL_ID];
			MipsCall *call = mipsCalls.get(callId);
			if (call) {
				call->savedV0 = retval;
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

void __KernelSwitchContext(Thread *target, const char *reason) 
{
	if (currentThread)  // It might just have been deleted.
	{
		__KernelSaveContext(&currentThread->context);
		DEBUG_LOG(HLE,"Context saved (%s): %i - %s - pc: %08x", reason, currentThread->GetUID(), currentThread->GetName(), currentMIPS->pc);
	}
	currentThread = target;
	__KernelLoadContext(&currentThread->context);
	DEBUG_LOG(HLE,"Context loaded (%s): %i - %s - pc: %08x", reason, currentThread->GetUID(), currentThread->GetName(), currentMIPS->pc);

	__KernelExecutePendingMipsCalls();
}

void __KernelChangeThreadState(Thread *thread, ThreadStatus newStatus) {
	if (!thread || thread->nt.status == newStatus)
		return;

	if (!dispatchEnabled && thread == currentThread && newStatus != THREADSTATUS_RUNNING) {
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

void __KernelCallAddress(Thread *thread, u32 entryPoint, Action *afterAction, bool returnVoid, std::vector<int> args) {
	if (thread) {
		ActionAfterMipsCall *after = new ActionAfterMipsCall();
		after->chainedAction = afterAction;
		after->thread = thread;
		after->status = thread->nt.status;
		after->waitType = thread->nt.waitType;
		after->waitId = thread->nt.waitID;
		after->waitInfo = thread->waitInfo;

		afterAction = after;

		// Release thread from waiting
		thread->nt.waitType = WAITTYPE_NONE;

		__KernelChangeThreadState(thread, THREADSTATUS_READY);
	}

	MipsCall *call = new MipsCall();
	call->entryPoint = entryPoint;
	for (int i = 0; i < args.size(); i++) {
		call->args[i] = args[i];
	}
	call->numArgs = args.size();
	call->doAfter = afterAction;
	call->tag = "callAddress";
	
	int callId = mipsCalls.add(call);

	bool called = false;
	if (!thread || thread == currentThread) {
		if (__CanExecuteCallbackNow(thread)) {
			thread = currentThread;
			__KernelChangeThreadState(thread, THREADSTATUS_RUNNING);
			__KernelExecuteMipsCallOnCurrentThread(callId);
			called = true;
		}
	}

	if (!called) {
		DEBUG_LOG(HLE, "Making mipscall pending on thread");
		thread->pendingMipsCalls.push_back(callId);
	}
}
	
void __KernelExecuteMipsCallOnCurrentThread(int callId)
{
	if (g_inCbCount > 0) {
		WARN_LOG(HLE, "__KernelExecuteMipsCallOnCurrentThread: Already in a callback!");
	}
	DEBUG_LOG(HLE, "Executing mipscall %i", callId);
	MipsCall *call = mipsCalls.get(callId);
	
	// Save the few regs that need saving
	call->savedPc = currentMIPS->pc;
	call->savedRa = currentMIPS->r[MIPS_REG_RA];
	call->savedV0 = currentMIPS->r[MIPS_REG_V0];
	call->savedV1 = currentMIPS->r[MIPS_REG_V1];
	call->savedIdRegister = currentMIPS->r[MIPS_REG_CALL_ID];
	call->returnVoid = false;

	// Set up the new state
	currentMIPS->pc = call->entryPoint;
	currentMIPS->r[MIPS_REG_RA] = __KernelMipsCallReturnAddress();
	currentMIPS->r[MIPS_REG_CALL_ID] = callId;
	for (int i = 0; i < call->numArgs; i++) {
		currentMIPS->r[MIPS_REG_A0 + i] = call->args[i];
	}

	g_inCbCount++;
}

void __KernelReturnFromMipsCall()
{
	int callId = currentMIPS->r[MIPS_REG_CALL_ID];

	MipsCall *call = mipsCalls.pop(callId);

	// Value returned by the callback function
	u32 retVal = currentMIPS->r[MIPS_REG_V0];
	DEBUG_LOG(HLE,"__KernelReturnFromMipsCall(), returned %08x", retVal);

	// Should also save/restore wait state here.
	if (call->doAfter)
		call->doAfter->run();

	currentMIPS->pc = call->savedPc;
	currentMIPS->r[MIPS_REG_RA] = call->savedRa;
	currentMIPS->r[MIPS_REG_V0] = call->savedV0;
	currentMIPS->r[MIPS_REG_V1] = call->savedV1;
	currentMIPS->r[MIPS_REG_CALL_ID] = call->savedIdRegister;

	g_inCbCount--;

	// yeah! back in the real world, let's keep going. Should we process more callbacks?
	__KernelCheckCallbacks();
	if (!__KernelExecutePendingMipsCalls())
	{
		// We should definitely reschedule as we might still be asleep. - except if we came from checkcallbacks?
		__KernelReSchedule("return from callback");
	}
}

bool __KernelExecutePendingMipsCalls()
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
		__KernelExecuteMipsCallOnCurrentThread(callId);
		return true;
	}
	return false;
}


class ActionAfterCallback : public Action
{
public:
	ActionAfterCallback(SceUID cbId_) : cbId(cbId_) {}
	virtual void run();
	SceUID cbId;
};

// Executes the callback, when it next is context switched to.
void __KernelRunCallbackOnThread(SceUID cbId, Thread *thread)
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

	Action *action = new ActionAfterCallback(cbId);
	__KernelCallAddress(thread, cb->nc.entrypoint, action, false, args);
}

void ActionAfterCallback::run() {
	if (cbId != -1) {
		u32 error;
		Callback *cb = kernelObjects.Get<Callback>(cbId, error);
		if (cb)
		{
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
bool __KernelCheckThreadCallbacks(Thread *thread) {
	if (!thread->isProcessingCallbacks)
		return false;

	for (int i = 0; i < THREAD_CALLBACK_NUM_TYPES; i++) {
		if (thread->readyCallbacks[i].size()) {
			SceUID readyCallback = thread->readyCallbacks[i].front();
			thread->readyCallbacks[i].pop_front();
			__KernelRunCallbackOnThread(readyCallback, thread);   // makes pending
			return true;
		}
	}
	return false;
}

// Checks for callbacks on all threads
bool __KernelCheckCallbacks() {
	// SceUID currentThread = __KernelGetCurThread();
	// currentThread->isProcessingCallbacks = true;
	// do {
		bool processed = false;

		for (std::vector<Thread *>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++) {
			Thread *thread = *iter;
			if (thread->isProcessingCallbacks && __KernelCheckThreadCallbacks(thread)) {
				processed = true;
			}
		}
	// } while (processed && currentThread == __KernelGetCurThread());
	return processed;
}

void sceKernelCheckCallback() {
	Thread *curThread = __GetCurrentThread();	

	// This thread can now process callbacks.
	curThread->isProcessingCallbacks = true;

	bool callbacksProcessed = __KernelCheckThreadCallbacks(curThread);

	// Note - same thread as above - checking callbacks may switch threads.
	curThread->isProcessingCallbacks = false;

	if (callbacksProcessed) {
		curThread->setReturnValue(1);
		ERROR_LOG(HLE,"sceKernelCheckCallback() - processed a callback.");
		__KernelExecutePendingMipsCalls();
	} else {
		RETURN(0);
		DEBUG_LOG(HLE,"sceKernelCheckCallback() - no callbacks to process, doing nothing");
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

void __KernelNotifyCallback(RegisteredCallbackType type, SceUID threadId, SceUID cbId, int notifyArg)
{
	u32 error;

	Callback *cb = kernelObjects.Get<Callback>(cbId, error);
	if (!cb) {
		// Yeah, we're screwed, this shouldn't happen.
		ERROR_LOG(HLE, "__KernelNotifyCallback - invalid callback 0");
		return;
	}
	cb->nc.notifyCount++;
	cb->nc.notifyArg = notifyArg;

	Thread *t = kernelObjects.Get<Thread>(threadId, error);
	t->readyCallbacks[type].remove(cbId);
	t->readyCallbacks[type].push_back(cbId);
}

// TODO: If cbId == -1, notify the callback ID on all threads that have it.
u32 __KernelNotifyCallbackType(RegisteredCallbackType type, SceUID cbId, int notifyArg)
{
	for (std::vector<Thread *>::iterator iter = threadqueue.begin(); iter != threadqueue.end(); iter++)	{
		Thread *t = *iter;
		for (std::set<SceUID>::iterator citer = t->registeredCallbacks[type].begin(); citer != t->registeredCallbacks[type].end(); citer++) {
			if (cbId == -1 || cbId == *citer) {
				__KernelNotifyCallback(type, t->GetUID(), *citer, notifyArg);
			}
		}
	}

	// checkCallbacks on other threads?
	return 0;
}

