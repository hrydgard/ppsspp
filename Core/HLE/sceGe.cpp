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
#include "sceKernelCallback.h"
#include "sceKernelInterrupt.h"

// TODO: Bad dependency.
#include "../../GPU/GLES/DisplayListInterpreter.h"

// TODO: This doesn't really belong here
static int state;


void __GeInit()
{
	state = 0;
}

void __GeShutdown()
{

}

// The GE is implemented wrong - it should be parallel to the CPU execution instead of
// synchronous.



u32 sceGeEdramGetAddr()
{
	u32 retVal = 0x04000000;
	DEBUG_LOG(HLE,"%08x = sceGeEdramGetAddr",retVal);
	return retVal;
}

u32 sceGeEdramGetSize()
{
	u32 retVal = 0x00200000;
	DEBUG_LOG(HLE,"%08x = sceGeEdramGetSize()",retVal);
	return retVal;
}

u32 sceGeListEnQueue(u32 listAddress, u32 stallAddress, u32 callbackId, u32 optParamAddr)
{
	if (PSP_CoreParameter().gpuCore == GPU_NULL)
	{
		DEBUG_LOG(HLE,"No GPU - ignoring sceGeListEnqueue");
		return 0;
	}

	u32 listID = GPU::EnqueueList(listAddress, stallAddress);
	// HACKY
	if (listID)
		state = SCE_GE_LIST_STALLING;
	else
		state = SCE_GE_LIST_COMPLETED;

	DEBUG_LOG(HLE,"%i=sceGeListEnQueue(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",listID,
		listAddress,stallAddress,callbackId,optParamAddr);
	DEBUG_LOG(HLE,"List enqueued.");
	//return display list ID
	return listID;
}

u32 sceGeListEnQueueHead(u32 listAddress, u32 stallAddress, u32 callbackId, u32 optParamAddr)
{
	u32 listID = GPU::EnqueueList(listAddress,stallAddress);
	// HACKY
	if (listID)
		state = SCE_GE_LIST_STALLING;
	else
		state = SCE_GE_LIST_COMPLETED;

	DEBUG_LOG(HLE,"%i=sceGeListEnQueueHead(addr=%08x, stall=%08x, cbid=%08x, param=%08x)",
		listID,	listAddress,stallAddress,callbackId,optParamAddr);
	DEBUG_LOG(HLE,"List enqueued.");
	//return display list ID
	return listID;
}

void sceGeListUpdateStallAddr(u32 displayListID, u32 stallAddress) 
{
	DEBUG_LOG(HLE,"sceGeListUpdateStallAddr(dlid=%i,stalladdr=%08x)",
		displayListID,stallAddress);

	GPU::UpdateStall(displayListID, stallAddress);
}

void sceGeListSync(u32 displayListID, u32 mode) //0 : wait for completion		1:check and return
{
	DEBUG_LOG(HLE,"sceGeListSync(dlid=%08x, mode=%08x)", displayListID,mode);
}

u32 sceGeDrawSync(u32)
{
	//wait/check entire drawing state
	u32 mode = PARAM(0); //0 : wait for completion		1:check and return
	DEBUG_LOG(HLE,"FAKE sceGeDrawSync(mode=%d)",mode);
	if (mode == 1)
	{
		return 0;
	}
	else
	{
		return 0;
	}
}

void sceGeBreak()
{
	u32 mode = PARAM(0); //0 : current dlist 1: all drawing
	ERROR_LOG(HLE,"UNIMPL sceGeBreak(mode=%d)",mode);
}

void sceGeContinue()
{
	ERROR_LOG(HLE,"UNIMPL sceGeContinue");
	// no arguments
}

u32 sceGeSetCallback(u32 structAddr)
{
	ERROR_LOG(HLE,"HALFIMPL sceGeSetCallback(struct=%08x)", structAddr);

	PspGeCallbackData ge_callback_data;
	Memory::ReadStruct(structAddr, &ge_callback_data);

	if (ge_callback_data.finish_func)
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_FINISH, ge_callback_data.finish_func, ge_callback_data.finish_arg);
	if (ge_callback_data.signal_func)
		sceKernelRegisterSubIntrHandler(PSP_GE_INTR, PSP_GE_SUBINTR_SIGNAL, ge_callback_data.signal_func, ge_callback_data.signal_arg);

	// TODO: This should return a callback ID
	return 0;
}

void sceGeUnsetCallback(u32 cbID)
{
	ERROR_LOG(HLE,"UNIMPL sceGeUnsetCallback(cbid=%08x)", cbID);
}

void sceGeSaveContext()
{
	ERROR_LOG(HLE,"UNIMPL sceGeSaveContext()");
}

void sceGeRestoreContext()
{
	ERROR_LOG(HLE,"UNIMPL sceGeRestoreContext()");
}

void sceGeGetMtx()
{
	ERROR_LOG(HLE,"UNIMPL sceGeGetMtx()");
}

void sceGeEdramSetAddrTranslation()
{
	int new_size = PARAM(0);
	INFO_LOG(HLE,"sceGeEdramSetAddrTranslation(%i)", new_size);
	static int EDRamWidth;
	int last = EDRamWidth;
	EDRamWidth = new_size;
	RETURN(last);
}

const HLEFunction sceGe_user[] =
{
	{0xE47E40E4,&WrapU_V<sceGeEdramGetAddr>,					"sceGeEdramGetAddr"},
	{0xAB49E76A,&WrapU_UUUU<sceGeListEnQueue>,				"sceGeListEnQueue"},
	{0x1C0D95A6,&WrapU_UUUU<sceGeListEnQueueHead>,		"sceGeListEnQueueHead"},
	{0xE0D68148,&WrapV_UU<sceGeListUpdateStallAddr>,	"sceGeListUpdateStallAddr"},
	{0x03444EB4,&WrapV_UU<sceGeListSync>,						 "sceGeListSync"},
	{0xB287BD61,&WrapU_U<sceGeDrawSync>,							"sceGeDrawSync"},
	{0xB448EC0D,sceGeBreak,							"sceGeBreak"},
	{0x4C06E472,sceGeContinue,					 "sceGeContinue"},
	{0xA4FC06A4,&WrapU_U<sceGeSetCallback>,	"sceGeSetCallback"},
	{0x05DB22CE,&WrapV_U<sceGeUnsetCallback>,					 "sceGeUnsetCallback"},
	{0x1F6752AD,&WrapU_V<sceGeEdramGetSize>, "sceGeEdramGetSize"},
	{0xB77905EA,&sceGeEdramSetAddrTranslation,"sceGeEdramSetAddrTranslation"},
	{0xDC93CFEF,0,"sceGeGetCmd"},
	{0x57C8945B,&sceGeGetMtx,"sceGeGetMtx"},
	{0x438A385A,0,"sceGeSaveContext"},
	{0x0BF608FB,0,"sceGeRestoreContext"},
	{0x5FB86AB0,0,"sceGeListDeQueue"},
};

void Register_sceGe_user()
{
	RegisterModule("sceGe_user", ARRAY_SIZE(sceGe_user), sceGe_user);
}
