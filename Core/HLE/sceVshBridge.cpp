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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceVshBridge.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelModule.h"

// sceVshBridge is the HLE surface of flash0:/kd/vshbridge.prx, a kernel-mode module that
// exposes a bunch of otherwise-kernel-only functionality (controller reads, LoadExec
// variants, audio/ME/display bits, MagicGate memory stick audio, etc.) to the VSH, which
// mostly runs in user mode.
//
// Unlike sceVshCommonUtil/sceVshCommonGui, we intentionally only implement the handful of
// NIDs that genuinely need to trap into PPSSPP's own host-level code (controller input,
// process control, our own module-loading machinery) - the same reasoning that applies to
// every other kernel syscall PPSSPP HLEs for regular games. We now load and start the real
// vshbridge.prx as part of booting the VSH (see LoadAndStartVshKernelModules in
// sceKernelModule.cpp), so registering a stub for every other NID here would silently discard
// vshbridge.prx's real, working exports in favor of a placeholder that just logs an error -
// Core/HLE/sceKernelModule.cpp's ExportFuncSymbol() ignores a module's real export if we claim
// to already have HLE for that NID. Found this the hard way with sceVshCommonUtil first.

const HLEFunction sceVshBridge[] = {
	{0XC9626587, &WrapI_UUUU<sceKernelLoadModuleBufferUsbWlan>, "vshKernelLoadModuleBufferVSH", 'i', "xxxx"},
	{0XC6395C03, &WrapI_UU<sceCtrlReadBufferPositive>,           "vshCtrlReadBufferPositive",    'i', "xx"  },
	{0X9929DDA5, &WrapV_V<sceKernelExitGame>,                    "vshKernelExitVSH",             'v', ""    },
};

void Register_sceVshBridge() {
	RegisterHLEModule("sceVshBridge", ARRAY_SIZE(sceVshBridge), sceVshBridge);
}
