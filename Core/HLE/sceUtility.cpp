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

#include <set>

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "Core/Reporting.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"

#include "sceCtrl.h"
#include "../Util/PPGeDraw.h"
#include "../Dialog/PSPSaveDialog.h"
#include "../Dialog/PSPMsgDialog.h"
#include "../Dialog/PSPPlaceholderDialog.h"
#include "../Dialog/PSPOskDialog.h"
#include "../Dialog/PSPGamedataInstallDialog.h"
#include "../Dialog/PSPNetconfDialog.h"

#include "../native/file/ini_file.h"

const int SCE_ERROR_MODULE_BAD_ID = 0x80111101;
const int SCE_ERROR_MODULE_ALREADY_LOADED = 0x80111102;
const int SCE_ERROR_MODULE_NOT_LOADED = 0x80111103;
const int SCE_ERROR_AV_MODULE_BAD_ID = 0x80110F01;
const u32 PSP_MODULE_NET_HTTP = 261;
const u32 PSP_MODULE_NET_HTTPSTORAGE = 264;

enum UtilityDialogType {
	UTILITY_DIALOG_NONE,
	UTILITY_DIALOG_SAVEDATA,
	UTILITY_DIALOG_MSG,
	UTILITY_DIALOG_OSK,
	UTILITY_DIALOG_NET,
	UTILITY_DIALOG_SCREENSHOT,
	UTILITY_DIALOG_GAMESHARING,
	UTILITY_DIALOG_GAMEDATAINSTALL,
};

// Only a single dialog is allowed at a time.
static UtilityDialogType currentDialogType;
static bool currentDialogActive;
static PSPSaveDialog saveDialog;
static PSPMsgDialog msgDialog;
static PSPOskDialog oskDialog;
static PSPNetconfDialog netDialog;
static PSPPlaceholderDialog screenshotDialog;
static PSPGamedataInstallDialog gamedataInstallDialog;

static std::set<int> currentlyLoadedModules;

void __UtilityInit()
{
	currentDialogType = UTILITY_DIALOG_NONE;
	currentDialogActive = false;
	SavedataParam::Init();
	currentlyLoadedModules.clear();
}

void __UtilityDoState(PointerWrap &p)
{
	auto s = p.Section("sceUtility", 1);
	if (!s)
		return;

	p.Do(currentDialogType);
	p.Do(currentDialogActive);
	saveDialog.DoState(p);
	msgDialog.DoState(p);
	oskDialog.DoState(p);
	netDialog.DoState(p);
	screenshotDialog.DoState(p);
	gamedataInstallDialog.DoState(p);
	p.Do(currentlyLoadedModules);
}

void __UtilityShutdown()
{
	saveDialog.Shutdown(true);
	msgDialog.Shutdown(true);
	oskDialog.Shutdown(true);
	netDialog.Shutdown(true);
	screenshotDialog.Shutdown(true);
	gamedataInstallDialog.Shutdown(true);
}

int __UtilityGetStatus()
{
	if (currentDialogType == UTILITY_DIALOG_NONE) {
		return 0;
	} else {
		WARN_LOG(SCEUTILITY, "__UtilityGetStatus() Faked dialog : wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
}

int sceUtilitySavedataInitStart(u32 paramAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataInitStart(%08x): wrong dialog type", paramAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_SAVEDATA;
	currentDialogActive = true;

	DEBUG_LOG(SCEUTILITY,"sceUtilitySavedataInitStart(%08x)", paramAddr);
	return saveDialog.Init(paramAddr);
}

int sceUtilitySavedataShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY,"sceUtilitySavedataShutdownStart()");
	return saveDialog.Shutdown();
}

int sceUtilitySavedataGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilitySavedataGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = saveDialog.GetStatus();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilitySavedataGetStatus()", status);
	return status;
}

int sceUtilitySavedataUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(SCEUTILITY,"sceUtilitySavedataUpdate(%d)", animSpeed);
	int result = saveDialog.Update(animSpeed);
	if (result >= 0)
		return hleDelayResult(result, "savedata update", 300);
	return result;
}

#define PSP_AV_MODULE_AVCODEC		0
#define PSP_AV_MODULE_SASCORE		1
#define PSP_AV_MODULE_ATRAC3PLUS	2 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MPEGBASE		3 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MP3		4
#define PSP_AV_MODULE_VAUDIO		5
#define PSP_AV_MODULE_AAC		6
#define PSP_AV_MODULE_G729		7

u32 sceUtilityLoadAvModule(u32 module)
{
	if (module > 7)
	{
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilityLoadAvModule(%i): invalid module id", module);
		return SCE_ERROR_AV_MODULE_BAD_ID;
	}

	DEBUG_LOG(SCEUTILITY,"sceUtilityLoadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module loaded", 25000);
}

u32 sceUtilityUnloadAvModule(u32 module)
{
	DEBUG_LOG(SCEUTILITY,"sceUtilityUnloadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module unloaded", 800);
}

u32 sceUtilityLoadModule(u32 module)
{
	// TODO: Not all modules between 0x100 and 0x601 are valid.
	if (module < 0x100 || module > 0x601)
	{
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilityLoadModule(%i): invalid module id", module);
		return SCE_ERROR_MODULE_BAD_ID;
	}

	if (currentlyLoadedModules.find(module) != currentlyLoadedModules.end())
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityLoadModule(%i): already loaded", module);
		return SCE_ERROR_MODULE_ALREADY_LOADED;
	}
	INFO_LOG(SCEUTILITY, "sceUtilityLoadModule(%i)", module);
	//Fix Kamen Rider Climax Heroes OOO - ULJS00331 loading
	//Fix Naruto Shippuden Kizuna Drive (error module load failed)
	if (module == PSP_MODULE_NET_HTTPSTORAGE && !(currentlyLoadedModules.find(PSP_MODULE_NET_HTTP) != currentlyLoadedModules.end()))
	{
		ERROR_LOG(SCEUTILITY, "sceUtilityLoadModule: Library not found");
		return SCE_KERNEL_ERROR_LIBRARY_NOTFOUND;
	}
	// TODO: Each module has its own timing, technically, but this is a low-end.
	// Note: Some modules have dependencies, but they still resched.

	currentlyLoadedModules.insert(module);

	if (module == 0x3FF)
		return hleDelayResult(0, "utility module loaded", 130);
	else
		return hleDelayResult(0, "utility module loaded", 25000);
}

u32 sceUtilityUnloadModule(u32 module)
{
	// TODO: Not all modules between 0x100 and 0x601 are valid.
	if (module < 0x100 || module > 0x601)
	{
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilityUnloadModule(%i): invalid module id", module);
		return SCE_ERROR_MODULE_BAD_ID;
	}

	if (currentlyLoadedModules.find(module) == currentlyLoadedModules.end())
	{
		WARN_LOG(SCEUTILITY, "sceUtilityUnloadModule(%i): not yet loaded", module);
		return SCE_ERROR_MODULE_NOT_LOADED;
	}
	currentlyLoadedModules.erase(module);

	INFO_LOG(SCEUTILITY, "sceUtilityUnloadModule(%i)", module);
	// TODO: Each module has its own timing, technically, but this is a low-end.
	// Note: If not loaded, it should not reschedule actually...
	if (module == 0x3FF)
		return hleDelayResult(0, "utility module unloaded", 110);
	else
		return hleDelayResult(0, "utility module unloaded", 400);
}

int sceUtilityMsgDialogInitStart(u32 structAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogInitStart(%08x): wrong dialog type", structAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_MSG;
	currentDialogActive = true;

	DEBUG_LOG(SCEUTILITY, "sceUtilityMsgDialogInitStart(%08x)", structAddr);
	return msgDialog.Init(structAddr);
}

int sceUtilityMsgDialogShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityMsgDialogShutdownStart()");
	return msgDialog.Shutdown();
}

int sceUtilityMsgDialogUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(SCEUTILITY,"sceUtilityMsgDialogUpdate(%i)", animSpeed);
	return msgDialog.Update(animSpeed);
}

int sceUtilityMsgDialogGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityMsgDialogGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = msgDialog.GetStatus();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityMsgDialogGetStatus()", status);
	return status;
}

int sceUtilityMsgDialogAbort()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityMsgDialogAbort()");
	return msgDialog.Abort();
}


// On screen keyboard

int sceUtilityOskInitStart(u32 oskPtr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityOskInitStart(%08x): wrong dialog type", oskPtr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_OSK;
	currentDialogActive = true;

	DEBUG_LOG(SCEUTILITY, "sceUtilityOskInitStart(%08x)", oskPtr);
	return oskDialog.Init(oskPtr);
}

int sceUtilityOskShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityOskShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityOskShutdownStart()");
	return oskDialog.Shutdown();
}

int sceUtilityOskUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(SCEUTILITY, "sceUtilityOskUpdate(%i)", animSpeed);
	return oskDialog.Update(animSpeed);
}

int sceUtilityOskGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityOskGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = oskDialog.GetStatus();

	// Seems that 4 is the cancelled status for OSK?
	if (status == 4)
	{
		status = 5;
	}

	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityOskGetStatus()", status);
	return status;
}


int sceUtilityNetconfInitStart(u32 paramsAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_NET) {
		WARN_LOG(SCEUTILITY, "sceUtilityNetconfInitStart(%08x): wrong dialog type", paramsAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_NET;
	currentDialogActive = true;	

	DEBUG_LOG(SCEUTILITY, "sceUtilityNetconfInitStart(%08x)", paramsAddr);
	return netDialog.Init(paramsAddr);
}

int sceUtilityNetconfShutdownStart(unsigned int unknown)
{
	if (currentDialogType != UTILITY_DIALOG_NET) {
		WARN_LOG(SCEUTILITY, "sceUtilityNetconfShutdownStartt(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityNetconfShutdownStart()");
	return netDialog.Shutdown();
}

int sceUtilityNetconfUpdate(int animSpeed)
{
	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityNetconfUpdate(%i)", animSpeed);
	return netDialog.Update(animSpeed);
}

int sceUtilityNetconfGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_NET) {
		WARN_LOG(SCEUTILITY, "sceUtilityNetconfGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = netDialog.GetStatus();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityNetconfGetStatus()", status);
	return status;
}

//TODO: Implement all sceUtilityScreenshot* for real, it doesn't seem to be complex
//but it requires more investigation
u32 sceUtilityScreenshotInitStart(u32 unknown1, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5, u32 unknown6)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotInitStart(%x, %x, %x, %x, %x, %x): wrong dialog type", unknown1, unknown2, unknown3, unknown4, unknown5, unknown6);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_SCREENSHOT;
	currentDialogActive = true;

	u32 retval = screenshotDialog.Init();
	WARN_LOG_REPORT(SCEUTILITY, "UNIMPL %i=sceUtilityScreenshotInitStart(%x, %x, %x, %x, %x, %x)", retval, unknown1, unknown2, unknown3, unknown4, unknown5, unknown6);
	return retval;
}

u32 sceUtilityScreenshotShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	WARN_LOG(SCEUTILITY, "UNTESTED sceUtilityScreenshotShutdownStart()");
	return screenshotDialog.Shutdown();
}

u32 sceUtilityScreenshotUpdate(u32 animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityScreenshotUpdate(%d)", animSpeed);
	return screenshotDialog.Update(animSpeed);
}

int sceUtilityScreenshotGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityScreenshotGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	u32 retval = screenshotDialog.GetStatus(); 
	WARN_LOG(SCEUTILITY, "UNIMPL %i=sceUtilityScreenshotGetStatus()", retval);
	return retval;
}

int sceUtilityGamedataInstallInitStart(u32 paramsAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallInitStart(%08x): wrong dialog type", paramsAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_GAMEDATAINSTALL;
	currentDialogActive = true;	

	DEBUG_LOG(SCEUTILITY, "sceUtilityGamedataInstallInitStart(%08x)", paramsAddr);
	return gamedataInstallDialog.Init(paramsAddr);
}

int sceUtilityGamedataInstallShutdownStart() {
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityGamedataInstallShutdownStart()");
	return gamedataInstallDialog.Shutdown();
}

int sceUtilityGamedataInstallUpdate(int animSpeed) {
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(SCEUTILITY, "sceUtilityGamedataInstallUpdate(%i)", animSpeed);
	return gamedataInstallDialog.Update(animSpeed);
}

int sceUtilityGamedataInstallGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		// This is called incorrectly all the time by some games. So let's not bother warning.
		// WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = gamedataInstallDialog.GetStatus();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityGamedataInstallGetStatus()", status);
	return status;
}

int sceUtilityGamedataInstallAbort()
{
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(SCEUTILITY, "sceUtilityGamedataInstallDialogAbort");
	return gamedataInstallDialog.Abort();
}

//TODO: should save to config file
u32 sceUtilitySetSystemParamString(u32 id, u32 strPtr)
{
	WARN_LOG_REPORT(SCEUTILITY, "sceUtilitySetSystemParamString(%i, %08x)", id, strPtr);
	return 0;
}

u32 sceUtilityGetSystemParamString(u32 id, u32 destaddr, int destSize)
{
	DEBUG_LOG(SCEUTILITY, "sceUtilityGetSystemParamString(%i, %08x, %i)", id, destaddr, destSize);
	char *buf = (char *)Memory::GetPointer(destaddr);
	switch (id) {
	case PSP_SYSTEMPARAM_ID_STRING_NICKNAME:
		// If there's not enough space for the string and null terminator, fail.
		if (destSize <= (int)g_Config.sNickName.length())
			return PSP_SYSTEMPARAM_RETVAL_STRING_TOO_LONG;
		strncpy(buf, g_Config.sNickName.c_str(), destSize);
		break;

	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	return 0;
}

u32 sceUtilityGetSystemParamInt(u32 id, u32 destaddr)
{
	DEBUG_LOG(SCEUTILITY,"sceUtilityGetSystemParamInt(%i, %08x)", id,destaddr);
	u32 param = 0;
	switch (id) {
	case PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL:
		param = g_Config.iWlanAdhocChannel;
		break;
	case PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE:
		param = g_Config.bWlanPowerSave?PSP_SYSTEMPARAM_WLAN_POWERSAVE_ON:PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT:
		param = g_Config.iDateFormat;
		INFO_LOG_REPORT(SCEUTILITY, "sceUtilitySystemGetSystemParamInt: %s", "Date Format")
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT:
		param = g_Config.iTimeFormat?PSP_SYSTEMPARAM_TIME_FORMAT_12HR:PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
		INFO_LOG_REPORT(SCEUTILITY, "sceUtilitySystemGetSystemParamInt: %s", "Time Format")
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIMEZONE:
		param = g_Config.iTimeZone;
		INFO_LOG_REPORT(SCEUTILITY, "sceUtilitySystemGetSystemParamInt: %s", "Timezone")
		break;
	case PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS:
		param = g_Config.bDayLightSavings?PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING:PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD;
		INFO_LOG_REPORT(SCEUTILITY, "sceUtilitySystemGetSystemParamInt: %s", "Daylight Savings");
		break;
	case PSP_SYSTEMPARAM_ID_INT_LANGUAGE:
		param = g_Config.iLanguage;
		break;
	case PSP_SYSTEMPARAM_ID_INT_BUTTON_PREFERENCE:
		param = g_Config.iButtonPreference;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LOCK_PARENTAL_LEVEL:
		param = g_Config.iLockParentalLevel;
		INFO_LOG_REPORT(SCEUTILITY, "sceUtilitySystemGetSystemParamInt: %s", "Lock Parental Level");
		break;
	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	Memory::Write_U32(param, destaddr);

	return 0;
}

u32 sceUtilityLoadNetModule(u32 module)
{
	DEBUG_LOG(SCEUTILITY,"FAKE: sceUtilityLoadNetModule(%i)", module);
	return 0;
}

u32 sceUtilityUnloadNetModule(u32 module)
{
	DEBUG_LOG(SCEUTILITY,"FAKE: sceUtilityUnloadNetModule(%i)", module);
	return 0;
}

void sceUtilityInstallInitStart(u32 unknown)
{
	WARN_LOG_REPORT(SCEUTILITY, "UNIMPL sceUtilityInstallInitStart()");
}

int sceUtilityStoreCheckoutShutdownStart()
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutShutdownStart()");
	return 0;
}

int sceUtilityStoreCheckoutInitStart(u32 paramsPtr)
{
	ERROR_LOG_REPORT(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutInitStart(%d)", paramsPtr);
	return 0;
}

int sceUtilityStoreCheckoutUpdate(int drawSpeed)
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutUpdate(%d)", drawSpeed);
	return 0;
}

int sceUtilityStoreCheckoutGetStatus()
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutGetStatus()");
	return 0;
}

int sceUtilityGameSharingShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGameSharingShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityGameSharingShutdownStart()");
	return 0;
}

int sceUtilityGameSharingInitStart(u32 paramsPtr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGameSharingInitStart(%08x)", paramsPtr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_GAMESHARING;
	currentDialogActive = true;

	ERROR_LOG_REPORT(SCEUTILITY, "UNIMPL sceUtilityGameSharingInitStart(%08x)", paramsPtr);
	return 0;
}

int sceUtilityGameSharingUpdate(int drawSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityGameSharingUpdate(%d)", drawSpeed);
	return 0;
}

int sceUtilityGameSharingGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityGameSharingGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityGameSharingGetStatus()");
	return 0;
}

const HLEFunction sceUtility[] = 
{
	{0x1579a159, &WrapU_U<sceUtilityLoadNetModule>, "sceUtilityLoadNetModule"},
	{0x64d50c56, &WrapU_U<sceUtilityUnloadNetModule>, "sceUtilityUnloadNetModule"},


	{0xf88155f6, &WrapI_U<sceUtilityNetconfShutdownStart>, "sceUtilityNetconfShutdownStart"},
	{0x4db1e739, &WrapI_U<sceUtilityNetconfInitStart>, "sceUtilityNetconfInitStart"},
	{0x91e70e35, &WrapI_I<sceUtilityNetconfUpdate>, "sceUtilityNetconfUpdate"},
	{0x6332aa39, &WrapI_V<sceUtilityNetconfGetStatus>, "sceUtilityNetconfGetStatus"},
	{0x5eee6548, 0, "sceUtilityCheckNetParam"},
	{0x434d4b3a, 0, "sceUtilityGetNetParam"},
	{0x4FED24D8, 0, "sceUtilityGetNetParamLatestID"},

	{0x67af3428, &WrapI_V<sceUtilityMsgDialogShutdownStart>, "sceUtilityMsgDialogShutdownStart"},
	{0x2ad8e239, &WrapI_U<sceUtilityMsgDialogInitStart>, "sceUtilityMsgDialogInitStart"},
	{0x95fc253b, &WrapI_I<sceUtilityMsgDialogUpdate>, "sceUtilityMsgDialogUpdate"},
	{0x9a1c91d7, &WrapI_V<sceUtilityMsgDialogGetStatus>, "sceUtilityMsgDialogGetStatus"},
	{0x4928bd96, &WrapI_V<sceUtilityMsgDialogAbort>, "sceUtilityMsgDialogAbort"},

	{0x9790b33c, &WrapI_V<sceUtilitySavedataShutdownStart>, "sceUtilitySavedataShutdownStart"},
	{0x50c4cd57, &WrapI_U<sceUtilitySavedataInitStart>, "sceUtilitySavedataInitStart"},
	{0xd4b95ffb, &WrapI_I<sceUtilitySavedataUpdate>, "sceUtilitySavedataUpdate"},
	{0x8874dbe0, &WrapI_V<sceUtilitySavedataGetStatus>, "sceUtilitySavedataGetStatus"},

	{0x3dfaeba9, &WrapI_V<sceUtilityOskShutdownStart>, "sceUtilityOskShutdownStart"},
	{0xf6269b82, &WrapI_U<sceUtilityOskInitStart>, "sceUtilityOskInitStart"},
	{0x4b85c861, &WrapI_I<sceUtilityOskUpdate>, "sceUtilityOskUpdate"},
	{0xf3f76017, &WrapI_V<sceUtilityOskGetStatus>, "sceUtilityOskGetStatus"},

	{0x41e30674, &WrapU_UU<sceUtilitySetSystemParamString>, "sceUtilitySetSystemParamString"},
	{0x45c18506, 0, "sceUtilitySetSystemParamInt"}, 
	{0x34b78343, &WrapU_UUI<sceUtilityGetSystemParamString>, "sceUtilityGetSystemParamString"},
	{0xA5DA2406, &WrapU_UU<sceUtilityGetSystemParamInt>, "sceUtilityGetSystemParamInt"},


	{0xc492f751, &WrapI_U<sceUtilityGameSharingInitStart>, "sceUtilityGameSharingInitStart"},
	{0xefc6f80f, &WrapI_V<sceUtilityGameSharingShutdownStart>, "sceUtilityGameSharingShutdownStart"},
	{0x7853182d, &WrapI_I<sceUtilityGameSharingUpdate>, "sceUtilityGameSharingUpdate"},
	{0x946963f3, &WrapI_V<sceUtilityGameSharingGetStatus>, "sceUtilityGameSharingGetStatus"},

	{0x2995d020, 0, "sceUtilitySavedataErrInitStart"},
	{0xb62a4061, 0, "sceUtilitySavedataErrShutdownStart"},
	{0xed0fad38, 0, "sceUtilitySavedataErrUpdate"},
	{0x88bc7406, 0, "sceUtilitySavedataErrGetStatus"},

	{0xbda7d894, 0, "sceUtilityHtmlViewerGetStatus"},
	{0xcdc3aa41, 0, "sceUtilityHtmlViewerInitStart"},
	{0xf5ce1134, 0, "sceUtilityHtmlViewerShutdownStart"},
	{0x05afb9e4, 0, "sceUtilityHtmlViewerUpdate"},

	{0x16a1a8d8, 0, "sceUtilityAuthDialogGetStatus"},
	{0x943cba46, 0, "sceUtilityAuthDialogInitStart"},
	{0x0f3eeaac, 0, "sceUtilityAuthDialogShutdownStart"},
	{0x147f7c85, 0, "sceUtilityAuthDialogUpdate"},

	{0xc629af26, &WrapU_U<sceUtilityLoadAvModule>, "sceUtilityLoadAvModule"},
	{0xf7d8d092, &WrapU_U<sceUtilityUnloadAvModule>, "sceUtilityUnloadAvModule"},

	{0x2a2b3de0, &WrapU_U<sceUtilityLoadModule>, "sceUtilityLoadModule"},
	{0xe49bfe92, &WrapU_U<sceUtilityUnloadModule>, "sceUtilityUnloadModule"},

	{0x0251B134, &WrapU_UUUUUU<sceUtilityScreenshotInitStart>, "sceUtilityScreenshotInitStart"},
	{0xF9E0008C, &WrapU_V<sceUtilityScreenshotShutdownStart>, "sceUtilityScreenshotShutdownStart"},
	{0xAB083EA9, &WrapU_U<sceUtilityScreenshotUpdate>, "sceUtilityScreenshotUpdate"},
	{0xD81957B7, &WrapI_V<sceUtilityScreenshotGetStatus>, "sceUtilityScreenshotGetStatus"},
	{0x86A03A27, 0, "sceUtilityScreenshotContStart"},

	{0x0D5BC6D2, 0, "sceUtilityLoadUsbModule"},
	{0xF64910F0, 0, "sceUtilityUnloadUsbModule"},

	{0x24AC31EB, &WrapI_U<sceUtilityGamedataInstallInitStart>, "sceUtilityGamedataInstallInitStart"},
	{0x32E32DCB, &WrapI_V<sceUtilityGamedataInstallShutdownStart>, "sceUtilityGamedataInstallShutdownStart"},
	{0x4AECD179, &WrapI_I<sceUtilityGamedataInstallUpdate>, "sceUtilityGamedataInstallUpdate"},
	{0xB57E95D9, &WrapI_V<sceUtilityGamedataInstallGetStatus>, "sceUtilityGamedataInstallGetStatus"},
	{0x180F7B62, &WrapI_V<sceUtilityGamedataInstallAbort>, "sceUtilityGamedataInstallAbort"},

	{0x16D02AF0, 0, "sceUtilityNpSigninInitStart"},
	{0xE19C97D6, 0, "sceUtilityNpSigninShutdownStart"},
	{0xF3FBC572, 0, "sceUtilityNpSigninUpdate"},
	{0x86ABDB1B, 0, "sceUtilityNpSigninGetStatus"},

	{0x1281DA8E, &WrapV_U<sceUtilityInstallInitStart>, "sceUtilityInstallInitStart"},
	{0x5EF1C24A, 0, "sceUtilityInstallShutdownStart"},
	{0xA03D29BA, 0, "sceUtilityInstallUpdate"},
	{0xC4700FA3, 0, "sceUtilityInstallGetStatus"},

	{0x54A5C62F, &WrapI_V<sceUtilityStoreCheckoutShutdownStart>, "sceUtilityStoreCheckoutShutdownStart"},
	{0xDA97F1AA, &WrapI_U<sceUtilityStoreCheckoutInitStart>, "sceUtilityStoreCheckoutInitStart"},
	{0xB8592D5F, &WrapI_I<sceUtilityStoreCheckoutUpdate>, "sceUtilityStoreCheckoutUpdate"},
	{0x3AAD51DC, &WrapI_V<sceUtilityStoreCheckoutGetStatus>, "sceUtilityStoreCheckoutGetStatus"},

	{0xd17a0573, 0, "sceUtilityPS3ScanShutdownStart"},
	{0x42071a83, 0, "sceUtilityPS3ScanInitStart"},
	{0xd852cdce, 0, "sceUtilityPS3ScanUpdate"},
	{0x89317c8f, 0, "sceUtilityPS3ScanGetStatus"},

	{0xe1bc175e, 0, "sceUtility_E1BC175E"},
	{0x43e521b7, 0, "sceUtility_43E521B7"},
	{0xdb4149ee, 0, "sceUtility_DB4149EE"},
	{0xcfe7c460, 0, "sceUtility_CFE7C460"},

	{0xc130d441, 0, "sceUtilityPsnShutdownStart"},
	{0xa7bb7c67, 0, "sceUtilityPsnInitStart"},
	{0x0940a1b9, 0, "sceUtilityPsnUpdate"},
	{0x094198b8, 0, "sceUtilityPsnGetStatus"},

	{0x9f313d14, 0, "sceUtilityAutoConnectShutdownStart"},
	{0x3a15cd0a, 0, "sceUtilityAutoConnectInitStart"},
	{0xd23665f4, 0, "sceUtilityAutoConnectUpdate"},
	{0xd4c2bd73, 0, "sceUtilityAutoConnectGetStatus"},
	{0x0e0c27af, 0, "sceUtilityAutoConnectAbort"},

	{0x06A48659, 0, "sceUtilityRssSubscriberShutdownStart"},
	{0x4B0A8FE5, 0, "sceUtilityRssSubscriberInitStart"},
	{0xA084E056, 0, "sceUtilityRssSubscriberUpdate"},
	{0x2B96173B, 0, "sceUtilityRssSubscriberGetStatus"},

	{0x149a7895, 0, "sceUtilityDNASShutdownStart"},
	{0xdde5389d, 0, "sceUtilityDNASInitStart"},
	{0x4a833ba4, 0, "sceUtilityDNASUpdate"},
	{0xa50e5b30, 0, "sceUtilityDNASGetStatus"},

	{0xe7b778d8, 0, "sceUtilityRssReaderShutdownStart"},
	{0x81c44706, 0, "sceUtilityRssReaderInitStart"},
	{0x6f56f9cf, 0, "sceUtilityRssReaderUpdate"},
	{0x8326ab05, 0, "sceUtilityRssReaderGetStatus"},
	{0xb0fb7ff5, 0, "sceUtilityRssReaderContStart"},

	{0xbc6b6296, 0, "sceNetplayDialogShutdownStart"},
	{0x3ad50ae7, 0, "sceNetplayDialogInitStart"},
	{0x417bed54, 0, "sceNetplayDialogUpdate"},
	{0xb6cee597, 0, "sceNetplayDialogGetStatus"},

	{0x28d35634, 0, "sceUtility_28D35634"},
	{0x70267adf, 0, "sceUtility_70267ADF"},
	{0xece1d3e5, 0, "sceUtility_ECE1D3E5"},
	{0xef3582b2, 0, "sceUtility_EF3582B2"},
};

void Register_sceUtility()
{
	RegisterModule("sceUtility", ARRAY_SIZE(sceUtility), sceUtility);
}

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping() {
	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
	IniFile mapping;
	mapping.LoadFromVFS("langregion.ini");
	std::vector<std::string> keys;
	mapping.GetKeys("LangRegionNames", keys);


	std::map<std::string, int> langCodeMapping;
	langCodeMapping["JAPANESE"] = PSP_SYSTEMPARAM_LANGUAGE_JAPANESE;
	langCodeMapping["ENGLISH"] = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	langCodeMapping["FRENCH"] = PSP_SYSTEMPARAM_LANGUAGE_FRENCH;
	langCodeMapping["SPANISH"] = PSP_SYSTEMPARAM_LANGUAGE_SPANISH;
	langCodeMapping["GERMAN"] = PSP_SYSTEMPARAM_LANGUAGE_GERMAN;
	langCodeMapping["ITALIAN"] = PSP_SYSTEMPARAM_LANGUAGE_ITALIAN;
	langCodeMapping["DUTCH"] = PSP_SYSTEMPARAM_LANGUAGE_DUTCH;
	langCodeMapping["PORTUGUESE"] = PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE;
	langCodeMapping["RUSSIAN"] = PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN;
	langCodeMapping["KOREAN"] = PSP_SYSTEMPARAM_LANGUAGE_KOREAN;
	langCodeMapping["CHINESE_TRADITIONAL"] = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL;
	langCodeMapping["CHINESE_SIMPLIFIED"] = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED;

	IniFile::Section *langRegionNames = mapping.GetOrCreateSection("LangRegionNames");
	IniFile::Section *systemLanguage = mapping.GetOrCreateSection("SystemLanguage");

	for (size_t i = 0; i < keys.size(); i++) {
		std::string langName;
		langRegionNames->Get(keys[i].c_str(), &langName, "ERROR");
		std::string langCode;
		systemLanguage->Get(keys[i].c_str(), &langCode, "ENGLISH");
		int iLangCode = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		if (langCodeMapping.find(langCode) != langCodeMapping.end())
			iLangCode = langCodeMapping[langCode];
		langValuesMapping[keys[i]] = std::make_pair(langName, iLangCode);
	}
	return langValuesMapping;
}

