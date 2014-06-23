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

#include <map>
#include <vector>

#include "base/mutex.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "Core/CoreParameter.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceGe.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

static PspGeCallbackData ge_callback_data[16];
static bool ge_used_callbacks[16] = {0};

typedef std::vector<SceUID> WaitingThreadList;
static std::map<int, WaitingThreadList> listWaitingThreads;
static WaitingThreadList drawWaitingThreads;

struct GeInterruptData
{
	int listid;
	u32 pc;
	u32 cmd;
};

template < typename T, class Alloc = std::allocator<T> >
class ThreadSafeList {
public:
	explicit ThreadSafeList(const Alloc &a = Alloc()) : list(a) {}
	explicit ThreadSafeList(std::size_t n, const T &v = T(), const Alloc &a = Alloc()) : list(n, v, a) {}
	ThreadSafeList(const std::list<T, Alloc> &other) : list(other) {}
	ThreadSafeList(const ThreadSafeList &other) {
		lock_guard guard(other.lock);
		list.assign(other.list);
	}

	template <class Iter>
	ThreadSafeList(Iter first, Iter last, const Alloc &a = Alloc()) : list(first, last, a) {}

	inline T front() const {
		lock_guard guard(lock);
		return list.front();
	}

	inline void pop_front() {
		lock_guard guard(lock);
		return list.pop_front();
	}

	inline void push_front(const T &v) {
		lock_guard guard(lock);
		return list.push_front(v);
	}

	inline T back() const {
		lock_guard guard(lock);
		return list.back();
	}

	inline void pop_back() {
		lock_guard guard(lock);
		return list.pop_back();
	}

	inline void push_back(const T &v) {
		lock_guard guard(lock);
		return list.push_back(v);
	}

	bool empty() const {
		lock_guard guard(lock);
		return list.empty();
	}

	inline void clear() {
		lock_guard guard(lock);
		return list.clear();
	}

	void DoState(PointerWrap &p) {
		lock_guard guard(lock);
		p.Do(list);
	}

private:
	mutable recursive_mutex lock;
	std::list<T, Alloc> list;
};

static ThreadSafeList<GeInterruptData> ge_pending_cb;
static int geSyncEvent;
static int geInterruptEvent;
static int geCycleEvent;

// Let's try updating 10 times per vblank.
const int geIntervalUs = 1000000 / (60 * 10);
const int geBehindThresholdUs = 1000000 / (60 * 10);

class GeIntrHandler : public IntrHandler
{
public:
	GeIntrHandler() : IntrHandler(PSP_GE_INTR) {}

	bool run(PendingInterrupt& pend)
	{
		GeInterruptData intrdata = ge_pending_cb.front();
		DisplayList* dl = gpu->getList(intrdata.listid);

		if (dl == NULL)
		{
			WARN_LOG(SCEGE, "Unable to run GE interrupt: list doesn't exist: %d", intrdata.listid);
			return false;
		}

		if (!dl->interruptsEnabled)
		{
			ERROR_LOG_REPORT(SCEGE, "Unable to run GE interrupt: list has interrupts disabled, should not happen");
			return false;
		}

		gpu->InterruptStart(intrdata.listid);

		const u32 cmd = intrdata.cmd;
		int subintr = -1;
		if (dl->subIntrBase >= 0)
		{
			switch (dl->signal)
			{
			case PSP_GE_SIGNAL_SYNC:
			case PSP_GE_SIGNAL_JUMP:
			case PSP_GE_SIGNAL_CALL:
			case PSP_GE_SIGNAL_RET:
				// Do nothing.
				break;

			case PSP_GE_SIGNAL_HANDLER_PAUSE:
				if (cmd == GE_CMD_FINISH)
					subintr = dl->subIntrBase | PSP_GE_SUBINTR_SIGNAL;
				break;

			default:
				if (cmd == GE_CMD_SIGNAL)
					subintr = dl->subIntrBase | PSP_GE_SUBINTR_SIGNAL;
				else
					subintr = dl->subIntrBase | PSP_GE_SUBINTR_FINISH;
				break;
			}
		}

		// Set the list as complete once the interrupt starts.
		// In other words, not before another interrupt finishes.
		if (dl->signal != PSP_GE_SIGNAL_HANDLER_PAUSE && cmd == GE_CMD_FINISH) {
			dl->state = PSP_GE_DL_STATE_COMPLETED;
		}

		SubIntrHandler* handler = get(subintr);
		if (handler != NULL)
		{
			DEBUG_LOG(CPU, "Entering GE interrupt handler %08x", handler->handlerAddress);
			currentMIPS->pc = handler->handlerAddress;
			u32 data = dl->subIntrToken;
			currentMIPS->r[MIPS_REG_A0] = data & 0xFFFF;
			currentMIPS->r[MIPS_REG_A1] = handler->handlerArg;
			currentMIPS->r[MIPS_REG_A2] = sceKernelGetCompiledSdkVersion() <= 0x02000010 ? 0 : intrdata.pc + 4;
			// RA is already taken care of in __RunOnePendingInterrupt

			return true;
		}

		ge_pending_cb.pop_front();
		gpu->InterruptEnd(intrdata.listid);

		// Seen in GoW.
		if (subintr >= 0)
			DEBUG_LOG(SCEGE, "Ignoring interrupt for display list %d, already been released.", intrdata.listid);
		return false;
	}

	virtual void handleResult(PendingInterrupt& pend)
	{
		GeInterruptData intrdata = ge_pending_cb.front();
		ge_pending_cb.pop_front();

		DisplayList* dl = gpu->getList(intrdata.listid);
		if (!dl->interruptsEnabled)
		{
			ERROR_LOG_REPORT(SCEGE, "Unable to finish GE interrupt: list has interrupts disabled, should not happen");
			return;
		}

		switch (dl->signal)
		{
		case PSP_GE_SIGNAL_HANDLER_SUSPEND:
			if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
			{
				// uofw says dl->state = endCmd & 0xFF;
				DisplayListState newState = static_cast<DisplayListState>(Memory::ReadUnchecked_U32(intrdata.pc - 4) & 0xFF);
				//dl->status = static_cast<DisplayListStatus>(Memory::ReadUnchecked_U32(intrdata.pc) & 0xFF);
				//if(dl->status < 0 || dl->status > PSP_GE_LIST_PAUSED)
				//	ERROR_LOG(SCEGE, "Weird DL status after signal suspend %x", dl->status);
				if (newState != PSP_GE_DL_STATE_RUNNING)
					DEBUG_LOG_REPORT(SCEGE, "GE Interrupt: newState might be %d", newState);

				if (dl->state != PSP_GE_DL_STATE_NONE && dl->state != PSP_GE_DL_STATE_COMPLETED) {
					dl->state = PSP_GE_DL_STATE_QUEUED;
				}
			}
			break;
		default:
			break;
		}

		gpu->InterruptEnd(intrdata.listid);
	}
};

void __GeExecuteSync(u64 userdata, int cyclesLate)
{
	int listid = userdata >> 32;
	GPUSyncType type = (GPUSyncType) (userdata & 0xFFFFFFFF);
	bool wokeThreads = __GeTriggerWait(type, listid);
	gpu->SyncEnd(type, listid, wokeThreads);
}

void __GeExecuteInterrupt(u64 userdata, int cyclesLate)
{
	__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_GE_INTR, PSP_INTR_SUB_NONE);
}

void __GeCheckCycles(u64 userdata, int cyclesLate)
{
	u64 geTicks = gpu->GetTickEstimate();
	if (geTicks != 0)
	{
		if (CoreTiming::GetTicks() > geTicks + usToCycles(geBehindThresholdUs)) {
			u64 diff = CoreTiming::GetTicks() - geTicks;
			gpu->SyncThread();
			CoreTiming::Advance();
		}
	}
	CoreTiming::ScheduleEvent(usToCycles(geIntervalUs), geCycleEvent, 0);
}

void __GeInit()
{
	memset(&ge_used_callbacks, 0, sizeof(ge_used_callbacks));
	ge_pending_cb.clear();
	__RegisterIntrHandler(PSP_GE_INTR, new GeIntrHandler());

	geSyncEvent = CoreTiming::RegisterEvent("GeSyncEvent", &__GeExecuteSync);
	geInterruptEvent = CoreTiming::RegisterEvent("GeInterruptEvent", &__GeExecuteInterrupt);
	geCycleEvent = CoreTiming::RegisterEvent("GeCycleEvent", &__GeCheckCycles);

	listWaitingThreads.clear();
	drawWaitingThreads.clear();

	// When we're using separate CPU/GPU threads, we need to keep them in sync.
	if (IsOnSeparateCPUThread()) {
		CoreTiming::ScheduleEvent(usToCycles(geIntervalUs), geCycleEvent, 0);
	}
}

struct GeInterruptData_v1
{
	int listid;
	u32 pc;
};

void __GeDoState(PointerWrap &p)
{
	auto s = p.Section("sceGe", 1, 2);
	if (!s)
		return;

	p.DoArray(ge_callback_data, ARRAY_SIZE(ge_callback_data));
	p.DoArray(ge_used_callbacks, ARRAY_SIZE(ge_used_callbacks));

	if (s >= 2) {
		p.Do(ge_pending_cb);
	} else {
		std::list<GeInterruptData_v1> old;
		p.Do(old);
		ge_pending_cb.clear();
		for (auto it = old.begin(), end = old.end(); it != end; ++it) {
			GeInterruptData intrdata = {it->listid, it->pc};
			intrdata.cmd = Memory::ReadUnchecked_U32(it->pc - 4) >> 24;
			ge_pending_cb.push_back(intrdata);
		}
	}

	p.Do(geSyncEvent);
	CoreTiming::RestoreRegisterEvent(geSyncEvent, "GeSyncEvent", &__GeExecuteSync);
	p.Do(geInterruptEvent);
	CoreTiming::RestoreRegisterEvent(geInterruptEvent, "GeInterruptEvent", &__GeExecuteInterrupt);
	p.Do(geCycleEvent);
	CoreTiming::RestoreRegisterEvent(geCycleEvent, "GeCycleEvent", &__GeCheckCycles);

	p.Do(listWaitingThreads);
	p.Do(drawWaitingThreads);

	// Everything else is done in sceDisplay.
}

void __GeShutdown()
{

}

// Warning: may be called from the GPU thread.
bool __GeTriggerSync(GPUSyncType type, int id, u64 atTicks)
{
	u64 userdata = (u64)id << 32 | (u64) type;
	s64 future = atTicks - CoreTiming::GetTicks();
	if (type == GPU_SYNC_DRAW)
	{
		s64 left = CoreTiming::UnscheduleThreadsafeEvent(geSyncEvent, userdata);
		if (left > future)
			future = left;
	}
	CoreTiming::ScheduleEvent_Threadsafe(future, geSyncEvent, userdata);
	return true;
}

// Warning: may be called from the GPU thread.
bool __GeTriggerInterrupt(int listid, u32 pc, u64 atTicks)
{
	GeInterruptData intrdata;
	intrdata.listid = listid;
	intrdata.pc = pc;
	intrdata.cmd = Memory::ReadUnchecked_U32(pc - 4) >> 24;

	ge_pending_cb.push_back(intrdata);

	u64 userdata = (u64)listid << 32 | (u64) pc;
	CoreTiming::ScheduleEvent_Threadsafe(atTicks - CoreTiming::GetTicks(), geInterruptEvent, userdata);
	return true;
}

void __GeWaitCurrentThread(GPUSyncType type, SceUID waitId, const char *reason)
{
	WaitType waitType;
	if (type == GPU_SYNC_DRAW) {
		drawWaitingThreads.push_back(__KernelGetCurThread());
		waitType = WAITTYPE_GEDRAWSYNC;
	} else if (type == GPU_SYNC_LIST) {
		listWaitingThreads[waitId].push_back(__KernelGetCurThread());
		waitType = WAITTYPE_GELISTSYNC;
	} else {
		ERROR_LOG_REPORT(SCEGE, "__GeWaitCurrentThread: bad wait type");
		return;
	}

	__KernelWaitCurThread(waitType, waitId, 0, 0, false, reason);
}

bool __GeTriggerWait(WaitType waitType, SceUID waitId, WaitingThreadList &waitingThreads)
{
	// TODO: Do they ever get a result other than 0?
	bool wokeThreads = false;
	for (auto it = waitingThreads.begin(), end = waitingThreads.end(); it != end; ++it)
		wokeThreads |= HLEKernel::ResumeFromWait(*it, waitType, waitId, 0);
	waitingThreads.clear();
	return wokeThreads;
}

bool __GeTriggerWait(GPUSyncType type, SceUID waitId)
{
	// We check for the old type for old savestate compatibility.
	if (type == GPU_SYNC_DRAW || (WaitType)type == WAITTYPE_GEDRAWSYNC)
		return __GeTriggerWait(WAITTYPE_GEDRAWSYNC, waitId, drawWaitingThreads);
	else if (type == GPU_SYNC_LIST || (WaitType)type == WAITTYPE_GELISTSYNC)
		return __GeTriggerWait(WAITTYPE_GELISTSYNC, waitId, listWaitingThreads[waitId]);
	else
		ERROR_LOG_REPORT(SCEGE, "__GeTriggerWait: bad wait type");
	return false;
}

u32 sceGeEdramGetAddr()
{
	u32 retVal = 0x04000000;
	DEBUG_LOG(SCEGE, "%08x = sceGeEdramGetAddr", retVal);
	hleEatCycles(150);
	return retVal;
}

u32 sceGeEdramGetSize()
{
	u32 retVal = 0x00200000;
	DEBUG_LOG(SCEGE, "%08x = sceGeEdramGetSize()", retVal);
	return retVal;
}

int __GeSubIntrBase(int callbackId)
{
	return callbackId * 2;
}

u32 sceGeListEnQueue(u32 listAddress, u32 stallAddress, int callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(SCEGE,
			"sceGeListEnQueue(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	auto optParam = PSPPointer<PspGeListArgs>::Create(optParamAddr);

	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), optParam, false);
	if ((int)listID >= 0)
		listID = 0x35000000 ^ listID;

	DEBUG_LOG(SCEGE, "List %i enqueued.", listID);
	hleEatCycles(490);
	CoreTiming::ForceCheck();
	return listID;
}

u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, int callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(SCEGE,
			"sceGeListEnQueueHead(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	auto optParam = PSPPointer<PspGeListArgs>::Create(optParamAddr);

	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), optParam, true);
	if ((int)listID >= 0)
		listID = 0x35000000 ^ listID;

	DEBUG_LOG(SCEGE, "List %i enqueued.", listID);
	hleEatCycles(480);
	CoreTiming::ForceCheck();
	return listID;
}

int sceGeListDeQueue(u32 listID)
{
	WARN_LOG(SCEGE, "sceGeListDeQueue(%08x)", listID);
	int result = gpu->DequeueList(0x35000000 ^ listID);
	hleReSchedule("dlist dequeued");
	return result;
}

int sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress)
{
	// Advance() might cause an interrupt, so defer the Advance but do it ASAP.
	// Final Fantasy Type-0 has a graphical artifact without this (timing issue.)
	hleEatCycles(190);
	CoreTiming::ForceCheck();

	DEBUG_LOG(SCEGE, "sceGeListUpdateStallAddr(dlid=%i, stalladdr=%08x)", displayListID, stallAddress);
	return gpu->UpdateStall(0x35000000 ^ displayListID, stallAddress);
}

int sceGeListSync(u32 displayListID, u32 mode) //0 : wait for completion		1:check and return
{
	DEBUG_LOG(SCEGE, "sceGeListSync(dlid=%08x, mode=%08x)", displayListID, mode);
	return gpu->ListSync(0x35000000 ^ displayListID, mode);
}

u32 sceGeDrawSync(u32 mode)
{
	//wait/check entire drawing state
	DEBUG_LOG(SCEGE, "sceGeDrawSync(mode=%d)  (0=wait for completion, 1=peek)", mode);
	return gpu->DrawSync(mode);
}

int sceGeContinue()
{
	DEBUG_LOG(SCEGE, "sceGeContinue");
	int ret = gpu->Continue();
	hleEatCycles(220);
	hleReSchedule("ge continue");
	return ret;
}

int sceGeBreak(u32 mode, u32 unknownPtr)
{
	if (mode > 1)
	{
		WARN_LOG(SCEGE, "sceGeBreak(mode=%d, unknown=%08x): invalid mode", mode, unknownPtr);
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
	// Not sure what this is supposed to be for...
	if ((int)unknownPtr < 0 || (int)unknownPtr + 16 < 0)
	{
		WARN_LOG_REPORT(SCEGE, "sceGeBreak(mode=%d, unknown=%08x): invalid ptr", mode, unknownPtr);
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;
	}
	else if (unknownPtr != 0)
		WARN_LOG_REPORT(SCEGE, "sceGeBreak(mode=%d, unknown=%08x): unknown ptr (%s)", mode, unknownPtr, Memory::IsValidAddress(unknownPtr) ? "valid" : "invalid");

	//mode => 0 : current dlist 1: all drawing
	DEBUG_LOG(SCEGE, "sceGeBreak(mode=%d, unknown=%08x)", mode, unknownPtr);
	int result = gpu->Break(mode);
	if (result >= 0 && mode == 0)
		return 0x35000000 ^ result;
	return result;
}

u32 sceGeSetCallback(u32 structAddr)
{
	DEBUG_LOG(SCEGE, "sceGeSetCallback(struct=%08x)", structAddr);

	int cbID = -1;
	for (size_t i = 0; i < ARRAY_SIZE(ge_used_callbacks); ++i)
		if (!ge_used_callbacks[i])
		{
			cbID = (int) i;
			break;
		}

	if (cbID == -1)
	{
		WARN_LOG(SCEGE, "sceGeSetCallback(): out of callback ids");
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}

	ge_used_callbacks[cbID] = true;
	Memory::ReadStruct(structAddr, &ge_callback_data[cbID]);

	int subIntrBase = __GeSubIntrBase(cbID);

	if (ge_callback_data[cbID].finish_func != 0)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH,
				ge_callback_data[cbID].finish_func, ge_callback_data[cbID].finish_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH);
	}
	if (ge_callback_data[cbID].signal_func != 0)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL,
				ge_callback_data[cbID].signal_func, ge_callback_data[cbID].signal_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL);
	}

	return cbID;
}

int sceGeUnsetCallback(u32 cbID)
{
	DEBUG_LOG(SCEGE, "sceGeUnsetCallback(cbid=%08x)", cbID);

	if (cbID >= ARRAY_SIZE(ge_used_callbacks))
	{
		WARN_LOG(SCEGE, "sceGeUnsetCallback(cbid=%08x): invalid callback id", cbID);
		return SCE_KERNEL_ERROR_INVALID_ID;
	}

	if (ge_used_callbacks[cbID])
	{
		int subIntrBase = __GeSubIntrBase(cbID);

		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH);
		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL);
	}
	else
		WARN_LOG(SCEGE, "sceGeUnsetCallback(cbid=%08x): ignoring unregistered callback id", cbID);

	ge_used_callbacks[cbID] = false;

	return 0;
}

// Points to 512 32-bit words, where we can probably layout the context however we want
// unless some insane game pokes it and relies on it...
u32 sceGeSaveContext(u32 ctxAddr)
{
	DEBUG_LOG(SCEGE, "sceGeSaveContext(%08x)", ctxAddr);
	gpu->SyncThread();

	if (gpu->BusyDrawing())
	{
		WARN_LOG(SCEGE, "sceGeSaveContext(%08x): lists in process, aborting", ctxAddr);
		// Real error code.
		return -1;
	}

	// Let's just dump gstate.
	if (Memory::IsValidAddress(ctxAddr))
	{
		gstate.Save((u32_le *)Memory::GetPointer(ctxAddr));
	}

	// This action should probably be pushed to the end of the queue of the display thread -
	// when we have one.
	return 0;
}

u32 sceGeRestoreContext(u32 ctxAddr)
{
	DEBUG_LOG(SCEGE, "sceGeRestoreContext(%08x)", ctxAddr);
	gpu->SyncThread();

	if (gpu->BusyDrawing())
	{
		WARN_LOG(SCEGE, "sceGeRestoreContext(%08x): lists in process, aborting", ctxAddr);
		return SCE_KERNEL_ERROR_BUSY;
	}

	if (Memory::IsValidAddress(ctxAddr))
	{
		gstate.Restore((u32_le *)Memory::GetPointer(ctxAddr));
	}
	ReapplyGfxState();

	return 0;
}

void __GeCopyMatrix(u32 matrixPtr, float *mtx, u32 size) {
	for (u32 i = 0; i < size / sizeof(float); ++i) {
		Memory::Write_U32(toFloat24(mtx[i]), matrixPtr + i * sizeof(float));
	}
}

int sceGeGetMtx(int type, u32 matrixPtr) {
	if (!Memory::IsValidAddress(matrixPtr)) {
		ERROR_LOG(SCEGE, "sceGeGetMtx(%d, %08x) - bad matrix ptr", type, matrixPtr);
		return -1;
	}

	INFO_LOG(SCEGE, "sceGeGetMtx(%d, %08x)", type, matrixPtr);
	switch (type) {
	case GE_MTX_BONE0:
	case GE_MTX_BONE1:
	case GE_MTX_BONE2:
	case GE_MTX_BONE3:
	case GE_MTX_BONE4:
	case GE_MTX_BONE5:
	case GE_MTX_BONE6:
	case GE_MTX_BONE7:
		{
			int n = type - GE_MTX_BONE0;
			__GeCopyMatrix(matrixPtr, gstate.boneMatrix + n * 12, 12 * sizeof(float));
		}
		break;
	case GE_MTX_TEXGEN:
		__GeCopyMatrix(matrixPtr, gstate.tgenMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_WORLD:
		__GeCopyMatrix(matrixPtr, gstate.worldMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_VIEW:
		__GeCopyMatrix(matrixPtr, gstate.viewMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_PROJECTION:
		__GeCopyMatrix(matrixPtr, gstate.projMatrix, 16 * sizeof(float));
		break;
	default:
		return SCE_KERNEL_ERROR_INVALID_INDEX;
	}
	return 0;
}

u32 sceGeGetCmd(int cmd) {
	INFO_LOG(SCEGE, "sceGeGetCmd(%i)", cmd);
	if (cmd >= 0 && cmd < (int)ARRAY_SIZE(gstate.cmdmem)) {
		return gstate.cmdmem[cmd];  // Does not mask away the high bits.
	} else {
		return SCE_KERNEL_ERROR_INVALID_INDEX;
	}
}

int sceGeGetStack(int index, u32 stackPtr) {
	WARN_LOG_REPORT(SCEGE, "sceGeGetStack(%i, %08x)", index, stackPtr);
	return gpu->GetStack(index, stackPtr);
}

u32 sceGeEdramSetAddrTranslation(int new_size) {
	bool outsideRange = new_size != 0 && (new_size < 0x200 || new_size > 0x1000);
	bool notPowerOfTwo = (new_size & (new_size - 1)) != 0;
	if (outsideRange || notPowerOfTwo) {
		WARN_LOG(SCEGE, "sceGeEdramSetAddrTranslation(%i): invalid value", new_size);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	DEBUG_LOG(SCEGE, "sceGeEdramSetAddrTranslation(%i)", new_size);
	static int EDRamWidth = 0x400;
	int last = EDRamWidth;
	EDRamWidth = new_size;
	return last;
}

const HLEFunction sceGe_user[] =
{
	{0xE47E40E4, WrapU_V<sceGeEdramGetAddr>,            "sceGeEdramGetAddr"},
	{0xAB49E76A, WrapU_UUIU<sceGeListEnQueue>,          "sceGeListEnQueue"},
	{0x1C0D95A6, WrapU_UUIU<sceGeListEnQueueHead>,      "sceGeListEnQueueHead"},
	{0xE0D68148, WrapI_UU<sceGeListUpdateStallAddr>,    "sceGeListUpdateStallAddr"},
	{0x03444EB4, WrapI_UU<sceGeListSync>,               "sceGeListSync"},
	{0xB287BD61, WrapU_U<sceGeDrawSync>,                "sceGeDrawSync"},
	{0xB448EC0D, WrapI_UU<sceGeBreak>,                  "sceGeBreak"},
	{0x4C06E472, WrapI_V<sceGeContinue>,                "sceGeContinue"},
	{0xA4FC06A4, WrapU_U<sceGeSetCallback>,             "sceGeSetCallback"},
	{0x05DB22CE, WrapI_U<sceGeUnsetCallback>,           "sceGeUnsetCallback"},
	{0x1F6752AD, WrapU_V<sceGeEdramGetSize>,            "sceGeEdramGetSize"},
	{0xB77905EA, WrapU_I<sceGeEdramSetAddrTranslation>, "sceGeEdramSetAddrTranslation"},
	{0xDC93CFEF, WrapU_I<sceGeGetCmd>,                  "sceGeGetCmd"},
	{0x57C8945B, WrapI_IU<sceGeGetMtx>,                 "sceGeGetMtx"},
	{0x438A385A, WrapU_U<sceGeSaveContext>,             "sceGeSaveContext"},
	{0x0BF608FB, WrapU_U<sceGeRestoreContext>,          "sceGeRestoreContext"},
	{0x5FB86AB0, WrapI_U<sceGeListDeQueue>,             "sceGeListDeQueue"},
	{0xE66CB92E, WrapI_IU<sceGeGetStack>,               "sceGeGetStack"},
};

void Register_sceGe_user()
{
	RegisterModule("sceGe_user", ARRAY_SIZE(sceGe_user), sceGe_user);
}
