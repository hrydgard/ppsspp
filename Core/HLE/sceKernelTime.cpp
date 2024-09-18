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
#else
#include <sys/time.h>
#endif

#include <time.h>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelTime.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceRtc.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "StringUtils.h"

// The time when the game started.
static time_t start_time;

void __KernelTimeInit()
{
	time(&start_time);
	if (PSP_CoreParameter().compat.flags().DateLimited) {
		// Car Jack Streets(NPUZ00043) requires that the date cannot exceed a certain time.
		// 2011 year makes it work fine.
		tm *tm;
		tm = localtime(&start_time);
		tm->tm_year = 111;// 2011 year.
		start_time = mktime(tm);
	}
}

void __KernelTimeDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelTime", 1, 2);
	if (!s)
		return;

	if (s < 2) {
		Do(p, start_time);
	} else {
		u64 t = start_time;
		Do(p, t);
		start_time = (time_t)t;
	}
}

int sceKernelGetSystemTime(u32 sysclockPtr)
{
	u64 t = CoreTiming::GetGlobalTimeUs();
	if (Memory::IsValidAddress(sysclockPtr)) 
		Memory::Write_U64(t, sysclockPtr);
	VERBOSE_LOG(Log::sceKernel, "sceKernelGetSystemTime(out:%16llx)", t);
	hleEatCycles(265);
	hleReSchedule("system time");
	return 0;
}

u32 sceKernelGetSystemTimeLow()
{
	// This clock should tick at 1 Mhz.
	u64 t = CoreTiming::GetGlobalTimeUs();
	VERBOSE_LOG(Log::sceKernel,"%08x=sceKernelGetSystemTimeLow()",(u32)t);
	hleEatCycles(165);
	if (PSP_CoreParameter().compat.flags().KernelGetSystemTimeLowEatMoreCycles)
		hleEatCycles(70000);
	hleReSchedule("system time");
	return (u32)t;
}

u64 sceKernelGetSystemTimeWide()
{
	u64 t = CoreTiming::GetGlobalTimeUsScaled();
	VERBOSE_LOG(Log::sceKernel,"%i=sceKernelGetSystemTimeWide()",(u32)t);
	hleEatCycles(250);
	hleReSchedule("system time");
	return t;
}

int sceKernelUSec2SysClock(u32 usec, u32 clockPtr)
{
	VERBOSE_LOG(Log::sceKernel, "sceKernelUSec2SysClock(%i, %08x)", usec, clockPtr);
	if (Memory::IsValidAddress(clockPtr))
		Memory::Write_U64((usec & 0xFFFFFFFFL), clockPtr);
	hleEatCycles(165);
	return 0;
}

u64 sceKernelUSec2SysClockWide(u32 usec)
{
	VERBOSE_LOG(Log::sceKernel, "sceKernelUSec2SysClockWide(%i)", usec);
	hleEatCycles(150);
	return usec; 
}

int sceKernelSysClock2USec(u32 sysclockPtr, u32 highPtr, u32 lowPtr)
{
	DEBUG_LOG(Log::sceKernel, "sceKernelSysClock2USec(clock = %08x, lo = %08x, hi = %08x)", sysclockPtr, highPtr, lowPtr);
	u64 time = Memory::Read_U64(sysclockPtr);
	u32 highResult = (u32)(time / 1000000);
	u32 lowResult = (u32)(time % 1000000);
	if (Memory::IsValidAddress(highPtr))
		Memory::Write_U32(highResult, highPtr);
	if (Memory::IsValidAddress(lowPtr))
		Memory::Write_U32(lowResult, lowPtr);
	hleEatCycles(415);
	return 0;
}

int sceKernelSysClock2USecWide(u32 lowClock, u32 highClock, u32 lowPtr, u32 highPtr)
{
	u64 sysClock = lowClock | ((u64)highClock << 32);
	DEBUG_LOG(Log::sceKernel, "sceKernelSysClock2USecWide(clock = %llu, lo = %08x, hi = %08x)", sysClock, lowPtr, highPtr);
	if (Memory::IsValidAddress(lowPtr)) {
		Memory::Write_U32((u32)(sysClock / 1000000), lowPtr);
		if (Memory::IsValidAddress(highPtr)) 
			Memory::Write_U32((u32)(sysClock % 1000000), highPtr);
	} else 
		if (Memory::IsValidAddress(highPtr)) 
			Memory::Write_U32((int) sysClock, highPtr);
	hleEatCycles(385);
	return 0;
}

u32 sceKernelLibcClock()
{
	u32 retVal = (u32) CoreTiming::GetGlobalTimeUs();
	DEBUG_LOG(Log::sceKernel, "%i = sceKernelLibcClock", retVal);
	hleEatCycles(330);
	hleReSchedule("libc clock");
	return retVal;
}

u32 sceKernelLibcTime(u32 outPtr)
{
	u32 t = (u32) start_time + (u32) (CoreTiming::GetGlobalTimeUs() / 1000000ULL);

	DEBUG_LOG(Log::sceKernel, "%i = sceKernelLibcTime(%08X)", t, outPtr);
	// The PSP sure takes its sweet time on this function.
	hleEatCycles(3385);

	if (Memory::IsValidAddress(outPtr))
		Memory::Write_U32(t, outPtr);
	else if (outPtr != 0)
		return 0;

	hleReSchedule("libc time");
	return t;
}

u32 sceKernelLibcGettimeofday(u32 timeAddr, u32 tzAddr)
{
	// TODO: tzAddr?
	if (Memory::IsValidAddress(timeAddr))
	{
		PSPTimeval *tv = (PSPTimeval *)Memory::GetPointer(timeAddr);
		__RtcTimeOfDay(tv);
	}

	DEBUG_LOG(Log::sceKernel,"sceKernelLibcGettimeofday(%08x, %08x)", timeAddr, tzAddr);
	hleEatCycles(1885);

	hleReSchedule("libc timeofday");
	return 0;
}

std::string KernelTimeNowFormatted() {
	time_t emulatedTime = (time_t)start_time + (u32)(CoreTiming::GetGlobalTimeUs() / 1000000ULL);
	tm* timePtr = localtime(&emulatedTime);
	bool DST = timePtr->tm_isdst != 0;
	u8 seconds = timePtr->tm_sec;
	u8 minutes = timePtr->tm_min;
	u8 hours = timePtr->tm_hour;
	if (DST)
		hours = timePtr->tm_hour + 1;
	u8 days = timePtr->tm_mday;
	u8 months = timePtr->tm_mon + 1;
	u16 years = timePtr->tm_year + 1900;
	std::string timestamp = StringFromFormat("%04d-%02d-%02d_%02d-%02d-%02d", years, months, days, hours, minutes, seconds);
	return timestamp;
}

void KernelTimeSetBase(int64_t seconds) {
	start_time = (time_t)seconds;
}
