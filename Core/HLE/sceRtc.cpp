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

#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/CoreTiming.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceRtc.h"

// This is a base time that everything is relative to.
// This way, time doesn't move strangely with savestates, turbo speed, etc.
static timeval rtcBaseTime;

// Grabbed from JPSCP
// This is the # of microseconds between January 1, 0001 and January 1, 1970.
const u64 rtcMagicOffset = 62135596800000000ULL;
// This is the # of microseconds between January 1, 0001 and January 1, 1601 (for Win32 FILETIME.)
const u64 rtcFiletimeOffset = 50491123200000000ULL;

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

#if defined(_WIN32)
#define FILETIME_FROM_UNIX_EPOCH_US (rtcMagicOffset - rtcFiletimeOffset)

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

time_t rtc_timegm(struct tm *tm)
{
	struct tm modified;
	memcpy(&modified, tm, sizeof(modified));
	return _mkgmtime(&modified);
}

#elif defined(__GLIBC__) && !defined(ANDROID)
#define rtc_timegm timegm
#else

time_t rtc_timegm(struct tm *tm)
{
	time_t ret;
	char *tz;
	std::string tzcopy;

	tz = getenv("TZ");
	if (tz)
		tzcopy = tz;

	setenv("TZ", "", 1);
	tzset();
	ret = mktime(tm);
	if (tz)
		setenv("TZ", tzcopy.c_str(), 1);
	else
		unsetenv("TZ");
	tzset();
	return ret;
}

#endif

void __RtcInit()
{
	// This is the base time, the only case we use gettimeofday() for.
	// Everything else is relative to that, "virtual time."
	gettimeofday(&rtcBaseTime, NULL);
}

void __RtcDoState(PointerWrap &p)
{
	p.Do(rtcBaseTime);

	p.DoMarker("sceRtc");
}

void __RtcTimeOfDay(timeval *tv)
{
	s64 additionalUs = cyclesToUs(CoreTiming::GetTicks());
	*tv = rtcBaseTime;

	s64 adjustedUs = additionalUs + tv->tv_usec;
	tv->tv_sec += long(adjustedUs / 1000000UL);
	tv->tv_usec = adjustedUs % 1000000UL;
}

void __RtcTmToPspTime(ScePspDateTime &t, tm *val)
{
	t.year = val->tm_year + 1900;
	t.month = val->tm_mon + 1;
	t.day = val->tm_mday;
	t.hour = val->tm_hour;
	t.minute = val->tm_min;
	t.second = val->tm_sec;
	t.microsecond = 0;
}

void __RtcTicksToPspTime(ScePspDateTime &t, u64 ticks)
{
	int numYearAdd = 0;
	if(ticks < 1000000ULL)
	{
		t.year = 1;
		t.month = 1;
		t.day = 1;
		t.hour = 0;
		t.minute = 0;
		t.second = 0;
		t.microsecond = ticks % 1000000ULL;
		return;
	}
	else if(ticks < rtcMagicOffset )
	{
		// Need to get a year past 1970 for gmtime
		// Add enough 400 year to pass over 1970.
		// Each 400 year are equal
		// 400 year is 20871 weeks
		u64 ticks400Y = (u64)20871 * 7 * 24 * 3600 * 1000000ULL;
		numYearAdd = (int) ((rtcMagicOffset - ticks) / ticks400Y + 1);
		ticks += ticks400Y * numYearAdd;

	}

	time_t time = (ticks - rtcMagicOffset) / 1000000ULL;
	t.microsecond = ticks % 1000000ULL;

	tm *local = gmtime(&time);
	if (!local)
	{
		ERROR_LOG(HLE, "Date is too high/low to handle, pretending to work.");
		return;
	}

	t.year = local->tm_year + 1900 - numYearAdd * 400;
	t.month = local->tm_mon + 1;
	t.day = local->tm_mday;
	t.hour = local->tm_hour;
	t.minute = local->tm_min;
	t.second = local->tm_sec;
}

u64 __RtcPspTimeToTicks(ScePspDateTime &pt)
{
	tm local;
	local.tm_year = pt.year - 1900;
	local.tm_mon = pt.month - 1;
	local.tm_mday = pt.day;
	local.tm_wday = -1;
	local.tm_yday = -1;
	local.tm_hour = pt.hour;
	local.tm_min = pt.minute;
	local.tm_sec = pt.second;
	local.tm_isdst = 0;

	time_t seconds = rtc_timegm(&local);
	u64 result = rtcMagicOffset + (u64) seconds * 1000000ULL;
	result += pt.microsecond;
	return result;
}

bool __RtcValidatePspTime(ScePspDateTime &t)
{
	return t.year > 0 && t.year <= 9999;
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
	hleEatCycles(300);
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
	__RtcTimeOfDay(&tv);

	time_t sec = (time_t) tv.tv_sec;
	tm *utc = gmtime(&sec);
	if (!utc)
	{
		ERROR_LOG(HLE, "Date is too high/low to handle, pretending to work.");
		return 0;
	}

	utc->tm_isdst = -1;
	utc->tm_min += tz;
	rtc_timegm(utc); // Return gmt time with timezone offset.

	ScePspDateTime ret;
	__RtcTmToPspTime(ret, utc);
	ret.microsecond = tv.tv_usec;

	if (Memory::IsValidAddress(pspTimePtr))
		Memory::WriteStruct(pspTimePtr, &ret);

	hleEatCycles(1900);
	return 0;
}

u32 sceRtcGetCurrentClockLocalTime(u32 pspTimePtr)
{
	DEBUG_LOG(HLE, "sceRtcGetCurrentClockLocalTime(%08x)", pspTimePtr);
	timeval tv;
	__RtcTimeOfDay(&tv);

	time_t sec = (time_t) tv.tv_sec;
	tm *local = localtime(&sec);
	if (!local)
	{
		ERROR_LOG(HLE, "Date is too high/low to handle, pretending to work.");
		return 0;
	}

	ScePspDateTime ret;
	__RtcTmToPspTime(ret, local);
	ret.microsecond = tv.tv_usec;

	if (Memory::IsValidAddress(pspTimePtr))
		Memory::WriteStruct(pspTimePtr, &ret);

	hleEatCycles(2000);
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

		u64 result = __RtcPspTimeToTicks(pt);

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
	DEBUG_LOG(HLE, "sceRtcIsLeapYear(%d)", year);
	return (year % 4 == 0) && (!(year % 100 == 0) || (year % 400 == 0));
}

int sceRtcConvertLocalTimeToUTC(u32 tickLocalPtr,u32 tickUTCPtr)	
{
	DEBUG_LOG(HLE, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickLocalPtr);
		// TODO : Let the user select his timezone / daylight saving instead of taking system param ?
#if defined(__GLIBC__) || defined(__SYMBIAN32__)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		srcTick -= time->tm_gmtoff*1000000ULL;
#else
		srcTick -= -timezone * 1000000ULL;
#endif
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
	DEBUG_LOG(HLE, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickUTCPtr);
		// TODO : Let the user select his timezone / daylight saving instead of taking system param ?
#if defined(__GLIBC__) || defined(__SYMBIAN32__)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		srcTick += time->tm_gmtoff*1000000ULL;
#else
		srcTick += -timezone * 1000000ULL;
#endif
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
	DEBUG_LOG(HLE, "sceRtcSetTime_t(%08x,%d)", datePtr, time);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;
		__RtcTicksToPspTime(pt, time*1000000ULL + rtcMagicOffset);
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
	DEBUG_LOG(HLE, "sceRtcSetTime64_t(%08x,%lld)", datePtr, time);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;
		__RtcTicksToPspTime(pt, time*1000000ULL + rtcMagicOffset);
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
	DEBUG_LOG(HLE, "sceRtcGetTime_t(%08x,%08x)", datePtr, timePtr);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(timePtr))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		u32 result = (u32) ((__RtcPspTimeToTicks(pt)-rtcMagicOffset)/1000000ULL);
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
	DEBUG_LOG(HLE, "sceRtcGetTime64_t(%08x,%08x)", datePtr, timePtr);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(timePtr))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);
		u64 result = (__RtcPspTimeToTicks(pt)-rtcMagicOffset)/1000000ULL;
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
	DEBUG_LOG(HLE, "sceRtcSetDosTime(%d,%d)", datePtr, dosTime);
	if (Memory::IsValidAddress(datePtr))
	{
		ScePspDateTime pt;

		int hms = dosTime & 0xFFFF;
		int ymd = dosTime >> 16;

		pt.year = 1980 + (ymd >> 9);
		pt.month = (ymd >> 5) & 0xF;
		pt.day = ymd & 0x1F;
		pt.hour = hms >> 11;
		pt.minute = (hms >> 5) & 0x3F;
		pt.second = (hms << 1) & 0x3E;
		pt.microsecond = 0;

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
	int retValue = 0;
	DEBUG_LOG(HLE, "sceRtcGetDosTime(%d,%d)", datePtr, dosTime);
	if (Memory::IsValidAddress(datePtr)&&Memory::IsValidAddress(dosTime))
	{
		ScePspDateTime pt;
		Memory::ReadStruct(datePtr, &pt);

		u32 result = 0;
		if(pt.year < 1980)
		{
			result = 0;
			retValue = -1;
		}
		else if(pt.year >= 2108)
		{
			result = 0xFF9FBF7D;
			retValue = -1;
		}
		else
		{
			int year = ((pt.year - 1980) & 0x7F) << 9;
			int month = ((pt.month) & 0xF ) << 5;
			int hour = ((pt.hour) & 0x1F ) << 11;
			int minute = ((pt.minute) & 0x3F ) << 5;
			int day = (pt.day) & 0x1F;
			int second = ((pt.second) >> 1) & 0x1F;
			int ymd = year | month | day;
			int hms = hour | minute | second;
			result = (ymd << 16) | hms;
			retValue = 0;
		}

		Memory::Write_U32(result, dosTime);
	}
	else
	{
		retValue = -1;
	}
	return retValue;
}

int sceRtcSetWin32FileTime(u32 datePtr, u64 win32Time)
{
	if (!Memory::IsValidAddress(datePtr))
	{
		ERROR_LOG_REPORT(HLE, "sceRtcSetWin32FileTime(%08x, %lld): invalid address", datePtr, win32Time);
		return -1;
	}

	DEBUG_LOG(HLE, "sceRtcSetWin32FileTime(%08x, %lld)", datePtr, win32Time);

	u64 ticks = (win32Time / 10) + rtcFiletimeOffset;
	auto pspTime = Memory::GetStruct<ScePspDateTime>(datePtr);
	__RtcTicksToPspTime(*pspTime, ticks);
	return 0;
}

int sceRtcGetWin32FileTime(u32 datePtr, u32 win32TimePtr)
{
	if (!Memory::IsValidAddress(datePtr))
	{
		ERROR_LOG_REPORT(HLE, "sceRtcGetWin32FileTime(%08x, %08x): invalid address", datePtr, win32TimePtr);
		return -1;
	}

	DEBUG_LOG(HLE, "sceRtcGetWin32FileTime(%08x, %08x)", datePtr, win32TimePtr);
	if (!Memory::IsValidAddress(win32TimePtr))
		return SCE_KERNEL_ERROR_INVALID_VALUE;

	auto pspTime = Memory::GetStruct<ScePspDateTime>(datePtr);
	u64 result = __RtcPspTimeToTicks(*pspTime);

	if (!__RtcValidatePspTime(*pspTime) || result < rtcFiletimeOffset)
	{
		Memory::Write_U64(0, win32TimePtr);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	Memory::Write_U64((result - rtcFiletimeOffset) * 10, win32TimePtr);
	return 0;
}

int sceRtcCompareTick(u32 tick1Ptr, u32 tick2Ptr)
{
	DEBUG_LOG(HLE, "sceRtcCompareTick(%d,%d)", tick1Ptr, tick2Ptr);
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

	DEBUG_LOG(HLE, "sceRtcTickAddMicroseconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numMS);
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
	DEBUG_LOG(HLE, "sceRtcTickAddSeconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numSecs);
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
	DEBUG_LOG(HLE, "sceRtcTickAddMinutes(%x,%x,%llu)", destTickPtr, srcTickPtr, numMins);
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
	DEBUG_LOG(HLE, "sceRtcTickAddMinutes(%d,%d,%d)", destTickPtr, srcTickPtr, numHours);
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
	DEBUG_LOG(HLE, "sceRtcTickAddDays(%d,%d,%d)", destTickPtr, srcTickPtr, numDays);
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
	DEBUG_LOG(HLE, "sceRtcTickAddWeeks(%d,%d,%d)", destTickPtr, srcTickPtr, numWeeks);
	return 0;
}

int sceRtcTickAddMonths(u32 destTickPtr, u32 srcTickPtr, int numMonths)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		ScePspDateTime pt;
		memset(&pt, 0, sizeof(pt));

		__RtcTicksToPspTime(pt,srcTick);
		if(((pt.year-1)*12+pt.month) + numMonths < 1 || ((pt.year-1)*12+pt.month) + numMonths > 9999*12)
		{
			srcTick = 0;
		}
		else
		{
			if(numMonths < 0)
			{
				pt.year += numMonths/12;
				int restMonth = pt.month + numMonths%12;
				if(restMonth < 1)
				{
					pt.month = 12+restMonth;
					pt.year--;
				}
				else
				{
					pt.month = restMonth;
				}
			}
			else
			{
				pt.year += numMonths/12;
				pt.month += numMonths%12;
				if(pt.month > 12)
				{
					pt.month -= 12;
					pt.year++;
				}
			}
			u64 yearTicks = __RtcPspTimeToTicks(pt);
			srcTick =yearTicks;
		}
		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(HLE, "sceRtcTickAddMonths(%d,%d,%d)", destTickPtr, srcTickPtr, numMonths);
	return 0;
}

int sceRtcTickAddYears(u32 destTickPtr, u32 srcTickPtr, int numYears)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		ScePspDateTime pt;
		memset(&pt, 0, sizeof(pt));

		__RtcTicksToPspTime(pt,srcTick);
		if(pt.year + numYears <= 0 || pt.year + numYears > 9999)
		{
			srcTick = 0;
		}
		else
		{
			pt.year += numYears;
			u64 yearTicks = __RtcPspTimeToTicks(pt);
			srcTick =yearTicks;
		}

		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(HLE, "sceRtcTickAddYears(%d,%d,%d)", destTickPtr, srcTickPtr, numYears);
	return 0;
}

int sceRtcParseDateTime(u32 destTickPtr, u32 dateStringPtr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceRtcParseDateTime(%d,%d)", destTickPtr, dateStringPtr);
	return 0;
}

int sceRtcGetLastAdjustedTime(u32 tickPtr)
{
	u64 curTick = __RtcGetCurrentTick();
	if (Memory::IsValidAddress(tickPtr))
		Memory::Write_U64(curTick, tickPtr);
	DEBUG_LOG(HLE, "sceRtcGetLastAdjustedTime(%d)", tickPtr);
	return 0;
}

//Returns 0 on success, according to Project Diva 2nd jpcsptrace log
int sceRtcSetAlarmTick(u32 unknown1, u32 unknown2)
{
	ERROR_LOG(HLE, "UNIMPL sceRtcSetAlarmTick(%x, %x)", unknown1, unknown2);
	return 0; 
}

const HLEFunction sceRtc[] =
{
	{0xC41C2853, &WrapU_V<sceRtcGetTickResolution>, "sceRtcGetTickResolution"},
	{0x3f7ad767, &WrapU_U<sceRtcGetCurrentTick>, "sceRtcGetCurrentTick"},
	{0x011F03C1, &WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumulativeTime"},
	{0x029CA3B3, &WrapU64_V<sceRtcGetAcculumativeTime>, "sceRtcGetAccumlativeTime"},
	{0x4cfa57b0, &WrapU_UI<sceRtcGetCurrentClock>, "sceRtcGetCurrentClock"},
	{0xE7C27D1B, &WrapU_U<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime"},
	{0x34885E0D, &WrapI_UU<sceRtcConvertUtcToLocalTime>, "sceRtcConvertUtcToLocalTime"},
	{0x779242A2, &WrapI_UU<sceRtcConvertLocalTimeToUTC>, "sceRtcConvertLocalTimeToUTC"},
	{0x42307A17, &WrapU_U<sceRtcIsLeapYear>, "sceRtcIsLeapYear"},
	{0x05ef322c, &WrapU_UU<sceRtcGetDaysInMonth>, "sceRtcGetDaysInMonth"},
	{0x57726bc1, &WrapU_UUU<sceRtcGetDayOfWeek>, "sceRtcGetDayOfWeek"},
	{0x4B1B5E82, &WrapI_U<sceRtcCheckValid>, "sceRtcCheckValid"},
	{0x3a807cc8, &WrapI_UU<sceRtcSetTime_t>, "sceRtcSetTime_t"},
	{0x27c4594c, &WrapI_UU<sceRtcGetTime_t>, "sceRtcGetTime_t"},
	{0xF006F264, &WrapI_UU<sceRtcSetDosTime>, "sceRtcSetDosTime"},
	{0x36075567, &WrapI_UU<sceRtcGetDosTime>, "sceRtcGetDosTime"},
	{0x7ACE4C04, &WrapI_UU64<sceRtcSetWin32FileTime>, "sceRtcSetWin32FileTime"},
	{0xCF561893, &WrapI_UU<sceRtcGetWin32FileTime>, "sceRtcGetWin32FileTime"},
	{0x7ED29E40, &WrapU_UU<sceRtcSetTick>, "sceRtcSetTick"},
	{0x6FF40ACC, &WrapU_UU<sceRtcGetTick>, "sceRtcGetTick"},
	{0x9ED0AE87, &WrapI_UU<sceRtcCompareTick>, "sceRtcCompareTick"},
	{0x44F45E05, &WrapI_UUU64<sceRtcTickAddTicks>, "sceRtcTickAddTicks"},
	{0x26D25A5D, &WrapI_UUU64<sceRtcTickAddMicroseconds>, "sceRtcTickAddMicroseconds"},
	{0xF2A4AFE5, &WrapI_UUU64<sceRtcTickAddSeconds>, "sceRtcTickAddSeconds"},
	{0xE6605BCA, &WrapI_UUU64<sceRtcTickAddMinutes>, "sceRtcTickAddMinutes"},
	{0x26D7A24A, &WrapI_UUI<sceRtcTickAddHours>, "sceRtcTickAddHours"},
	{0xE51B4B7A, &WrapI_UUI<sceRtcTickAddDays>, "sceRtcTickAddDays"},
	{0xCF3A2CA8, &WrapI_UUI<sceRtcTickAddWeeks>, "sceRtcTickAddWeeks"},
	{0xDBF74F1B, &WrapI_UUI<sceRtcTickAddMonths>, "sceRtcTickAddMonths"},
	{0x42842C77, &WrapI_UUI<sceRtcTickAddYears>, "sceRtcTickAddYears"},
	{0xC663B3B9, 0, "sceRtcFormatRFC2822"},
	{0x7DE6711B, 0, "sceRtcFormatRFC2822LocalTime"},
	{0x0498FB3C, 0, "sceRtcFormatRFC3339"},
	{0x27F98543, 0, "sceRtcFormatRFC3339LocalTime"},
	{0xDFBC5F16, &WrapI_UU<sceRtcParseDateTime>, "sceRtcParseDateTime"},
	{0x28E1E988, 0, "sceRtcParseRFC3339"},
	{0xe1c93e47, &WrapI_UU<sceRtcGetTime64_t>, "sceRtcGetTime64_t"},
	{0x1909c99b, &WrapI_UU64<sceRtcSetTime64_t>, "sceRtcSetTime64_t"},
	{0x62685E98, &WrapI_U<sceRtcGetLastAdjustedTime>, "sceRtcGetLastAdjustedTime"},
	{0x203ceb0d, 0, "sceRtcGetLastReincarnatedTime"},
	{0x7d1fbed3, &WrapI_UU<sceRtcSetAlarmTick>, "sceRtcSetAlarmTick"},
	{0xf5fcc995, 0, "sceRtc_F5FCC995"},
};

void Register_sceRtc()
{
	RegisterModule("sceRtc", ARRAY_SIZE(sceRtc), sceRtc);
}
