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

u32 sceKernelCreateVTimer(const char *name, u32 optParamAddr);
u32 sceKernelDeleteVTimer(u32 uid);
u32 sceKernelStartVTimer(u32 uid);
u32 sceKernelStopVTimer(u32 uid);
u32 sceKernelSetVTimerHandler(u32 uid, u32 scheduleAddr, u32 handlerFuncAddr, u32 commonAddr);
u32 sceKernelSetVTimerHandlerWide(u32 uid, u64 schedule, u32 handlerFuncAddr, u32 commonAddr);
u32 sceKernelCancelVTimerHandler(u32 uid);
u32 sceKernelReferVTimerStatus(u32 uid, u32 statusAddr);
u32 sceKernelGetVTimerBase(u32 uid, u32 baseClockAddr); //SceKernelSysClock
u64 sceKernelGetVTimerBaseWide(u32 uid);
u32 sceKernelGetVTimerTime(u32 uid, u32 timeClockAddr);
u64 sceKernelGetVTimerTimeWide(u32 uid);
u32 sceKernelSetVTimerTime(u32 uid, u32 timeClockAddr);
u32 sceKernelSetVTimerTimeWide(u32 uid, u64 timeClock);

// TODO
void _sceKernelReturnFromTimerHandler();

void __KernelVTimerInit();
void __KernelVTimerDoState(PointerWrap &p);
KernelObject *__KernelVTimerObject();
