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

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"

#include "sceCtrl.h"
#include "../Util/PPGeDraw.h"
#include "../Dialog/PSPSaveDialog.h"
#include "../Dialog/PSPMsgDialog.h"
#include "../Dialog/PSPPlaceholderDialog.h"
#include "../Dialog/PSPOskDialog.h"

PSPSaveDialog saveDialog;
PSPMsgDialog msgDialog;
PSPOskDialog oskDialog;
PSPPlaceholderDialog netDialog;

void __UtilityInit()
{
	SavedataParam::Init();
}

int sceUtilitySavedataInitStart(u32 paramAddr)
{
	saveDialog.Init(paramAddr);
	return 0;
}

int sceUtilitySavedataShutdownStart()
{
	DEBUG_LOG(HLE,"sceUtilitySavedataShutdownStart()");
	saveDialog.Shutdown();
	return 0;
}

int sceUtilitySavedataGetStatus()
{
	return saveDialog.GetStatus();
}

void sceUtilitySavedataUpdate(u32 unknown)
{
	DEBUG_LOG(HLE,"sceUtilitySavedataUpdate()");

	saveDialog.Update();

	return;
}

#define PSP_AV_MODULE_AVCODEC					 0
#define PSP_AV_MODULE_SASCORE					 1
#define PSP_AV_MODULE_ATRAC3PLUS				2 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MPEGBASE					3 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MP3							 4
#define PSP_AV_MODULE_VAUDIO						5
#define PSP_AV_MODULE_AAC							 6
#define PSP_AV_MODULE_G729							7

//TODO: Shouldn't be void
void sceUtilityLoadAvModule(u32 module)
{
	DEBUG_LOG(HLE,"sceUtilityLoadAvModule(%i)", module);
	RETURN(0);
	__KernelReSchedule("utilityloadavmodule");
}

//TODO: Shouldn't be void
void sceUtilityLoadModule(u32 module)
{
	DEBUG_LOG(HLE,"sceUtilityLoadModule(%i)", module);
	RETURN(0);
	__KernelReSchedule("utilityloadmodule");
}

void sceUtilityMsgDialogInitStart(u32 structAddr)
{
	DEBUG_LOG(HLE,"sceUtilityMsgDialogInitStart(%i)", structAddr);
	msgDialog.Init(structAddr);
}

void sceUtilityMsgDialogShutdownStart(u32 unknown)
{
	DEBUG_LOG(HLE,"FAKE sceUtilityMsgDialogShutdownStart(%i)", unknown);
	msgDialog.Shutdown();
}

void sceUtilityMsgDialogUpdate(int animSpeed)
{
	DEBUG_LOG(HLE,"sceUtilityMsgDialogUpdate(%i)", animSpeed);
	msgDialog.Update();
}

u32 sceUtilityMsgDialogGetStatus()
{
	return msgDialog.GetStatus();
}


// On screen keyboard

int sceUtilityOskInitStart(u32 oskPtr)
{
	ERROR_LOG(HLE,"FAKE sceUtilityOskInitStart(%i)", PARAM(0));
	return oskDialog.Init(oskPtr);
}

int sceUtilityOskShutdownStart()
{
	ERROR_LOG(HLE,"FAKE sceUtilityOskShutdownStart(%i)", PARAM(0));
	oskDialog.Shutdown();
	return 0;
}

void sceUtilityOskUpdate(unsigned int unknown)
{
	ERROR_LOG(HLE,"FAKE sceUtilityOskUpdate(%i)", unknown);
	oskDialog.Update();
}

int sceUtilityOskGetStatus()
{
	int status =  oskDialog.GetStatus();
	// Seems that 4 is the cancelled status for OSK?
	if (status == 4)
	{
		status = 5;
	}
	return status;
}


void sceUtilityNetconfInitStart(unsigned int unknown)
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfInitStart(%i)", unknown);
	netDialog.Init();
}

void sceUtilityNetconfShutdownStart(unsigned int unknown)
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfShutdownStart(%i)", unknown);
	netDialog.Shutdown();
}

void sceUtilityNetconfUpdate(int unknown)
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfUpdate(%i)", unknown);
	netDialog.Update();
}

unsigned int sceUtilityNetconfGetStatus()
{
	DEBUG_LOG(HLE,"sceUtilityNetconfGetStatus()");
	return netDialog.GetStatus();
}

int sceUtilityScreenshotGetStatus()
{
	u32 retval =  0;//__UtilityGetStatus();
	DEBUG_LOG(HLE,"%i=sceUtilityScreenshotGetStatus()", retval);
	return retval;
}

int sceUtilityGamedataInstallGetStatus()
{
	u32 retval = 0;//__UtilityGetStatus();
	DEBUG_LOG(HLE,"%i=sceUtilityGamedataInstallGetStatus()", retval);
	return retval;
}

#define PSP_SYSTEMPARAM_ID_STRING_NICKNAME	1
#define PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL	2
#define PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE	3
#define PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT	4
#define PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT	5
//Timezone offset from UTC in minutes, (EST = -300 = -5 * 60)
#define PSP_SYSTEMPARAM_ID_INT_TIMEZONE		6
#define PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS	7
#define PSP_SYSTEMPARAM_ID_INT_LANGUAGE		8
/**
* #9 seems to be Region or maybe X/O button swap.
* It doesn't exist on JAP v1.0
* is 1 on NA v1.5s
* is 0 on JAP v1.5s
* is read-only
*/
#define PSP_SYSTEMPARAM_ID_INT_UNKNOWN		9

/**
* Return values for the SystemParam functions
*/
#define PSP_SYSTEMPARAM_RETVAL_OK	0
#define PSP_SYSTEMPARAM_RETVAL_FAIL	0x80110103

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL
*/
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC 0
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_1		1
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_6		6
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_11	11

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE
*/
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF	0
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_ON	1

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT
*/
#define PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD	0
#define PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY	1
#define PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY	2

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT
*/
#define PSP_SYSTEMPARAM_TIME_FORMAT_24HR	0
#define PSP_SYSTEMPARAM_TIME_FORMAT_12HR	1

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS
*/
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD	0
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING	1

/**
* Valid values for PSP_SYSTEMPARAM_ID_INT_LANGUAGE
*/
#define PSP_SYSTEMPARAM_LANGUAGE_JAPANESE	0
#define PSP_SYSTEMPARAM_LANGUAGE_ENGLISH	1
#define PSP_SYSTEMPARAM_LANGUAGE_FRENCH		2
#define PSP_SYSTEMPARAM_LANGUAGE_SPANISH	3
#define PSP_SYSTEMPARAM_LANGUAGE_GERMAN		4
#define PSP_SYSTEMPARAM_LANGUAGE_ITALIAN	5
#define PSP_SYSTEMPARAM_LANGUAGE_DUTCH		6
#define PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE	7
#define PSP_SYSTEMPARAM_LANGUAGE_KOREAN		8


//TODO: should save to config file
u32 sceUtilitySetSystemParamString(u32 id, u32 strPtr)
{
	DEBUG_LOG(HLE,"sceUtilitySetSystemParamString(%i, %08x)", id,strPtr);
	return 0;
}

//TODO: Should load from config file
u32 sceUtilityGetSystemParamString(u32 id, u32 destaddr, u32 unknownparam)
{
	DEBUG_LOG(HLE,"sceUtilityGetSystemParamString(%i, %08x, %i)", id,destaddr,unknownparam);
	char *buf = (char *)Memory::GetPointer(destaddr);
	switch (id) {
	case PSP_SYSTEMPARAM_ID_STRING_NICKNAME:
		strcpy(buf, "shadow");
		break;

	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	return 0;
}

//TODO: Should load from config file
u32 sceUtilityGetSystemParamInt(u32 id, u32 destaddr)
{
	DEBUG_LOG(HLE,"sceUtilityGetSystemParamInt(%i, %08x)", id,destaddr);
	u32 param = 0;
	switch (id) {
	case PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL:
		param = PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC;
		break;
	case PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE:
		param = PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT:
		param = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT:
		param = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIMEZONE:
		param = 60;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS:
		param = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LANGUAGE:
		param = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		break;
	case PSP_SYSTEMPARAM_ID_INT_UNKNOWN:
		param = 1;
		break;
	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	Memory::Write_U32(param, destaddr);

	return 0;
}

u32 sceUtilityLoadNetModule(u32 module)
{
	DEBUG_LOG(HLE,"FAKE: sceUtilityLoadNetModule(%i)", module);
	return 0;
}

u32 sceUtilityUnloadNetModule(u32 module)
{
	DEBUG_LOG(HLE,"FAKE: sceUtilityUnloadNetModule(%i)", module);
	return 0;
}

const HLEFunction sceUtility[] = 
{
	{0x1579a159, &WrapU_U<sceUtilityLoadNetModule>, "sceUtilityLoadNetModule"},
	{0x64d50c56, &WrapU_U<sceUtilityUnloadNetModule>, "sceUtilityUnloadNetModule"}, 


	{0xf88155f6, &WrapV_U<sceUtilityNetconfShutdownStart>, "sceUtilityNetconfShutdownStart"},
	{0x4db1e739, &WrapV_U<sceUtilityNetconfInitStart>, "sceUtilityNetconfInitStart"},
	{0x91e70e35, &WrapV_I<sceUtilityNetconfUpdate>, "sceUtilityNetconfUpdate"},
	{0x6332aa39, &WrapU_V<sceUtilityNetconfGetStatus>, "sceUtilityNetconfGetStatus"},
	{0x5eee6548, 0, "sceUtilityCheckNetParam"}, 
	{0x434d4b3a, 0, "sceUtilityGetNetParam"}, 
	{0x4FED24D8, 0, "sceUtilityGetNetParamLatestID"},

	{0x67af3428, &WrapV_U<sceUtilityMsgDialogShutdownStart>, "sceUtilityMsgDialogShutdownStart"},	
	{0x2ad8e239, &WrapV_U<sceUtilityMsgDialogInitStart>, "sceUtilityMsgDialogInitStart"},			
	{0x95fc253b, &WrapV_I<sceUtilityMsgDialogUpdate>, "sceUtilityMsgDialogUpdate"},				 
	{0x9a1c91d7, &WrapU_V<sceUtilityMsgDialogGetStatus>, "sceUtilityMsgDialogGetStatus"},		
	{0x4928bd96, 0, "sceUtilityMsgDialogAbort"}, 

	{0x9790b33c, &WrapI_V<sceUtilitySavedataShutdownStart>, "sceUtilitySavedataShutdownStart"},	 
	{0x50c4cd57, &WrapI_U<sceUtilitySavedataInitStart>, "sceUtilitySavedataInitStart"},			 
	{0xd4b95ffb, &WrapV_U<sceUtilitySavedataUpdate>, "sceUtilitySavedataUpdate"},					
	{0x8874dbe0, &WrapI_V<sceUtilitySavedataGetStatus>, "sceUtilitySavedataGetStatus"},

	{0x3dfaeba9, &WrapI_V<sceUtilityOskShutdownStart>, "sceUtilityOskShutdownStart"},
	{0xf6269b82, &WrapI_U<sceUtilityOskInitStart>, "sceUtilityOskInitStart"},
	{0x4b85c861, &WrapV_U<sceUtilityOskUpdate>, "sceUtilityOskUpdate"},
	{0xf3f76017, &WrapI_V<sceUtilityOskGetStatus>, "sceUtilityOskGetStatus"},

	{0x41e30674, &WrapU_UU<sceUtilitySetSystemParamString>, "sceUtilitySetSystemParamString"},
	{0x45c18506, 0, "sceUtilitySetSystemParamInt"}, 
	{0x34b78343, &WrapU_UUU<sceUtilityGetSystemParamString>, "sceUtilityGetSystemParamString"}, 
	{0xA5DA2406, &WrapU_UU<sceUtilityGetSystemParamInt>, "sceUtilityGetSystemParamInt"},


	{0xc492f751, 0, "sceUtilityGameSharingInitStart"}, 
	{0xefc6f80f, 0, "sceUtilityGameSharingShutdownStart"}, 
	{0x7853182d, 0, "sceUtilityGameSharingUpdate"}, 
	{0x946963f3, 0, "sceUtilityGameSharingGetStatus"}, 

	{0x2995d020, 0, "sceUtility_2995d020"}, 
	{0xb62a4061, 0, "sceUtility_b62a4061"}, 
	{0xed0fad38, 0, "sceUtility_ed0fad38"}, 
	{0x88bc7406, 0, "sceUtility_88bc7406"}, 

	{0xbda7d894, 0, "sceUtilityHtmlViewerGetStatus"}, 
	{0xcdc3aa41, 0, "sceUtilityHtmlViewerInitStart"}, 
	{0xf5ce1134, 0, "sceUtilityHtmlViewerShutdownStart"}, 
	{0x05afb9e4, 0, "sceUtilityHtmlViewerUpdate"}, 

	{0xc629af26, &WrapV_U<sceUtilityLoadAvModule>, "sceUtilityLoadAvModule"}, 
	{0xf7d8d092, 0, "sceUtilityUnloadAvModule"},

	{0x2a2b3de0, &WrapV_U<sceUtilityLoadModule>, "sceUtilityLoadModule"},
	{0xe49bfe92, 0, "sceUtilityUnloadModule"},

	{0x0251B134, 0, "sceUtilityScreenshotInitStart"},
	{0xF9E0008C, 0, "sceUtilityScreenshotShutdownStart"},
	{0xAB083EA9, 0, "sceUtilityScreenshotUpdate"},
	{0xD81957B7, &WrapI_V<sceUtilityScreenshotGetStatus>, "sceUtilityScreenshotGetStatus"},
	{0x86A03A27, 0, "sceUtilityScreenshotContStart"},

	{0x0D5BC6D2, 0, "sceUtilityLoadUsbModule"},
	{0xF64910F0, 0, "sceUtilityUnloadUsbModule"},

	{0x24AC31EB, 0, "sceUtilityGamedataInstallInitStart"},
	{0x32E32DCB, 0, "sceUtilityGamedataInstallShutdownStart"},
	{0x4AECD179, 0, "sceUtilityGamedataInstallUpdate"},
	{0xB57E95D9, &WrapI_V<sceUtilityGamedataInstallGetStatus>, "sceUtilityGamedataInstallGetStatus"},
	{0x180F7B62, 0, "sceUtilityGamedataInstallAbortFunction"},

	{0x16D02AF0, 0, "sceUtilityNpSigninInitStart"},
	{0xE19C97D6, 0, "sceUtilityNpSigninShutdownStart"},
	{0xF3FBC572, 0, "sceUtilityNpSigninUpdate"},
	{0x86ABDB1B, 0, "sceUtilityNpSigninGetStatus"},

	{0x1281DA8E, 0, "sceUtilityInstallInitStart"},
	{0x5EF1C24A, 0, "sceUtilityInstallShutdownStart"},
	{0xA03D29BA, 0, "sceUtilityInstallUpdate"},
	{0xC4700FA3, 0, "sceUtilityInstallGetStatus"}, 

};

void Register_sceUtility()
{
	RegisterModule("sceUtility", ARRAY_SIZE(sceUtility), sceUtility);
}
