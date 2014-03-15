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
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/sceUtility.h"

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
	language = g_Config.iLanguage;
	buttonValue = g_Config.iButtonPreference;
	umdPopup = PSP_UMD_POPUP_DISABLE;
	backlightOffTime = 0;
}

void __ImposeDoState(PointerWrap &p)
{
	auto s = p.Section("sceImpose", 1);
	if (!s)
		return;

	p.Do(language);
	p.Do(buttonValue);
	p.Do(umdPopup);
	p.Do(backlightOffTime);
}

u32 sceImposeGetBatteryIconStatus(u32 chargingPtr, u32 iconStatusPtr)
{
	DEBUG_LOG(SCEUTILITY, "sceImposeGetBatteryIconStatus(%08x, %08x)", chargingPtr, iconStatusPtr);
	if (Memory::IsValidAddress(chargingPtr))
		Memory::Write_U32(PSP_IMPOSE_BATTICON_NONE, chargingPtr);
	if (Memory::IsValidAddress(iconStatusPtr))
		Memory::Write_U32(3, iconStatusPtr);
	return 0;
}

u32 sceImposeSetLanguageMode(u32 languageVal, u32 buttonVal)
{
	DEBUG_LOG(SCEUTILITY, "sceImposeSetLanguageMode(%08x, %08x)", languageVal, buttonVal);
	language = languageVal;
	buttonValue = buttonVal;
	return 0;
}

u32 sceImposeGetLanguageMode(u32 languagePtr, u32 btnPtr)
{
	DEBUG_LOG(SCEUTILITY, "sceImposeGetLanguageMode(%08x, %08x)", languagePtr, btnPtr);
	if (Memory::IsValidAddress(languagePtr))
		Memory::Write_U32(language, languagePtr);
	if (Memory::IsValidAddress(btnPtr))
		Memory::Write_U32(buttonValue, btnPtr);
	return 0;
}

u32 sceImposeSetUMDPopup(int mode) {
	DEBUG_LOG(SCEUTILITY, "sceImposeSetUMDPopup(%i)", mode);
	umdPopup = mode;
	return 0;
}

u32 sceImposeGetUMDPopup() {
	DEBUG_LOG(SCEUTILITY, "sceImposeGetUMDPopup()");
	return umdPopup;
}

u32 sceImposeSetBacklightOffTime(int time) {
	DEBUG_LOG(SCEUTILITY, "sceImposeSetBacklightOffTime(%i)", time);
	backlightOffTime = time;
	return 0;
}

u32 sceImposeGetBacklightOffTime() {
	DEBUG_LOG(SCEUTILITY, "sceImposeGetBacklightOffTime()");
	return backlightOffTime;
}

//OSD stuff? home button?
const HLEFunction sceImpose[] =
{
	{0x36aa6e91, WrapU_UU<sceImposeSetLanguageMode>, "sceImposeSetLanguageMode"},  // Seen
	{0x381bd9e7, 0, "sceImposeHomeButton"},
	{0x0F341BE4, 0, "sceImposeGetHomePopup"},
	{0x5595A71A, 0, "sceImposeSetHomePopup"},
	{0x24fd7bcf, WrapU_UU<sceImposeGetLanguageMode>, "sceImposeGetLanguageMode"},
	{0x8c943191, WrapU_UU<sceImposeGetBatteryIconStatus>, "sceImposeGetBatteryIconStatus"},
	{0x72189C48, WrapU_I<sceImposeSetUMDPopup>, "sceImposeSetUMDPopup"},
	{0xE0887BC8, WrapU_V<sceImposeGetUMDPopup>, "sceImposeGetUMDPopup"},
	{0x8F6E3518, WrapU_V<sceImposeGetBacklightOffTime>, "sceImposeGetBacklightOffTime"},
	{0x967F6D4A, WrapU_I<sceImposeSetBacklightOffTime>, "sceImposeSetBacklightOffTime"},
	{0xfcd44963, 0, "sceImpose_FCD44963"},
	{0xa9884b00, 0, "sceImpose_A9884B00"},
	{0xbb3f5dec, 0, "sceImpose_BB3F5DEC"},
	{0x9ba61b49, 0, "sceImpose_9BA61B49"},
	{0xff1a2f07, 0, "sceImpose_FF1A2F07"},
};

void Register_sceImpose()
{
	RegisterModule("sceImpose", ARRAY_SIZE(sceImpose), sceImpose);
}
