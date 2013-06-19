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
#include "Core/CoreTiming.h"
#include "ChunkFile.h"
#include <list>

const int NATIVEALARM_SIZE = 20;

std::list<SceUID> triggeredAlarm;

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
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Alarm; }
	int GetIDType() const { return SCE_KERNEL_TMID_Alarm; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(alm);
		p.DoMarker("Alarm");
	}

	NativeAlarm alm;
};

void __KernelScheduleAlarm(Alarm *alarm, u64 ticks);

class AlarmIntrHandler : public IntrHandler
{
public:
	AlarmIntrHandler() : IntrHandler(PSP_SYSTIMER0_INTR) {}

	virtual bool run(PendingInterrupt& pend)
	{
		u32 error;
		int alarmID = triggeredAlarm.front();

		Alarm *alarm = kernelObjects.Get<Alarm>(alarmID, error);
		if (error)
		{
			WARN_LOG(HLE, "Ignoring deleted alarm %08x", alarmID);
			return false;
		}

		currentMIPS->pc = alarm->alm.handlerPtr;
		currentMIPS->r[MIPS_REG_A0] = alarm->alm.commonPtr;
		DEBUG_LOG(HLE, "Entering alarm %08x handler: %08x", alarmID, currentMIPS->pc);

		return true;
	}

	virtual void handleResult(PendingInterrupt& pend)
	{
		int result = currentMIPS->r[MIPS_REG_V0];

		int alarmID = triggeredAlarm.front();
		triggeredAlarm.pop_front();

		// A non-zero result means to reschedule.
		if (result > 0)
		{
			DEBUG_LOG(HLE, "Rescheduling alarm %08x for +%dms", alarmID, result);
			u32 error;
			Alarm *alarm = kernelObjects.Get<Alarm>(alarmID, error);
			__KernelScheduleAlarm(alarm, (u64) usToCycles(result));
		}
		else
		{
			if (result < 0)
				WARN_LOG(HLE, "Alarm requested reschedule for negative value %u, ignoring", (unsigned) result);

			DEBUG_LOG(HLE, "Finished alarm %08x", alarmID);

			// Delete the alarm if it's not rescheduled.
			kernelObjects.Destroy<Alarm>(alarmID);
		}
	}
};

static int alarmTimer = -1;

void __KernelTriggerAlarm(u64 userdata, int cyclesLate)
{
	int uid = (int) userdata;

	u32 error;
	Alarm *alarm = kernelObjects.Get<Alarm>(uid, error);
	if (alarm)
	{
		triggeredAlarm.push_back(uid);
		__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_SYSTIMER0_INTR);
	}
}

void __KernelAlarmInit()
{
	triggeredAlarm.clear();
	__RegisterIntrHandler(PSP_SYSTIMER0_INTR, new AlarmIntrHandler());
	alarmTimer = CoreTiming::RegisterEvent("Alarm", __KernelTriggerAlarm);
}

void __KernelAlarmDoState(PointerWrap &p)
{
	p.Do(alarmTimer);
	p.Do(triggeredAlarm);
	CoreTiming::RestoreRegisterEvent(alarmTimer, "Alarm", __KernelTriggerAlarm);
	p.DoMarker("sceKernelAlarm");
}

KernelObject *__KernelAlarmObject()
{
	// Default object to load from state.
	return new Alarm;
}

void __KernelScheduleAlarm(Alarm *alarm, u64 ticks)
{
	alarm->alm.schedule = (CoreTiming::GetTicks() + ticks) / (u64) CoreTiming::GetClockFrequencyMHz();
	CoreTiming::ScheduleEvent((int) ticks, alarmTimer, alarm->GetUID());
}

SceUID __KernelSetAlarm(u64 ticks, u32 handlerPtr, u32 commonPtr)
{
	if (!Memory::IsValidAddress(handlerPtr))
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	Alarm *alarm = new Alarm;
	SceUID uid = kernelObjects.Create(alarm);

	alarm->alm.size = NATIVEALARM_SIZE;
	alarm->alm.handlerPtr = handlerPtr;
	alarm->alm.commonPtr = commonPtr;

	__KernelScheduleAlarm(alarm, ticks);
	return uid;
}

SceUID sceKernelSetAlarm(SceUInt micro, u32 handlerPtr, u32 commonPtr)
{
	DEBUG_LOG(HLE, "sceKernelSetAlarm(%d, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm(usToCycles((u64) micro), handlerPtr, commonPtr);
}

SceUID sceKernelSetSysClockAlarm(u32 microPtr, u32 handlerPtr, u32 commonPtr)
{
	u64 micro;

	if (Memory::IsValidAddress(microPtr))
		micro = Memory::Read_U64(microPtr);
	else
		return -1;

	DEBUG_LOG(HLE, "sceKernelSetSysClockAlarm(%lld, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm(usToCycles(micro), handlerPtr, commonPtr);
}

int sceKernelCancelAlarm(SceUID uid)
{
	DEBUG_LOG(HLE, "sceKernelCancelAlarm(%08x)", uid);

	CoreTiming::UnscheduleEvent(alarmTimer, uid);

	return kernelObjects.Destroy<Alarm>(uid);
}

int sceKernelReferAlarmStatus(SceUID uid, u32 infoPtr)
{
	u32 error;
	Alarm *alarm = kernelObjects.Get<Alarm>(uid, error);
	if (!alarm)
	{
		ERROR_LOG(HLE, "sceKernelReferAlarmStatus(%08x, %08x): invalid alarm", uid, infoPtr);
		return error;
	}

	DEBUG_LOG(HLE, "sceKernelReferAlarmStatus(%08x, %08x)", uid, infoPtr);

	if (!Memory::IsValidAddress(infoPtr))
		return -1;

	u32 size = Memory::Read_U32(infoPtr);

	// Alarms actually respect size and write (kinda) what it can hold.
	if (size > 0)
		Memory::Write_U32(alarm->alm.size, infoPtr);
	if (size > 4)
		Memory::Write_U64(alarm->alm.schedule, infoPtr + 4);
	if (size > 12)
		Memory::Write_U32(alarm->alm.handlerPtr, infoPtr + 12);
	if (size > 16)
		Memory::Write_U32(alarm->alm.commonPtr, infoPtr + 16);

	return 0;
}