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

#include "sceKernel.h"
#include "sceKernelVTimer.h"
#include "HLE.h"

//////////////////////////////////////////////////////////////////////////
// VTIMER
//////////////////////////////////////////////////////////////////////////

struct NativeVTimer
{
	SceSize 	size;
	char 		name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	u64 startTime;
	bool running;
	u32 handler;
	u64 handlerTime;
	u32 argument;
};

struct VTimer : public KernelObject
{
	const char *GetName() {return nvt.name;}
	const char *GetTypeName() {return "VTimer";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VTID; }
	int GetIDType() const { return SCE_KERNEL_TMID_VTimer; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nvt);
		p.DoMarker("VTimer");
	}

	NativeVTimer nvt;
};

KernelObject *__KernelVTimerObject()
{
	return new VTimer;
}

void sceKernelCreateVTimer()
{
	DEBUG_LOG(HLE,"sceKernelCreateVTimer");
	const char *name = Memory::GetCharPointer(PARAM(0));

	VTimer *vt = new VTimer();

	SceUID uid = kernelObjects.Create(vt);
	strncpy(vt->nvt.name, name, 32);
	vt->nvt.running = true;
	vt->nvt.startTime = 0; //TODO fix
	RETURN(uid); //TODO: return timer ID
}

void sceKernelStartVTimer()
{
	int timerID = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelStartVTimer(%i)", timerID);

	RETURN(0); //ok; 1=alreadyrunning
}

void sceKernelSetVTimerHandler()
{
	DEBUG_LOG(HLE,"sceKernelSetVTimerHandler");

}

// Not sure why this is exposed...
void _sceKernelReturnFromTimerHandler()
{
	DEBUG_LOG(HLE,"_sceKernelReturnFromTimerHandler");

}
