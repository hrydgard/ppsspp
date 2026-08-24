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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceImpose.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"

// sceImpose handles overlays like low-battery icons, and some other popup/UI related things.

const int PSP_UMD_POPUP_DISABLE = 0;
const int PSP_UMD_POPUP_ENABLE = 1;

#define	PSP_IMPOSE_BATTICON_NONE    0x80000000
#define	PSP_IMPOSE_BATTICON_VISIBLE 0x00000000
#define	PSP_IMPOSE_BATTICON_BLINK   0x00000001

static u32 language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
static u32 buttonValue = PSP_SYSTEMPARAM_BUTTON_CIRCLE;
static u32 umdPopup = PSP_UMD_POPUP_DISABLE;
static u32 backlightOffTime;
// See sceImposeChanges, further down.
static u32 imposeChanges;
static u32 imposeAvls;

void __ImposeInit() {
	language = GetPSPLanguage();
	if (PSP_CoreParameter().compat.flags().EnglishOrJapaneseOnly) {
		if (language != PSP_SYSTEMPARAM_LANGUAGE_ENGLISH && language != PSP_SYSTEMPARAM_LANGUAGE_JAPANESE) {
			language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		}
	}
	buttonValue = PSP_CoreParameter().compat.flags().ForceCircleButtonConfirm ? PSP_SYSTEMPARAM_BUTTON_CIRCLE : g_Config.iButtonPreference;
	umdPopup = PSP_UMD_POPUP_DISABLE;
	backlightOffTime = 0;
	imposeChanges = 0;
	imposeAvls = 0;
}

void __ImposeDoState(PointerWrap &p) {
	auto s = p.Section("sceImpose", 1, 2);
	if (!s)
		return;

	Do(p, language);
	Do(p, buttonValue);
	Do(p, umdPopup);
	Do(p, backlightOffTime);
	if (s >= 2) {
		Do(p, imposeChanges);
		Do(p, imposeAvls);
	}
}

static u32 sceImposeGetBatteryIconStatus(u32 chargingPtr, u32 iconStatusPtr)
{
	// The first output is a plain "is it charging" boolean, not a BATTICON_ value - we used to
	// write PSP_IMPOSE_BATTICON_NONE (0x80000000) here, which games ignore but which the VSH
	// reads as "no battery" and draws the empty-battery indicator for. Matches JPCSP now:
	// charging is 0/1, and the icon status is the charge level in four steps.
	if (Memory::IsValidAddress(chargingPtr))
		Memory::WriteUnchecked_U32(0, chargingPtr);
	if (Memory::IsValidAddress(iconStatusPtr))
		Memory::WriteUnchecked_U32(3, iconStatusPtr);
	return hleLogDebug(Log::sceUtility, 0);
}

static u32 sceImposeSetLanguageMode(u32 languageVal, u32 buttonVal) {
	language = languageVal;
	buttonValue = buttonVal;
	if (language != GetPSPLanguage()) {
		return hleLogWarning(Log::sceUtility, 0, "ignoring requested language");
	}
	return hleLogDebug(Log::sceUtility, 0);
}

static u32 sceImposeGetLanguageMode(u32 languagePtr, u32 btnPtr) {
	if (Memory::IsValidAddress(languagePtr))
		Memory::WriteUnchecked_U32(language, languagePtr);
	if (Memory::IsValidAddress(btnPtr))
		Memory::WriteUnchecked_U32(buttonValue, btnPtr);
	return hleLogDebug(Log::sceUtility, 0);
}

static u32 sceImposeSetUMDPopup(int mode) {
	umdPopup = mode;
	return hleLogDebug(Log::sceUtility, 0);
}

static u32 sceImposeGetUMDPopup() {
	return hleLogDebug(Log::sceUtility, umdPopup);
}

static u32 sceImposeSetBacklightOffTime(int time) {
	backlightOffTime = time;
	return hleLogDebug(Log::sceUtility, 0);
}

static u32 sceImposeGetBacklightOffTime() {
	return hleLogDebug(Log::sceUtility, backlightOffTime);
}

// OSD stuff? home button?
const HLEFunction sceImpose[] = {
	{0X36AA6E91, &WrapU_UU<sceImposeSetLanguageMode>,      "sceImposeSetLanguageMode",      'i', "ii"},
	{0X381BD9E7, nullptr,                                  "sceImposeHomeButton",           '?', ""  },
	{0X0F341BE4, nullptr,                                  "sceImposeGetHomePopup",         '?', ""  },
	{0X5595A71A, nullptr,                                  "sceImposeSetHomePopup",         '?', ""  },
	{0X24FD7BCF, &WrapU_UU<sceImposeGetLanguageMode>,      "sceImposeGetLanguageMode",      'x', "xx"},
	{0X8C943191, &WrapU_UU<sceImposeGetBatteryIconStatus>, "sceImposeGetBatteryIconStatus", 'x', "xx"},
	{0X72189C48, &WrapU_I<sceImposeSetUMDPopup>,           "sceImposeSetUMDPopup",          'x', "i" },
	{0XE0887BC8, &WrapU_V<sceImposeGetUMDPopup>,           "sceImposeGetUMDPopup",          'x', ""  },
	{0X8F6E3518, &WrapU_V<sceImposeGetBacklightOffTime>,   "sceImposeGetBacklightOffTime",  'x', ""  },
	{0X967F6D4A, &WrapU_I<sceImposeSetBacklightOffTime>,   "sceImposeSetBacklightOffTime",  'x', "i" },
	{0XFCD44963, nullptr,                                  "sceImpose_FCD44963",            '?', ""  },
	{0XA9884B00, nullptr,                                  "sceImpose_A9884B00",            '?', ""  },
	{0XBB3F5DEC, nullptr,                                  "sceImpose_BB3F5DEC",            '?', ""  },
	{0X9BA61B49, nullptr,                                  "sceImpose_9BA61B49",            '?', ""  }, // jpcsp: isHomeButtonPressed() -> bool
	{0XFF1A2F07, nullptr,                                  "sceImpose_FF1A2F07",            '?', ""  },
};

void Register_sceImpose() {
	RegisterHLEModule("sceImpose", ARRAY_SIZE(sceImpose), sceImpose);
}

static int sceImpose_driver_B497314D(int param, u32 resultAddr) {
	auto result = PSPPointer<u64_le>::Create(resultAddr);
	if (result.IsValid())
		*result = 0;
	return hleLogDebug(Log::sceUtility, 0, "UNTESTED");
}

// The settings sceImposeGetParam/SetParam address, as a bitfield - sceImposeChanges reports which
// of them have been changed since it was last asked, so the VSH can refresh only what moved.
enum : u32 {
	PSP_IMPOSE_MAIN_VOLUME            = 0x1,
	PSP_IMPOSE_BACKLIGHT_BRIGHTNESS   = 0x2,
	PSP_IMPOSE_EQUALIZER_MODE         = 0x4,
	PSP_IMPOSE_MUTE                   = 0x8,
	PSP_IMPOSE_AVLS                   = 0x10,
	PSP_IMPOSE_TIME_FORMAT            = 0x20,
	PSP_IMPOSE_DATE_FORMAT            = 0x40,
	PSP_IMPOSE_LANGUAGE               = 0x80,
	PSP_IMPOSE_00000100               = 0x100,
	PSP_IMPOSE_BACKLIGHT_OFF_INTERVAL = 0x200,
	PSP_IMPOSE_SOUND_REDUCTION        = 0x400,
	// Named after their own values, as in JPCSP - real meanings unknown.
	PSP_IMPOSE_20000000               = 0x20000000,
	PSP_IMPOSE_80000001               = 0x80000001,
	PSP_IMPOSE_80000002               = 0x80000002,
	PSP_IMPOSE_80000003               = 0x80000003,
	PSP_IMPOSE_80000004               = 0x80000004,
	PSP_IMPOSE_80000005               = 0x80000005,
	PSP_IMPOSE_80000006               = 0x80000006,
	PSP_IMPOSE_80000007               = 0x80000007,
	PSP_IMPOSE_80000008               = 0x80000008,
	PSP_IMPOSE_80000009               = 0x80000009,
	PSP_IMPOSE_8000000A               = 0x8000000A,
	PSP_IMPOSE_8000000B               = 0x8000000B,
};

// Returns the current value of one setting. Values follow JPCSP's, which are the plausible
// defaults for a console with nothing unusual configured.
//
// The split between the last two cases matters and matches JPCSP: a parameter that exists but that
// we don't model reads as 0, while one that isn't a parameter at all is rejected. Answering 0 to
// everything tells a caller probing for a feature that the feature is there and switched off,
// which is a different thing from "no such setting".
static int sceImposeGetParam(int param) {
	switch ((u32)param) {
	case PSP_IMPOSE_MAIN_VOLUME:
		return hleLogDebug(Log::sceUtility, 30);  // Full, on a 0-30 scale.
	case PSP_IMPOSE_BACKLIGHT_BRIGHTNESS:
		return hleLogDebug(Log::sceUtility, 3);
	case PSP_IMPOSE_AVLS:
		return hleLogDebug(Log::sceUtility, imposeAvls);
	case PSP_IMPOSE_MUTE:
	case PSP_IMPOSE_SOUND_REDUCTION:
	case PSP_IMPOSE_BACKLIGHT_OFF_INTERVAL:
		return hleLogDebug(Log::sceUtility, 0);
	case PSP_IMPOSE_EQUALIZER_MODE:
	case PSP_IMPOSE_TIME_FORMAT:
	case PSP_IMPOSE_DATE_FORMAT:
	case PSP_IMPOSE_LANGUAGE:
	case PSP_IMPOSE_00000100:
	case PSP_IMPOSE_20000000:
	case PSP_IMPOSE_80000001:
	case PSP_IMPOSE_80000002:
	case PSP_IMPOSE_80000003:
	case PSP_IMPOSE_80000004:
	case PSP_IMPOSE_80000005:
	case PSP_IMPOSE_80000006:
	case PSP_IMPOSE_80000007:
	case PSP_IMPOSE_80000008:
	case PSP_IMPOSE_80000009:
	case PSP_IMPOSE_8000000A:
	case PSP_IMPOSE_8000000B:
		return hleLogDebug(Log::sceUtility, 0, "param %08x not modelled", param);
	default:
		return hleLogWarning(Log::sceUtility, SCE_KERNEL_ERROR_INVALID_MODE, "invalid param %08x", param);
	}
}

static int sceImposeSetParam(int param, int value) {
	switch (param) {
	case PSP_IMPOSE_AVLS:
		if (value < 0 || value > 1)
			return hleLogWarning(Log::sceUtility, SCE_KERNEL_ERROR_INVALID_VALUE);
		imposeAvls = value;
		imposeChanges |= PSP_IMPOSE_AVLS | PSP_IMPOSE_MAIN_VOLUME;
		break;
	case PSP_IMPOSE_MAIN_VOLUME:
		if (value < 0 || value >= 31)
			return hleLogWarning(Log::sceUtility, SCE_KERNEL_ERROR_INVALID_VALUE);
		imposeChanges |= PSP_IMPOSE_MAIN_VOLUME;
		break;
	default:
		// Recording the change is the part callers actually observe, through sceImposeChanges.
		imposeChanges |= param;
		break;
	}
	return hleLogDebug(Log::sceUtility, 0);
}

// Which settings changed since the last call - reading clears them.
static int sceImposeChanges() {
	const u32 changes = imposeChanges;
	imposeChanges = 0;
	return hleLogDebug(Log::sceUtility, changes);
}

static int sceImposeSetStatus(int status) {
	return hleLogDebug(Log::sceUtility, 0, "UNIMPL");
}

const HLEFunction sceImpose_driver[] = {
	{0XB497314D, &WrapI_IU<sceImpose_driver_B497314D>,     "sceImpose_driver_B497314D",     'i', "ix"},
	{0XDC3BECFF, &WrapI_I<sceImposeGetParam>,              "sceImposeGetParam",             'i', "i" },
	{0X3C318569, &WrapI_II<sceImposeSetParam>,             "sceImposeSetParam",             'i', "ii"},
	{0X0F067E16, &WrapI_V<sceImposeChanges>,               "sceImposeChanges",              'i', ""  },
	{0XBB12F974, &WrapI_I<sceImposeSetStatus>,             "sceImposeSetStatus",            'i', "i" },
	// The 6.60 name for the same call the user-mode module exports as 0x8C943191.
	{0X5557F4E2, &WrapU_UU<sceImposeGetBatteryIconStatus>, "sceImposeGetBatteryIconStatus", 'x', "xx"},
};

void Register_sceImpose_driver() {
	RegisterHLEModule("sceImpose_driver", ARRAY_SIZE(sceImpose_driver), sceImpose_driver);
}
