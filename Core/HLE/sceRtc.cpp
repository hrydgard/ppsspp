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
#else
#include <sys/time.h>
#endif

#include <time.h>
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

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

void gettimeofday(timeval *tv, void *ignore)
{
	FILETIME ft_utc, ft_local;
	GetSystemTimeAsFileTime(&ft_utc);
	ft_local = ft_utc;

	u64 from_1601_us = (((u64) ft_local.dwHighDateTime << 32ULL) + (u64) ft_local.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	tv->tv_sec = long(from_1970_us / 1000000UL);
	tv->tv_usec = from_1970_us % 1000000UL;
}
#endif

void __RtcTmToPspTime(ScePspDateTime &t, tm *val)
{
	t.year = val->tm_year + 1900;
	t.month = val->tm_mon + 1;
	t.day = val->tm_mday;
	t.hour = val->tm_hour;
	t.minute = val->tm_min;
	t.second = val->tm_sec;
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

u32 sceRtcGetCurrentClock(u32 pspTimePtr, int tz)
{
	DEBUG_LOG(HLE,"sceRtcGetCurrentClock(%08x, %d)", pspTimePtr, tz);
	timeval tv;
	gettimeofday(&tv, NULL);

	time_t sec = (time_t) tv.tv_sec;
	tm *utc = gmtime(&sec);

	utc->tm_isdst = -1;
	utc->tm_min += tz;
	mktime(utc);

	ScePspDateTime ret;
	__RtcTmToPspTime(ret, utc);
	ret.microsecond = tv.tv_usec;

	Memory::WriteStruct(pspTimePtr, &ret);

	return 0;
}

u32 sceRtcGetCurrentClockLocalTime(u32 pspTimePtr)
{
	DEBUG_LOG(HLE,"sceRtcGetCurrentClockLocalTime(%08x)", pspTimePtr);
	timeval tv;
	gettimeofday(&tv, NULL);

	time_t sec = (time_t) tv.tv_sec;
	tm *local = localtime(&sec);

	ScePspDateTime ret;
	__RtcTmToPspTime(ret, local);
	ret.microsecond = tv.tv_usec;

	Memory::WriteStruct(pspTimePtr, &ret);

	return 0;
}

u32 sceRtcGetTick()
{
	ERROR_LOG(HLE,"UNIMPL 0=sceRtcGetTick(...)");
	return 0;
}

u32 sceRtcGetDayOfWeek(u32 year, u32 month, u32 day)
{
	DEBUG_LOG(HLE,"sceRtcGetDayOfWeek(%d, %d, %d)", year, month, day);
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
	{0x4cfa57b0, WrapU_UI<sceRtcGetCurrentClock>, "sceRtcGetCurrentClock"},
	{0xE7C27D1B, WrapU_U<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime"},
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

