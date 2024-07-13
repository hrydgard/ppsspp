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
#include "Core/HLE/sceImpose.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"

const int PSP_UMD_POPUP_DISABLE = 0;
const int PSP_UMD_POPUP_ENABLE = 1;

#define	PSP_IMPOSE_BATTICON_NONE    0x80000000
#define	PSP_IMPOSE_BATTICON_VISIBLE 0x00000000
#define	PSP_IMPOSE_BATTICON_BLINK   0x00000001

static u32 language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
static u32 buttonValue = PSP_SYSTEMPARAM_BUTTON_CIRCLE;
static u32 umdPopup = PSP_UMD_POPUP_DISABLE;
static u32 backlightOffTime;

void __ImposeInit()
{
	language = g_Config.GetPSPLanguage();
	if (PSP_CoreParameter().compat.flags().EnglishOrJapaneseOnly) {
		if (language != PSP_SYSTEMPARAM_LANGUAGE_ENGLISH && language != PSP_SYSTEMPARAM_LANGUAGE_JAPANESE) {
			language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		}
	}
	buttonValue = PSP_CoreParameter().compat.flags().ForceCircleButtonConfirm ? PSP_SYSTEMPARAM_BUTTON_CIRCLE : g_Config.iButtonPreference;
	umdPopup = PSP_UMD_POPUP_DISABLE;
	backlightOffTime = 0;
}

void __ImposeDoState(PointerWrap &p)
{
	auto s = p.Section("sceImpose", 1);
	if (!s)
		return;

	Do(p, language);
	Do(p, buttonValue);
	Do(p, umdPopup);
	Do(p, backlightOffTime);
}

static u32 sceImposeGetBatteryIconStatus(u32 chargingPtr, u32 iconStatusPtr)
{
	DEBUG_LOG(Log::sceUtility, "sceImposeGetBatteryIconStatus(%08x, %08x)", chargingPtr, iconStatusPtr);
	if (Memory::IsValidAddress(chargingPtr))
		Memory::Write_U32(PSP_IMPOSE_BATTICON_NONE, chargingPtr);
	if (Memory::IsValidAddress(iconStatusPtr))
		Memory::Write_U32(3, iconStatusPtr);
	return 0;
}

static u32 sceImposeSetLanguageMode(u32 languageVal, u32 buttonVal) {
	language = languageVal;
	buttonValue = buttonVal;
	if (language != g_Config.GetPSPLanguage()) {
		return hleLogWarning(Log::sceUtility, 0, "ignoring requested language");
	}
	return hleLogSuccessI(Log::sceUtility, 0);
}

static u32 sceImposeGetLanguageMode(u32 languagePtr, u32 btnPtr)
{
	DEBUG_LOG(Log::sceUtility, "sceImposeGetLanguageMode(%08x, %08x)", languagePtr, btnPtr);
	if (Memory::IsValidAddress(languagePtr))
		Memory::Write_U32(language, languagePtr);
	if (Memory::IsValidAddress(btnPtr))
		Memory::Write_U32(buttonValue, btnPtr);
	return 0;
}

static u32 sceImposeSetUMDPopup(int mode) {
	DEBUG_LOG(Log::sceUtility, "sceImposeSetUMDPopup(%i)", mode);
	umdPopup = mode;
	return 0;
}

static u32 sceImposeGetUMDPopup() {
	DEBUG_LOG(Log::sceUtility, "sceImposeGetUMDPopup()");
	return umdPopup;
}

static u32 sceImposeSetBacklightOffTime(int time) {
	DEBUG_LOG(Log::sceUtility, "sceImposeSetBacklightOffTime(%i)", time);
	backlightOffTime = time;
	return 0;
}

static u32 sceImposeGetBacklightOffTime() {
	DEBUG_LOG(Log::sceUtility, "sceImposeGetBacklightOffTime()");
	return backlightOffTime;
}

//OSD stuff? home button?
const HLEFunction sceImpose[] =
{
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
	{0X9BA61B49, nullptr,                                  "sceImpose_9BA61B49",            '?', ""  },
	{0XFF1A2F07, nullptr,                                  "sceImpose_FF1A2F07",            '?', ""  },
};

void Register_sceImpose()
{
	RegisterModule("sceImpose", ARRAY_SIZE(sceImpose), sceImpose);
}
