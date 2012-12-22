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
#include "sceGe.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"
#include "../GPU/GPUState.h"
#include "../GPU/GPUInterface.h"

static PspGeCallbackData ge_callback_data[16];
static bool ge_used_callbacks[16] = {0};

void __GeInit()
{
	memset(&ge_used_callbacks, 0, sizeof(ge_used_callbacks));
}

void __GeDoState(PointerWrap &p)
{
	p.DoArray(ge_callback_data, ARRAY_SIZE(ge_callback_data));
	p.DoArray(ge_used_callbacks, ARRAY_SIZE(ge_used_callbacks));
	// Everything else is done in sceDisplay.
	p.DoMarker("sceGe");
}

void __GeShutdown()
{

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

// TODO: Probably shouldn't use an interrupt?
int __GeSubIntrBase(int callbackId)
{
	// Negative means don't use.
	if (callbackId < 0)
		return 0;

	if (callbackId >= ARRAY_SIZE(ge_used_callbacks))
	{
		WARN_LOG(HLE, "Unexpected (too high) GE callback id %d, ignoring", callbackId);
		return 0;
	}

	if (!ge_used_callbacks[callbackId])
	{
		WARN_LOG(HLE, "Unregistered GE callback id %d, ignoring", callbackId);
		return 0;
	}

	return (callbackId + 1) << 16;
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
	//if (!stallAddress)
	//	stallAddress = listAddress;
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, __GeSubIntrBase(callbackId), true);

	DEBUG_LOG(HLE, "List %i enqueued.", listID);
	//return display list ID
	return listID;
}

int sceGeListDeQueue(u32 listID)
{
	ERROR_LOG(HLE, "UNIMPL sceGeListDeQueue(%08x)", listID);
	return 0;
}

int sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress)
{
	DEBUG_LOG(HLE, "sceGeListUpdateStallAddr(dlid=%i,stalladdr=%08x)",
			displayListID, stallAddress);

	gpu->UpdateStall(displayListID, stallAddress);
	return 0;
}

int sceGeListSync(u32 displayListID, u32 mode) //0 : wait for completion		1:check and return
{
	DEBUG_LOG(HLE, "sceGeListSync(dlid=%08x, mode=%08x)", displayListID, mode);
	if(mode == 1) {
		return gpu->listStatus(displayListID);
	}
	return 0;
}

u32 sceGeDrawSync(u32 mode)
{
	//wait/check entire drawing state
	DEBUG_LOG(HLE, "FAKE sceGeDrawSync(mode=%d)  (0=wait for completion)",
			mode);
	gpu->DrawSync(mode);
	return 0;
}

int sceGeContinue()
{
	ERROR_LOG(HLE, "UNIMPL sceGeContinue");
	// no arguments
	return 0;
}

int sceGeBreak(u32 mode)
{
	//mode => 0 : current dlist 1: all drawing
	ERROR_LOG(HLE, "UNIMPL sceGeBreak(mode=%d)", mode);
	return 0;
}

u32 sceGeSetCallback(u32 structAddr)
{
	DEBUG_LOG(HLE, "sceGeSetCallback(struct=%08x)", structAddr);

	int cbID = -1;
	for (int i = 0; i < ARRAY_SIZE(ge_used_callbacks); ++i)
		if (!ge_used_callbacks[i])
		{
			cbID = i;
			break;
		}

	if (cbID == -1)
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;

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
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (ge_used_callbacks[cbID])
	{
		int subIntrBase = __GeSubIntrBase(cbID);

		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_FINISH);
		sceKernelReleaseSubIntrHandler(PSP_GE_INTR, subIntrBase | PSP_GE_SUBINTR_SIGNAL);
	}

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
	ERROR_LOG(HLE, "UNIMPL sceGeGetMtx(%d, %08x)", type, matrixPtr);
	return 0;
}

u32 sceGeGetCmd(int cmd)
{
	ERROR_LOG(HLE, "UNIMPL sceGeGetCmd()");
	return 0;
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
