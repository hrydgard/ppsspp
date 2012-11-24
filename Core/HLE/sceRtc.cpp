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
// This is # of microseconds between January 1, 0001 and January 1, 1970.
const u64 rtcMagicOffset = 62135596800000000L;

u64 __RtcGetCurrentTick()
{
	// TODO: It's probably expecting ticks since January 1, 0001?
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

//based on  http://stackoverflow.com/a/11197532
void __RtcTicksToPspTime(ScePspDateTime &t, u64 ticks)
{
	u64 sec;
	u16 quadricentennials, centennials, quadrennials, annuals/*1-ennial?*/;
	u16 year, leap;
	u16 yday, hour, min;
	u16 month, mday, wday;
	static const u16 daysSinceJan1st[2][13]=
	{
		{0,31,59,90,120,151,181,212,243,273,304,334,365}, // 365 days, non-leap
		{0,31,60,91,121,152,182,213,244,274,305,335,366}  // 366 days, leap
	};
	sec = ticks / 1000000UL;
	wday = (u16)((sec / 86400 + 1) % 7); // day of week

	// Remove multiples of 400 years (incl. 97 leap days)
	quadricentennials = (u16)(sec / 12622780800ULL); // 400*365.2425*24*3600
	sec %= 12622780800ULL;

	// Remove multiples of 100 years (incl. 24 leap days), can't be more than 3
	// (because multiples of 4*100=400 years (incl. leap days) have been removed)
	centennials = (u16)(sec / 3155673600ULL); // 100*(365+24/100)*24*3600
	if (centennials > 3)
	{
		centennials = 3;
	}
	sec -= centennials * 3155673600ULL;

	// Remove multiples of 4 years (incl. 1 leap day), can't be more than 24
	// (because multiples of 25*4=100 years (incl. leap days) have been removed)
	quadrennials = (u16)(sec / 126230400); // 4*(365+1/4)*24*3600
	if (quadrennials > 24)
	{
		quadrennials = 24;
	}
	sec -= quadrennials * 126230400ULL;

	// Remove multiples of years (incl. 0 leap days), can't be more than 3
	// (because multiples of 4 years (incl. leap days) have been removed)
	annuals = (u16)(sec / 31536000); // 365*24*3600
	if (annuals > 3)
	{
		annuals = 3;
	}
	sec -= annuals * 31536000ULL;

	// Calculate the year and find out if it's leap
	year = 1 + quadricentennials * 400 + centennials * 100 + quadrennials * 4 + annuals;
	leap = !(year % 4) && (year % 100 || !(year % 400));

	// Calculate the day of the year and the time
	yday = sec / 86400;
	sec %= 86400;
	hour = sec / 3600;
	sec %= 3600;
	min = sec / 60;
	sec %= 60;

	// Calculate the month
	for (mday = month = 1; month < 13; month++)
	{
		if (yday < daysSinceJan1st[leap][month])
		{
			mday += yday - daysSinceJan1st[leap][month - 1];
			break;
		}
	}

	t.year = year;
	t.month = month;
	t.day = mday;
	t.hour = hour;
	t.minute = min;
	t.second = sec;
	t.microsecond = ticks % 1000000;
}



bool __RtcValidatePspTime(ScePspDateTime &t)
{
	return t.year > 0;
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
		Memory::Write_U64(curTick, tickPtr);

	return 0;
}

u64 sceRtcGetAcculumativeTime() 
{
	DEBUG_LOG(HLE, "sceRtcGetAcculumativeTime()");
	return __RtcGetCurrentTick();
}

u32 sceRtcGetCurrentClock(u32 pspTimePtr, int tz)
{
	DEBUG_LOG(HLE, "sceRtcGetCurrentClock(%08x, %d)", pspTimePtr, tz);
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

	if (Memory::IsValidAddress(pspTimePtr))
		Memory::WriteStruct(pspTimePtr, &ret);

	return 0;
}

u32 sceRtcGetCurrentClockLocalTime(u32 pspTimePtr)
{
	DEBUG_LOG(HLE, "sceRtcGetCurrentClockLocalTime(%08x)", pspTimePtr);
	timeval tv;
	gettimeofday(&tv, NULL);

	time_t sec = (time_t) tv.tv_sec;
	tm *local = localtime(&sec);

	ScePspDateTime ret;
	__RtcTmToPspTime(ret, local);
	ret.microsecond = tv.tv_usec;

	if (Memory::IsValidAddress(pspTimePtr))
		Memory::WriteStruct(pspTimePtr, &ret);

	return 0;
}

u32 sceRtcSetTick(u32 pspTimePtr, u32 tickPtr)
{
	DEBUG_LOG(HLE, "sceRtcSetTick(%08x, %08x)", pspTimePtr, tickPtr);
	if (Memory::IsValidAddress(pspTimePtr) && Memory::IsValidAddress(tickPtr))
	{
		time_t seconds = Memory::Read_U64(tickPtr);

		ScePspDateTime ret;
		__RtcTicksToPspTime(ret, seconds);
		Memory::WriteStruct(pspTimePtr, &ret);
	}
	return 0;
}

u32 sceRtcGetTick(u32 pspTimePtr, u32 tickPtr)
{
	DEBUG_LOG(HLE, "sceRtcGetTick(%08x, %08x)", pspTimePtr, tickPtr);
	ScePspDateTime pt;

	if (Memory::IsValidAddress(pspTimePtr) && Memory::IsValidAddress(tickPtr))
	{
		Memory::ReadStruct(pspTimePtr, &pt);

		if (!__RtcValidatePspTime(pt))
			return SCE_KERNEL_ERROR_INVALID_VALUE;

		tm local;
		local.tm_year = pt.year - 1900;
		local.tm_mon = pt.month - 1;
		local.tm_mday = pt.day;
		local.tm_wday = -1;
		local.tm_yday = -1;
		local.tm_hour = pt.hour;
		local.tm_min = pt.minute;
		local.tm_sec = pt.second;
		local.tm_isdst = -1;

		time_t seconds = mktime(&local);
		u64 result = rtcMagicOffset + (u64) seconds * 1000000ULL;
		result += pt.microsecond;

		Memory::Write_U64(result, tickPtr);
	}

	return 0;
}

u32 sceRtcGetDayOfWeek(u32 year, u32 month, u32 day)
{
	DEBUG_LOG(HLE, "sceRtcGetDayOfWeek(%d, %d, %d)", year, month, day);
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
	DEBUG_LOG(HLE, "sceRtcGetDaysInMonth(%d, %d)", year, month);
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
	{0x7ED29E40, WrapU_UU<sceRtcSetTick>, "sceRtcSetTick"},
	{0x6FF40ACC, WrapU_UU<sceRtcGetTick>, "sceRtcGetTick"},
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

