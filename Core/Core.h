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

#include "Core/System.h"
#include "Core/CoreParameter.h"

class GraphicsContext;

// called from emu thread
void UpdateRunLoop();

void Core_Run(GraphicsContext *ctx);
void Core_Stop();
void Core_ErrorPause();
// For platforms that don't call Core_Run
void Core_SetGraphicsContext(GraphicsContext *ctx);

void Core_RunRenderThreadFrame();

// called from gui
void Core_EnableStepping(bool step);
void Core_DoSingleStep();
void Core_UpdateSingleStep();

typedef void (* Core_ShutdownFunc)();
void Core_ListenShutdown(Core_ShutdownFunc func);
void Core_NotifyShutdown();
void Core_Halt(const char *msg);

bool Core_IsStepping();

bool Core_IsActive();
bool Core_IsInactive();
void Core_WaitInactive();
void Core_WaitInactive(int milliseconds);

bool UpdateScreenScale(int width, int height);

// Don't run the core when minimized etc.
void Core_NotifyWindowHidden(bool hidden);
void Core_NotifyActivity();

void Core_SetPowerSaving(bool mode);
bool Core_GetPowerSaving();
