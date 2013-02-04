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
#include "sceKernelThread.h"
#include "sceKernelVTimer.h"
#include "HLE.h"
#include "ChunkFile.h"

// Using ERROR_LOG liberally when this is in development. When done,
// should be changed to DEBUG_LOG wherever applicable.

struct NativeVTimer {
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	int running;
	SceKernelSysClock basetime;
	SceKernelSysClock curtime;
	SceKernelSysClock scheduletime;
	u32 handlerAddr;
	u32 argument;
};

struct VTimer : public KernelObject {
	const char *GetName() {return nvt.name;}
	const char *GetTypeName() {return "VTimer";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VTID; }
	int GetIDType() const { return SCE_KERNEL_TMID_VTimer; }

	virtual void DoState(PointerWrap &p) {
		p.Do(nvt);
		p.DoMarker("VTimer");
	}

	NativeVTimer nvt;
};

KernelObject *__KernelVTimerObject() {
	return new VTimer;
}

u32 sceKernelCreateVTimer(const char *name, u32 optParamAddr) {
	ERROR_LOG(HLE,"FAKE sceKernelCreateVTimer(%s, %08x)", name, optParamAddr);

	VTimer *vt = new VTimer();

	SceUID uid = kernelObjects.Create(vt);
	memset(&vt->nvt, 0, sizeof(vt->nvt));
	if (name)
		strncpy(vt->nvt.name, name, 32);
	vt->nvt.running = 1;
	return uid; //TODO: return timer ID
}

u32 sceKernelDeleteVTimer(u32 uid) {
	ERROR_LOG(HLE,"FAKE sceKernelDeleteVTimer(%i)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO: Deschedule events here. Might share code with Stop.

	return kernelObjects.Destroy<VTimer>(uid);
}

u32 sceKernelStartVTimer(u32 uid) {
	ERROR_LOG(HLE,"FAKE sceKernelStartVTimer(%i)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	if (vt->nvt.running) {
		// Already running
		return 1;
	} else {
		vt->nvt.running = 1;
		// TODO: Schedule events etc.
		return 0;
	}
}

u32 sceKernelStopVTimer(u32 uid) {
	ERROR_LOG(HLE,"FAKE sceKernelStartVTimer(%i)", uid);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	if (vt->nvt.running) {
		// Already running
		return 0;
	} else {
		vt->nvt.running = 0;
		// TODO: Deschedule events etc.
		return 1;
	}
}

u32 sceKernelSetVTimerHandler(u32 uid, u32 scheduleAddr, u32 handlerFuncAddr, u32 commonAddr) {
	ERROR_LOG(HLE,"UNIMPL sceKernelSetVTimerHandler(%i, %08x, %08x, %08x)",
			uid, scheduleAddr, handlerFuncAddr, commonAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO
	return 0;
}

u32 sceKernelSetVTimerHandlerWide(u32 uid, u64 schedule, u32 handlerFuncAddr, u32 commonAddr) {
	ERROR_LOG(HLE,"UNIMPL sceKernelSetVTimerHandlerWide(%i, %llu, %08x, %08x)",
			uid, schedule, handlerFuncAddr, commonAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO
	return 0;
}

u32 sceKernelCancelVTimerHandler(u32 uid) {
	ERROR_LOG(HLE,"UNIMPL sceKernelCancelVTimerHandler(%i)", uid);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO
	return 0;
}

u32 sceKernelReferVTimerStatus(u32 uid, u32 statusAddr) {
	ERROR_LOG(HLE,"sceKernelReferVTimerStatus(%i, %08x)", uid, statusAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO: possibly update time values here?
	Memory::WriteStruct(statusAddr, &vt->nvt);
	return 0;
}

u32 sceKernelGetVTimerBase(u32 uid, u32 baseClockAddr) {
	ERROR_LOG(HLE,"sceKernelGetVTimerBase(%i, %08x)", uid, baseClockAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	Memory::WriteStruct(baseClockAddr, &vt->nvt.basetime);
	return 0;
}

u64 sceKernelGetVTimerBaseWide(u32 uid) {
	ERROR_LOG(HLE,"sceKernelGetVTimerWide(%i)", uid);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO: probably update the timer somehow?
	u64 t = vt->nvt.curtime.lo;
	t |= (u64)(vt->nvt.curtime.hi) << 32;
	return t;
}

u32 sceKernelGetVTimerTime(u32 uid, u32 timeClockAddr) {
	ERROR_LOG(HLE,"sceKernelGetVTimerTime(%i, %08x)", uid, timeClockAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO: probably update the timer somehow?
	Memory::WriteStruct(timeClockAddr, &vt->nvt.curtime);
	return 0;
}

u64 sceKernelGetVTimerTimeWide(u32 uid) {
	ERROR_LOG(HLE,"sceKernelGetVTimerTimeWide(%i)", uid);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	// TODO: probably update the timer somehow?
	u64 t = vt->nvt.curtime.lo;
	t |= (u64)(vt->nvt.curtime.hi) << 32;
	return t;
}

u32 sceKernelSetVTimerTime(u32 uid, u32 timeClockAddr) {
	ERROR_LOG(HLE,"sceKernelSetVTimerTime(%i, %08x)", uid, timeClockAddr);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	Memory::ReadStruct(timeClockAddr, &vt->nvt.curtime);
	return 0;
}

u32 sceKernelSetVTimerTimeWide(u32 uid, u64 timeClock) {
	ERROR_LOG(HLE,"sceKernelSetVTimerTime(%i, %llu)", uid, timeClock);
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return error;
	}
	vt->nvt.curtime.lo = timeClock & 0xFFFFFFFF;
	vt->nvt.curtime.hi = timeClock >> 32;
	return 0;
}

// Not sure why this is exposed...
void _sceKernelReturnFromTimerHandler()
{
	ERROR_LOG(HLE,"_sceKernelReturnFromTimerHandler - should not be called!");
}
