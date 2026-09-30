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
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelAlarm.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
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
			u32 error;
			PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(alarmID, error);
			if (alarm) {
				DEBUG_LOG(Log::sceKernel, "Rescheduling alarm %08x for +%dus", alarmID, result);
				__KernelScheduleAlarm(alarm, result);
			} else {
				// The handler can have deleted its own alarm, in which case there's nothing to reschedule.
				WARN_LOG(Log::sceKernel, "Alarm %08x requested a reschedule but no longer exists", alarmID);
			}
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
	// On hardware a thread that keeps running loses ~70us to an alarm handler, and one the handler
	// wakes runs ~50us after it (pspautotests threads/scheduling/alarmcosts).
	__SetIntrHandlerCosts(PSP_SYSTIMER0_INTR, (int)usToCycles(17), (int)usToCycles(40));
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
	return new PSPAlarm();
}

// Re-arms an alarm from its handler's return value. That counts from the previous deadline, so a
// repeating alarm doesn't drift by the time it takes to get into and out of the handler
// (pspautotests threads/alarm/set) - unless that's already gone by, say with interrupts suspended
// for a while, when it counts from now instead of firing to catch up (threads/alarm/alarm).
void __KernelScheduleAlarm(PSPAlarm *alarm, u64 micro) {
	const u64 now = CoreTiming::GetGlobalTimeUs();
	alarm->alm.schedule = alarm->alm.schedule + micro > now ? alarm->alm.schedule + micro : now + micro;
	CoreTiming::ScheduleEvent(usToCycles(alarm->alm.schedule - now), alarmTimer, alarm->GetUID());
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

	// On hardware the call takes about 40us, and the alarm doesn't go off sooner than about 215us
	// after the deadline is taken however short it's asked to be (pspautotests
	// threads/scheduling/alarmcosts). The status still shows the time asked for.
	hleEatCycles(usToCycles(20));
	alarm->alm.schedule = CoreTiming::GetGlobalTimeUs() + micro;
	// Clamped to a few thousand years, so the conversion to cycles doesn't overflow.
	CoreTiming::ScheduleEvent(usToCycles((s64)std::clamp(micro, (u64)215, (u64)1 << 52)), alarmTimer, alarm->GetUID());
	hleEatCycles(usToCycles(20));
	return uid;
}

SceUID sceKernelSetAlarm(SceUInt micro, u32 handlerPtr, u32 commonPtr) {
	return hleLogDebug(Log::sceKernel, __KernelSetAlarm((u64) micro, handlerPtr, commonPtr));
}

SceUID sceKernelSetSysClockAlarm(u32 microPtr, u32 handlerPtr, u32 commonPtr) {
	u64 micro;
	// Note: we read 8 bytes here, so the whole range has to be valid, not just the first word.
	if (Memory::IsValid4AlignedRange(microPtr, 8))
		micro = Memory::ReadUnchecked_U64(microPtr);
	else
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid microPtr");

	return hleLogDebug(Log::sceKernel, __KernelSetAlarm(micro, handlerPtr, commonPtr));
}

int sceKernelCancelAlarm(SceUID uid) {
	CoreTiming::UnscheduleEvent(alarmTimer, uid);

	return hleLogDebug(Log::sceKernel, kernelObjects.Destroy<PSPAlarm>(uid));
}

int sceKernelReferAlarmStatus(SceUID uid, u32 infoPtr) {
	u32 error;
	PSPAlarm *alarm = kernelObjects.Get<PSPAlarm>(uid, error);
	if (!alarm) {
		return hleLogError(Log::sceKernel, error, "invalid alarm");
	}

	if (!Memory::IsValidRange(infoPtr, 20)) {
		return hleLogError(Log::sceKernel, -1);
	}

	u32 size = Memory::ReadUnchecked_U32(infoPtr);

	// Alarms actually respect size and write (kinda) what it can hold.
	if (size > 0)
		Memory::WriteUnchecked_U32(alarm->alm.size, infoPtr);
	if (size > 4)
		Memory::WriteUnchecked_U64(alarm->alm.schedule, infoPtr + 4);
	if (size > 12)
		Memory::WriteUnchecked_U32(alarm->alm.handlerPtr, infoPtr + 12);
	if (size > 16)
		Memory::WriteUnchecked_U32(alarm->alm.commonPtr, infoPtr + 16);

	return hleLogDebug(Log::sceKernel, 0);
}
