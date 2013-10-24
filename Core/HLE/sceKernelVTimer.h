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
u32 sceKernelDeleteVTimer(SceUID uid);
u32 sceKernelStartVTimer(SceUID uid);
u32 sceKernelStopVTimer(SceUID uid);
u32 sceKernelSetVTimerHandler(SceUID uid, u32 scheduleAddr, u32 handlerFuncAddr, u32 commonAddr);
u32 sceKernelSetVTimerHandlerWide(SceUID uid, u64 schedule, u32 handlerFuncAddr, u32 commonAddr);
u32 sceKernelCancelVTimerHandler(SceUID uid);
u32 sceKernelReferVTimerStatus(SceUID uid, u32 statusAddr);
u32 sceKernelGetVTimerBase(SceUID uid, u32 baseClockAddr); //SceKernelSysClock
u64 sceKernelGetVTimerBaseWide(SceUID uid);
u32 sceKernelGetVTimerTime(SceUID uid, u32 timeClockAddr);
u64 sceKernelGetVTimerTimeWide(SceUID uid);
u32 sceKernelSetVTimerTime(SceUID uid, u32 timeClockAddr);
u64 sceKernelSetVTimerTimeWide(SceUID uid, u64 timeClock);

void _sceKernelReturnFromTimerHandler();

void __KernelVTimerInit();
void __KernelVTimerDoState(PointerWrap &p);
KernelObject *__KernelVTimerObject();
