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

void __GeInit()
{
}

void __GeDoState(PointerWrap &p)
{
	// Everything is done in sceDisplay.
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

u32 sceGeListEnQueue(u32 listAddress, u32 stallAddress, u32 callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(HLE,
			"sceGeListEnQueue(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	//if (!stallAddress)
	//	stallAddress = listAddress;
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, false);

	DEBUG_LOG(HLE, "List %i enqueued.", listID);
	//return display list ID
	return listID;
}

u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, u32 callbackId,
		u32 optParamAddr)
{
	DEBUG_LOG(HLE,
			"sceGeListEnQueueHead(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
			listAddress, stallAddress, callbackId, optParamAddr);
	//if (!stallAddress)
	//	stallAddress = listAddress;
	u32 listID = gpu->EnqueueList(listAddress, stallAddress, true);

	DEBUG_LOG(HLE, "List %i enqueued.", listID);
	//return display list ID
	return listID;
}

void sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress)
{
	DEBUG_LOG(HLE, "sceGeListUpdateStallAddr(dlid=%i,stalladdr=%08x)",
			displayListID, stallAddress);

	gpu->UpdateStall(displayListID, stallAddress);
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

void sceGeContinue()
{
	ERROR_LOG(HLE, "UNIMPL sceGeContinue");
	// no arguments
}

void sceGeBreak(u32 mode)
{
	//mode => 0 : current dlist 1: all drawing
	ERROR_LOG(HLE, "UNIMPL sceGeBreak(mode=%d)", mode);
}


u32 sceGeSetCallback(u32 structAddr)
{
	DEBUG_LOG(HLE, "sceGeSetCallback(struct=%08x)", structAddr);

	PspGeCallbackData ge_callback_data;
	Memory::ReadStruct(structAddr, &ge_callback_data);

	if (ge_callback_data.finish_func)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_FINISH,
				ge_callback_data.finish_func, ge_callback_data.finish_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, PSP_GE_SUBINTR_FINISH);
	}
	if (ge_callback_data.signal_func)
	{
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_SIGNAL,
				ge_callback_data.signal_func, ge_callback_data.signal_arg);
		sceKernelEnableSubIntr(PSP_GE_INTR, PSP_GE_SUBINTR_SIGNAL);
	}

	// TODO: This should return a callback ID
	return 0;
}

void sceGeUnsetCallback(u32 cbID) {
	DEBUG_LOG(HLE, "sceGeUnsetCallback(cbid=%08x)", cbID);
	sceKernelReleaseSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_FINISH);
	sceKernelReleaseSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_SIGNAL);
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

void sceGeGetMtx()
{
	ERROR_LOG(HLE, "UNIMPL sceGeGetMtx()");
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
	{0xE47E40E4,&WrapU_V<sceGeEdramGetAddr>,					"sceGeEdramGetAddr"},
	{0xAB49E76A,&WrapU_UUUU<sceGeListEnQueue>,				"sceGeListEnQueue"},
	{0x1C0D95A6,&WrapU_UUUU<sceGeListEnQueueHead>,		"sceGeListEnQueueHead"},
	{0xE0D68148,&WrapV_UU<sceGeListUpdateStallAddr>,	"sceGeListUpdateStallAddr"},
	{0x03444EB4,&WrapI_UU<sceGeListSync>,						 "sceGeListSync"},
	{0xB287BD61,&WrapU_U<sceGeDrawSync>,							"sceGeDrawSync"},
	{0xB448EC0D,&WrapV_U<sceGeBreak>,							"sceGeBreak"},
	{0x4C06E472,sceGeContinue,					 "sceGeContinue"},
	{0xA4FC06A4,&WrapU_U<sceGeSetCallback>,	"sceGeSetCallback"},
	{0x05DB22CE,&WrapV_U<sceGeUnsetCallback>,					 "sceGeUnsetCallback"},
	{0x1F6752AD,&WrapU_V<sceGeEdramGetSize>, "sceGeEdramGetSize"},
	{0xB77905EA,&WrapU_I<sceGeEdramSetAddrTranslation>,"sceGeEdramSetAddrTranslation"},
	{0xDC93CFEF,0,"sceGeGetCmd"},
	{0x57C8945B,&sceGeGetMtx,"sceGeGetMtx"},
	{0x438A385A,&WrapU_U<sceGeSaveContext>,"sceGeSaveContext"},
	{0x0BF608FB,&WrapU_U<sceGeRestoreContext>,"sceGeRestoreContext"},
	{0x5FB86AB0,0,"sceGeListDeQueue"},
};

void Register_sceGe_user()
{
	RegisterModule("sceGe_user", ARRAY_SIZE(sceGe_user), sceGe_user);
}
