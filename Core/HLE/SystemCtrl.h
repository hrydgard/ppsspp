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

#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

void __SystemCtrlInit();
void __SystemCtrlDoState(PointerWrap &p);

void Register_SystemCtrlForKernel();
void Register_SystemCtrlForUser();
void Register_SystemCtrlGuestCalls();
void Register_SysclibForUser();
void Register_ArkCtrl();
void Register_SystemCtrlHardware();

// Whether a CFW looking up that driver function should get ours (and in which library).
const char *__SystemCtrlHardwareLibrary(const char *library, u32 nid);

// SysMemForKernel's, kept here with the rest of what CFW needs.
u32 sceKernelGetGameInfo();

// Takes the ARK version that sctrlSEGetVersion and friends report from ARK's own systemctrl.
void __SystemCtrlReadArkVersion(const std::vector<u8> &prx);
