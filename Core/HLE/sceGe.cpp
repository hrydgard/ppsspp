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

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../System.h"
#include "../CoreParameter.h"
#include "../CoreTiming.h"
#include "../Reporting.h"
#include "sceGe.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"
#include "../GPU/GPUState.h"
#include "../GPU/GPUInterface.h"

static PspGeCallbackData ge_callback_data[16];
static bool ge_used_callbacks[16] = {0};

struct GeInterruptData
{
	int listid;
	u32 pc;
};

static std::list<GeInterruptData> ge_pending_cb;
static int geSyncEvent;
static int geInterruptEvent;

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
			WARN_LOG(HLE, "Unable to run GE interrupt: list doesn't exist: %d", intrdata.listid);
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
			DEBUG_LOG(HLE, "Ignoring interrupt for display list %d, already been released.", intrdata.listid);
		return false;
	}

	virtual void handleResult(PendingInterrupt& pend)
	{
		GeInterruptData intrdata = ge_pending_cb.front();
		ge_pending_cb.pop_front();

		DisplayList* dl = gpu->getList(intrdata.listid);

		switch (dl->signal)
		{
		case PSP_GE_SIGNAL_HANDLER_SUSPEND:
			if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
			{
				// uofw says dl->state = endCmd & 0xFF;
				DisplayListState newState = static_cast<DisplayListState>(Memory::ReadUnchecked_U32(intrdata.pc - 4) & 0xFF);
				//dl->status = static_cast<DisplayListStatus>(Memory::ReadUnchecked_U32(intrdata.pc) & 0xFF);
				//if(dl->status < 0 || dl->status > PSP_GE_LIST_PAUSED)
				//	ERROR_LOG(HLE, "Weird DL status after signal suspend %x", dl->status);
				if (newState != PSP_GE_DL_STATE_RUNNING)
					WARN_LOG_REPORT(HLE, "GE Interrupt: newState might be %d", newState);

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
	bool wokeThreads = __KernelTriggerWait(waitType, listid, 0, "GeSync", true);
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

void __GeInit()
{
	memset(&ge_used_callbacks, 0, sizeof(ge_used_callbacks));
	ge_pending_cb.clear();
	__RegisterIntrHandler(PSP_GE_INTR, new GeIntrHandler());

	geSyncEvent = CoreTiming::RegisterEvent("GeSyncEvent", &__GeExecuteSync);
	geInterruptEvent = CoreTiming::RegisterEvent("GeInterruptEvent", &__GeExecuteInterrupt);
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
		s64 left = CoreTiming::UnscheduleEvent(geSyncEvent, userdata);
		if (left > future)
			future = left;
	}
	CoreTiming::ScheduleEvent(future, geSyncEvent, userdata);
	return true;
}

// Warning: may be called from the GPU thread.
bool __GeTriggerInterrupt(int listid, u32 pc, u64 atTicks)
{
	u64 userdata = (u64)listid << 32 | (u64) pc;
	CoreTiming::ScheduleEvent(atTicks - CoreTiming::GetTicks(), geInterruptEvent, userdata);
	return true;
}

bool __GeHasPendingInterrupt()
{
	return !ge_pending_cb.empty();
}

// The GE is implemented wrong - it should be parallel to the CPU execution instead of
// synchronous.

u32 sceGeEdramGetAddr()
{
	u32 retVal = 0x04000000;
	DEBUG_LOG(HLE, "%08x = sceGeEdramGetAddr", retVal);
	return retVal;
}

u32 sceGeEdramGetSize()
{
	u32 retVal = 0x00200000;
	DEBUG_LOG(HLE, "%08x = sceGeEdramGetSize()", retVal);
	return retVal;
}

int __GeSubIntrBase(int callbackId)
{
	return callbackId * 2;
}

u32 sceGeListEnQueue(u32 listAddress, u32 stallAddress, int callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(HLE,
			"sceGeListEnQueue(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	//if (!stallAddress)
	//	stallAddress = listAddress;
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), false);

	DEBUG_LOG(HLE, "List %i enqueued.", listID);
	//return display list ID
	return listID;
}

u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, int callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(HLE,
			"sceGeListEnQueueHead(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), true);

	DEBUG_LOG(HLE, "List %i enqueued.", listID);
	return listID;
}

int sceGeListDeQueue(u32 listID)
{
	WARN_LOG(HLE, "sceGeListDeQueue(%08x)", listID);
	return gpu->DequeueList(listID);
}

int sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress)
{
	DEBUG_LOG(HLE, "sceGeListUpdateStallAddr(dlid=%i, stalladdr=%08x)", displayListID, stallAddress);
	hleEatCycles(190);
	CoreTiming::AdvanceQuick();
	return gpu->UpdateStall(displayListID, stallAddress);
}

int sceGeListSync(u32 displayListID, u32 mode) //0 : wait for completion		1:check and return
{
	DEBUG_LOG(HLE, "sceGeListSync(dlid=%08x, mode=%08x)", displayListID, mode);
	return gpu->ListSync(displayListID, mode);
}

u32 sceGeDrawSync(u32 mode)
{
	//wait/check entire drawing state
	DEBUG_LOG(HLE, "sceGeDrawSync(mode=%d)  (0=wait for completion, 1=peek)", mode);
	return gpu->DrawSync(mode);
}

int sceGeContinue()
{
	DEBUG_LOG(HLE, "sceGeContinue");
	return gpu->Continue();
}

int sceGeBreak(u32 mode)
{
	//mode => 0 : current dlist 1: all drawing
	DEBUG_LOG(HLE, "sceGeBreak(mode=%d)", mode);
	return gpu->Break(mode);
}

u32 sceGeSetCallback(u32 structAddr)
{
	DEBUG_LOG(HLE, "sceGeSetCallback(struct=%08x)", structAddr);

	int cbID = -1;
	for (size_t i = 0; i < ARRAY_SIZE(ge_used_callbacks); ++i)
		if (!ge_used_callbacks[i])
		{
			cbID = (int) i;
			break;
		}

	if (cbID == -1)
	{
		WARN_LOG(HLE, "sceGeSetCallback(): out of callback ids");
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}

	ge_used_callbacks[cbID] = true;
	Memory::ReadStruct(structAddr, &ge_callback_data[cbID]);

	int subIntrBase = __GeSubIntrBase(cbID);

	if (ge_callback_data[cbID].finish_func)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH,
				ge_callback_data[cbID].finish_func, ge_callback_data[cbID].finish_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH);
	}
	if (ge_callback_data[cbID].signal_func)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL,
				ge_callback_data[cbID].signal_func, ge_callback_data[cbID].signal_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL);
	}

	return cbID;
}

int sceGeUnsetCallback(u32 cbID)
{
	DEBUG_LOG(HLE, "sceGeUnsetCallback(cbid=%08x)", cbID);

	if (cbID >= ARRAY_SIZE(ge_used_callbacks))
	{
		WARN_LOG(HLE, "sceGeUnsetCallback(cbid=%08x): invalid callback id", cbID);
		return SCE_KERNEL_ERROR_INVALID_ID;
	}

	if (ge_used_callbacks[cbID])
	{
		int subIntrBase = __GeSubIntrBase(cbID);

		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH);
		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL);
	}
	else
		WARN_LOG(HLE, "sceGeUnsetCallback(cbid=%08x): ignoring unregistered callback id", cbID);

	ge_used_callbacks[cbID] = false;

	return 0;
}

// Points to 512 32-bit words, where we can probably layout the context however we want
// unless some insane game pokes it and relies on it...
u32 sceGeSaveContext(u32 ctxAddr)
{
	DEBUG_LOG(HLE, "sceGeSaveContext(%08x)", ctxAddr);
	gpu->Flush();
	if (sizeof(gstate) > 512 * 4)
	{
		ERROR_LOG(HLE, "AARGH! sizeof(gstate) has grown too large!");
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
	DEBUG_LOG(HLE, "sceGeRestoreContext(%08x)", ctxAddr);
	gpu->Flush();

	if (sizeof(gstate) > 512 * 4)
	{
		ERROR_LOG(HLE, "AARGH! sizeof(gstate) has grown too large!");
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
		ERROR_LOG(HLE, "sceGeGetMtx(%d, %08x) - bad matrix ptr", type, matrixPtr);
		return -1;
	}

	INFO_LOG(HLE, "sceGeGetMtx(%d, %08x)", type, matrixPtr);
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
	INFO_LOG(HLE, "sceGeGetCmd(%i)", cmd);
	return gstate.cmdmem[cmd];  // Does not mask away the high bits.
}

u32 sceGeEdramSetAddrTranslation(int new_size)
{
	INFO_LOG(HLE, "sceGeEdramSetAddrTranslation(%i)", new_size);
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
