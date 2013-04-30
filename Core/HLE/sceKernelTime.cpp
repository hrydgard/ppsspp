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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <time.h>

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelTime.h"

#include "../CoreTiming.h"
#include "ChunkFile.h"

//////////////////////////////////////////////////////////////////////////
// State
//////////////////////////////////////////////////////////////////////////

// The time when the game started.
time_t start_time;

//////////////////////////////////////////////////////////////////////////
// Other clock stuff
//////////////////////////////////////////////////////////////////////////

void __KernelTimeInit()
{
	time(&start_time);
}

void __KernelTimeDoState(PointerWrap &p)
{
	p.Do(start_time);
	p.DoMarker("sceKernelTime");
}

struct SceKernelSysClock
{
	u32 lo;
	u32 hi;
};

int sceKernelGetSystemTime(u32 sysclockPtr)
{
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	if (Memory::IsValidAddress(sysclockPtr)) 
		Memory::Write_U64(t, sysclockPtr);
	DEBUG_LOG(HLE, "sceKernelGetSystemTime(out:%16llx)", t);
	hleEatCycles(2 * 222);
	return 0;
}

u32 sceKernelGetSystemTimeLow()
{
	// This clock should tick at 1 Mhz.
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	VERBOSE_LOG(HLE,"%08x=sceKernelGetSystemTimeLow()",(u32)t);
	return (u32)t;
}

u64 sceKernelGetSystemTimeWide()
{
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	DEBUG_LOG(HLE,"%i=sceKernelGetSystemTimeWide()",(u32)t);
	hleEatCycles(1 * 222);
	return t;
}

int sceKernelUSec2SysClock(u32 usec, u32 clockPtr)
{
	DEBUG_LOG(HLE,"sceKernelUSec2SysClock(%i, %08x )", usec, clockPtr);
	if (Memory::IsValidAddress(clockPtr))
		Memory::Write_U32((usec & 0xFFFFFFFFL), clockPtr);
	hleEatCycles(2 * 222);
	return 0;
}

u64 sceKernelUSec2SysClockWide(u32 usec)
{
	DEBUG_LOG(HLE, "sceKernelUSec2SysClockWide(%i)", usec);
	return usec; 
}

int sceKernelSysClock2USec(u32 sysclockPtr, u32 highPtr, u32 lowPtr)
{
	DEBUG_LOG(HLE, "sceKernelSysClock2USec(clock = %08x, lo = %08x, hi = %08x)", sysclockPtr, highPtr, lowPtr);
	u64 time = Memory::Read_U64(sysclockPtr);
	u32 highResult = (u32)(time / 1000000);
	u32 lowResult = (u32)(time % 1000000);
	if (Memory::IsValidAddress(highPtr))
		Memory::Write_U32(highResult, highPtr);
	if (Memory::IsValidAddress(lowPtr))
		Memory::Write_U32(lowResult, lowPtr);
	hleEatCycles(2 * 222);
	return 0;
}

int sceKernelSysClock2USecWide(u32 lowClock, u32 highClock, u32 lowPtr, u32 highPtr)
{
	u64 sysClock = lowClock | ((u64)highClock << 32);
	DEBUG_LOG(HLE, "sceKernelSysClock2USecWide(clock = %llu, lo = %08x, hi = %08x)", sysClock, lowPtr, highPtr);
	if (Memory::IsValidAddress(lowPtr)) {
		Memory::Write_U32((u32)(sysClock / 1000000), lowPtr);
		if (Memory::IsValidAddress(highPtr)) 
			Memory::Write_U32((u32)(sysClock % 1000000), highPtr);
	} else 
		if (Memory::IsValidAddress(highPtr)) 
			Memory::Write_U32((int) sysClock, highPtr);
	return 0;
}

u32 sceKernelLibcClock()
{
	u32 retVal = (u32) (CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz());
	DEBUG_LOG(HLE, "%i = sceKernelLibcClock", retVal);
	return retVal;
}

u32 sceKernelLibcTime(u32 outPtr)
{
	u32 t = (u32) start_time + (u32) (CoreTiming::GetTicks() / CPU_HZ);

	DEBUG_LOG(HLE, "%i = sceKernelLibcTime(%08X)", t, outPtr);

	if (Memory::IsValidAddress(outPtr))
		Memory::Write_U32(t, outPtr);
	else if (outPtr != 0)
		return 0;

	return t;
}

u32 sceKernelLibcGettimeofday(u32 timeAddr)
{
#ifdef _WIN32
	union {
		s64 ns100; /*time since 1 Jan 1601 in 100ns units */
		FILETIME ft;
	} now;

	struct timeval
	{
		u32 tv_sec;
		u32 tv_usec;
	};

	timeval *tv = (timeval*)Memory::GetPointer(timeAddr);

	GetSystemTimeAsFileTime (&now.ft);
	tv->tv_usec = (long) ((now.ns100 / 10LL) % 1000000LL);
	tv->tv_sec = (long) ((now.ns100 - 116444736000000000LL) / 10000000LL);
#else
	timeval *tv = (timeval*)Memory::GetPointer(timeAddr);
	gettimeofday(tv, NULL);
#endif
	DEBUG_LOG(HLE,"sceKernelLibcGettimeofday()");
    return 0;
}
