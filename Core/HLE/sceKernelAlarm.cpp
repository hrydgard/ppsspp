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

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelAlarm.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/HLE.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Common/ChunkFile.h"
#include <list>

const int NATIVEALARM_SIZE = 20;

std::list<SceUID> triggeredAlarm;

struct NativeAlarm
{
	SceSize_le size;
	u32_le pad;
	u64_le schedule;
	u32_le handlerPtr;
	u32_le commonPtr;
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
		auto s = p.Section("Alarm", 1);
		if (!s)
			return;

		p.Do(alm);
	}

	NativeAlarm alm;
};

void __KernelScheduleAlarm(Alarm *alarm, u64 micro);

class AlarmIntrHandler : public IntrHandler
{
public:
	AlarmIntrHandler() : IntrHandler(PSP_SYSTIMER0_INTR) {}

	bool run(PendingInterrupt& pend) override
	{
		u32 error;
		int alarmID = triggeredAlarm.front();

		Alarm *alarm = kernelObjects.Get<Alarm>(alarmID, error);
		if (error)
		{
			WARN_LOG(SCEKERNEL, "Ignoring deleted alarm %08x", alarmID);
			return false;
		}

		currentMIPS->pc = alarm->alm.handlerPtr;
		currentMIPS->r[MIPS_REG_A0] = alarm->alm.commonPtr;
		DEBUG_LOG(SCEKERNEL, "Entering alarm %08x handler: %08x", alarmID, currentMIPS->pc);

		return true;
	}

	void handleResult(PendingInterrupt& pend) override
	{
		int result = currentMIPS->r[MIPS_REG_V0];

		int alarmID = triggeredAlarm.front();
		triggeredAlarm.pop_front();

		// A non-zero result means to reschedule.
		if (result > 0)
		{
			DEBUG_LOG(SCEKERNEL, "Rescheduling alarm %08x for +%dms", alarmID, result);
			u32 error;
			Alarm *alarm = kernelObjects.Get<Alarm>(alarmID, error);
			__KernelScheduleAlarm(alarm, result);
		}
		else
		{
			if (result < 0)
				WARN_LOG(SCEKERNEL, "Alarm requested reschedule for negative value %u, ignoring", (unsigned) result);

			DEBUG_LOG(SCEKERNEL, "Finished alarm %08x", alarmID);

			// Delete the alarm if it's not rescheduled.
			kernelObjects.Destroy<Alarm>(alarmID);
		}
	}
};

static int alarmTimer = -1;

static void __KernelTriggerAlarm(u64 userdata, int cyclesLate)
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
	auto s = p.Section("sceKernelAlarm", 1);
	if (!s)
		return;

	p.Do(alarmTimer);
	p.Do(triggeredAlarm);
	CoreTiming::RestoreRegisterEvent(alarmTimer, "Alarm", __KernelTriggerAlarm);
}

KernelObject *__KernelAlarmObject()
{
	// Default object to load from state.
	return new Alarm;
}

void __KernelScheduleAlarm(Alarm *alarm, u64 micro)
{
	alarm->alm.schedule = CoreTiming::GetGlobalTimeUs() + micro;
	CoreTiming::ScheduleEvent(usToCycles(micro), alarmTimer, alarm->GetUID());
}

static SceUID __KernelSetAlarm(u64 micro, u32 handlerPtr, u32 commonPtr)
{
	if (!Memory::IsValidAddress(handlerPtr))
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	Alarm *alarm = new Alarm;
	SceUID uid = kernelObjects.Create(alarm);

	alarm->alm.size = NATIVEALARM_SIZE;
	alarm->alm.handlerPtr = handlerPtr;
	alarm->alm.commonPtr = commonPtr;

	__KernelScheduleAlarm(alarm, micro);
	return uid;
}

SceUID sceKernelSetAlarm(SceUInt micro, u32 handlerPtr, u32 commonPtr)
{
	DEBUG_LOG(SCEKERNEL, "sceKernelSetAlarm(%d, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm((u64) micro, handlerPtr, commonPtr);
}

SceUID sceKernelSetSysClockAlarm(u32 microPtr, u32 handlerPtr, u32 commonPtr)
{
	u64 micro;

	if (Memory::IsValidAddress(microPtr))
		micro = Memory::Read_U64(microPtr);
	else
		return -1;

	DEBUG_LOG(SCEKERNEL, "sceKernelSetSysClockAlarm(%lld, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm(micro, handlerPtr, commonPtr);
}

int sceKernelCancelAlarm(SceUID uid)
{
	DEBUG_LOG(SCEKERNEL, "sceKernelCancelAlarm(%08x)", uid);

	CoreTiming::UnscheduleEvent(alarmTimer, uid);

	return kernelObjects.Destroy<Alarm>(uid);
}

int sceKernelReferAlarmStatus(SceUID uid, u32 infoPtr)
{
	u32 error;
	Alarm *alarm = kernelObjects.Get<Alarm>(uid, error);
	if (!alarm)
	{
		ERROR_LOG(SCEKERNEL, "sceKernelReferAlarmStatus(%08x, %08x): invalid alarm", uid, infoPtr);
		return error;
	}

	DEBUG_LOG(SCEKERNEL, "sceKernelReferAlarmStatus(%08x, %08x)", uid, infoPtr);

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