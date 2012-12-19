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
#include "sceKernelAlarm.h"
#include "sceKernelInterrupt.h"
#include "HLE.h"
#include "../../Core/CoreTiming.h"

struct NativeAlarm
{
	SceSize size;
	u64 schedule;
	u32 handlerPtr;
	u32 commonPtr;
};

struct Alarm : public KernelObject
{
	const char *GetName() {return "[Alarm]";}
	const char *GetTypeName() {return "Alarm";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_ALMID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Alarm; }
	NativeAlarm alm;
};

void __KernelScheduleAlarm(Alarm *alarm, int ticks);

class AlarmIntrHandler : public SubIntrHandler
{
public:
	AlarmIntrHandler(Alarm *alarm)
	{
		this->alarm = alarm;
		handlerAddress = alarm->alm.handlerPtr;
		enabled = true;
	}

	virtual void copyArgsToCPU(const PendingInterrupt &pend)
	{
		SubIntrHandler::copyArgsToCPU(pend);

		currentMIPS->r[MIPS_REG_A0] = alarm->alm.commonPtr;
	}

	virtual void handleResult(int result)
	{
		// A non-zero result means to reschedule.
		// TODO: Do sysclock alarms return a different value unit?
		if (result > 0)
			__KernelScheduleAlarm(alarm, usToCycles(result));
		else if (result < 0)
			WARN_LOG(HLE, "Alarm requested reschedule for negative value %u, ignoring", (unsigned) result);
	}

	Alarm *alarm;
};

bool alarmInitComplete = false;
int alarmTimer = 0;

void __KernelTriggerAlarm(u64 userdata, int cyclesLate);

void __KernelAlarmInit()
{
	alarmTimer = CoreTiming::RegisterEvent("Alarm", __KernelTriggerAlarm);

	alarmInitComplete = true;
}

void __KernelTriggerAlarm(u64 userdata, int cyclesLate)
{
	int uid = (int) userdata;

	u32 error;
	Alarm *alarm = kernelObjects.Get<Alarm>(uid, error);

	// TODO: Need to find out the return value.
	if (alarm)
		__TriggerInterrupt(PSP_SYSTIMER0_INTR, uid);
}

void __KernelScheduleAlarm(Alarm *alarm, int ticks)
{
	alarm->alm.schedule = CoreTiming::GetTicks() + ticks;
	CoreTiming::ScheduleEvent((int) ticks, alarmTimer, alarm->GetUID());
}

SceUID __KernelSetAlarm(u64 ticks, u32 handlerPtr, u32 commonPtr)
{
	if (!alarmInitComplete)
		__KernelAlarmInit();

	Alarm *alarm = new Alarm;
	SceUID uid = kernelObjects.Create(alarm);

	alarm->alm.size = sizeof(NativeAlarm);
	alarm->alm.schedule = CoreTiming::GetTicks() + ticks;
	alarm->alm.handlerPtr = handlerPtr;
	alarm->alm.commonPtr = commonPtr;

	u32 error = __RegisterSubInterruptHandler(PSP_SYSTIMER0_INTR, uid, new AlarmIntrHandler(alarm));
	if (error != 0)
		return error;

	__KernelScheduleAlarm(alarm, (int) ticks);
	return uid;
}

SceUID sceKernelSetAlarm(SceUInt micro, u32 handlerPtr, u32 commonPtr)
{
	DEBUG_LOG(HLE, "sceKernelSetAlarm(%d, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm(usToCycles((int) micro), handlerPtr, commonPtr);
}

SceUID sceKernelSetSysClockAlarm(u32 ticksPtr, u32 handlerPtr, u32 commonPtr)
{
	u64 ticks;

	if (Memory::IsValidAddress(ticksPtr))
		ticks = Memory::Read_U64(ticksPtr);
	// TODO: What to do when invalid?
	else
		return -1;

	ERROR_LOG(HLE, "UNTESTED sceKernelSetSysClockAlarm(%lld, %08x, %08x)", ticks, handlerPtr, commonPtr);
	// TODO: Is this precise or is this relative?
	return __KernelSetAlarm(ticks, handlerPtr, commonPtr);
}

int sceKernelCancelAlarm(SceUID uid)
{
	DEBUG_LOG(HLE, "sceKernelCancelAlarm(%08x)", uid);

	CoreTiming::UnscheduleEvent(alarmTimer, uid);
	__ReleaseSubInterruptHandler(PSP_SYSTIMER0_INTR, uid);

	return kernelObjects.Destroy<Alarm>(uid);
}

int sceKernelReferAlarmStatus(SceUID uid, u32 infoPtr)
{
	ERROR_LOG(HLE, "UNIMPL sceKernelReferAlarmStatus(%08x, %08x)", uid, infoPtr);
	return -1;
}