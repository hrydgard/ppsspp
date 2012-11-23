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

#ifdef _WIN32
#include <windows.h>
#endif

#include "base/timeutil.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceRtc.h"
#include "../CoreTiming.h"

// Grabbed from JPSCP
const u64 rtcMagicOffset = 62135596800000000L;

u64 __RtcGetCurrentTick()
{
	return cyclesToUs(CoreTiming::GetTicks()) + rtcMagicOffset;
}

u32 sceRtcGetTickResolution()
{
	DEBUG_LOG(HLE, "sceRtcGetTickResolution()");
	return 1000000;
}

u32 sceRtcGetCurrentTick(u32 tickPtr)
{
	DEBUG_LOG(HLE, "sceRtcGetCurrentTick(%08x)", tickPtr);

	u64 curTick = __RtcGetCurrentTick();
	if (Memory::IsValidAddress(tickPtr))
	{
		Memory::Write_U32(tickPtr, curTick & 0xFFFFFFFF);
		Memory::Write_U32(tickPtr + 4, (curTick >> 32) & 0xFFFFFFFF);
	}

	return 0;
}

u64 sceRtcGetAcculumativeTime() 
{
	DEBUG_LOG(HLE, "sceRtcGetAcculumativeTime()");
	return __RtcGetCurrentTick();
}

u32 sceRtcGetCurrentClockLocalTime()
{
	ERROR_LOG(HLE,"UNIMPL 0=sceRtcGetCurrentClockLocalTime()");
	return 0;
}
u32 sceRtcGetTick()
{
	ERROR_LOG(HLE,"UNIMPL 0=sceRtcGetTick(...)");
	return 0;
}

u32 sceRtcGetDayOfWeek(u32 year, u32 month, u32 day)
{
	DEBUG_LOG(HLE,"sceRtcGetDayOfWeek()");
	static u32 t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (month > 12 || month < 1) {
		// Preventive crashfix
		ERROR_LOG(HLE,"Bad month");
		return 0;
	}
	year -= month < 3;
	return ( year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
}

u32 sceRtcGetDaysInMonth(u32 year, u32 month)
{
	DEBUG_LOG(HLE,"sceRtcGetDaysInMonth(%d, %d)", year, month);
	u32 numberOfDays;

	if (year <= 0 || month <= 0 || month > 12)
		return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

	switch (month)
	{
	case 4:
	case 6:
	case 9:
	case 11:
		numberOfDays = 30;
		break;
	case 2:
		if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
			numberOfDays = 29;
		else
			numberOfDays = 28;
		break;

	default:
		numberOfDays = 31;
		break;
	}

	return numberOfDays;
}

const HLEFunction sceRtc[] = 
{
	{0xC41C2853, WrapU_V<sceRtcGetTickResolution>, "sceRtcGetTickResolution"},
	{0x3f7ad767, WrapU_U<sceRtcGetCurrentTick>, "sceRtcGetCurrentTick"},	
	{0x011F03C1, WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumulativeTime"},
	{0x029CA3B3, WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumlativeTime"},
	{0x4cfa57b0, 0, "sceRtcGetCurrentClock"},
	{0xE7C27D1B, WrapU_V<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime"},
	{0x34885E0D, 0, "sceRtcConvertUtcToLocalTime"},
	{0x779242A2, 0, "sceRtcConvertLocalTimeToUTC"},
	{0x42307A17, 0, "sceRtcIsLeapYear"},
	{0x05ef322c, WrapU_UU<sceRtcGetDaysInMonth>, "sceRtcGetDaysInMonth"},
	{0x57726bc1, WrapU_UUU<sceRtcGetDayOfWeek>, "sceRtcGetDayOfWeek"},
	{0x4B1B5E82, 0, "sceRtcCheckValid"},
	{0x3a807cc8, 0, "sceRtcSetTime_t"},
	{0x27c4594c, 0, "sceRtcGetTime_t"},
	{0xF006F264, 0, "sceRtcSetDosTime"},
	{0x36075567, 0, "sceRtcGetDosTime"},
	{0x7ACE4C04, 0, "sceRtcSetWin32FileTime"},
	{0xCF561893, 0, "sceRtcGetWin32FileTime"},
	{0x7ED29E40, 0, "sceRtcSetTick"},
	{0x6FF40ACC, WrapU_V<sceRtcGetTick>, "sceRtcGetTick"},
	{0x9ED0AE87, 0, "sceRtcCompareTick"},
	{0x44F45E05, 0, "sceRtcTickAddTicks"},
	{0x26D25A5D, 0, "sceRtcTickAddMicroseconds"},
	{0xF2A4AFE5, 0, "sceRtcTickAddSeconds"},
	{0xE6605BCA, 0, "sceRtcTickAddMinutes"},
	{0x26D7A24A, 0, "sceRtcTickAddHours"},
	{0xE51B4B7A, 0, "sceRtcTickAddDays"},
	{0xCF3A2CA8, 0, "sceRtcTickAddWeeks"},
	{0xDBF74F1B, 0, "sceRtcTickAddMonths"},
	{0x42842C77, 0, "sceRtcTickAddYears"},
	{0xC663B3B9, 0, "sceRtcFormatRFC2822"},
	{0x7DE6711B, 0, "sceRtcFormatRFC2822LocalTime"},
	{0x0498FB3C, 0, "sceRtcFormatRFC3339"},
	{0x27F98543, 0, "sceRtcFormatRFC3339LocalTime"},
	{0xDFBC5F16, 0, "sceRtcParseDateTime"},
	{0x28E1E988, 0, "sceRtcParseRFC3339"},
};

void Register_sceRtc()
{
	RegisterModule("sceRtc", ARRAY_SIZE(sceRtc), sceRtc);
}

