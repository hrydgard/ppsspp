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

#pragma once

u32 sceKernelLibcGettimeofday(u32 timeAddr, u32 tzAddr);
u32 sceKernelLibcTime(u32 outPtr);
int sceKernelUSec2SysClock(u32 microsec, u32 clockPtr);
int sceKernelGetSystemTime(u32 sysclockPtr);
u32 sceKernelGetSystemTimeLow();
u64 sceKernelGetSystemTimeWide();
int sceKernelSysClock2USec(u32 sysclockPtr, u32 highPtr, u32 lowPtr);
int sceKernelSysClock2USecWide(u32 lowClock, u32 highClock, u32 lowPtr, u32 highPtr);
u64 sceKernelUSec2SysClockWide(u32 usec);
u32 sceKernelLibcClock();

void __KernelTimeInit();
void __KernelTimeDoState(PointerWrap &p);
