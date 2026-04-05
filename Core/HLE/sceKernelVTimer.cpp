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

#include <algorithm>
#include <list>
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeList.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelVTimer.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"

static int vtimerTimer = -1;
static SceUID runningVTimer = 0;
static std::list<SceUID> vtimers;

struct NativeVTimer {
	SceSize_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	s32_le active;
	u64_le base;
	u64_le current;
	u64_le schedule;
	u32_le handlerAddr;
	u32_le commonAddr;
};

struct VTimer : public KernelObject {
	const char *GetName() override { return nvt.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "VTimer"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_VTID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_VTimer; }
	int GetIDType() const override { return SCE_KERNEL_TMID_VTimer; }

	void DoState(PointerWrap &p) override {
		auto s = p.Section("VTimer", 1, 2);
		if (!s)
			return;

		Do(p, nvt);
		if (s < 2) {
			u32 memoryPtr;
			Do(p, memoryPtr);
		}
	}

	NativeVTimer nvt{};
};

KernelObject *__KernelVTimerObject() {
	return new VTimer();
}

static u64 __getVTimerRunningTime(const VTimer *vt) {
	if (vt->nvt.active == 0)
		return 0;

	return CoreTiming::GetGlobalTimeUs() - vt->nvt.base;
}

static u64 __getVTimerCurrentTime(VTimer* vt) {
	return vt->nvt.current + __getVTimerRunningTime(vt);
}

static int __KernelCancelVTimer(SceUID id) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(id, error);
	if (!vt) {
		return error;
	}

	CoreTiming::UnscheduleEvent(vtimerTimer, id);
	vt->nvt.handlerAddr = 0;
	return 0;
}

static void __KernelScheduleVTimer(VTimer *vt, u64 schedule) {
	CoreTiming::UnscheduleEvent(vtimerTimer, vt->GetUID());

	vt->nvt.schedule = schedule;

	if (vt->nvt.active == 1 && vt->nvt.handlerAddr != 0) {
		// The "real" base is base + current.  But when setting the time, base is important.
		// The schedule is relative to those.
		u64 cyclesIntoFuture;
		if (schedule < 250) {
			schedule = 250;
		}
		s64 goalUs = (u64)vt->nvt.base + schedule - (u64)vt->nvt.current;
		s64 minGoalUs = CoreTiming::GetGlobalTimeUs() + 250;
		if (goalUs < minGoalUs) {
			cyclesIntoFuture = usToCycles(250);
		} else {
			cyclesIntoFuture = usToCycles(goalUs - CoreTiming::GetGlobalTimeUs());
		}

		CoreTiming::ScheduleEvent(cyclesIntoFuture, vtimerTimer, vt->GetUID());
	}
}

static void __rescheduleVTimer(SceUID id, u32 delay) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(id, error);

	if (error)
		return;

	__KernelScheduleVTimer(vt, vt->nvt.schedule + delay);
}

class VTimerIntrHandler : public IntrHandler
{
	static const int HANDLER_STACK_SPACE = 48;

public:
	VTimerIntrHandler() : IntrHandler(PSP_SYSTIMER1_INTR) {}

	bool run(PendingInterrupt &pend) override {
		u32 error;
		SceUID vtimerID = vtimers.front();

		VTimer *vtimer = kernelObjects.Get<VTimer>(vtimerID, error);

		if (error)
			return false;

		// Reserve some stack space for arguments.
		u32 argArea = currentMIPS->r[MIPS_REG_SP];
		currentMIPS->r[MIPS_REG_SP] -= HANDLER_STACK_SPACE;

		Memory::Write_U64(vtimer->nvt.schedule, argArea - 16);
		Memory::Write_U64(__getVTimerCurrentTime(vtimer), argArea - 8);

		currentMIPS->pc = vtimer->nvt.handlerAddr;
		currentMIPS->r[MIPS_REG_A0] = vtimer->GetUID();
		currentMIPS->r[MIPS_REG_A1] = argArea - 16;
		currentMIPS->r[MIPS_REG_A2] = argArea - 8;
		currentMIPS->r[MIPS_REG_A3] = vtimer->nvt.commonAddr;

		runningVTimer = vtimerID;

		return true;
	}

	void handleResult(PendingInterrupt &pend) override {
		u32 result = currentMIPS->r[MIPS_REG_V0];

		currentMIPS->r[MIPS_REG_SP] += HANDLER_STACK_SPACE;

		int vtimerID = vtimers.front();
		vtimers.pop_front();

		runningVTimer = 0;

		if (result == 0)
			__KernelCancelVTimer(vtimerID);
		else
			__rescheduleVTimer(vtimerID, result);
	}
};

static void __KernelTriggerVTimer(u64 userdata, int cyclesLate) {
	SceUID uid = (SceUID) userdata;

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (vt)	{
		vtimers.push_back(uid);
		__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_SYSTIMER1_INTR);
	}
}

void __KernelVTimerDoState(PointerWrap &p) {
	auto s = p.Section("sceKernelVTimer", 1, 2);
	if (!s)
		return;

	Do(p, vtimerTimer);
	Do(p, vtimers);
	CoreTiming::RestoreRegisterEvent(vtimerTimer, "VTimer", __KernelTriggerVTimer);

	if (s >= 2)
		Do(p, runningVTimer);
	else
		runningVTimer = 0;
}

void __KernelVTimerInit() {
	vtimers.clear();
	__RegisterIntrHandler(PSP_SYSTIMER1_INTR, new VTimerIntrHandler());
	vtimerTimer = CoreTiming::RegisterEvent("VTimer", __KernelTriggerVTimer);

	// Intentionally starts at 0.  This explains the behavior where 0 is treated differently outside a timer.
	runningVTimer = 0;
}

u32 sceKernelCreateVTimer(const char *name, u32 optParamAddr) {
	if (!name) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid name");
	}

	VTimer *vtimer = new VTimer;
	SceUID id = kernelObjects.Create(vtimer);

	memset(&vtimer->nvt, 0, sizeof(NativeVTimer));
	vtimer->nvt.size = sizeof(NativeVTimer);
	strncpy(vtimer->nvt.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	vtimer->nvt.name[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

	if (optParamAddr != 0) {
		u32 size = Memory::Read_U32(optParamAddr);
		if (size > 4)
			WARN_LOG_REPORT_ONCE(vtimeropt, Log::sceKernel, "sceKernelCreateVTimer(%s) unsupported options parameter, size = %d", name, size);
	}

	return hleLogDebug(Log::sceKernel, id);
}

u32 sceKernelDeleteVTimer(SceUID uid) {
	u32 error;
	VTimer* vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	for (std::list<SceUID>::iterator it = vtimers.begin(); it != vtimers.end(); ++it) {
		if (*it == vt->GetUID()) {
			vtimers.erase(it);
			break;
		}
	}

	return hleLogDebugOrError(Log::sceKernel, kernelObjects.Destroy<VTimer>(uid));
}

u32 sceKernelGetVTimerBase(SceUID uid, u32 baseClockAddr) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	if (Memory::IsValidAddress(baseClockAddr))
		Memory::Write_U64(vt->nvt.base, baseClockAddr);

	return hleLogDebug(Log::sceKernel, 0);
}

u64 sceKernelGetVTimerBaseWide(SceUID uid) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, -1, "bad timer ID");
	}

	return hleLogDebug(Log::sceKernel, vt->nvt.base);
}

u32 sceKernelGetVTimerTime(SceUID uid, u32 timeClockAddr) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	u64 time = __getVTimerCurrentTime(vt);
	if (Memory::IsValidAddress(timeClockAddr))
		Memory::Write_U64(time, timeClockAddr);

	return hleLogDebug(Log::sceKernel, 0);
}

u64 sceKernelGetVTimerTimeWide(SceUID uid) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		// Strange error code! But, it matches tests.
		return hleLogError(Log::sceKernel, -1, "bad timer ID. error=%08x", error);
	}

	u64 time = __getVTimerCurrentTime(vt);
	return hleLogDebug(Log::sceKernel, time);
}

static u64 __KernelSetVTimer(VTimer *vt, u64 time) {
	u64 current = __getVTimerCurrentTime(vt);
	vt->nvt.current = time - __getVTimerRunningTime(vt);

	// Run if we're now passed the schedule.
	__KernelScheduleVTimer(vt, vt->nvt.schedule);

	return current;
}

u32 sceKernelSetVTimerTime(SceUID uid, u32 timeClockAddr) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	u64 time = Memory::Read_U64(timeClockAddr);
	if (Memory::IsValidAddress(timeClockAddr))
		Memory::Write_U64(__KernelSetVTimer(vt, time), timeClockAddr);

	return hleLogDebug(Log::sceKernel, 0);
}

u64 sceKernelSetVTimerTimeWide(SceUID uid, u64 timeClock) {
	if (__IsInInterrupt()) {
		return hleLogWarning(Log::sceKernel, -1, "in interrupt");
	}

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		// Strange error code! But, it matches tests.
		return hleLogError(Log::sceKernel, -1, "bad timer ID. error=%08x", error);
	}

	return hleLogDebug(Log::sceKernel, __KernelSetVTimer(vt, timeClock));
}

static void __startVTimer(VTimer *vt) {
	vt->nvt.active = 1;
	vt->nvt.base = CoreTiming::GetGlobalTimeUs();

	if (vt->nvt.handlerAddr != 0)
		__KernelScheduleVTimer(vt, vt->nvt.schedule);
}

u32 sceKernelStartVTimer(SceUID uid) {
	hleEatCycles(12200);
	if (uid == runningVTimer) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_VTID, "invalid vtimer - can't be runningVTimer");
	}

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	if (vt->nvt.active) {
		return hleLogDebug(Log::sceKernel, 1);
	}

	__startVTimer(vt);
	return hleLogDebug(Log::sceKernel, 0);
}

static void __stopVTimer(VTimer *vt) {
	// This increases (__getVTimerCurrentTime includes nvt.current.)
	vt->nvt.current = __getVTimerCurrentTime(vt);
	vt->nvt.active = 0;
	vt->nvt.base = 0;
}

u32 sceKernelStopVTimer(SceUID uid) {
	if (uid == runningVTimer) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_VTID, "invalid vtimer - can't be runningVTimer");
	}

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	if (vt->nvt.active == 0) {
		return hleLogDebug(Log::sceKernel, 0);
	}

	__stopVTimer(vt);
	return hleLogDebug(Log::sceKernel, 1);
}

u32 sceKernelSetVTimerHandler(SceUID uid, u32 scheduleAddr, u32 handlerFuncAddr, u32 commonAddr) {
	hleEatCycles(900);
	if (uid == runningVTimer) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_VTID, "invalid vtimer - can't be runningVTimer");
	}

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	hleEatCycles(2000);

	u64 schedule = Memory::Read_U64(scheduleAddr);
	vt->nvt.handlerAddr = handlerFuncAddr;
	if (handlerFuncAddr) {
		vt->nvt.commonAddr = commonAddr;
		__KernelScheduleVTimer(vt, schedule);
	} else {
		__KernelScheduleVTimer(vt, vt->nvt.schedule);
	}

	return hleLogDebug(Log::sceKernel, 0);
}

u32 sceKernelSetVTimerHandlerWide(SceUID uid, u64 schedule, u32 handlerFuncAddr, u32 commonAddr) {
	hleEatCycles(900);
	if (uid == runningVTimer) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_VTID, "invalid vtimer - can't be runningVTimer");
	}

	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	vt->nvt.handlerAddr = handlerFuncAddr;
	if (handlerFuncAddr) {
		vt->nvt.commonAddr = commonAddr;
		__KernelScheduleVTimer(vt, schedule);
	} else {
		__KernelScheduleVTimer(vt, vt->nvt.schedule);
	}

	return hleLogDebug(Log::sceKernel, 0);
}

u32 sceKernelCancelVTimerHandler(SceUID uid) {
	if (uid == runningVTimer) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_VTID, "invalid vtimer - can't be runningVTimer");
	}

	//__cancelVTimer checks if uid is valid
	return hleLogDebugOrError(Log::sceKernel, __KernelCancelVTimer(uid));
}

u32 sceKernelReferVTimerStatus(SceUID uid, u32 statusAddr) {
	u32 error;
	VTimer *vt = kernelObjects.Get<VTimer>(uid, error);
	if (!vt) {
		return hleLogError(Log::sceKernel, error, "bad timer ID");
	}

	if (Memory::IsValidAddress(statusAddr)) {
		NativeVTimer status = vt->nvt;
		u32 size = Memory::Read_U32(statusAddr);
		status.current = __getVTimerCurrentTime(vt);
		Memory::Memcpy(statusAddr, &status, std::min(size, (u32)sizeof(status)), "VTimerStatus");
	}

	return hleLogDebug(Log::sceKernel, 0);
}

// Not sure why this is exposed...
void _sceKernelReturnFromTimerHandler() {
	ERROR_LOG_REPORT(Log::sceKernel,"_sceKernelReturnFromTimerHandler - should not be called!");
	return hleNoLogVoid();
}
