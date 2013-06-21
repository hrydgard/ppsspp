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

#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "sceKernel.h"
#include "sceKernelInterrupt.h"
#include "sceKernelMemory.h"
#include "sceKernelVTimer.h"
#include "HLE.h"
#include "ChunkFile.h"

static int vtimerTimer = -1;
static std::list<SceUID> vtimers;

struct NativeVTimer {
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	int active;
	u64 base;
	u64 current;
	u64 schedule;
	u32 handlerAddr;
	u32 commonAddr;
};

struct VTimer : public KernelObject {
	const char *GetName() {return nvt.name;}
	const char *GetTypeName() {return "VTimer";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VTID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_VTimer; }
	int GetIDType() const { return SCE_KERNEL_TMID_VTimer; }

	virtual void DoState(PointerWrap &p) {
		p.Do(nvt);
		p.Do(memoryPtr);
		p.DoMarker("VTimer");
	}

	NativeVTimer nvt;
	u32 memoryPtr;
};

KernelObject *__KernelVTimerObject() {
	return new VTimer;
}

u64 __getVTimerRunningTime(VTimer *vt) {
	if (!vt->nvt.active)
		return 0;

	return cyclesToUs(CoreTiming::GetTicks()) - vt->nvt.base;
}

u64 __getVTimerCurrentTime(VTimer* vt) {
	return vt->nvt.current + __getVTimerRunningTime(vt);
}

void __cancelVTimer(SceUID id) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(id, error);

	if (error)
		return;

	CoreTiming::UnscheduleEvent(vtimerTimer, id);
	vt->nvt.schedule = 0;
	vt->nvt.handlerAddr = 0;
	vt->nvt.commonAddr = 0;
}

void __KernelScheduleVTimer(VTimer *vt, u64 schedule) {
	CoreTiming::UnscheduleEvent(vtimerTimer, vt->GetUID());

	vt->nvt.schedule = schedule;

	if (vt->nvt.active == 1 && vt->nvt.handlerAddr != 0)
		// this delay makes the test pass, not sure if it's right
		CoreTiming::ScheduleEvent(usToCycles(vt->nvt.schedule + 372), vtimerTimer, vt->GetUID());
}

void __rescheduleVTimer(SceUID id, int delay) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(id, error);

	if (error)
		return;

	if (delay < 0)
		delay = 100;

	u64 schedule = vt->nvt.schedule + delay;

	__KernelScheduleVTimer(vt, schedule);
}

class VTimerIntrHandler : public IntrHandler
{
public:
	VTimerIntrHandler() : IntrHandler(PSP_SYSTIMER1_INTR) {}

	virtual bool run(PendingInterrupt &pend) {
		u32 error;
		SceUID vtimerID = vtimers.front();

		VTimer *vtimer = kernelObjects.Get<VTimer>(vtimerID, error);

		if (error)
			return false;

		if (vtimer->memoryPtr == 0) {
			u32 size = 16;
			vtimer->memoryPtr = kernelMemory.Alloc(size, true, "VTimer");
		}

		Memory::Write_U64(vtimer->nvt.schedule, vtimer->memoryPtr);
		Memory::Write_U64(__getVTimerCurrentTime(vtimer), vtimer->memoryPtr + 8);

		currentMIPS->pc = vtimer->nvt.handlerAddr;
		currentMIPS->r[MIPS_REG_A0] = vtimer->GetUID();
		currentMIPS->r[MIPS_REG_A1] = vtimer->memoryPtr;
		currentMIPS->r[MIPS_REG_A2] = vtimer->memoryPtr + 8;
		currentMIPS->r[MIPS_REG_A3] = vtimer->nvt.commonAddr;

		return true;
	}

	virtual void handleResult(PendingInterrupt &pend) {
		int result = currentMIPS->r[MIPS_REG_V0];

		int vtimerID = vtimers.front();
		vtimers.pop_front();

		if (result == 0)
			__cancelVTimer(vtimerID);
		else
			__rescheduleVTimer(vtimerID, result);
	}
};

void __KernelTriggerVTimer(u64 userdata, int cyclesLate) {
	SceUID uid = (SceUID) userdata;

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (vt)	{
		vtimers.push_back(uid);
		__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_SYSTIMER1_INTR);
	}
}

void __KernelVTimerDoState(PointerWrap &p) {
	p.Do(vtimerTimer);
	p.Do(vtimers);
	CoreTiming::RestoreRegisterEvent(vtimerTimer, "VTimer", __KernelTriggerVTimer);
	p.DoMarker("sceKernelVTimer");
}

void __KernelVTimerInit() {
	vtimers.clear();
	__RegisterIntrHandler(PSP_SYSTIMER1_INTR, new VTimerIntrHandler());
	vtimerTimer = CoreTiming::RegisterEvent("VTimer", __KernelTriggerVTimer);
}

u32 sceKernelCreateVTimer(const char *name, u32 optParamAddr) {
	DEBUG_LOG(HLE, "sceKernelCreateVTimer(%s, %08x)", name, optParamAddr);
	if (optParamAddr != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateVTimer: unsupported options parameter %08x", optParamAddr);

	VTimer *vtimer = new VTimer;
	SceUID id = kernelObjects.Create(vtimer);

	memset(&vtimer->nvt, 0, sizeof(NativeVTimer));
	vtimer->nvt.size = sizeof(NativeVTimer);
	strncpy(vtimer->nvt.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	vtimer->nvt.name[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
	vtimer->memoryPtr = 0;

	return id;
}

u32 sceKernelDeleteVTimer(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelDeleteVTimer(%08x)", uid);

	u32 error;
	VTimer* vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelDeleteVTimer(%08x)", error, uid);
		return error;
	}

	for (std::list<SceUID>::iterator it = vtimers.begin(); it != vtimers.end(); ++it) {
		if (*it == vt->GetUID()) {
			vtimers.erase(it);
			break;
		}
	}

	if (vt->memoryPtr != 0)
		kernelMemory.Free(vt->memoryPtr);

	return kernelObjects.Destroy<VTimer>(uid);
}

u32 sceKernelGetVTimerBase(u32 uid, u32 baseClockAddr) {
	DEBUG_LOG(HLE, "sceKernelGetVTimerBase(%08x, %08x)", uid, baseClockAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelGetVTimerBase(%08x, %08x)", error, uid, baseClockAddr);
		return error;
	}

	if (Memory::IsValidAddress(baseClockAddr))
		Memory::Write_U64(vt->nvt.base, baseClockAddr);

	return 0;
}

u64 sceKernelGetVTimerBaseWide(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelGetVTimerBaseWide(%08x)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelGetVTimerBaseWide(%08x)", error, uid);
		return error;
	}

	return vt->nvt.base;
}

u32 sceKernelGetVTimerTime(u32 uid, u32 timeClockAddr) {
	DEBUG_LOG(HLE, "sceKernelGetVTimerTime(%08x, %08x)", uid, timeClockAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelGetVTimerTime(%08x, %08x)", error, uid, timeClockAddr);
		return error;
	}

	u64 time = __getVTimerCurrentTime(vt);
	if (Memory::IsValidAddress(timeClockAddr))
		Memory::Write_U64(time, timeClockAddr);

	return 0;
}

u64 sceKernelGetVTimerTimeWide(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelGetVTimerTimeWide(%08x)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelGetVTimerTimeWide(%08x)", error, uid);
		return error;
	}

	u64 time = __getVTimerCurrentTime(vt);
	return time;
}

u64 __setVTimer(VTimer *vt, u64 time) {
	u64 current = __getVTimerCurrentTime(vt);
	vt->nvt.base = vt->nvt.base + __getVTimerCurrentTime(vt) - time;
	vt->nvt.current = 0;

	return current;
}

u32 sceKernelSetVTimerTime(u32 uid, u32 timeClockAddr) {
	DEBUG_LOG(HLE, "sceKernelSetVTimerTime(%08x, %08x)", uid, timeClockAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelSetVTimerTime(%08x, %08x)", error, uid, timeClockAddr);
		return error;
	}

	u64 time = Memory::Read_U64(timeClockAddr);
	if (Memory::IsValidAddress(timeClockAddr))
		Memory::Write_U64(__setVTimer(vt, time), timeClockAddr);

	return 0;
}

u32 sceKernelSetVTimerTimeWide(u32 uid, u64 timeClock) {
	DEBUG_LOG(HLE, "sceKernelSetVTimerTimeWide(%08x, %llu", uid, timeClock);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelSetVTimerTimeWide(%08x, %llu)", error, uid, timeClock);
		return error;
	}

	if (vt == NULL) {
		return -1;
	}

	return __setVTimer(vt, timeClock);
}

void __startVTimer(VTimer *vt) {
	vt->nvt.active = 1;
	vt->nvt.base = cyclesToUs(CoreTiming::GetTicks());

	if (vt->nvt.schedule != 0 && vt->nvt.handlerAddr != 0)
		__KernelScheduleVTimer(vt, vt->nvt.schedule);
}

u32 sceKernelStartVTimer(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelStartVTimer(%08x)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (vt)	{
		if (vt->nvt.active)
			return 1;

		__startVTimer(vt);
		return 0;
	}

	return error;
}

void __stopVTimer(VTimer *vt) {
	vt->nvt.current += __getVTimerCurrentTime(vt);
	vt->nvt.active = 0;
	vt->nvt.base = 0;
}

u32 sceKernelStopVTimer(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelStopVTimer(%08x)", uid);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (vt)	{
		if (vt->nvt.active == 0)
			return 0;

		__stopVTimer(vt);
		return 1;
	}

	return error;
}

u32 sceKernelSetVTimerHandler(u32 uid, u32 scheduleAddr, u32 handlerFuncAddr, u32 commonAddr) {
	DEBUG_LOG(HLE, "sceKernelSetVTimerHandler(%08x, %08x, %08x, %08x)", uid, scheduleAddr, handlerFuncAddr, commonAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelSetVTimerHandler(%08x, %08x, %08x, %08x)", error, uid, scheduleAddr, handlerFuncAddr, commonAddr);
		return error;
	}

	u64 schedule = Memory::Read_U64(scheduleAddr);
	vt->nvt.handlerAddr = handlerFuncAddr;
	vt->nvt.commonAddr = commonAddr;

	__KernelScheduleVTimer(vt, schedule);

	return 0;
}

u32 sceKernelSetVTimerHandlerWide(u32 uid, u64 schedule, u32 handlerFuncAddr, u32 commonAddr) {
	DEBUG_LOG(HLE, "sceKernelSetVTimerHandlerWide(%08x, %llu, %08x, %08x)", uid, schedule, handlerFuncAddr, commonAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelSetVTimerHandlerWide(%08x, %llu, %08x, %08x)", error, uid, schedule, handlerFuncAddr, commonAddr);
		return error;
	}

	vt->nvt.handlerAddr = handlerFuncAddr;
	vt->nvt.commonAddr = commonAddr;

	__KernelScheduleVTimer(vt, schedule);

	return 0;
}

u32 sceKernelCancelVTimerHandler(u32 uid) {
	DEBUG_LOG(HLE, "sceKernelCancelVTimerHandler(%08x)", uid);

	//__cancelVTimer checks if uid is valid
	__cancelVTimer(uid);

	return 0;
}

u32 sceKernelReferVTimerStatus(u32 uid, u32 statusAddr) {
	DEBUG_LOG(HLE, "sceKernelReferVTimerStatus(%08x, %08x)", uid, statusAddr);

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);

	if (error) {
		WARN_LOG(HLE, "%08x=sceKernelReferVTimerStatus(%08x, %08x)", error, uid, statusAddr);
		return error;
	}

	if (Memory::IsValidAddress(statusAddr))
		Memory::WriteStruct(statusAddr, &vt->nvt);

	return 0;
}

// Not sure why this is exposed...
void _sceKernelReturnFromTimerHandler() {
	ERROR_LOG(HLE,"_sceKernelReturnFromTimerHandler - should not be called!");
}
