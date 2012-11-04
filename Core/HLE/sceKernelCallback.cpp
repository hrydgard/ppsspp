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

// Refer to http://code.google.com/p/yaupspe/source/detail?r=741987a938f87e55ac284f8c2af6d40b4fb30ffc#

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../MIPS/MIPSCodeUtils.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"
#include "sceKernelCallback.h"


class Callback : public KernelObject
{
public:
	const char *GetName() {return name;}
	const char *GetTypeName() {return "CallBack";}

	void GetQuickInfo(char *ptr, int size)
	{
		sprintf(ptr, "thread=%i, argument= %08x",
			//hackAddress,
			threadId,
			argument);
	}

	~Callback()
	{
	}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_CBID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Callback; }

	SceUInt size;
	char name[32];
	u32 entrypoint;
	SceUID threadId;
	u32 argument;

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
	Action *actionAfter;
};

int g_inCbCount = 0;

//////////////////////////////////////////////////////////////////////////
// CALLBACKS
//////////////////////////////////////////////////////////////////////////


// Internal API
u32 __KernelCreateCallback(const char *name, u32 entrypoint, u32 callbackArg, Action *actionAfter)
{
	Callback *c = new Callback;
	SceUID id = kernelObjects.Create(c);

	c->size = sizeof(Callback);
	strcpy(c->name, name);

	c->entrypoint = entrypoint;
	c->threadId = __KernelGetCurThread();
	c->argument = callbackArg;
	c->actionAfter = actionAfter;

	return id;
}


//extern Thread *currentThread;
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
	RETURN(kernelObjects.Destroy<Callback>(id));
}

void sceKernelNotifyCallback()
{
	SceUID cbid = PARAM(0);
	u32 arg = PARAM(1);
	DEBUG_LOG(HLE,"sceKernelNotifyCallback(%i, %i)", cbid, arg);

	__KernelNotifyCallback(__KernelGetCurThread(), cbid, arg);
	RETURN(0);
}

void sceKernelCancelCallback()
{
	SceUID cbid = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL sceKernelCancelCallback(%i)", cbid);
	//__KernelCancelCallback(__KernelGetCurThread(), cbid);
	RETURN(0);
}

void sceKernelGetCallbackCount()
{
	SceUID cbid = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL sceKernelGetCallbackCount(%i)", cbid);

	RETURN(0);
}

void sceKernelReferCallbackStatus()
{
	SceUID cbid = PARAM(0);
	u32 statusAddr = PARAM(1);
	ERROR_LOG(HLE,"UNIMPL sceKernelReferCallbackStatus(%i, %08x)", cbid, statusAddr);

	RETURN(0);
}

void __KernelCallCallback(SceUID id)
{
	//First, make sure we're on the right thread
	u32 error;
	Callback *c = kernelObjects.Get<Callback>(id, error);
	if (c)
	{
		if (c->threadId == __KernelGetCurThread())
		{
			//Alright, we're on the right thread

			// Save the few regs that need saving
			c->savedPC = currentMIPS->pc;
			c->savedRA = currentMIPS->r[MIPS_REG_RA];
			c->savedV0 = currentMIPS->r[MIPS_REG_V0];
			c->savedV1 = currentMIPS->r[MIPS_REG_V1];
			c->savedIdRegister = currentMIPS->r[MIPS_REG_CB_ID];

			// Set up the new state
			// TODO: check?
			CallbackNotification *notify = __KernelGetCallbackNotification(id);
			if (notify != NULL)
			{
				currentMIPS->r[4] = notify->count;
				currentMIPS->r[5] = notify->arg;
				notify->count = 0;
				notify->arg = 0;
			}
			else
			{
				currentMIPS->r[4] = 0;
				currentMIPS->r[5] = 0;
			}

			currentMIPS->r[6] = c->argument;
			currentMIPS->pc = c->entrypoint;
			currentMIPS->r[MIPS_REG_RA] = __KernelCallbackReturnAddress();

			g_inCbCount++;
		}
	}
	else
	{
		//ARGH!
	}
}

void _sceKernelReturnFromCallback()
{
	SceUID cbid = currentMIPS->r[MIPS_REG_CB_ID];	// yeah!
	DEBUG_LOG(HLE,"_sceKernelReturnFromCallback(cbid=%i)", cbid);
	// Value returned by the callback function
	u32 retVal = currentMIPS->r[MIPS_REG_V0];

	u32 error;
	Callback *cb = kernelObjects.Get<Callback>(cbid, error);
	if (!cb)
	{
		ERROR_LOG(HLE, "_sceKernelReturnFromCallback(): INVALID CBID in register! we're screwed");
		return;
	}

	currentMIPS->pc = cb->savedPC;
	currentMIPS->r[MIPS_REG_RA] = cb->savedRA;
	currentMIPS->r[MIPS_REG_V0] = cb->savedV0;
	currentMIPS->r[MIPS_REG_V1] = cb->savedV1;
	currentMIPS->r[MIPS_REG_CB_ID] = cb->savedIdRegister;

	// Callbacks that don't return 0 are deleted. But should this be done here?
	if (retVal != 0 || cb->forceDelete)
	{
		DEBUG_LOG(HLE, "_sceKernelReturnFromCallback(): Callback returned non-zero, gets deleted!");
		kernelObjects.Destroy<Callback>(cbid);
	}

	if (cb->actionAfter)
	{
		cb->actionAfter->run();
	}
	g_inCbCount--;
	// yeah! back in the real world, let's keep going....
}

u32 sceKernelCheckCallback()
{
	//only check those of current thread
	ERROR_LOG(HLE,"UNIMPL sceKernelCheckCallback()");

	// HACK that makes the audio thread work in Puyo Puyo Fever - the main thread never yields!
	// Probably because callbacks aren't working right.
	__KernelReSchedule("checkcallbackhack");

	return 0;
}

bool __KernelInCallback()
{
    return (g_inCbCount != 0);
}

