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

#include <algorithm>
#include <set>

#include "file/ini_file.h"

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/Config.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUtility.h"

#include "Core/HLE/sceCtrl.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/Dialog/PSPMsgDialog.h"
#include "Core/Dialog/PSPPlaceholderDialog.h"
#include "Core/Dialog/PSPOskDialog.h"
#include "Core/Dialog/PSPGamedataInstallDialog.h"
#include "Core/Dialog/PSPNetconfDialog.h"
#include "Core/Dialog/PSPScreenshotDialog.h"

#define PSP_AV_MODULE_AVCODEC     0
#define PSP_AV_MODULE_SASCORE     1
#define PSP_AV_MODULE_ATRAC3PLUS  2 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MPEGBASE    3 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MP3         4
#define PSP_AV_MODULE_VAUDIO      5
#define PSP_AV_MODULE_AAC         6
#define PSP_AV_MODULE_G729        7

#define PSP_USB_MODULE_PSPCM 1
#define PSP_USB_MODULE_ACC 2
#define PSP_USB_MODULE_MIC 3 // Requires PSP_USB_MODULE_ACC loading first
#define PSP_USB_MODULE_CAM 4 // Requires PSP_USB_MODULE_ACC loading first
#define PSP_USB_MODULE_GPS 5 // Requires PSP_USB_MODULE_ACC loading first

const int SCE_ERROR_MODULE_BAD_ID = 0x80111101;
const int SCE_ERROR_MODULE_ALREADY_LOADED = 0x80111102;
const int SCE_ERROR_MODULE_NOT_LOADED = 0x80111103;
const int SCE_ERROR_AV_MODULE_BAD_ID = 0x80110F01;
const u32 PSP_MODULE_NET_HTTP = 261;
const u32 PSP_MODULE_NET_HTTPSTORAGE = 264;
int oldStatus = 100; //random value

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
static PSPScreenshotDialog screenshotDialog;
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

static int sceUtilitySavedataInitStart(u32 paramAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataInitStart(%08x): wrong dialog type", paramAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	oldStatus = 100;
	currentDialogType = UTILITY_DIALOG_SAVEDATA;
	currentDialogActive = true;
	int ret = saveDialog.Init(paramAddr);
	DEBUG_LOG(SCEUTILITY,"%08x=sceUtilitySavedataInitStart(%08x)",ret,paramAddr);
	return ret;
}

static int sceUtilitySavedataShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret = saveDialog.Shutdown();
	DEBUG_LOG(SCEUTILITY,"%08x=sceUtilitySavedataShutdownStart()",ret);
	return ret;
}

static int sceUtilitySavedataGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilitySavedataGetStatus(): wrong dialog type");
		hleEatCycles(200);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = saveDialog.GetStatus();
	if (oldStatus != status) {
		oldStatus = status;
		DEBUG_LOG(SCEUTILITY, "%08x=sceUtilitySavedataGetStatus()", status);
	}
	hleEatCycles(200);
	return status;
}

static int sceUtilitySavedataUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(SCEUTILITY, "sceUtilitySavedataUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int result = saveDialog.Update(animSpeed);
	DEBUG_LOG(SCEUTILITY,"%08x=sceUtilitySavedataUpdate(%i)", result, animSpeed);
	if (result >= 0)
		return hleDelayResult(result, "savedata update", 300);
	return result;
}

static u32 sceUtilityLoadAvModule(u32 module)
{
	if (module > 7)
	{
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilityLoadAvModule(%i): invalid module id", module);
		return SCE_ERROR_AV_MODULE_BAD_ID;
	}
	
	INFO_LOG(SCEUTILITY, "0=sceUtilityLoadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module loaded", 25000);
}

static u32 sceUtilityUnloadAvModule(u32 module)
{
	INFO_LOG(SCEUTILITY,"0=sceUtilityUnloadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module unloaded", 800);
}

static u32 sceUtilityLoadModule(u32 module)
{
	// TODO: Not all modules between 0x100 and 0x601 are valid.
	if (module < 0x100 || module > 0x601)
	{
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilityLoadModule(%i): invalid module id", module);
		return SCE_ERROR_MODULE_BAD_ID;
	}

	if (currentlyLoadedModules.find(module) != currentlyLoadedModules.end())
	{
		ERROR_LOG(SCEUTILITY, "sceUtilityLoadModule(%i): already loaded", module);
		return SCE_ERROR_MODULE_ALREADY_LOADED;
	}
	INFO_LOG(SCEUTILITY, "sceUtilityLoadModule(%i)", module);
	// Fix Kamen Rider Climax Heroes OOO - ULJS00331 loading
	// Fix Naruto Shippuden Kizuna Drive (error module load failed)
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

static u32 sceUtilityUnloadModule(u32 module)
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

static int sceUtilityMsgDialogInitStart(u32 paramAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogInitStart(%08x): wrong dialog type", paramAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	oldStatus = 100;
	currentDialogType = UTILITY_DIALOG_MSG;
	currentDialogActive = true;
	int ret = msgDialog.Init(paramAddr);
	INFO_LOG(SCEUTILITY, "%08x=sceUtilityMsgDialogInitStart(%08x)", ret, paramAddr);
	return ret;
}

static int sceUtilityMsgDialogShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret = msgDialog.Shutdown();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityMsgDialogShutdownStart()", ret);
	return ret;
}

static int sceUtilityMsgDialogUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = msgDialog.Update(animSpeed);
	DEBUG_LOG(SCEUTILITY,"%08x=sceUtilityMsgDialogUpdate(%i)", ret, animSpeed);
	if (ret >= 0)
		return hleDelayResult(ret, "msgdialog update", 800);
	return ret;
}

static int sceUtilityMsgDialogGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityMsgDialogGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = msgDialog.GetStatus();
	if (oldStatus != status) {
		oldStatus = status;
		DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityMsgDialogGetStatus()", status);
	}
	return status;
}

static int sceUtilityMsgDialogAbort()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityMsgDialogAbort(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = msgDialog.Abort();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityMsgDialogAbort()", ret);
	return ret;
}


// On screen keyboard
static int sceUtilityOskInitStart(u32 oskPtr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityOskInitStart(%08x): wrong dialog type", oskPtr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	oldStatus = 100;
	currentDialogType = UTILITY_DIALOG_OSK;
	currentDialogActive = true;
	int ret = oskDialog.Init(oskPtr);
	INFO_LOG(SCEUTILITY, "%08x=sceUtilityOskInitStart(%08x)", ret, oskPtr);
	return ret;
}

static int sceUtilityOskShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityOskShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret = oskDialog.Shutdown();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityOskShutdownStart()",ret);
	return ret;
}

static int sceUtilityOskUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityOskUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = oskDialog.Update(animSpeed);
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityOskUpdate(%i)", ret, animSpeed);
	return ret;
}

static int sceUtilityOskGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityOskGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = oskDialog.GetStatus();
	if (oldStatus != status) {
		oldStatus = status;
		DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityOskGetStatus()", status);
	}
	return status;
}


static int sceUtilityNetconfInitStart(u32 paramsAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_NET) {
		WARN_LOG(SCEUTILITY, "sceUtilityNetconfInitStart(%08x): wrong dialog type", paramsAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	oldStatus = 100;
	currentDialogType = UTILITY_DIALOG_NET;
	currentDialogActive = true;	
	int ret = netDialog.Init(paramsAddr);
	INFO_LOG(SCEUTILITY, "%08x=sceUtilityNetconfInitStart(%08x)", ret, paramsAddr);
	return ret;
}

static int sceUtilityNetconfShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_NET) {
		WARN_LOG(SCEUTILITY, "sceUtilityNetconfShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret = netDialog.Shutdown();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityNetconfShutdownStart()",ret);
	return ret;
}

static int sceUtilityNetconfUpdate(int animSpeed)
{
	int ret = netDialog.Update(animSpeed);
	ERROR_LOG(SCEUTILITY, "UNIMPL %08x=sceUtilityNetconfUpdate(%i)", ret, animSpeed);
	return ret;
}

static int sceUtilityNetconfGetStatus()
{
	// Spam in Danball Senki BOOST
	if (currentDialogType != UTILITY_DIALOG_NET) {
		DEBUG_LOG(SCEUTILITY, "sceUtilityNetconfGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = netDialog.GetStatus();
	if (oldStatus != status) {
		oldStatus = status;
		DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityNetconfGetStatus()", status);
	}
	return status;
}

static int sceUtilityCheckNetParam(int id)
{
	bool available = (id >= 0 && id <= 24);
	int ret = available ? 0 : 0X80110601;
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityCheckNetParam(%d)", ret, id);
	return ret;
}


//TODO: Implement all sceUtilityScreenshot* for real, it doesn't seem to be complex
//but it requires more investigation
static int sceUtilityScreenshotInitStart(u32 paramAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotInitStart(%08x): wrong dialog type", paramAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	oldStatus = 100;
	currentDialogType = UTILITY_DIALOG_SCREENSHOT;
	currentDialogActive = true;
	u32 retval = screenshotDialog.Init(paramAddr);
	WARN_LOG_REPORT(SCEUTILITY, "%08x=sceUtilityScreenshotInitStart(%08x)", retval, paramAddr);
	return retval;
}

static int sceUtilityScreenshotShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret  = screenshotDialog.Shutdown();
	WARN_LOG(SCEUTILITY, "%08x=sceUtilityScreenshotShutdownStart()", ret);
	return ret;
}

static int sceUtilityScreenshotUpdate(u32 animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = screenshotDialog.Update(animSpeed);
	WARN_LOG(SCEUTILITY, "%08x=sceUtilityScreenshotUpdate(%i)", ret, animSpeed);
	return ret;
}

static int sceUtilityScreenshotGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityScreenshotGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = screenshotDialog.GetStatus(); 
	if (oldStatus != status) {
		oldStatus = status;
		WARN_LOG(SCEUTILITY, "%08x=sceUtilityScreenshotGetStatus()", status);
	}
	return status;
}

static int sceUtilityScreenshotContStart(u32 paramAddr)
{
	if (currentDialogType != UTILITY_DIALOG_SCREENSHOT)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityScreenshotContStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = screenshotDialog.ContStart();
	WARN_LOG(SCEUTILITY, "%08x=sceUtilityScreenshotContStart(%08x)", ret, paramAddr);
	return ret;
}

static int sceUtilityGamedataInstallInitStart(u32 paramsAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallInitStart(%08x): wrong dialog type", paramsAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogType = UTILITY_DIALOG_GAMEDATAINSTALL;
	currentDialogActive = true;	
	int ret = gamedataInstallDialog.Init(paramsAddr);
	INFO_LOG(SCEUTILITY, "%08x=sceUtilityGamedataInstallInitStart(%08x)",ret,paramsAddr);
	return ret;
}

static int sceUtilityGamedataInstallShutdownStart() {
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	DEBUG_LOG(SCEUTILITY, "sceUtilityGamedataInstallShutdownStart()");
	return gamedataInstallDialog.Shutdown();
}

static int sceUtilityGamedataInstallUpdate(int animSpeed) {
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	int ret = gamedataInstallDialog.Update(animSpeed);
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityGamedataInstallUpdate(%i)", ret, animSpeed);
	return ret;
}

static int sceUtilityGamedataInstallGetStatus()
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

static int sceUtilityGamedataInstallAbort()
{
	if (currentDialogType != UTILITY_DIALOG_GAMEDATAINSTALL)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGamedataInstallAbort(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	
	currentDialogActive = false;
	int ret = gamedataInstallDialog.Abort();
	DEBUG_LOG(SCEUTILITY, "%08x=sceUtilityGamedataInstallDialogAbort",ret);
	return ret;
}

//TODO: should save to config file
static u32 sceUtilitySetSystemParamString(u32 id, u32 strPtr)
{
	WARN_LOG_REPORT(SCEUTILITY, "sceUtilitySetSystemParamString(%i, %08x)", id, strPtr);
	return 0;
}

static u32 sceUtilityGetSystemParamString(u32 id, u32 destaddr, int destSize)
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

static u32 sceUtilityGetSystemParamInt(u32 id, u32 destaddr)
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
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT:
		param = g_Config.iTimeFormat?PSP_SYSTEMPARAM_TIME_FORMAT_12HR:PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIMEZONE:
		param = g_Config.iTimeZone;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS:
		param = g_Config.bDayLightSavings?PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING:PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LANGUAGE:
		param = g_Config.iLanguage;
		break;
	case PSP_SYSTEMPARAM_ID_INT_BUTTON_PREFERENCE:
		param = g_Config.iButtonPreference;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LOCK_PARENTAL_LEVEL:
		param = g_Config.iLockParentalLevel;
		break;
	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	Memory::Write_U32(param, destaddr);

	return 0;
}

static u32 sceUtilityLoadNetModule(u32 module)
{
	DEBUG_LOG(SCEUTILITY,"FAKE: sceUtilityLoadNetModule(%i)", module);
	return 0;
}

static u32 sceUtilityUnloadNetModule(u32 module)
{
	DEBUG_LOG(SCEUTILITY,"FAKE: sceUtilityUnloadNetModule(%i)", module);
	return 0;
}

static void sceUtilityInstallInitStart(u32 unknown)
{
	WARN_LOG_REPORT(SCEUTILITY, "UNIMPL sceUtilityInstallInitStart()");
}

static int sceUtilityStoreCheckoutShutdownStart()
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutShutdownStart()");
	return 0;
}

static int sceUtilityStoreCheckoutInitStart(u32 paramsPtr)
{
	ERROR_LOG_REPORT(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutInitStart(%d)", paramsPtr);
	return 0;
}

static int sceUtilityStoreCheckoutUpdate(int drawSpeed)
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutUpdate(%d)", drawSpeed);
	return 0;
}

static int sceUtilityStoreCheckoutGetStatus()
{
	ERROR_LOG(SCEUTILITY,"UNIMPL sceUtilityStoreCheckoutGetStatus()");
	return 0;
}

static int sceUtilityGameSharingShutdownStart()
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

static int sceUtilityGameSharingInitStart(u32 paramsPtr)
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

static int sceUtilityGameSharingUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		WARN_LOG(SCEUTILITY, "sceUtilityGameSharingUpdate(%i): wrong dialog type", animSpeed);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityGameSharingUpdate(%i)", animSpeed);
	return 0;
}

static int sceUtilityGameSharingGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_GAMESHARING)
	{
		DEBUG_LOG(SCEUTILITY, "sceUtilityGameSharingGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	ERROR_LOG(SCEUTILITY, "UNIMPL sceUtilityGameSharingGetStatus()");
	return 0;
}

static u32 sceUtilityLoadUsbModule(u32 module)
{
	if (module < 1 || module > 5)
	{
		ERROR_LOG(SCEUTILITY, "sceUtilityLoadUsbModule(%i): invalid module id", module);
	}

	ERROR_LOG_REPORT(SCEUTILITY, "UNIMPL sceUtilityLoadUsbModule(%i)", module);
	return 0;
}

static u32 sceUtilityUnloadUsbModule(u32 module)
{
	if (module < 1 || module > 5)
	{
		ERROR_LOG(SCEUTILITY, "sceUtilityUnloadUsbModule(%i): invalid module id", module);
	}

	ERROR_LOG_REPORT(SCEUTILITY, "UNIMPL sceUtilityUnloadUsbModule(%i)", module);
	return 0;
}

const HLEFunction sceUtility[] = 
{
	{0X1579A159, &WrapU_U<sceUtilityLoadNetModule>,                "sceUtilityLoadNetModule",                'x', "x"  },
	{0X64D50C56, &WrapU_U<sceUtilityUnloadNetModule>,              "sceUtilityUnloadNetModule",              'x', "x"  },

	{0XF88155F6, &WrapI_V<sceUtilityNetconfShutdownStart>,         "sceUtilityNetconfShutdownStart",         'i', ""   },
	{0X4DB1E739, &WrapI_U<sceUtilityNetconfInitStart>,             "sceUtilityNetconfInitStart",             'i', "x"  },
	{0X91E70E35, &WrapI_I<sceUtilityNetconfUpdate>,                "sceUtilityNetconfUpdate",                'i', "i"  },
	{0X6332AA39, &WrapI_V<sceUtilityNetconfGetStatus>,             "sceUtilityNetconfGetStatus",             'i', ""   },
	{0X5EEE6548, &WrapI_I<sceUtilityCheckNetParam>,                "sceUtilityCheckNetParam",                'i', "i"  },
	{0X434D4B3A, nullptr,                                          "sceUtilityGetNetParam",                  '?', ""   },
	{0X4FED24D8, nullptr,                                          "sceUtilityGetNetParamLatestID",          '?', ""   },

	{0X67AF3428, &WrapI_V<sceUtilityMsgDialogShutdownStart>,       "sceUtilityMsgDialogShutdownStart",       'i', ""   },
	{0X2AD8E239, &WrapI_U<sceUtilityMsgDialogInitStart>,           "sceUtilityMsgDialogInitStart",           'i', "x"  },
	{0X95FC253B, &WrapI_I<sceUtilityMsgDialogUpdate>,              "sceUtilityMsgDialogUpdate",              'i', "i"  },
	{0X9A1C91D7, &WrapI_V<sceUtilityMsgDialogGetStatus>,           "sceUtilityMsgDialogGetStatus",           'i', ""   },
	{0X4928BD96, &WrapI_V<sceUtilityMsgDialogAbort>,               "sceUtilityMsgDialogAbort",               'i', ""   },

	{0X9790B33C, &WrapI_V<sceUtilitySavedataShutdownStart>,        "sceUtilitySavedataShutdownStart",        'i', ""   },
	{0X50C4CD57, &WrapI_U<sceUtilitySavedataInitStart>,            "sceUtilitySavedataInitStart",            'i', "x"  },
	{0XD4B95FFB, &WrapI_I<sceUtilitySavedataUpdate>,               "sceUtilitySavedataUpdate",               'i', "i"  },
	{0X8874DBE0, &WrapI_V<sceUtilitySavedataGetStatus>,            "sceUtilitySavedataGetStatus",            'i', ""   },

	{0X3DFAEBA9, &WrapI_V<sceUtilityOskShutdownStart>,             "sceUtilityOskShutdownStart",             'i', ""   },
	{0XF6269B82, &WrapI_U<sceUtilityOskInitStart>,                 "sceUtilityOskInitStart",                 'i', "x"  },
	{0X4B85C861, &WrapI_I<sceUtilityOskUpdate>,                    "sceUtilityOskUpdate",                    'i', "i"  },
	{0XF3F76017, &WrapI_V<sceUtilityOskGetStatus>,                 "sceUtilityOskGetStatus",                 'i', ""   },

	{0X41E30674, &WrapU_UU<sceUtilitySetSystemParamString>,        "sceUtilitySetSystemParamString",         'x', "xx" },
	{0X45C18506, nullptr,                                          "sceUtilitySetSystemParamInt",            '?', ""   },
	{0X34B78343, &WrapU_UUI<sceUtilityGetSystemParamString>,       "sceUtilityGetSystemParamString",         'x', "xxi"},
	{0XA5DA2406, &WrapU_UU<sceUtilityGetSystemParamInt>,           "sceUtilityGetSystemParamInt",            'x', "xx" },


	{0XC492F751, &WrapI_U<sceUtilityGameSharingInitStart>,         "sceUtilityGameSharingInitStart",         'i', "x"  },
	{0XEFC6F80F, &WrapI_V<sceUtilityGameSharingShutdownStart>,     "sceUtilityGameSharingShutdownStart",     'i', ""   },
	{0X7853182D, &WrapI_I<sceUtilityGameSharingUpdate>,            "sceUtilityGameSharingUpdate",            'i', "i"  },
	{0X946963F3, &WrapI_V<sceUtilityGameSharingGetStatus>,         "sceUtilityGameSharingGetStatus",         'i', ""   },

	{0X2995D020, nullptr,                                          "sceUtilitySavedataErrInitStart",         '?', ""   },
	{0XB62A4061, nullptr,                                          "sceUtilitySavedataErrShutdownStart",     '?', ""   },
	{0XED0FAD38, nullptr,                                          "sceUtilitySavedataErrUpdate",            '?', ""   },
	{0X88BC7406, nullptr,                                          "sceUtilitySavedataErrGetStatus",         '?', ""   },

	{0XBDA7D894, nullptr,                                          "sceUtilityHtmlViewerGetStatus",          '?', ""   },
	{0XCDC3AA41, nullptr,                                          "sceUtilityHtmlViewerInitStart",          '?', ""   },
	{0XF5CE1134, nullptr,                                          "sceUtilityHtmlViewerShutdownStart",      '?', ""   },
	{0X05AFB9E4, nullptr,                                          "sceUtilityHtmlViewerUpdate",             '?', ""   },

	{0X16A1A8D8, nullptr,                                          "sceUtilityAuthDialogGetStatus",          '?', ""   },
	{0X943CBA46, nullptr,                                          "sceUtilityAuthDialogInitStart",          '?', ""   },
	{0X0F3EEAAC, nullptr,                                          "sceUtilityAuthDialogShutdownStart",      '?', ""   },
	{0X147F7C85, nullptr,                                          "sceUtilityAuthDialogUpdate",             '?', ""   },

	{0XC629AF26, &WrapU_U<sceUtilityLoadAvModule>,                 "sceUtilityLoadAvModule",                 'x', "x"  },
	{0XF7D8D092, &WrapU_U<sceUtilityUnloadAvModule>,               "sceUtilityUnloadAvModule",               'x', "x"  },

	{0X2A2B3DE0, &WrapU_U<sceUtilityLoadModule>,                   "sceUtilityLoadModule",                   'x', "x"  },
	{0XE49BFE92, &WrapU_U<sceUtilityUnloadModule>,                 "sceUtilityUnloadModule",                 'x', "x"  },

	{0X0251B134, &WrapI_U<sceUtilityScreenshotInitStart>,          "sceUtilityScreenshotInitStart",          'i', "x"  },
	{0XF9E0008C, &WrapI_V<sceUtilityScreenshotShutdownStart>,      "sceUtilityScreenshotShutdownStart",      'i', ""   },
	{0XAB083EA9, &WrapI_U<sceUtilityScreenshotUpdate>,             "sceUtilityScreenshotUpdate",             'i', "x"  },
	{0XD81957B7, &WrapI_V<sceUtilityScreenshotGetStatus>,          "sceUtilityScreenshotGetStatus",          'i', ""   },
	{0X86A03A27, &WrapI_U<sceUtilityScreenshotContStart>,          "sceUtilityScreenshotContStart",          'i', "x"  },

	{0X0D5BC6D2, &WrapU_U<sceUtilityLoadUsbModule>,                "sceUtilityLoadUsbModule",                'x', "x"  },
	{0XF64910F0, &WrapU_U<sceUtilityUnloadUsbModule>,              "sceUtilityUnloadUsbModule",              'x', "x"  },

	{0X24AC31EB, &WrapI_U<sceUtilityGamedataInstallInitStart>,     "sceUtilityGamedataInstallInitStart",     'i', "x"  },
	{0X32E32DCB, &WrapI_V<sceUtilityGamedataInstallShutdownStart>, "sceUtilityGamedataInstallShutdownStart", 'i', ""   },
	{0X4AECD179, &WrapI_I<sceUtilityGamedataInstallUpdate>,        "sceUtilityGamedataInstallUpdate",        'i', "i"  },
	{0XB57E95D9, &WrapI_V<sceUtilityGamedataInstallGetStatus>,     "sceUtilityGamedataInstallGetStatus",     'i', ""   },
	{0X180F7B62, &WrapI_V<sceUtilityGamedataInstallAbort>,         "sceUtilityGamedataInstallAbort",         'i', ""   },

	{0X16D02AF0, nullptr,                                          "sceUtilityNpSigninInitStart",            '?', ""   },
	{0XE19C97D6, nullptr,                                          "sceUtilityNpSigninShutdownStart",        '?', ""   },
	{0XF3FBC572, nullptr,                                          "sceUtilityNpSigninUpdate",               '?', ""   },
	{0X86ABDB1B, nullptr,                                          "sceUtilityNpSigninGetStatus",            '?', ""   },

	{0X1281DA8E, &WrapV_U<sceUtilityInstallInitStart>,             "sceUtilityInstallInitStart",             'v', "x"  },
	{0X5EF1C24A, nullptr,                                          "sceUtilityInstallShutdownStart",         '?', ""   },
	{0XA03D29BA, nullptr,                                          "sceUtilityInstallUpdate",                '?', ""   },
	{0XC4700FA3, nullptr,                                          "sceUtilityInstallGetStatus",             '?', ""   },

	{0X54A5C62F, &WrapI_V<sceUtilityStoreCheckoutShutdownStart>,   "sceUtilityStoreCheckoutShutdownStart",   'i', ""   },
	{0XDA97F1AA, &WrapI_U<sceUtilityStoreCheckoutInitStart>,       "sceUtilityStoreCheckoutInitStart",       'i', "x"  },
	{0XB8592D5F, &WrapI_I<sceUtilityStoreCheckoutUpdate>,          "sceUtilityStoreCheckoutUpdate",          'i', "i"  },
	{0X3AAD51DC, &WrapI_V<sceUtilityStoreCheckoutGetStatus>,       "sceUtilityStoreCheckoutGetStatus",       'i', ""   },

	{0XD17A0573, nullptr,                                          "sceUtilityPS3ScanShutdownStart",         '?', ""   },
	{0X42071A83, nullptr,                                          "sceUtilityPS3ScanInitStart",             '?', ""   },
	{0XD852CDCE, nullptr,                                          "sceUtilityPS3ScanUpdate",                '?', ""   },
	{0X89317C8F, nullptr,                                          "sceUtilityPS3ScanGetStatus",             '?', ""   },

	{0XE1BC175E, nullptr,                                          "sceUtility_E1BC175E",                    '?', ""   },
	{0X43E521B7, nullptr,                                          "sceUtility_43E521B7",                    '?', ""   },
	{0XDB4149EE, nullptr,                                          "sceUtility_DB4149EE",                    '?', ""   },
	{0XCFE7C460, nullptr,                                          "sceUtility_CFE7C460",                    '?', ""   },

	{0XC130D441, nullptr,                                          "sceUtilityPsnShutdownStart",             '?', ""   },
	{0XA7BB7C67, nullptr,                                          "sceUtilityPsnInitStart",                 '?', ""   },
	{0X0940A1B9, nullptr,                                          "sceUtilityPsnUpdate",                    '?', ""   },
	{0X094198B8, nullptr,                                          "sceUtilityPsnGetStatus",                 '?', ""   },

	{0X9F313D14, nullptr,                                          "sceUtilityAutoConnectShutdownStart",     '?', ""   },
	{0X3A15CD0A, nullptr,                                          "sceUtilityAutoConnectInitStart",         '?', ""   },
	{0XD23665F4, nullptr,                                          "sceUtilityAutoConnectUpdate",            '?', ""   },
	{0XD4C2BD73, nullptr,                                          "sceUtilityAutoConnectGetStatus",         '?', ""   },
	{0X0E0C27AF, nullptr,                                          "sceUtilityAutoConnectAbort",             '?', ""   },

	{0X06A48659, nullptr,                                          "sceUtilityRssSubscriberShutdownStart",   '?', ""   },
	{0X4B0A8FE5, nullptr,                                          "sceUtilityRssSubscriberInitStart",       '?', ""   },
	{0XA084E056, nullptr,                                          "sceUtilityRssSubscriberUpdate",          '?', ""   },
	{0X2B96173B, nullptr,                                          "sceUtilityRssSubscriberGetStatus",       '?', ""   },

	{0X149A7895, nullptr,                                          "sceUtilityDNASShutdownStart",            '?', ""   },
	{0XDDE5389D, nullptr,                                          "sceUtilityDNASInitStart",                '?', ""   },
	{0X4A833BA4, nullptr,                                          "sceUtilityDNASUpdate",                   '?', ""   },
	{0XA50E5B30, nullptr,                                          "sceUtilityDNASGetStatus",                '?', ""   },

	{0XE7B778D8, nullptr,                                          "sceUtilityRssReaderShutdownStart",       '?', ""   },
	{0X81C44706, nullptr,                                          "sceUtilityRssReaderInitStart",           '?', ""   },
	{0X6F56F9CF, nullptr,                                          "sceUtilityRssReaderUpdate",              '?', ""   },
	{0X8326AB05, nullptr,                                          "sceUtilityRssReaderGetStatus",           '?', ""   },
	{0XB0FB7FF5, nullptr,                                          "sceUtilityRssReaderContStart",           '?', ""   },

	{0XBC6B6296, nullptr,                                          "sceNetplayDialogShutdownStart",          '?', ""   },
	{0X3AD50AE7, nullptr,                                          "sceNetplayDialogInitStart",              '?', ""   },
	{0X417BED54, nullptr,                                          "sceNetplayDialogUpdate",                 '?', ""   },
	{0XB6CEE597, nullptr,                                          "sceNetplayDialogGetStatus",              '?', ""   },

	{0X28D35634, nullptr,                                          "sceUtility_28D35634",                    '?', ""   },
	{0X70267ADF, nullptr,                                          "sceUtility_70267ADF",                    '?', ""   },
	{0XECE1D3E5, nullptr,                                          "sceUtility_ECE1D3E5",                    '?', ""   },
	{0XEF3582B2, nullptr,                                          "sceUtility_EF3582B2",                    '?', ""   },
};

void Register_sceUtility()
{
	RegisterModule("sceUtility", ARRAY_SIZE(sceUtility), sceUtility);
}
