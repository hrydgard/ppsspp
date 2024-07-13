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
#include "Common/CommonWindows.h"
 // timeval already defined in xtl.h
#include <Winsock2.h>
#else
#include <sys/time.h>
#endif

#include <time.h>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceRtc.h"

#ifdef HAVE_LIBNX
// I guess that works...
#define setenv(x, y, z) (void*)0
#define tzset() (void*)0
#define unsetenv(x) (void*)0
#endif // HAVE_LIBNX

// This is a base time that everything is relative to.
// This way, time doesn't move strangely with savestates, turbo speed, etc.
static PSPTimeval rtcBaseTime;
static u64 rtcBaseTicks;

// Grabbed from JPSCP
// This is the # of microseconds between January 1, 0001 and January 1, 1970.
const u64 rtcMagicOffset = 62135596800000000ULL;
// This is the # of microseconds between January 1, 0001 and January 1, 1601 (for Win32 FILETIME.)
const u64 rtcFiletimeOffset = 50491123200000000ULL;

// 400 years is a convenient number, since leap days and everything cycle every 400 years.
// 400 years is in other words 20871 full weeks.
const u64 rtc400YearTicks = (u64)20871 * 7 * 24 * 3600 * 1000000ULL;

// This is the last moment the clock was adjusted.
// It's possible games may not like the clock being adjusted in the past hour (cheating?)
// So this returns a static time.
const u64 rtcLastAdjustedTicks = rtcMagicOffset + 41 * 365 * 24 * 3600 * 1000000ULL;
// The reincarnated time seems related to the battery or manufacturing date.
// On a test PSP, it was over 3 years in the past, so we again pick a fixed date.
const u64 rtcLastReincarnatedTicks = rtcMagicOffset + 40 * 365 * 24 * 3600 * 1000000ULL;

const int PSP_TIME_INVALID_YEAR = -1;
const int PSP_TIME_INVALID_MONTH = -2;
const int PSP_TIME_INVALID_DAY = -3;
const int PSP_TIME_INVALID_HOUR = -4;
const int PSP_TIME_INVALID_MINUTES = -5;
const int PSP_TIME_INVALID_SECONDS = -6;
const int PSP_TIME_INVALID_MICROSECONDS = -7;

static u64 __RtcGetCurrentTick()
{
	// TODO: It's probably expecting ticks since January 1, 0001?
	return CoreTiming::GetGlobalTimeUs() + rtcBaseTicks;
}

#if defined(__MINGW32__)
errno_t _get_timezone(long *seconds)
{
  time_t now = time(NULL);

  struct tm *gm = gmtime(&now);
  time_t gmt = mktime(gm);

  struct tm *loc = localtime(&now);
  time_t local = mktime(loc);

  *seconds = local - gmt;
  return 0;
}
#endif

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
	return _mkgmtime(tm);
}

#elif (defined(__GLIBC__) && !defined(__ANDROID__))
#define rtc_timegm timegm
#else

static time_t rtc_timegm(struct tm *tm)
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

static void RtcUpdateBaseTicks() {
	rtcBaseTicks = 1000000ULL * rtcBaseTime.tv_sec + rtcBaseTime.tv_usec + rtcMagicOffset;
}

void __RtcInit()
{
	// This is the base time, the only case we use gettimeofday() for.
	// Everything else is relative to that, "virtual time."
	timeval tv;
	gettimeofday(&tv, NULL);
	rtcBaseTime.tv_sec = tv.tv_sec;
	rtcBaseTime.tv_usec = 0;
	// Precalculate the current time in microseconds (rtcMagicOffset is offset to 1970.)
	RtcUpdateBaseTicks();
}

void __RtcDoState(PointerWrap &p)
{
	auto s = p.Section("sceRtc", 1);
	if (!s)
		return;

	Do(p, rtcBaseTime);
	// Update the precalc, pointless to savestate this as it's just based on the other value.
	RtcUpdateBaseTicks();
}

void __RtcTimeOfDay(PSPTimeval *tv)
{
	s64 additionalUs = CoreTiming::GetGlobalTimeUs();
	*tv = rtcBaseTime;

	s64 adjustedUs = additionalUs + tv->tv_usec;
	tv->tv_sec += long(adjustedUs / 1000000UL);
	tv->tv_usec = adjustedUs % 1000000UL;
}

int32_t RtcBaseTime(int32_t *micro) {
	if (micro) {
		*micro = rtcBaseTime.tv_usec;
	}
	return rtcBaseTime.tv_sec;
}

void RtcSetBaseTime(int32_t seconds, int32_t micro) {
	rtcBaseTime.tv_sec = seconds;
	rtcBaseTime.tv_usec = micro;
	RtcUpdateBaseTicks();
}

static void __RtcTmToPspTime(ScePspDateTime &t, const tm *val)
{
	t.year = val->tm_year + 1900;
	t.month = val->tm_mon + 1;
	t.day = val->tm_mday;
	t.hour = val->tm_hour;
	t.minute = val->tm_min;
	t.second = val->tm_sec;
	t.microsecond = 0;
}

static void __RtcPspTimeToTm(tm &val, const ScePspDateTime &pt)
{
	val.tm_year = pt.year - 1900;
	val.tm_mon = pt.month - 1;
	val.tm_mday = pt.day;
	val.tm_wday = -1;
	val.tm_yday = -1;
	val.tm_hour = pt.hour;
	val.tm_min = pt.minute;
	val.tm_sec = pt.second;
	val.tm_isdst = 0;
}

// Based on http://howardhinnant.github.io/date_algorithms.html
static s64 days_from_civil(s64 y, u32 m, u32 d)
{
	y -= m <= 2;
	const s64 era = (y >= 0 ? y : y-399) / 400;
	const u32 yoe = static_cast<u32>(y - era * 400);      // [0, 399]
	const u32 doy = (153*(m > 2 ? m-3 : m+9) + 2)/5 + d-1;// [0, 365]
	const u32 doe = yoe * 365 + yoe/4 - yoe/100 + doy;    // [0, 146096]
	return era * 146097 + static_cast<s64>(doe) - 719468;
}

static void civil_from_days(s64 z, s64 &out_y, u32 &out_m, u32 &out_d)
{
	z += 719468;
	const s64 era = (z >= 0 ? z : z - 146096) / 146097;
	const u32 doe = static_cast<u32>(z - era * 146097);              // [0, 146096]
	const u32 yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; // [0, 399]
	const s64 y = static_cast<s64>(yoe) + era * 400;
	const u32 doy = doe - (365*yoe + yoe/4 - yoe/100);               // [0, 365]
	const u32 mp = (5*doy + 2)/153;                                  // [0, 11]
	out_d = doy - (153*mp+2)/5 + 1;                                  // [1, 31]
	out_m = mp < 10 ? mp+3 : mp-9;                                   // [1, 12]
	out_y = y + (out_m <= 2);
}

static void __RtcTicksToPspTime(ScePspDateTime &t, u64 ticks)
{
	u64 Day = 24ull * 60ull * 60ull * 1000000ull;
	t.microsecond = ticks % 1000000ull;
	t.second = ticks / 1000000ull % 60ull;
	t.minute = ticks / 1000000ull / 60ull % 60ull;
	t.hour   = ticks / 1000000ull / 60ull / 60ull % 24ull;
	s64 z = s64(ticks / Day) - s64(rtcMagicOffset / Day);
        s64 y;
	u32 m, d;
	civil_from_days(z, y, m, d);
	t.day   = d;
	t.month = m;
	t.year  = y;
}

static u64 __RtcPspTimeToTicks(const ScePspDateTime &pt)
{
	s64 z = days_from_civil(s64(pt.year), pt.month, pt.day);
	return rtcMagicOffset +
		pt.microsecond + 
		1000000ull * (pt.second +
		60ull * (pt.minute + 
		60ull * (pt.hour + 
		24ull * u64(z))));
}

static bool __RtcValidatePspTime(const ScePspDateTime &t)
{
	return t.year > 0 && t.year <= 9999;
}

static u32 sceRtcGetTickResolution()
{
	DEBUG_LOG(Log::sceRtc, "sceRtcGetTickResolution()");
	return 1000000;
}

static u32 sceRtcGetCurrentTick(u32 tickPtr)
{
	VERBOSE_LOG(Log::sceRtc, "sceRtcGetCurrentTick(%08x)", tickPtr);

	u64 curTick = __RtcGetCurrentTick();
	if (Memory::IsValidAddress(tickPtr))
		Memory::Write_U64(curTick, tickPtr);
	hleEatCycles(300);
	hleReSchedule("rtc current tick");
	return 0;
}

static u64 sceRtcGetAccumulativeTime()
{
	DEBUG_LOG(Log::sceRtc, "sceRtcGetAccumulativeTime()");
	hleEatCycles(300);
	hleReSchedule("rtc accumulative time");
	return __RtcGetCurrentTick();
}

static u32 sceRtcGetCurrentClock(u32 pspTimePtr, int tz) {
	auto pt = PSPPointer<ScePspDateTime>::Create(pspTimePtr);

	PSPTimeval tv;
	__RtcTimeOfDay(&tv);

	time_t sec = (time_t)tv.tv_sec;
	tm *utc = gmtime(&sec);
	if (!utc) {
		return hleLogError(Log::sceRtc, 0, "Date is too high/low to handle, pretending to work");
	}

	utc->tm_isdst = -1;
	utc->tm_min += tz;
	rtc_timegm(utc); // Return gmt time with timezone offset.

	if (pt.IsValid()) {
		__RtcTmToPspTime(*pt, utc);
		pt->microsecond = tv.tv_usec;
	}

	hleEatCycles(1900);
	hleReSchedule("rtc current clock");
	return hleLogSuccessI(Log::sceRtc, 0);
}

static u32 sceRtcGetCurrentClockLocalTime(u32 pspTimePtr) {
	auto pt = PSPPointer<ScePspDateTime>::Create(pspTimePtr);

	PSPTimeval tv;
	__RtcTimeOfDay(&tv);

	time_t sec = (time_t)tv.tv_sec;
	const tm *local = localtime(&sec);
	if (!local) {
		return hleLogError(Log::sceRtc, 0, "Date is too high/low to handle, pretending to work");
	}

	if (pt.IsValid()) {
		__RtcTmToPspTime(*pt, local);
		pt->microsecond = tv.tv_usec;
	}

	hleEatCycles(2000);
	hleReSchedule("rtc current clock local");
	return hleLogSuccessI(Log::sceRtc, 0);
}

static u32 sceRtcSetTick(u32 pspTimePtr, u32 tickPtr) {
	auto pt = PSPPointer<ScePspDateTime>::Create(pspTimePtr);
	auto tick = PSPPointer<u64_le>::Create(tickPtr);

	if (!pt.IsValid() || !tick.IsValid())
		return hleLogError(Log::sceRtc, 0, "bad address");

	__RtcTicksToPspTime(*pt, *tick);
	return hleLogSuccessI(Log::sceRtc, 0);
}

static u32 sceRtcGetTick(u32 pspTimePtr, u32 tickPtr) {
	auto pt = PSPPointer<const ScePspDateTime>::Create(pspTimePtr);
	auto tick = PSPPointer<u64_le>::Create(tickPtr);

	if (!pt.IsValid() || !tick.IsValid())
		return hleLogError(Log::sceRtc, 0, "bad address");
	if (!__RtcValidatePspTime(*pt))
		return hleLogWarning(Log::sceRtc, SCE_KERNEL_ERROR_INVALID_VALUE, "invalid time");

	*tick = __RtcPspTimeToTicks(*pt);
	return hleLogSuccessI(Log::sceRtc, 0);
}

static u32 sceRtcGetDayOfWeek(u32 year, u32 month, u32 day)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcGetDayOfWeek(%d, %d, %d)", year, month, day);

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
		if (restMonth > 0)
			day += t[restMonth-1];
		month = 12;
	}

	return ((days_from_civil(s64(year), month, day) % 7LL) + 11LL) % 7LL;
}

static bool __RtcIsLeapYear(u32 year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int __RtcDaysInMonth(u32 year, u32 month)
{
	switch (month)
	{
	case 4:
	case 6:
	case 9:
	case 11:
		return 30;

	case 2:
		if (__RtcIsLeapYear(year))
			return 29;
		return 28;

	default:
		return 31;
	}
}

static u32 sceRtcGetDaysInMonth(u32 year, u32 month)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcGetDaysInMonth(%d, %d)", year, month);

	if (year == 0 || month == 0 || month > 12)
		return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

	return __RtcDaysInMonth(year, month);
}

static u32 sceRtcIsLeapYear(u32 year)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcIsLeapYear(%d)", year);
	return __RtcIsLeapYear(year) ? 1 : 0;
}

static int sceRtcConvertLocalTimeToUTC(u32 tickLocalPtr,u32 tickUTCPtr)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickLocalPtr);
		// TODO : Let the user select his timezone / daylight saving instead of taking system param ?
#ifdef _WIN32
		long timezone_val;
		_get_timezone(&timezone_val);
		srcTick -= -timezone_val * 1000000ULL;
#elif !defined(_AIX) && !defined(__sgi) && !defined(__hpux) && !defined(HAVE_LIBNX)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		srcTick -= time->tm_gmtoff*1000000ULL;
#endif
		Memory::Write_U64(srcTick, tickUTCPtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

static int sceRtcConvertUtcToLocalTime(u32 tickUTCPtr,u32 tickLocalPtr)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcConvertLocalTimeToUTC(%d, %d)", tickLocalPtr, tickUTCPtr);
	if (Memory::IsValidAddress(tickLocalPtr) && Memory::IsValidAddress(tickUTCPtr))
	{
		u64 srcTick = Memory::Read_U64(tickUTCPtr);
		// TODO : Let the user select his timezone / daylight saving instead of taking system param ?
#ifdef _WIN32
		long timezone_val;
		_get_timezone(&timezone_val);
		srcTick += -timezone_val * 1000000ULL;
#elif !defined(_AIX) && !defined(__sgi) && !defined(__hpux) && !defined(HAVE_LIBNX)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		srcTick += time->tm_gmtoff*1000000ULL;
#endif
		Memory::Write_U64(srcTick, tickLocalPtr);
	}
	else
	{
		return 1;
	}
	return 0;
}

static int sceRtcCheckValid(u32 datePtr) {
	auto pt = PSPPointer<const ScePspDateTime>::Create(datePtr);

	if (!pt.IsValid() )
		return hleLogError(Log::sceRtc, -1, "bad address");

	int result = 0;
	if (pt->year < 1 || pt->year > 9999)
		result = PSP_TIME_INVALID_YEAR;
	else if (pt->month < 1 || pt->month > 12)
		result = PSP_TIME_INVALID_MONTH;
	else if (pt->day < 1 || pt->day > 31)
		result = PSP_TIME_INVALID_DAY;
	else if (pt->day > __RtcDaysInMonth((s16)pt->year, (s16)pt->month))
		result = PSP_TIME_INVALID_DAY;
	else if (pt->hour < 0 || pt->hour > 23)
		result = PSP_TIME_INVALID_HOUR;
	else if (pt->minute < 0 || pt->minute > 59)
		result = PSP_TIME_INVALID_MINUTES;
	else if (pt->second < 0 || pt->second > 59)
		result = PSP_TIME_INVALID_SECONDS;
	else if (pt->microsecond >= 1000000UL)
		result = PSP_TIME_INVALID_MICROSECONDS;
	return hleLogSuccessI(Log::sceRtc, result);
}

static int sceRtcSetTime_t(u32 datePtr, u32 time) {
	auto pt = PSPPointer<ScePspDateTime>::Create(datePtr);
	if (!pt.IsValid())
		return hleLogError(Log::sceRtc, 1, "bad address");

	__RtcTicksToPspTime(*pt, time * 1000000ULL + rtcMagicOffset);
	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcSetTime64_t(u32 datePtr, u64 time) {
	auto pt = PSPPointer<ScePspDateTime>::Create(datePtr);
	if (!pt.IsValid())
		return hleLogError(Log::sceRtc, 1, "bad address");

	__RtcTicksToPspTime(*pt, time * 1000000ULL + rtcMagicOffset);
	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcGetTime_t(u32 datePtr, u32 timePtr) {
	auto pt = PSPPointer<const ScePspDateTime>::Create(datePtr);
	auto timep = PSPPointer<u32_le>::Create(timePtr);
	if (!pt.IsValid() || !timep.IsValid())
		return hleLogError(Log::sceRtc, 1, "bad address");

	*timep = (u32)((__RtcPspTimeToTicks(*pt) - rtcMagicOffset) / 1000000ULL);
	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcGetTime64_t(u32 datePtr, u32 timePtr) {
	auto pt = PSPPointer<const ScePspDateTime>::Create(datePtr);
	auto timep = PSPPointer<u64_le>::Create(timePtr);
	if (!pt.IsValid() || !timep.IsValid())
		return hleLogError(Log::sceRtc, 1, "bad address");

	*timep = (__RtcPspTimeToTicks(*pt) - rtcMagicOffset) / 1000000ULL;
	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcSetDosTime(u32 datePtr, u32 dosTime) {
	auto pt = PSPPointer<ScePspDateTime>::Create(datePtr);
	if (!pt.IsValid())
		return hleLogError(Log::sceRtc, 1, "bad address");

	int hms = dosTime & 0xFFFF;
	int ymd = dosTime >> 16;

	pt->year = 1980 + (ymd >> 9);
	pt->month = (ymd >> 5) & 0xF;
	pt->day = ymd & 0x1F;
	pt->hour = hms >> 11;
	pt->minute = (hms >> 5) & 0x3F;
	pt->second = (hms << 1) & 0x3E;
	pt->microsecond = 0;

	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcGetDosTime(u32 datePtr, u32 dosTime) {
	auto datep = PSPPointer<ScePspDateTime>::Create(datePtr);
	auto dosp = PSPPointer<u32_le>::Create(dosTime);
	if (!datep.IsValid() || !dosp.IsValid())
		return hleLogError(Log::sceRtc, -1, "bad address");

	if (datep->year < 1980) {
		*dosp = 0;
		return hleLogWarning(Log::sceRtc, -1, "invalid year");
	} else if (datep->year >= 2108) {
		*dosp = 0xFF9FBF7D;
		return hleLogWarning(Log::sceRtc, -1, "invalid year");
	}

	int year = ((datep->year - 1980) & 0x7F) << 9;
	int month = (datep->month & 0xF) << 5;
	int hour = (datep->hour & 0x1F) << 11;
	int minute = (datep->minute & 0x3F) << 5;
	int day = datep->day & 0x1F;
	int second = (datep->second >> 1) & 0x1F;
	int ymd = year | month | day;
	int hms = hour | minute | second;

	*dosp = (ymd << 16) | hms;
	return hleLogSuccessI(Log::sceRtc, 0);
}

static int sceRtcSetWin32FileTime(u32 datePtr, u64 win32Time)
{
	if (!Memory::IsValidAddress(datePtr))
	{
		ERROR_LOG_REPORT(Log::sceRtc, "sceRtcSetWin32FileTime(%08x, %lld): invalid address", datePtr, win32Time);
		return -1;
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcSetWin32FileTime(%08x, %lld)", datePtr, win32Time);

	u64 ticks = (win32Time / 10) + rtcFiletimeOffset;
	auto pspTime = PSPPointer<ScePspDateTime>::Create(datePtr);
	__RtcTicksToPspTime(*pspTime, ticks);
	return 0;
}

static int sceRtcGetWin32FileTime(u32 datePtr, u32 win32TimePtr)
{
	if (!Memory::IsValidAddress(datePtr))
	{
		ERROR_LOG_REPORT(Log::sceRtc, "sceRtcGetWin32FileTime(%08x, %08x): invalid address", datePtr, win32TimePtr);
		return -1;
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcGetWin32FileTime(%08x, %08x)", datePtr, win32TimePtr);
	if (!Memory::IsValidAddress(win32TimePtr))
		return SCE_KERNEL_ERROR_INVALID_VALUE;

	auto pspTime = PSPPointer<const ScePspDateTime>::Create(datePtr);
	u64 result = __RtcPspTimeToTicks(*pspTime);

	if (!__RtcValidatePspTime(*pspTime) || result < rtcFiletimeOffset)
	{
		Memory::Write_U64(0, win32TimePtr);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	Memory::Write_U64((result - rtcFiletimeOffset) * 10, win32TimePtr);
	return 0;
}

static int sceRtcCompareTick(u32 tick1Ptr, u32 tick2Ptr)
{
	DEBUG_LOG(Log::sceRtc, "sceRtcCompareTick(%d,%d)", tick1Ptr, tick2Ptr);
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

static int sceRtcTickAddTicks(u32 destTickPtr, u32 srcTickPtr, u64 numTicks)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		u64 srcTick = Memory::Read_U64(srcTickPtr);

		srcTick += numTicks;
		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddTicks(%x,%x,%llu)", destTickPtr, srcTickPtr, numTicks);
	return 0;
}

static int sceRtcTickAddMicroseconds(u32 destTickPtr,u32 srcTickPtr, u64 numMS)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numMS;
		Memory::Write_U64(srcTick, destTickPtr);
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddMicroseconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numMS);
	return 0;
}

static int sceRtcTickAddSeconds(u32 destTickPtr, u32 srcTickPtr, u64 numSecs)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numSecs * 1000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddSeconds(%x,%x,%llu)", destTickPtr, srcTickPtr, numSecs);
	return 0;
}

static int sceRtcTickAddMinutes(u32 destTickPtr, u32 srcTickPtr, u64 numMins)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numMins*60000000UL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddMinutes(%x,%x,%llu)", destTickPtr, srcTickPtr, numMins);
	return 0;
}

static int sceRtcTickAddHours(u32 destTickPtr, u32 srcTickPtr, int numHours)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);
		srcTick += numHours * 3600ULL * 1000000ULL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddMinutes(%d,%d,%d)", destTickPtr, srcTickPtr, numHours);
	return 0;
}

static int sceRtcTickAddDays(u32 destTickPtr, u32 srcTickPtr, int numDays)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numDays * 86400ULL * 1000000ULL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddDays(%d,%d,%d)", destTickPtr, srcTickPtr, numDays);
	return 0;
}

static int sceRtcTickAddWeeks(u32 destTickPtr, u32 srcTickPtr, int numWeeks)
{
	if (Memory::IsValidAddress(destTickPtr) && Memory::IsValidAddress(srcTickPtr))
	{
		s64 srcTick = (s64)Memory::Read_U64(srcTickPtr);

		srcTick += numWeeks * 7ULL * 86400ULL * 1000000ULL;
		Memory::Write_U64(srcTick, destTickPtr);
	}
	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddWeeks(%d,%d,%d)", destTickPtr, srcTickPtr, numWeeks);
	return 0;
}

static int sceRtcTickAddMonths(u32 destTickPtr, u32 srcTickPtr, int numMonths)
{
	if (!Memory::IsValidAddress(destTickPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		WARN_LOG(Log::sceRtc, "sceRtcTickAddMonths(%08x, %08x, %d): invalid address", destTickPtr, srcTickPtr, numMonths);
		return -1;
	}

	u64 srcTick = Memory::Read_U64(srcTickPtr);

	ScePspDateTime pt;
	memset(&pt, 0, sizeof(pt));

	__RtcTicksToPspTime(pt,srcTick);
	pt.year += numMonths / 12;
	pt.month += numMonths % 12;

	if (pt.month < 1)
	{
		pt.month += 12;
		pt.year--;
	}
	if (pt.month > 12)
	{
		pt.month -= 12;
		pt.year++;
	}

	if (__RtcValidatePspTime(pt))
	{
		// Did we land on a year that isn't a leap year?
		if (pt.month == 2 && pt.day == 29 && !__RtcIsLeapYear((s16)pt.year))
			pt.day = 28;
		Memory::Write_U64(__RtcPspTimeToTicks(pt), destTickPtr);
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddMonths(%08x, %08x = %lld, %d)", destTickPtr, srcTickPtr, srcTick, numMonths);
	return 0;
}

static int sceRtcTickAddYears(u32 destTickPtr, u32 srcTickPtr, int numYears)
{
	if (!Memory::IsValidAddress(destTickPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		WARN_LOG(Log::sceRtc, "sceRtcTickAddYears(%08x, %08x, %d): invalid address", destTickPtr, srcTickPtr, numYears);
		return -1;
	}

	u64 srcTick = Memory::Read_U64(srcTickPtr);

	ScePspDateTime pt;
	memset(&pt, 0, sizeof(pt));

	__RtcTicksToPspTime(pt, srcTick);
	pt.year += numYears;

	if (__RtcValidatePspTime(pt))
	{
		// Did we land on a year that isn't a leap year?
		if (pt.month == 2 && pt.day == 29 && !__RtcIsLeapYear((s16)pt.year))
			pt.day = 28;
		Memory::Write_U64(__RtcPspTimeToTicks(pt), destTickPtr);
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcTickAddYears(%08x, %08x = %lld, %d)", destTickPtr, srcTickPtr, srcTick, numYears);
	return 0;
}

static int sceRtcParseDateTime(u32 destTickPtr, u32 dateStringPtr)
{
	ERROR_LOG_REPORT(Log::sceRtc, "UNIMPL sceRtcParseDateTime(%d,%d)", destTickPtr, dateStringPtr);
	return 0;
}

static int sceRtcGetLastAdjustedTime(u32 tickPtr)
{
	if (Memory::IsValidAddress(tickPtr))
		Memory::Write_U64(rtcLastAdjustedTicks, tickPtr);
	DEBUG_LOG(Log::sceRtc, "sceRtcGetLastAdjustedTime(%d)", tickPtr);
	return 0;
}

static int sceRtcGetLastReincarnatedTime(u32 tickPtr)
{
	if (Memory::IsValidAddress(tickPtr))
		Memory::Write_U64(rtcLastReincarnatedTicks, tickPtr);
	DEBUG_LOG(Log::sceRtc, "sceRtcGetLastReincarnatedTime(%d)", tickPtr);
	return 0;
}

//Returns 0 on success, according to Project Diva 2nd jpcsptrace log
static int sceRtcSetAlarmTick(u32 unknown1, u32 unknown2)
{
	ERROR_LOG_REPORT(Log::sceRtc, "UNIMPL sceRtcSetAlarmTick(%x, %x)", unknown1, unknown2);
	return 0; 
}

// Caller must check outPtr and srcTickPtr.
static int __RtcFormatRFC2822(u32 outPtr, u32 srcTickPtr, int tz)
{
	u64 srcTick = Memory::Read_U64(srcTickPtr);

	ScePspDateTime pt;
	memset(&pt, 0, sizeof(pt));

	__RtcTicksToPspTime(pt, srcTick);

	tm local;
	__RtcPspTimeToTm(local, pt);
	while (local.tm_year < 70)
		local.tm_year += 400;
	while (local.tm_year >= 470)
		local.tm_year -= 400;
	local.tm_min += tz;
	rtc_timegm(&local);

	char *out = (char *)Memory::GetPointerWriteUnchecked(outPtr);
	char *end = out + 32;
	out += strftime(out, end - out, "%a, %d %b ", &local);
	out += snprintf(out, end - out, "%04d", pt.year);
	out += strftime(out, end - out, " %H:%M:%S ", &local);
	if (tz < 0)
		out += snprintf(out, end - out, "-%02d%02d", -tz / 60, -tz % 60);
	else
		out += snprintf(out, end - out, "+%02d%02d", tz / 60, tz % 60);

	return 0;
}

static int __RtcFormatRFC3339(u32 outPtr, u32 srcTickPtr, int tz)
{
	u64 srcTick = Memory::Read_U64(srcTickPtr);

	ScePspDateTime pt;
	memset(&pt, 0, sizeof(pt));

	__RtcTicksToPspTime(pt, srcTick);

	tm local;
	__RtcPspTimeToTm(local, pt);
	while (local.tm_year < 70)
		local.tm_year += 400;
	while (local.tm_year >= 470)
		local.tm_year -= 400;
	local.tm_min += tz;
	rtc_timegm(&local);

	char *out = (char *)Memory::GetPointerWriteUnchecked(outPtr);
	char *end = out + 32;
	out += snprintf(out, end - out, "%04d", pt.year);
	out += strftime(out, end - out, "-%m-%dT%H:%M:%S.00", &local);
	if (tz == 0)
		out += snprintf(out, end - out, "Z");
	else if (tz < 0)
		out += snprintf(out, end - out, "-%02d:%02d", -tz / 60, -tz % 60);
	else
		out += snprintf(out, end - out, "+%02d:%02d", tz / 60, tz % 60);

	return 0;
}

static int sceRtcFormatRFC2822(u32 outPtr, u32 srcTickPtr, int tz)
{
	if (!Memory::IsValidAddress(outPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		// TODO: Not well tested.
		ERROR_LOG(Log::sceRtc, "sceRtcFormatRFC2822(%08x, %08x, %d): invalid address", outPtr, srcTickPtr, tz);
		return -1;
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcFormatRFC2822(%08x, %08x, %d)", outPtr, srcTickPtr, tz);
	return __RtcFormatRFC2822(outPtr, srcTickPtr, tz);
}

static int sceRtcFormatRFC2822LocalTime(u32 outPtr, u32 srcTickPtr)
{
	if (!Memory::IsValidAddress(outPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		// TODO: Not well tested.
		ERROR_LOG(Log::sceRtc, "sceRtcFormatRFC2822LocalTime(%08x, %08x): invalid address", outPtr, srcTickPtr);
		return -1;
	}

	int tz_seconds;
#ifdef _WIN32
		long timezone_val;
		_get_timezone(&timezone_val);
		tz_seconds = -timezone_val;
#elif !defined(_AIX) && !defined(__sgi) && !defined(__hpux) && !defined(HAVE_LIBNX)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		tz_seconds = time->tm_gmtoff;
#endif

	DEBUG_LOG(Log::sceRtc, "sceRtcFormatRFC2822LocalTime(%08x, %08x)", outPtr, srcTickPtr);
	return __RtcFormatRFC2822(outPtr, srcTickPtr, tz_seconds / 60);
}

static int sceRtcFormatRFC3339(u32 outPtr, u32 srcTickPtr, int tz)
{
	if (!Memory::IsValidAddress(outPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		// TODO: Not well tested.
		ERROR_LOG(Log::sceRtc, "sceRtcFormatRFC3339(%08x, %08x, %d): invalid address", outPtr, srcTickPtr, tz);
		return -1;
	}

	DEBUG_LOG(Log::sceRtc, "sceRtcFormatRFC3339(%08x, %08x, %d)", outPtr, srcTickPtr, tz);
	return __RtcFormatRFC3339(outPtr, srcTickPtr, tz);
}

static int sceRtcFormatRFC3339LocalTime(u32 outPtr, u32 srcTickPtr)
{
	if (!Memory::IsValidAddress(outPtr) || !Memory::IsValidAddress(srcTickPtr))
	{
		// TODO: Not well tested.
		ERROR_LOG(Log::sceRtc, "sceRtcFormatRFC3339LocalTime(%08x, %08x): invalid address", outPtr, srcTickPtr);
		return -1;
	}

	int tz_seconds;
#ifdef _WIN32
		long timezone_val;
		_get_timezone(&timezone_val);
		tz_seconds = -timezone_val;
#elif !defined(_AIX) && !defined(__sgi) && !defined(__hpux) && !defined(HAVE_LIBNX)
		time_t timezone = 0;
		tm *time = localtime(&timezone);
		tz_seconds = time->tm_gmtoff;
#endif

	DEBUG_LOG(Log::sceRtc, "sceRtcFormatRFC3339LocalTime(%08x, %08x)", outPtr, srcTickPtr);
	return __RtcFormatRFC3339(outPtr, srcTickPtr, tz_seconds / 60);
}

const HLEFunction sceRtc[] =
{
	{0XC41C2853, &WrapU_V<sceRtcGetTickResolution>,        "sceRtcGetTickResolution",        'x', ""   },
	{0X3F7AD767, &WrapU_U<sceRtcGetCurrentTick>,           "sceRtcGetCurrentTick",           'x', "x"  },
	{0X011F03C1, &WrapU64_V<sceRtcGetAccumulativeTime>,    "sceRtcGetAccumulativeTime",      'X', ""   },
	{0X029CA3B3, &WrapU64_V<sceRtcGetAccumulativeTime>,    "sceRtcGetAccumlativeTime",       'X', ""   },
	{0X4CFA57B0, &WrapU_UI<sceRtcGetCurrentClock>,         "sceRtcGetCurrentClock",          'i', "xi" },
	{0XE7C27D1B, &WrapU_U<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime", 'i', "x"  },
	{0X34885E0D, &WrapI_UU<sceRtcConvertUtcToLocalTime>,   "sceRtcConvertUtcToLocalTime",    'i', "xx" },
	{0X779242A2, &WrapI_UU<sceRtcConvertLocalTimeToUTC>,   "sceRtcConvertLocalTimeToUTC",    'i', "xx" },
	{0X42307A17, &WrapU_U<sceRtcIsLeapYear>,               "sceRtcIsLeapYear",               'x', "x"  },
	{0X05EF322C, &WrapU_UU<sceRtcGetDaysInMonth>,          "sceRtcGetDaysInMonth",           'x', "xx" },
	{0X57726BC1, &WrapU_UUU<sceRtcGetDayOfWeek>,           "sceRtcGetDayOfWeek",             'x', "xxx"},
	{0X4B1B5E82, &WrapI_U<sceRtcCheckValid>,               "sceRtcCheckValid",               'i', "x"  },
	{0X3A807CC8, &WrapI_UU<sceRtcSetTime_t>,               "sceRtcSetTime_t",                'i', "xx" },
	{0X27C4594C, &WrapI_UU<sceRtcGetTime_t>,               "sceRtcGetTime_t",                'i', "xp" },
	{0XF006F264, &WrapI_UU<sceRtcSetDosTime>,              "sceRtcSetDosTime",               'i', "xx" },
	{0X36075567, &WrapI_UU<sceRtcGetDosTime>,              "sceRtcGetDosTime",               'i', "xp" },
	{0X7ACE4C04, &WrapI_UU64<sceRtcSetWin32FileTime>,      "sceRtcSetWin32FileTime",         'i', "xX" },
	{0XCF561893, &WrapI_UU<sceRtcGetWin32FileTime>,        "sceRtcGetWin32FileTime",         'i', "xx" },
	{0X7ED29E40, &WrapU_UU<sceRtcSetTick>,                 "sceRtcSetTick",                  'x', "xP" },
	{0X6FF40ACC, &WrapU_UU<sceRtcGetTick>,                 "sceRtcGetTick",                  'i', "xP" },
	{0X9ED0AE87, &WrapI_UU<sceRtcCompareTick>,             "sceRtcCompareTick",              'i', "xx" },
	{0X44F45E05, &WrapI_UUU64<sceRtcTickAddTicks>,         "sceRtcTickAddTicks",             'i', "xxX"},
	{0X26D25A5D, &WrapI_UUU64<sceRtcTickAddMicroseconds>,  "sceRtcTickAddMicroseconds",      'i', "xxX"},
	{0XF2A4AFE5, &WrapI_UUU64<sceRtcTickAddSeconds>,       "sceRtcTickAddSeconds",           'i', "xxX"},
	{0XE6605BCA, &WrapI_UUU64<sceRtcTickAddMinutes>,       "sceRtcTickAddMinutes",           'i', "xxX"},
	{0X26D7A24A, &WrapI_UUI<sceRtcTickAddHours>,           "sceRtcTickAddHours",             'i', "xxi"},
	{0XE51B4B7A, &WrapI_UUI<sceRtcTickAddDays>,            "sceRtcTickAddDays",              'i', "xxi"},
	{0XCF3A2CA8, &WrapI_UUI<sceRtcTickAddWeeks>,           "sceRtcTickAddWeeks",             'i', "xxi"},
	{0XDBF74F1B, &WrapI_UUI<sceRtcTickAddMonths>,          "sceRtcTickAddMonths",            'i', "xxi"},
	{0X42842C77, &WrapI_UUI<sceRtcTickAddYears>,           "sceRtcTickAddYears",             'i', "xxi"},
	{0XC663B3B9, &WrapI_UUI<sceRtcFormatRFC2822>,          "sceRtcFormatRFC2822",            'i', "xxi"},
	{0X7DE6711B, &WrapI_UU<sceRtcFormatRFC2822LocalTime>,  "sceRtcFormatRFC2822LocalTime",   'i', "xx" },
	{0X0498FB3C, &WrapI_UUI<sceRtcFormatRFC3339>,          "sceRtcFormatRFC3339",            'i', "xxi"},
	{0X27F98543, &WrapI_UU<sceRtcFormatRFC3339LocalTime>,  "sceRtcFormatRFC3339LocalTime",   'i', "xx" },
	{0XDFBC5F16, &WrapI_UU<sceRtcParseDateTime>,           "sceRtcParseDateTime",            'i', "xx" },
	{0X28E1E988, nullptr,                                  "sceRtcParseRFC3339",             '?', ""   },
	{0XE1C93E47, &WrapI_UU<sceRtcGetTime64_t>,             "sceRtcGetTime64_t",              'i', "xP" },
	{0X1909C99B, &WrapI_UU64<sceRtcSetTime64_t>,           "sceRtcSetTime64_t",              'i', "xX" },
	{0X62685E98, &WrapI_U<sceRtcGetLastAdjustedTime>,      "sceRtcGetLastAdjustedTime",      'i', "x"  },
	{0X203CEB0D, &WrapI_U<sceRtcGetLastReincarnatedTime>,  "sceRtcGetLastReincarnatedTime",  'i', "x"  },
	{0X7D1FBED3, &WrapI_UU<sceRtcSetAlarmTick>,            "sceRtcSetAlarmTick",             'i', "xx" },
	{0XF5FCC995, nullptr,                                  "sceRtcGetCurrentNetworkTick",    '?', ""   },
	{0X81FCDA34, nullptr,                                  "sceRtcIsAlarmed",                '?', ""   },
	{0XFB3B18CD, nullptr,                                  "sceRtcRegisterCallback",         '?', ""   },
	{0X6A676D2D, nullptr,                                  "sceRtcUnregisterCallback",       '?', ""   },
	{0XC2DDBEB5, nullptr,                                  "sceRtcGetAlarmTick",             '?', ""   },
};

void Register_sceRtc()
{
	RegisterModule("sceRtc", ARRAY_SIZE(sceRtc), sceRtc);
}
