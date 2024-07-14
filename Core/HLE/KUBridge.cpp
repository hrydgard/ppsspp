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

// KUBridge seems to be some utility module that comes with some custom firmware for PSP,
// providing the ability to call some kernel-only functions from user mode.
// A few homebrew applications use this. We only simulate a small subset of the functionality for now.

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/KUBridge.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/Config.h"

static int kuKernelLoadModule(const char *path, uint32_t flags, uint32_t lmOptionAddr) {
	INFO_LOG(Log::sceKernel, "kuKernelLoadModule - forwarding to sceKernelLoadModule");
	// Simply forward the call, like JPSCP does.
	return sceKernelLoadModule(path, flags, lmOptionAddr);
}

static int kuKernelGetModel() {
	INFO_LOG(Log::sceKernel, "kuKernelGetModel()");
	return g_Config.iPSPModel;
}

const HLEFunction KUBridge[] =
{
	{ 0x4C25EA72, &WrapI_CUU<kuKernelLoadModule>, "kuKernelLoadModule", 'i', "sxx" },
	{ 0x24331850, &WrapI_V<kuKernelGetModel>, "kuKernelGetModel", 'i', "" },
};

void Register_KUBridge() {
	RegisterModule("KUBridge", ARRAY_SIZE(KUBridge), KUBridge);
}
