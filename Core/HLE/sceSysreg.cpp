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

// The parts of the firmware's system register driver homebrew uses to start the Media Engine. Each
// sets or clears a bit of a register the way the driver does (lowio.prx 6.61 at 080001b8 and
// 080004b8, sysreg.prx 1.50 the same), returning whether it was set. Only the NIDs of the firmware
// versions homebrew was built for (1.50, and 3.71 and later's for the ME calls), so the VSH's own
// later lowio.prx keeps its exports.

#include "Common/CommonTypes.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceSysreg.h"
#include "Core/HW/MMIO.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MeCore.h"

enum : u32 {
	SYSREG_RESET = 0xBC10004C,
	SYSREG_BUS_CLOCK = 0xBC100050,

	RESET_ME = 0x04,
	RESET_VME = 0x10,
	RESET_AVC = 0x20,
	BUS_CLOCK_ME = 0x01,
};

static int SetSysregBit(MIPSState *mips, u32 reg, u32 bit, bool set) {
	const u32 old = MMIO::Read(mips, reg, 4);
	MMIO::Write(mips, reg, 4, set ? (old | bit) : (old & ~bit));
	return (old & bit) ? 1 : 0;
}

static int sceSysregMeResetEnable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_ME, true));
}

static int sceSysregMeResetDisable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_ME, false));
}

static int sceSysregMeBusClockEnable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_BUS_CLOCK, BUS_CLOCK_ME, true));
}

static int sceSysregMeBusClockDisable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_BUS_CLOCK, BUS_CLOCK_ME, false));
}

static int sceSysregVmeResetEnable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_VME, true));
}

static int sceSysregVmeResetDisable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_VME, false));
}

static int sceSysregAvcResetEnable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_AVC, true));
}

static int sceSysregAvcResetDisable(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_RESET, RESET_AVC, false));
}

const HLEFunction sceSysreg_driver[] = {
	{0xDE59DACB, &WrapI_V<sceSysregMeResetEnable>,     "sceSysregMeResetEnable",     'i', "", HLE_KERNEL_SYSCALL },
	{0x2DB0EB28, &WrapI_V<sceSysregMeResetDisable>,    "sceSysregMeResetDisable",    'i', "", HLE_KERNEL_SYSCALL },
	{0x44F6CDA7, &WrapI_V<sceSysregMeBusClockEnable>,  "sceSysregMeBusClockEnable",  'i', "", HLE_KERNEL_SYSCALL },
	{0x158AD4FC, &WrapI_V<sceSysregMeBusClockDisable>, "sceSysregMeBusClockDisable", 'i', "", HLE_KERNEL_SYSCALL },
	{0xD20581EA, &WrapI_V<sceSysregVmeResetEnable>,    "sceSysregVmeResetEnable",    'i', "", HLE_KERNEL_SYSCALL },
	{0x7558064A, &WrapI_V<sceSysregVmeResetDisable>,   "sceSysregVmeResetDisable",   'i', "", HLE_KERNEL_SYSCALL },
	{0x9BB70D34, &WrapI_V<sceSysregAvcResetEnable>,    "sceSysregAvcResetEnable",    'i', "", HLE_KERNEL_SYSCALL },
	{0xFD6C562B, &WrapI_V<sceSysregAvcResetDisable>,   "sceSysregAvcResetDisable",   'i', "", HLE_KERNEL_SYSCALL },
	// 3.71 and later.
	{0xA9997109, &WrapI_V<sceSysregMeResetEnable>,     "sceSysregMeResetEnable371",    'i', "", HLE_KERNEL_SYSCALL },
	{0x76220E94, &WrapI_V<sceSysregMeResetDisable>,    "sceSysregMeResetDisable371",   'i', "", HLE_KERNEL_SYSCALL },
	{0x3199CF1C, &WrapI_V<sceSysregMeBusClockEnable>,  "sceSysregMeBusClockEnable371", 'i', "", HLE_KERNEL_SYSCALL },
};

// Custom-core homebrew (mcidclan's libraries) walks this to find me_wrapper's "SceMeRpc" handler,
// and puts its own in it. MeCore::KernelInit() makes that one record.
static u32 sceKernelReferSysEventHandler() {
	return hleLogDebug(Log::ME, MeCore::g_meState ? MeCore::g_meState->sysEventHandlers : 0);
}

// Takes a handler out of that list (snes9xTYL does it to the "SceMeRpc" one). Next is at 0x18.
static int sceKernelUnregisterSysEventHandler(u32 handler) {
	if (!MeCore::g_meState)
		return hleLogWarning(Log::ME, 0, "no handlers");
	u32 *link = &MeCore::g_meState->sysEventHandlers;
	while (*link && Memory::IsValidRange(*link, 0x40)) {
		if (*link == handler) {
			*link = Memory::ReadUnchecked_U32(handler + 0x18);
			return hleLogDebug(Log::ME, 0);
		}
		link = (u32 *)Memory::GetPointerWriteUnchecked(*link + 0x18);
	}
	return hleLogWarning(Log::ME, -1, "not registered");
}

const HLEFunction sceSysEventForKernel[] = {
	{0x68D55505, &WrapU_V<sceKernelReferSysEventHandler>,      "sceKernelReferSysEventHandler",      'x', "",  HLE_KERNEL_SYSCALL },
	{0xD7D3FDCD, &WrapI_U<sceKernelUnregisterSysEventHandler>, "sceKernelUnregisterSysEventHandler", 'i', "x", HLE_KERNEL_SYSCALL },
};

void Register_sceSysEventForKernel() {
	RegisterHLEModule("sceSysEventForKernel", ARRAY_SIZE(sceSysEventForKernel), sceSysEventForKernel);
}

void Register_sceSysreg_driver() {
	RegisterHLEModule("sceSysreg_driver", ARRAY_SIZE(sceSysreg_driver), sceSysreg_driver);
}
