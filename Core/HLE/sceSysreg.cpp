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
#include "Core/MIPS/MIPS.h"

static u32 sceSysregMeResetEnable371() { return 0; }
static u32 sceSysregMeBusClockEnable371() { return 0; }
static u32 sceSysregMeResetDisable371() { Core_EnableME(); return 0; }
static u32 sceSysregVmeResetEnable371() { return 0; }
static u32 sceSysregAvcResetEnable371() { return 0; }
static u32 sceSysregMeBusClockDisable371() { return 0; }

const HLEFunction sceSysreg_driver[] = {
	// FW 3.71+ NIDs:
	{0XA9997109, &WrapU_V<sceSysregMeResetEnable371>,      "sceSysregMeResetEnable371",      'x', "" },
	{0X3199CF1C, &WrapU_V<sceSysregMeBusClockEnable371>,   "sceSysregMeBusClockEnable371",   'x', "" },
	{0X76220E94, &WrapU_V<sceSysregMeResetDisable371>,     "sceSysregMeResetDisable371",     'x', "" },
	{0X17A22D51, &WrapU_V<sceSysregVmeResetEnable371>,     "sceSysregVmeResetEnable371",     'x', "" },
	{0XE5B3D348, &WrapU_V<sceSysregAvcResetEnable371>,     "sceSysregAvcResetEnable371",     'x', "" },
	{0X07881A0B, &WrapU_V<sceSysregMeBusClockDisable371>,  "sceSysregMeBusClockDisable371",  'x', "" },
	// Pre-3.71 NIDs (same functions, different NID hashes):
	{0XDE59DACB, &WrapU_V<sceSysregMeResetEnable371>,      "sceSysregMeResetEnable",         'x', "" },
	{0X2DB0EB28, &WrapU_V<sceSysregMeResetDisable371>,     "sceSysregMeResetDisable",        'x', "" },
	{0XD20581EA, &WrapU_V<sceSysregVmeResetEnable371>,     "sceSysregVmeResetEnable",        'x', "" },
	{0X9BB70D34, &WrapU_V<sceSysregAvcResetEnable371>,     "sceSysregAvcResetEnable",        'x', "" },
	{0X44F6CDA7, &WrapU_V<sceSysregMeBusClockEnable371>,   "sceSysregMeBusClockEnable",      'x', "" },
	{0X158AD4FC, &WrapU_V<sceSysregMeBusClockDisable371>,  "sceSysregMeBusClockDisable",     'x', "" },
};

void Register_sceSysreg_driver() {
	RegisterHLEModule("sceSysreg_driver", ARRAY_SIZE(sceSysreg_driver), sceSysreg_driver);
}
