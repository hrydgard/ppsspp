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

#include "Core/HLE/HLE.h"
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
};

static std::list<GeInterruptData> ge_pending_cb;
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

		u32 cmd = Memory::ReadUnchecked_U32(intrdata.pc - 4) >> 24;
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

		SubIntrHandler* handler = get(subintr);
		if (handler != NULL)
		{
			DEBUG_LOG(CPU, "Entering interrupt handler %08x", handler->handlerAddress);
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
					INFO_LOG_REPORT(SCEGE, "GE Interrupt: newState might be %d", newState);

				dl->state = PSP_GE_DL_STATE_RUNNING;
			}
			break;
		default:
			break;
		}

		dl->signal = PSP_GE_SIGNAL_NONE;

		gpu->InterruptEnd(intrdata.listid);
	}
};

void __GeExecuteSync(u64 userdata, int cyclesLate)
{
	int listid = userdata >> 32;
	WaitType waitType = (WaitType) (userdata & 0xFFFFFFFF);
	bool wokeThreads = __GeTriggerWait(waitType, listid);
	gpu->SyncEnd(waitType, listid, wokeThreads);
}

void __GeExecuteInterrupt(u64 userdata, int cyclesLate)
{
	int listid = userdata >> 32;
	u32 pc = userdata & 0xFFFFFFFF;

	GeInterruptData intrdata;
	intrdata.listid = listid;
	intrdata.pc     = pc;
	ge_pending_cb.push_back(intrdata);
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

void __GeDoState(PointerWrap &p)
{
	p.DoArray(ge_callback_data, ARRAY_SIZE(ge_callback_data));
	p.DoArray(ge_used_callbacks, ARRAY_SIZE(ge_used_callbacks));
	p.Do(ge_pending_cb);

	p.Do(geSyncEvent);
	CoreTiming::RestoreRegisterEvent(geSyncEvent, "GeSyncEvent", &__GeExecuteSync);
	p.Do(geInterruptEvent);
	CoreTiming::RestoreRegisterEvent(geInterruptEvent, "GeInterruptEvent", &__GeExecuteInterrupt);
	p.Do(geCycleEvent);
	CoreTiming::RestoreRegisterEvent(geCycleEvent, "GeCycleEvent", &__GeCheckCycles);

	p.Do(listWaitingThreads);
	p.Do(drawWaitingThreads);

	// Everything else is done in sceDisplay.
	p.DoMarker("sceGe");
}

void __GeShutdown()
{

}

// Warning: may be called from the GPU thread.
bool __GeTriggerSync(WaitType waitType, int id, u64 atTicks)
{
	u64 userdata = (u64)id << 32 | (u64) waitType;
	s64 future = atTicks - CoreTiming::GetTicks();
	if (waitType == WAITTYPE_GEDRAWSYNC)
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
	u64 userdata = (u64)listid << 32 | (u64) pc;
	CoreTiming::ScheduleEvent_Threadsafe(atTicks - CoreTiming::GetTicks(), geInterruptEvent, userdata);
	return true;
}

void __GeWaitCurrentThread(WaitType type, SceUID waitId, const char *reason)
{
	if (type == WAITTYPE_GEDRAWSYNC)
		drawWaitingThreads.push_back(__KernelGetCurThread());
	else if (type == WAITTYPE_GELISTSYNC)
		listWaitingThreads[waitId].push_back(__KernelGetCurThread());
	else
		ERROR_LOG_REPORT(SCEGE, "__GeWaitCurrentThread: bad wait type");

	__KernelWaitCurThread(type, waitId, 0, 0, false, reason);
}

bool __GeTriggerWait(WaitType type, SceUID waitId, WaitingThreadList &waitingThreads)
{
	// TODO: Do they ever get a result other than 0?
	bool wokeThreads = false;
	for (auto it = waitingThreads.begin(), end = waitingThreads.end(); it != end; ++it)
		wokeThreads |= HLEKernel::ResumeFromWait(*it, type, waitId, 0);
	waitingThreads.clear();
	return wokeThreads;
}

bool __GeTriggerWait(WaitType type, SceUID waitId)
{
	if (type == WAITTYPE_GEDRAWSYNC)
		return __GeTriggerWait(type, waitId, drawWaitingThreads);
	else if (type == WAITTYPE_GELISTSYNC)
		return __GeTriggerWait(type, waitId, listWaitingThreads[waitId]);
	else
		ERROR_LOG_REPORT(SCEGE, "__GeTriggerWait: bad wait type");
	return false;
}

bool __GeHasPendingInterrupt()
{
	return !ge_pending_cb.empty();
}

u32 sceGeEdramGetAddr()
{
	u32 retVal = 0x04000000;
	DEBUG_LOG(SCEGE, "%08x = sceGeEdramGetAddr", retVal);
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
	//if (!stallAddress)
	//	stallAddress = listAddress;
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), false);

	DEBUG_LOG(SCEGE, "List %i enqueued.", listID);
	//return display list ID
	return listID;
}

u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, int callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(SCEGE,
			"sceGeListEnQueueHead(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), true);

	DEBUG_LOG(SCEGE, "List %i enqueued.", listID);
	return listID;
}

int sceGeListDeQueue(u32 listID)
{
	WARN_LOG(SCEGE, "sceGeListDeQueue(%08x)", listID);
	int result = gpu->DequeueList(listID);
	hleReSchedule("dlist dequeued");
	return result;
}

int sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress)
{
	DEBUG_LOG(SCEGE, "sceGeListUpdateStallAddr(dlid=%i, stalladdr=%08x)", displayListID, stallAddress);
	hleEatCycles(190);
	CoreTiming::Advance();
	return gpu->UpdateStall(displayListID, stallAddress);
}

int sceGeListSync(u32 displayListID, u32 mode) //0 : wait for completion		1:check and return
{
	DEBUG_LOG(SCEGE, "sceGeListSync(dlid=%08x, mode=%08x)", displayListID, mode);
	return gpu->ListSync(displayListID, mode);
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
	return gpu->Continue();
}

int sceGeBreak(u32 mode)
{
	//mode => 0 : current dlist 1: all drawing
	DEBUG_LOG(SCEGE, "sceGeBreak(mode=%d)", mode);
	return gpu->Break(mode);
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

	if (sizeof(gstate) > 512 * 4)
	{
		ERROR_LOG(SCEGE, "AARGH! sizeof(gstate) has grown too large!");
		return 0;
	}

	// Let's just dump gstate.
	if (Memory::IsValidAddress(ctxAddr))
	{
		Memory::WriteStruct(ctxAddr, &gstate);
	}

	// This action should probably be pushed to the end of the queue of the display thread -
	// when we have one.
	return 0;
}

u32 sceGeRestoreContext(u32 ctxAddr)
{
	DEBUG_LOG(SCEGE, "sceGeRestoreContext(%08x)", ctxAddr);
	gpu->SyncThread();

	if (sizeof(gstate) > 512 * 4)
	{
		ERROR_LOG(SCEGE, "AARGH! sizeof(gstate) has grown too large!");
		return 0;
	}

	if (Memory::IsValidAddress(ctxAddr))
	{
		Memory::ReadStruct(ctxAddr, &gstate);
	}
	ReapplyGfxState();

	return 0;
}

int sceGeGetMtx(int type, u32 matrixPtr)
{
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
			Memory::Memcpy(matrixPtr, gstate.boneMatrix + n * 12, 12 * sizeof(float));
		}
		break;
	case GE_MTX_TEXGEN:
		Memory::Memcpy(matrixPtr, gstate.tgenMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_WORLD:
		Memory::Memcpy(matrixPtr, gstate.worldMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_VIEW:
		Memory::Memcpy(matrixPtr, gstate.viewMatrix, 12 * sizeof(float));
		break;
	case GE_MTX_PROJECTION:
		Memory::Memcpy(matrixPtr, gstate.projMatrix, 16 * sizeof(float));
		break;
	}
	return 0;
}

u32 sceGeGetCmd(int cmd)
{
	INFO_LOG(SCEGE, "sceGeGetCmd(%i)", cmd);
	return gstate.cmdmem[cmd];  // Does not mask away the high bits.
}

u32 sceGeEdramSetAddrTranslation(int new_size)
{
	INFO_LOG(SCEGE, "sceGeEdramSetAddrTranslation(%i)", new_size);
	static int EDRamWidth;
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
	{0xB448EC0D, WrapI_U<sceGeBreak>,                   "sceGeBreak"},
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
};

void Register_sceGe_user()
{
	RegisterModule("sceGe_user", ARRAY_SIZE(sceGe_user), sceGe_user);
}
