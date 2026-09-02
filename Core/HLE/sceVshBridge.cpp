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

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/Serializer.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceVshBridge.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"

namespace {

u32 vshMediaPoolObject;
u32 vshMediaPoolBase;
u32 vshMediaPoolSize;
u32 vshMediaPoolOwner;

bool EnsureVshMediaPool() {
	if (Memory::IsValidRange(vshMediaPoolObject, 12) &&
		Memory::IsValidRange(vshMediaPoolBase, vshMediaPoolSize)) {
		return true;
	}

	// vsh_module's native bootstrap selects 12 MiB on Slim memory and 8 MiB
	// otherwise (see its sceKernelTotalMemSize comparison). Direct VSH begins
	// above that bootstrap, so provide the same shared application pool from
	// PPSSPP's serialized user-memory allocator.
	const u32 totalUserMemory = PSP_GetUserMemoryEnd() - PSP_GetUserMemoryBase();
	vshMediaPoolSize = totalUserMemory > 0x023FFFFF ? 0x00C00000 : 0x00800000;
	vshMediaPoolBase = userMemory.Alloc(vshMediaPoolSize, true, "VSHMediaPool");
	if (vshMediaPoolBase == (u32)-1) {
		vshMediaPoolBase = 0;
		vshMediaPoolSize = 0;
		return false;
	}

	u32 objectSize = 12;
	vshMediaPoolObject = userMemory.Alloc(objectSize, false, "VSHMediaPoolObject");
	if (vshMediaPoolObject == (u32)-1) {
		userMemory.Free(vshMediaPoolBase);
		vshMediaPoolObject = 0;
		vshMediaPoolBase = 0;
		vshMediaPoolSize = 0;
		return false;
	}

	Memory::WriteUnchecked_U32(vshMediaPoolBase, vshMediaPoolObject + 0);
	Memory::WriteUnchecked_U32(vshMediaPoolSize, vshMediaPoolObject + 4);
	Memory::WriteUnchecked_U32(0, vshMediaPoolObject + 8);
	vshMediaPoolOwner = 0;
	NOTICE_LOG(Log::ME, "Initialized Direct VSH shared media pool: %08x size=%u", vshMediaPoolBase, vshMediaPoolSize);
	return true;
}

u32 vsh_FC5BB3A0() {
	if (!__KernelIsRunningVSH()) {
		return hleLogError(Log::ME, 0, "not in Direct VSH");
	}
	if (!EnsureVshMediaPool()) {
		return hleLogError(Log::ME, 0, "could not allocate shared media pool");
	}
	return hleLogDebug(Log::ME, vshMediaPoolObject);
}

u32 vsh_FAB1C740(u32 objectAddr, u32 owner) {
	if (!EnsureVshMediaPool() || objectAddr != vshMediaPoolObject ||
		!Memory::IsValidRange(objectAddr, 12)) {
		return hleLogError(Log::ME, 0, "invalid media pool object %08x", objectAddr);
	}

	// Native VSH records the acquiring component in object+8. Direct VSH media
	// applications are mutually exclusive; accepting a later owner models the
	// same handoff even when a plugin remains resident between XMB sessions.
	if (vshMediaPoolOwner != 0 && vshMediaPoolOwner != owner) {
		INFO_LOG(Log::ME, "Direct VSH media pool owner changed %08x -> %08x", vshMediaPoolOwner, owner);
	}
	vshMediaPoolOwner = owner;
	Memory::WriteUnchecked_U32(owner, objectAddr + 8);
	return hleLogDebug(Log::ME, vshMediaPoolBase, "owner=%08x", owner);
}

const HLEFunction vsh[] = {
	{0XFC5BB3A0, &WrapU_V<vsh_FC5BB3A0>,       "vsh_FC5BB3A0", 'x', ""  },
	{0XFAB1C740, &WrapU_UU<vsh_FAB1C740>,      "vsh_FAB1C740", 'x', "xx"},
};

}  // namespace

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

void Register_vsh() {
	RegisterHLEModule("vsh", ARRAY_SIZE(vsh), vsh);
}

void __VshBridgeInit() {
	vshMediaPoolObject = 0;
	vshMediaPoolBase = 0;
	vshMediaPoolSize = 0;
	vshMediaPoolOwner = 0;
}

void __VshBridgeDoState(PointerWrap &p) {
	auto s = p.Section("DirectVSHMediaPool", 1);
	if (!s) {
		return;
	}
	Do(p, vshMediaPoolObject);
	Do(p, vshMediaPoolBase);
	Do(p, vshMediaPoolSize);
	Do(p, vshMediaPoolOwner);
}

VSHMediaPoolDebugStatus __VshBridgeGetMediaPoolDebugStatus() {
	return {vshMediaPoolObject, vshMediaPoolBase, vshMediaPoolSize, vshMediaPoolOwner};
}
