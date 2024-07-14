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

#include <list>
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeList.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelAlarm.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/HLE.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"

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

struct PSPAlarm : public KernelObject {
	const char *GetName() override {return "[Alarm]";}
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Alarm"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_ALMID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Alarm; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Alarm; }

	void DoState(PointerWrap &p) override {
		auto s = p.Section("Alarm", 1);
		if (!s)
			return;

		Do(p, alm);
	}

	NativeAlarm alm;
};

void __KernelScheduleAlarm(PSPAlarm *alarm, u64 micro);

class AlarmIntrHandler : public IntrHandler
{
public:
	AlarmIntrHandler() : IntrHandler(PSP_SYSTIMER0_INTR) {}

	bool run(PendingInterrupt& pend) override
	{
		u32 error;
		int alarmID = triggeredAlarm.front();

		PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(alarmID, error);
		if (error)
		{
			WARN_LOG(Log::sceKernel, "Ignoring deleted alarm %08x", alarmID);
			return false;
		}

		currentMIPS->pc = alarm->alm.handlerPtr;
		currentMIPS->r[MIPS_REG_A0] = alarm->alm.commonPtr;
		DEBUG_LOG(Log::sceKernel, "Entering alarm %08x handler: %08x", alarmID, currentMIPS->pc);

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
			DEBUG_LOG(Log::sceKernel, "Rescheduling alarm %08x for +%dms", alarmID, result);
			u32 error;
			PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(alarmID, error);
			__KernelScheduleAlarm(alarm, result);
		}
		else
		{
			if (result < 0)
				WARN_LOG(Log::sceKernel, "Alarm requested reschedule for negative value %u, ignoring", (unsigned) result);

			DEBUG_LOG(Log::sceKernel, "Finished alarm %08x", alarmID);

			// Delete the alarm if it's not rescheduled.
			kernelObjects.Destroy<PSPAlarm>(alarmID);
		}
	}
};

static int alarmTimer = -1;

static void __KernelTriggerAlarm(u64 userdata, int cyclesLate) {
	int uid = (int) userdata;

	u32 error;
	PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(uid, error);
	if (alarm) {
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

	Do(p, alarmTimer);
	Do(p, triggeredAlarm);
	CoreTiming::RestoreRegisterEvent(alarmTimer, "Alarm", __KernelTriggerAlarm);
}

KernelObject *__KernelAlarmObject() {
	// Default object to load from state.
	return new PSPAlarm;
}

void __KernelScheduleAlarm(PSPAlarm *alarm, u64 micro) {
	alarm->alm.schedule = CoreTiming::GetGlobalTimeUs() + micro;
	CoreTiming::ScheduleEvent(usToCycles(micro), alarmTimer, alarm->GetUID());
}

static SceUID __KernelSetAlarm(u64 micro, u32 handlerPtr, u32 commonPtr)
{
	if (!Memory::IsValidAddress(handlerPtr))
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	PSPAlarm *alarm = new PSPAlarm();
	SceUID uid = kernelObjects.Create(alarm);

	alarm->alm.size = NATIVEALARM_SIZE;
	alarm->alm.handlerPtr = handlerPtr;
	alarm->alm.commonPtr = commonPtr;

	__KernelScheduleAlarm(alarm, micro);
	return uid;
}

SceUID sceKernelSetAlarm(SceUInt micro, u32 handlerPtr, u32 commonPtr)
{
	DEBUG_LOG(Log::sceKernel, "sceKernelSetAlarm(%d, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm((u64) micro, handlerPtr, commonPtr);
}

SceUID sceKernelSetSysClockAlarm(u32 microPtr, u32 handlerPtr, u32 commonPtr)
{
	u64 micro;

	if (Memory::IsValidAddress(microPtr))
		micro = Memory::Read_U64(microPtr);
	else
		return -1;

	DEBUG_LOG(Log::sceKernel, "sceKernelSetSysClockAlarm(%lld, %08x, %08x)", micro, handlerPtr, commonPtr);
	return __KernelSetAlarm(micro, handlerPtr, commonPtr);
}

int sceKernelCancelAlarm(SceUID uid)
{
	DEBUG_LOG(Log::sceKernel, "sceKernelCancelAlarm(%08x)", uid);

	CoreTiming::UnscheduleEvent(alarmTimer, uid);

	return kernelObjects.Destroy<PSPAlarm>(uid);
}

int sceKernelReferAlarmStatus(SceUID uid, u32 infoPtr)
{
	u32 error;
	PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(uid, error);
	if (!alarm)
	{
		ERROR_LOG(Log::sceKernel, "sceKernelReferAlarmStatus(%08x, %08x): invalid alarm", uid, infoPtr);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelReferAlarmStatus(%08x, %08x)", uid, infoPtr);

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
