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

void sceKernelGetSystemTime()
{
	SceKernelSysClock *clock = (SceKernelSysClock*)Memory::GetPointer(PARAM(0));
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	clock->lo = (u32)t;
	clock->hi = t >> 32;
	DEBUG_LOG(HLE,"sceKernelGetSystemTime(out:%08x%08x)",clock->hi, clock->lo);
	RETURN(0);
}

void sceKernelGetSystemTimeLow()
{
	// This clock should tick at 1 Mhz.
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	// DEBUG_LOG(HLE,"%08x=sceKernelGetSystemTimeLow()",(u32)t);
	RETURN((u32)t);
}

void sceKernelGetSystemTimeWide()
{
	u64 t = CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz();
	DEBUG_LOG(HLE,"%i=sceKernelGetSystemTimeWide()",(u32)t);
	RETURN((u32)t);
	currentMIPS->r[3] = t >> 32;
}

void sceKernelUSec2SysClock()
{
	u32 microseconds = PARAM(0);
	SceKernelSysClock *clock = (SceKernelSysClock*)Memory::GetPointer(PARAM(1));
	clock->lo = microseconds; //TODO: fix
	DEBUG_LOG(HLE,"sceKernelUSec2SysClock(%i, %08x )",PARAM(0), PARAM(1));
	RETURN(0);
}

void sceKernelSysClock2USec()
{
	SceKernelSysClock clock;
	Memory::ReadStruct(PARAM(0), &clock);
	DEBUG_LOG(HLE, "sceKernelSysClock2USec(clock = , lo = %08x, hi = %08x)", PARAM(1), PARAM(2));
	u64 time = clock.lo | ((u64)clock.hi << 32);
	if (Memory::IsValidAddress(PARAM(1)))
		Memory::Write_U32((u32)(time / 1000000), PARAM(1));
	if (Memory::IsValidAddress(PARAM(2)))
		Memory::Write_U32((u32)(time % 1000000), PARAM(2));
	RETURN(0);
}

void sceKernelSysClock2USecWide()
{
	u64 clock = PARAM(0) | ((u64)PARAM(1) << 32);
	DEBUG_LOG(HLE, "sceKernelSysClock2USecWide(clock = %llu, lo = %08x, hi = %08x)", clock, PARAM(2), PARAM(3));
	if (Memory::IsValidAddress(PARAM(2)))
		Memory::Write_U32((u32)(clock / 1000000), PARAM(2));
	if (Memory::IsValidAddress(PARAM(3)))
		Memory::Write_U32((u32)(clock % 1000000), PARAM(3));
	RETURN(0);
}

u32 sceKernelUSec2SysClockWide(u32 usec)
{
	DEBUG_LOG(HLE, "sceKernelUSec2SysClockWide(%i)", usec);
	return usec * 1000000;  // ?
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
	DEBUG_LOG(HLE,"sceKernelLibcGettimeofday()");

	GetSystemTimeAsFileTime (&now.ft);
	tv->tv_usec = (long) ((now.ns100 / 10LL) % 1000000LL);
	tv->tv_sec = (long) ((now.ns100 - 116444736000000000LL) / 10000000LL);
#endif
    return 0;
}
