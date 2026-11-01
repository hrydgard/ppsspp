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

// The parts of the firmware's system register driver homebrew and the POPS manager use to start the
// Media Engine. Each sets or clears a bit of a register the way the driver does (lowio.prx 6.61 at
// 080001b8, 080004b8, 08000880 and 08000cd8, sysreg.prx 1.50 the same), returning whether it was
// set. Under --vsh these stand in for lowio.prx's own, which write the same registers.

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
	SYSREG_CLOCK_58 = 0xBC100058,
	SYSREG_SEMA = 0xBC100048,
	SYSREG_60 = 0xBC100060,
	SYSREG_IO_78 = 0xBC100078,

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

// Tries to take the semaphore the main CPU and the ME share: 0 if it got it, -1 if the other CPU
// holds it. As lowio.prx does: set this CPU's bit (CPU ID + 1) and read it back.
static int sceSysregSemaTryLock(MIPSState *mips) {
	const u32 own = mips->IsMediaEngine() ? 2 : 1;
	MMIO::Write(mips, SYSREG_SEMA, 4, own);
	return hleLogDebug(Log::ME, (MMIO::Read(mips, SYSREG_SEMA, 4) & 3) == own ? 0 : -1);
}

static int sceSysregSemaUnlock(MIPSState *mips) {
	MMIO::Write(mips, SYSREG_SEMA, 4, 0);
	return hleLogDebug(Log::ME, 0);
}

// The 6.60/6.61 calls popsman.prx makes whose names aren't known, after what lowio.prx does.
// Those taking an index only take 0 or 1.
static int SetSysregIndexedBit(MIPSState *mips, u32 reg, u32 bit0, int index) {
	if (index < 0 || index >= 2)
		return hleLogError(Log::ME, 0x80000102, "bad index");
	return hleLogDebug(Log::ME, SetSysregBit(mips, reg, bit0 << index, true));
}

static int sceSysreg_driver_8B1DD83A(MIPSState *mips, int index) {
	return SetSysregIndexedBit(mips, SYSREG_BUS_CLOCK, 0x8000, index);
}

static int sceSysreg_driver_B1751B06(MIPSState *mips, int index) {
	return SetSysregIndexedBit(mips, SYSREG_CLOCK_58, 0x10000, index);
}

static int sceSysreg_driver_4634C9DC(MIPSState *mips, int index) {
	return SetSysregIndexedBit(mips, SYSREG_IO_78, 0x80, index);
}

static int sceSysreg_driver_4B4CCE80(MIPSState *mips) {
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_IO_78, 0x800, true));
}

// Sets 0x60 bit 18 to value, returning what it was.
static int sceSysreg_driver_41B0337B(MIPSState *mips, int value) {
	if (value < 0 || value >= 2)
		return hleLogError(Log::ME, 0x800001FE, "bad value");
	return hleLogDebug(Log::ME, SetSysregBit(mips, SYSREG_60, 1 << 18, value != 0));
}

// popsman calls it with (0, 1) before setting up audio. Not read yet.
static int sceSysreg_driver_C36775AD(int a, int b) {
	return hleLogWarning(Log::ME, 0, "unimplemented");
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
	// 6.60 and 6.61, which popsman.prx uses.
	{0x457FEBA9, &WrapI_V<sceSysregMeResetEnable>,     "sceSysregMeResetEnable660",    'i', "",   HLE_KERNEL_SYSCALL },
	{0x48F1C4AD, &WrapI_V<sceSysregMeResetDisable>,    "sceSysregMeResetDisable660",   'i', "",   HLE_KERNEL_SYSCALL },
	{0x38527743, &WrapI_V<sceSysregMeBusClockEnable>,  "sceSysregMeBusClockEnable660", 'i', "",   HLE_KERNEL_SYSCALL },
	{0x0AE8E549, &WrapI_V<sceSysregAvcResetEnable>,    "sceSysregAvcResetEnable660",   'i', "",   HLE_KERNEL_SYSCALL },
	{0x8B1DD83A, &WrapI_I<sceSysreg_driver_8B1DD83A>,  "sceSysreg_driver_8B1DD83A",    'i', "i",  HLE_KERNEL_SYSCALL },
	{0xB1751B06, &WrapI_I<sceSysreg_driver_B1751B06>,  "sceSysreg_driver_B1751B06",    'i', "i",  HLE_KERNEL_SYSCALL },
	{0x4634C9DC, &WrapI_I<sceSysreg_driver_4634C9DC>,  "sceSysreg_driver_4634C9DC",    'i', "i",  HLE_KERNEL_SYSCALL },
	{0x4B4CCE80, &WrapI_V<sceSysreg_driver_4B4CCE80>,  "sceSysreg_driver_4B4CCE80",    'i', "",   HLE_KERNEL_SYSCALL },
	{0x41B0337B, &WrapI_I<sceSysreg_driver_41B0337B>,  "sceSysreg_driver_41B0337B",    'i', "i",  HLE_KERNEL_SYSCALL },
	{0xC36775AD, &WrapI_II<sceSysreg_driver_C36775AD>, "sceSysreg_driver_C36775AD",    'i', "ii", HLE_KERNEL_SYSCALL },
	{0x0143E8A8, &WrapI_V<sceSysregSemaTryLock>,       "sceSysregSemaTryLock660",      'i', "",   HLE_KERNEL_SYSCALL },
	{0xDB97C70E, &WrapI_V<sceSysregSemaUnlock>,        "sceSysregSemaUnlock660",       'i', "",   HLE_KERNEL_SYSCALL },
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
	// The list is in guest memory, so a loop in it shouldn't hang us.
	int count = 0;
	while (*link && Memory::IsValid4AlignedRange(*link, 0x40) && count++ < 0x10000) {
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
