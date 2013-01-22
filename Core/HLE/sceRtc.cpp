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

const int PSP_TIME_INVALID_YEAR = -1;
const int PSP_TIME_INVALID_MONTH = -2;
const int PSP_TIME_INVALID_DAY = -3;
const int PSP_TIME_INVALID_HOUR = -4;
const int PSP_TIME_INVALID_MINUTES = -5;
const int PSP_TIME_INVALID_SECONDS = -6;
const int PSP_TIME_INVALID_MICROSECONDS = -7;

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
//TODO: Doesn't take PSPs daylight saving into account and doesnt check for overflow errors
void __RtcTicksToPspTime(ScePspDateTime &t, u64 ticks)
{
	u64 sec;
	u16 quadricentennials, centennials, quadrennials, annuals;
	u16 year, leap;
	u16 yday, hour, min;
	u16 month, mday;
	static const u16 daysSinceJan1st[2][13]=
	{
		{0,31,59,90,120,151,181,212,243,273,304,334,365}, // 365 days, non-leap
		{0,31,60,91,121,152,182,213,244,274,305,335,366}  // 366 days, leap
	};
	sec = ticks / 1000000UL;

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
	yday = (u16)(sec / 86400);
	sec %= 86400;
	hour = (u16)(sec / 3600);
	sec %= 3600;
	min = (u16)(sec / 60);
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
	t.second = (u16)sec;
	t.microsecond = ticks % 1000000;
}

u64 JumpYMD(u64 year, u64 month, u64 day) {
	return 367*year - 7*(year+(month+9)/12)/4 + 275*month/9 + day;
}

u64 JumpSeconds(u64 year, u64 month, u64 day, u64 hour, u64 minute, u64 second) {
	static const u64 secs_per_day = 24 * 60 * 60;
	return JumpYMD(year, month, day) * secs_per_day + hour * 3600 + minute * 60 + second;
}

u64 __RtcPspTimeToTicks(ScePspDateTime &t)
{
	u64 seconds = JumpSeconds(t.year, t.month, t.day, t.hour, t.minute, t.second);
	u64 ticks = (seconds * 1000000UL) + t.microsecond;

	return ticks;
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
	//Don't spam the log
	//DEBUG_LOG(HLE, "sceRtcGetCurrentTick(%08x)", tickPtr);

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
		u64 ticks = Memory::Read_U64(tickPtr);

		ScePspDateTime ret;
		__RtcTicksToPspTime(ret, ticks);
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

	if(month == 0)	// Mark month 0 as august, don't know why, but works
	{
		month = 8;
	}

	if(month > 12) // After month 12, psp months does 31/31/30/31/30 and repeat
	{
		int restMonth = month-12;
		int grp5 = restMonth / 5;
		restMonth = restMonth % 5;
		day += grp5 * (31*3+30*2);
		static u32 t[] = { 31, 31*2, 31*2+30, 31*3+30, 31*3+30*2 };
		day += t[restMonth-1];
		month = 12;
	}

	tm local;
	local.tm_year = year - 1900;
	local.tm_mon = month - 1;
	local.tm_mday = day;
	local.tm_wday = -1;
	local.tm_yday = -1;
	local.tm_hour = 0;
	local.tm_min = 0;
	local.tm_sec = 0;
	local.tm_isdst = -1;

	mktime(&local);
	return local.tm_wday;
}

u32 sceRtcGetDaysInMonth(u32 year, u32 month)
{
	DEBUG_LOG(HLE, "sceRtcGetDaysInMonth(%d, %d)", year, month);
	u32 numberOfDays;

	if (year == 0 || month == 0 || month > 12)
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

u32 sceRtcIsLeapYear(u32 year)
{
	ERROR_LOG(HLE, "sceRtcIsLeapYear(%d)", year);
	return (year % 4 == 0) && (!(year % 100 == 0) || (year % 400 == 0));
}

int sceRtcConvertLocalTimeToUTC(u32 tickLocalPtr,u32 tickUTCPtr)	
{
	DEBUG_LOG(HLE, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickLocalPtr);
		// TODO:convert UTC as ticks to localtime as ticks.. fake it by preteding timezone is UTC for now
		Memory::Write_U64(srcTick, tickUTCPtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcConvertUtcToLocalTime(u32 tickUTCPtr,u32 tickLocalPtr) 
{
	ERROR_LOG(HLE, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickUTCPtr);
		// TODO:convert localtime as ticks to UTC as ticks.. fake it by preteding timezone is UTC for now
		Memory::Write_U64(srcTick, tickLocalPtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcCheckValid(u32 datePtr)
{
	DEBUG_LOG(HLE, "sceRtcCheckValid(%d)", datePtr);

	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		if (pt.year < 1 || pt.year > 9999)
		{
			return PSP_TIME_INVALID_YEAR;
		}
		else if (pt.month < 1 || pt.month > 12)
		{
			return PSP_TIME_INVALID_MONTH;
		}
		else if (pt.day < 1 || pt.day > 31)
		{
			return PSP_TIME_INVALID_DAY;
		}
		else if (pt.day > 31) // TODO: Needs to check actual days in month, including leaps
		{
			return PSP_TIME_INVALID_DAY;
		}
		else if (pt.hour > 23)
		{
			return PSP_TIME_INVALID_HOUR;
		}
		else if (pt.minute > 59)
		{
			return PSP_TIME_INVALID_MINUTES;
		}
		else if (pt.second > 59)
		{
			return PSP_TIME_INVALID_SECONDS;
		}
		else if (pt.microsecond >= 1000000)
		{
			return PSP_TIME_INVALID_MICROSECONDS;
		}
		else {
			return 0;
		}
	}
	else
	{
		return -1;
	}
}

int sceRtcSetTime_t(u32 datePtr, u32 time)
{
	ERROR_LOG(HLE, "HACK sceRtcSetTime_t(%08x,%d)", datePtr, time);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;
		__RtcTicksToPspTime(pt, time*1000000ULL);
		pt.year += 1969;
		Memory::WriteStruct(datePtr, &pt);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcSetTime64_t(u32 datePtr, u64 time)
{
	ERROR_LOG(HLE, "HACK sceRtcSetTime64_t(%08x,%lld)", datePtr, time);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;
		__RtcTicksToPspTime(pt, time*1000000ULL);
		pt.year += 1969;
		Memory::WriteStruct(datePtr, &pt);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcGetTime_t(u32 datePtr, u32 timePtr)
{
	ERROR_LOG(HLE, "HACK sceRtcGetTime_t(%08x,%08x)", datePtr, timePtr);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(timePtr))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		pt.year-=1969;
		u32 result = (u32) (__RtcPspTimeToTicks(pt)/1000000ULL);
		Memory::Write_U32(result, timePtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcGetTime64_t(u32 datePtr, u32 timePtr)
{
	ERROR_LOG(HLE, "HACK sceRtcGetTime64_t(%08x,%08x)", datePtr, timePtr);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(timePtr))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		pt.year-=1969;
		u64 result = __RtcPspTimeToTicks(pt)/1000000ULL;
		Memory::Write_U64(result, timePtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcSetDosTime(u32 datePtr, u32 dosTime)
{
	ERROR_LOG(HLE, "HACK sceRtcSetDosTime(%d,%d)", datePtr, dosTime);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;

		__RtcTicksToPspTime(pt, dosTime);
		Memory::WriteStruct(datePtr, &pt);
	}
	else
	{
		return 1;
	}
	return 0;
}

int sceRtcGetDosTime(u32 datePtr, u32 dosTime)
{
	ERROR_LOG(HLE, "HACK sceRtcGetDosTime(%d,%d)", datePtr, dosTime);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(dosTime))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		u64 result = __RtcPspTimeToTicks(pt);
		Memory::Write_U64(result, dosTime);
	}
	else
	{
		return 1;
	}
	return 0;

}

int sceRtcSetWin32FileTime(u32 datePtr, u32 win32TimePtr)
{
	ERROR_LOG(HLE, "UNIMPL sceRtcSetWin32FileTime(%d,%d)", datePtr, win32TimePtr);
	return 0;
}

int sceRtcGetWin32FileTime(u32 datePtr, u32 win32TimePtr)
{
	ERROR_LOG(HLE, "UNIMPL sceRtcGetWin32FileTime(%d,%d)", datePtr, win32TimePtr);
	return 0;
}

int sceRtcCompareTick(u32 tick1Ptr, u32 tick2Ptr)
{
	ERROR_LOG(HLE, "HACK sceRtcCompareTick(%d,%d)", tick1Ptr, tick2Ptr);
	if (Memory::IsValidAddress(tick1Ptr) && Memory::IsValidAddress(tick2Ptr))
	{
		u64 tick1 = Memory::Read_U64(tick1Ptr);
		u64 tick2 = Memory::Read_U64(tick2Ptr);
		if (tick1 > tick2)
			return 1;
		if (tick1 < tick2)
			return -1;
	}
	return 0;
}

int sceRtcTickAddTicks(u32 destTickPtr, u32 srcTickPtr, u64 numTicks)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		srcTick += numTicks;
		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(HLE, "sceRtcTickAddTicks(%x,%x,%llu)", destTickPtr, srcTickPtr, numTicks);
	return 0;
}

int sceRtcTickAddMicroseconds(u32 destTickPtr,u32 srcTickPtr, u64 numMS)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numMS;
		Memory::Write_U64(srcTick, destTickPtr);
	}

	ERROR_LOG(HLE, "HACK sceRtcTickAddMicroseconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numMS);
	return 0;
}

int sceRtcTickAddSeconds(u32 destTickPtr, u32 srcTickPtr, u64 numSecs)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numSecs * 1000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	ERROR_LOG(HLE, "HACK sceRtcTickAddSeconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numSecs);
	return 0;
}

int sceRtcTickAddMinutes(u32 destTickPtr, u32 srcTickPtr, u64 numMins)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numMins*60000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	ERROR_LOG(HLE, "HACK sceRtcTickAddMinutes(%x,%x,%llu)", destTickPtr, srcTickPtr, numMins);
	return 0;
}

int sceRtcTickAddHours(u32 destTickPtr, u32 srcTickPtr, int numHours)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);
		srcTick += numHours*3600000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	ERROR_LOG(HLE, "HACK sceRtcTickAddMinutes(%d,%d,%d)", destTickPtr, srcTickPtr, numHours);
	return 0;
}

int sceRtcTickAddDays(u32 destTickPtr, u32 srcTickPtr, int numDays)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numDays*86400000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	ERROR_LOG(HLE, "HACK sceRtcTickAddDays(%d,%d,%d)", destTickPtr, srcTickPtr, numDays);
	return 0;
}

int sceRtcTickAddWeeks(u32 destTickPtr, u32 srcTickPtr, int numWeeks)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numWeeks*604800000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	ERROR_LOG(HLE, "HACK sceRtcTickAddWeeks(%d,%d,%d)", destTickPtr, srcTickPtr, numWeeks);
	return 0;
}

int sceRtcTickAddMonths(u32 destTickPtr, u32 srcTickPtr, int numMonths)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		// slightly bodgy but we need to add months to a pt and then convert to ticks to cover different day count in months and leapyears
		ScePspDateTime pt;
		memset(&pt, 0, sizeof(pt));
		if (numMonths < 0)
		{
			numMonths = -numMonths;;
			int years = numMonths /12;
			int realmonths = numMonths % 12;

			pt.year = years;
			pt.month = realmonths;
			u64 monthTicks =__RtcPspTimeToTicks(pt);

			if (monthTicks <= srcTick)
			{
				srcTick-=monthTicks;
			}
			else
			{
				srcTick=0;
			}

		}
		else
		{
			int years = numMonths /12;
			int realmonths = numMonths % 12;
			pt.year = years;
			pt.month = realmonths;
			srcTick +=__RtcPspTimeToTicks(pt);
		}
		Memory::Write_U64(srcTick, destTickPtr);
	}

	ERROR_LOG(HLE, "HACK sceRtcTickAddMonths(%d,%d,%d)", destTickPtr, srcTickPtr, numMonths);
	return 0;
}

// TODO: off by 6 days every 2000 years.
int sceRtcTickAddYears(u32 destTickPtr, u32 srcTickPtr, int numYears)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		ScePspDateTime pt;
		memset(&pt, 0, sizeof(pt));

		if (numYears < 0)
		{
			pt.year = -numYears;
			u64 yearTicks = __RtcPspTimeToTicks(pt);
			if (yearTicks <= srcTick)
			{
				srcTick-=yearTicks;
			}
			else
			{
				srcTick=0;
			}
		}
		else
		{
			pt.year = numYears;
			u64 yearTicks = __RtcPspTimeToTicks(pt);
			srcTick +=yearTicks;
		}

		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(HLE, "HACK sceRtcTickAddYears(%d,%d,%d)", destTickPtr, srcTickPtr, numYears);
	return 0;
}

int sceRtcParseDateTime(u32 destTickPtr, u32 dateStringPtr)
{
	ERROR_LOG(HLE, "UNIMPL sceRtcParseDateTime(%d,%d)", destTickPtr, dateStringPtr);
	return 0;
}

const HLEFunction sceRtc[] =
{
	{0xC41C2853, WrapU_V<sceRtcGetTickResolution>, "sceRtcGetTickResolution"},
	{0x3f7ad767, WrapU_U<sceRtcGetCurrentTick>, "sceRtcGetCurrentTick"},
	{0x011F03C1, WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumulativeTime"},
	{0x029CA3B3, WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumlativeTime"},
	{0x4cfa57b0, WrapU_UI<sceRtcGetCurrentClock>, "sceRtcGetCurrentClock"},
	{0xE7C27D1B, WrapU_U<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime"},
	{0x34885E0D, WrapI_UU<sceRtcConvertUtcToLocalTime>, "sceRtcConvertUtcToLocalTime"},
	{0x779242A2, WrapI_UU<sceRtcConvertLocalTimeToUTC>, "sceRtcConvertLocalTimeToUTC"},
	{0x42307A17, WrapU_U<sceRtcIsLeapYear>, "sceRtcIsLeapYear"},
	{0x05ef322c, WrapU_UU<sceRtcGetDaysInMonth>, "sceRtcGetDaysInMonth"},
	{0x57726bc1, WrapU_UUU<sceRtcGetDayOfWeek>, "sceRtcGetDayOfWeek"},
	{0x4B1B5E82, WrapI_U<sceRtcCheckValid>, "sceRtcCheckValid"},
	{0x3a807cc8, WrapI_UU<sceRtcSetTime_t>, "sceRtcSetTime_t"},
	{0x27c4594c, WrapI_UU<sceRtcGetTime_t>, "sceRtcGetTime_t"},
	{0xF006F264, WrapI_UU<sceRtcSetDosTime>, "sceRtcSetDosTime"},
	{0x36075567, WrapI_UU<sceRtcGetDosTime>, "sceRtcGetDosTime"},
	{0x7ACE4C04, WrapI_UU<sceRtcSetWin32FileTime>, "sceRtcSetWin32FileTime"},
	{0xCF561893, WrapI_UU<sceRtcGetWin32FileTime>, "sceRtcGetWin32FileTime"},
	{0x7ED29E40, WrapU_UU<sceRtcSetTick>, "sceRtcSetTick"},
	{0x6FF40ACC, WrapU_UU<sceRtcGetTick>, "sceRtcGetTick"},
	{0x9ED0AE87, WrapI_UU<sceRtcCompareTick>, "sceRtcCompareTick"},
	{0x44F45E05, WrapI_UUU64<sceRtcTickAddTicks>, "sceRtcTickAddTicks"},
	{0x26D25A5D, WrapI_UUU64<sceRtcTickAddMicroseconds>, "sceRtcTickAddMicroseconds"},
	{0xF2A4AFE5, WrapI_UUU64<sceRtcTickAddSeconds>, "sceRtcTickAddSeconds"},
	{0xE6605BCA, WrapI_UUU64<sceRtcTickAddMinutes>, "sceRtcTickAddMinutes"},
	{0x26D7A24A, WrapI_UUI<sceRtcTickAddHours>, "sceRtcTickAddHours"},
	{0xE51B4B7A, WrapI_UUI<sceRtcTickAddDays>, "sceRtcTickAddDays"},
	{0xCF3A2CA8, WrapI_UUI<sceRtcTickAddWeeks>, "sceRtcTickAddWeeks"},
	{0xDBF74F1B, WrapI_UUI<sceRtcTickAddMonths>, "sceRtcTickAddMonths"},
	{0x42842C77, WrapI_UUI<sceRtcTickAddYears>, "sceRtcTickAddYears"},
	{0xC663B3B9, 0, "sceRtcFormatRFC2822"},
	{0x7DE6711B, 0, "sceRtcFormatRFC2822LocalTime"},
	{0x0498FB3C, 0, "sceRtcFormatRFC3339"},
	{0x27F98543, 0, "sceRtcFormatRFC3339LocalTime"},
	{0xDFBC5F16, WrapI_UU<sceRtcParseDateTime>, "sceRtcParseDateTime"},
	{0x28E1E988, 0, "sceRtcParseRFC3339"},
	{0xe1c93e47, WrapI_UU<sceRtcGetTime64_t>, "sceRtcGetTime64_t"},
	{0x1909c99b, WrapI_UU64<sceRtcSetTime64_t>, "sceRtcSetTime64_t"},
};

void Register_sceRtc()
{
	RegisterModule("sceRtc", ARRAY_SIZE(sceRtc), sceRtc);
}
