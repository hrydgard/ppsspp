// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "HLE.h"

#include "HLETables.h"

#include "sceCtrl.h"
#include "sceDisplay.h"
#include "sceHttp.h"
#include "sceAtrac.h"
#include "sceIo.h"
#include "sceHprm.h"
#include "scePower.h"
#include "sceNet.h"
#include "sceMpeg.h"
#include "sceGe.h"
#include "scePsmf.h"
#include "sceRtc.h"
#include "sceSas.h"
#include "sceUmd.h"
#include "sceDmac.h"
#include "sceKernel.h"
#include "sceKernelEventFlag.h"
#include "sceKernelCallback.h"
#include "sceKernelMemory.h"
#include "sceKernelInterrupt.h"
#include "sceKernelModule.h"
#include "sceKernelSemaphore.h"
#include "sceKernelThread.h"
#include "sceKernelTime.h"
#include "sceAudio.h"
#include "sceUtility.h"

#define N(s) s

//\*\*\ found\:\ {[a-zA-Z]*}\ {0x[a-zA-Z0-9]*}\ \*\*
//{FID(\2),0,N("\1")},

//Metal Gear Acid modules:
//kjfs
//sound
//zlibdec
const HLEFunction FakeSysCalls[] =
{
  {NID_THREADRETURN, _sceKernelReturnFromThread, "_sceKernelReturnFromThread"},
  {NID_CALLBACKRETURN, _sceKernelReturnFromCallback, "_sceKernelReturnFromCallback"},
	{NID_INTERRUPTRETURN, _sceKernelReturnFromInterrupt, "_sceKernelReturnFromInterrupt"},
  {NID_IDLE, _sceKernelIdle, "_sceKernelIdle"},
};

const HLEFunction UtilsForUser[] = 
{
	{0x91E4F6A7, &Wrap<sceKernelLibcClock>, "sceKernelLibcClock"},
	{0x27CC57F0, &Wrap<sceKernelLibcTime>, "sceKernelLibcTime"},
	{0x71EC4271, &Wrap<sceKernelLibcGettimeofday>, "sceKernelLibcGettimeofday"},
	{0xBFA98062, 0, "sceKernelDcacheInvalidateRange"},
	{0xC8186A58, 0, "sceKernelUtilsMd5Digest"},
	{0x9E5C5086, 0, "sceKernelUtilsMd5BlockInit"},
	{0x61E1E525, 0, "sceKernelUtilsMd5BlockUpdate"},
	{0xB8D24E78, 0, "sceKernelUtilsMd5BlockResult"},
	{0x840259F1, 0, "sceKernelUtilsSha1Digest"},
	{0xF8FCD5BA, 0, "sceKernelUtilsSha1BlockInit"},
	{0x346F6DA8, 0, "sceKernelUtilsSha1BlockUpdate"},
	{0x585F1C09, 0, "sceKernelUtilsSha1BlockResult"},
	{0xE860E75E, 0, "sceKernelUtilsMt19937Init"},
	{0x06FB8A63, 0, "sceKernelUtilsMt19937UInt"},
	{0x37FB5C42, &Wrap<sceKernelGetGPI>, "sceKernelGetGPI"},
	{0x6AD345D7, &Wrap<sceKernelSetGPO>, "sceKernelSetGPO"},
	{0x79D1C3FA, &Wrap<sceKernelDcacheWritebackAll>, "sceKernelDcacheWritebackAll"},
	{0xB435DEC5, &Wrap<sceKernelDcacheWritebackInvalidateAll>, "sceKernelDcacheWritebackInvalidateAll"},
	{0x3EE30821, &Wrap<sceKernelDcacheWritebackRange>, "sceKernelDcacheWritebackRange"},
	{0x34B9FA9E, &Wrap<sceKernelDcacheWritebackInvalidateRange>, "sceKernelDcacheWritebackInvalidateRange"},
	{0x80001C4C, 0, "sceKernelDcacheProbe"},
	{0x16641D70, 0, "sceKernelDcacheReadTag"},
	{0x4FD31C9D, 0, "sceKernelIcacheProbe"},
	{0xFB05FAD0, 0, "sceKernelIcacheReadTag"},
	{0x920f104a, &Wrap<sceKernelIcacheInvalidateAll>, "sceKernelIcacheInvalidateAll"}
};				   


const HLEFunction sceRtc[] = 
{
  {0xC41C2853, &Wrap<sceRtcGetTickResolution>, "sceRtcGetTickResolution"},
  {0x3f7ad767, &Wrap<sceRtcGetCurrentTick>, "sceRtcGetCurrentTick"},	
  {0x011F03C1, 0, "sceRtcGetAccumulativeTime"},
  {0x029CA3B3, 0, "sceRtcGetAccumlativeTime"},
  {0x4cfa57b0, 0, "sceRtcGetCurrentClock"},
  {0xE7C27D1B, &Wrap<sceRtcGetCurrentClockLocalTime>, "sceRtcGetCurrentClockLocalTime"},
  {0x34885E0D, 0, "sceRtcConvertUtcToLocalTime"},
  {0x779242A2, 0, "sceRtcConvertLocalTimeToUTC"},
  {0x42307A17, 0, "sceRtcIsLeapYear"},
  {0x05ef322c, 0, "sceRtcGetDaysInMonth"},
  {0x57726bc1, 0, "sceRtcGetDayOfWeek"},
  {0x4B1B5E82, 0, "sceRtcCheckValid"},
  {0x3a807cc8, 0, "sceRtcSetTime_t"},
  {0x27c4594c, 0, "sceRtcGetTime_t"},
  {0xF006F264, 0, "sceRtcSetDosTime"},
  {0x36075567, 0, "sceRtcGetDosTime"},
  {0x7ACE4C04, 0, "sceRtcSetWin32FileTime"},
  {0xCF561893, 0, "sceRtcGetWin32FileTime"},
  {0x7ED29E40, 0, "sceRtcSetTick"},
  {0x6FF40ACC, &Wrap<sceRtcGetTick>, "sceRtcGetTick"},
  {0x9ED0AE87, 0, "sceRtcCompareTick"},
  {0x44F45E05, 0, "sceRtcTickAddTicks"},
  {0x26D25A5D, 0, "sceRtcTickAddMicroseconds"},
  {0xF2A4AFE5, 0, "sceRtcTickAddSeconds"},
  {0xE6605BCA, 0, "sceRtcTickAddMinutes"},
  {0x26D7A24A, 0, "sceRtcTickAddHours"},
  {0xE51B4B7A, 0, "sceRtcTickAddDays"},
  {0xCF3A2CA8, 0, "sceRtcTickAddWeeks"},
  {0xDBF74F1B, 0, "sceRtcTickAddMonths"},
  {0x42842C77, 0, "sceRtcTickAddYears"},
  {0xC663B3B9, 0, "sceRtcFormatRFC2822"},
  {0x7DE6711B, 0, "sceRtcFormatRFC2822LocalTime"},
  {0x0498FB3C, 0, "sceRtcFormatRFC3339"},
  {0x27F98543, 0, "sceRtcFormatRFC3339LocalTime"},
  {0xDFBC5F16, 0, "sceRtcParseDateTime"},
  {0x28E1E988, 0, "sceRtcParseRFC3339"},
};

const HLEFunction IoFileMgrForKernel[] =
{
  {0xa905b705, 0, "sceIoCloseAll"},
  {0x411106BA, 0, "sceIoGetThreadCwd"},
  {0xCB0A151F, 0, "sceIoChangeThreadCwd"},
  {0x8E982A74, 0, "sceIoAddDrv"},
  {0xC7F35804, 0, "sceIoDelDrv"},
  {0x3C54E908, 0, "sceIoReopen"},
};
const HLEFunction StdioForKernel[] = 
{
  {0x98220F3E, 0, "sceKernelStdoutReopen"},
  {0xFB5380C5, 0, "sceKernelStderrReopen"},
  {0x2CCF071A, 0, "fdprintf"},
};
const HLEFunction LoadCoreForKernel[] = 
{
  {0xACE23476, 0, "sceKernelCheckPspConfig"},
  {0x7BE1421C, 0, "sceKernelCheckExecFile"},
  {0xBF983EF2, 0, "sceKernelProbeExecutableObject"},
  {0x7068E6BA, 0, "sceKernelLoadExecutableObject"},
  {0xB4D6FECC, 0, "sceKernelApplyElfRelSection"},
  {0x54AB2675, 0, "LoadCoreForKernel_54AB2675"},
  {0x2952F5AC, 0, "sceKernelDcacheWBinvAll"},
  {0xD8779AC6, 0, "sceKernelIcacheClearAll"},
  {0x99A695F0, 0, "sceKernelRegisterLibrary"},
  {0x5873A31F, 0, "sceKernelRegisterLibraryForUser"},
  {0x0B464512, 0, "sceKernelReleaseLibrary"},
  {0x9BAF90F6, 0, "sceKernelCanReleaseLibrary"},
  {0x0E760DBA, 0, "sceKernelLinkLibraryEntries"},
  {0x0DE1F600, 0, "sceKernelLinkLibraryEntriesForUser"},
  {0xDA1B09AA, 0, "sceKernelUnLinkLibraryEntries"},
  {0xC99DD47A, 0, "sceKernelQueryLoadCoreCB"},
  {0x616FCCCD, 0, "sceKernelSetBootCallbackLevel"},
  {0xF32A2940, 0, "sceKernelModuleFromUID"},
  {0xCD0F3BAC, 0, "sceKernelCreateModule"},
  {0x6B2371C2, 0, "sceKernelDeleteModule"},
  {0x7320D964, 0, "sceKernelModuleAssign"},
  {0x44B292AB, 0, "sceKernelAllocModule"},
  {0xBD61D4D5, 0, "sceKernelFreeModule"},
  {0xAE7C6E76, 0, "sceKernelRegisterModule"},
  {0x74CF001A, 0, "sceKernelReleaseModule"},
  {0xFB8AE27D, 0, "sceKernelFindModuleByAddress"},
  {0xCCE4A157, 0, "sceKernelFindModuleByUID"},
  {0x82CE54ED, 0, "sceKernelModuleCount"},
  {0xC0584F0C, 0, "sceKernelGetModuleList"},
  {0xCF8A41B1, &Wrap<sceKernelFindModuleByName>,"sceKernelFindModuleByName"},
};


const HLEFunction KDebugForKernel[] = 
{
	{0xE7A3874D, 0, "sceKernelRegisterAssertHandler"},
	{0x2FF4E9F9, 0, "sceKernelAssert"},
	{0x9B868276, 0, "sceKernelGetDebugPutchar"},
	{0xE146606D, 0, "sceKernelRegisterDebugPutchar"},
	{0x7CEB2C09, &Wrap<sceKernelRegisterKprintfHandler>, "sceKernelRegisterKprintfHandler"},
	{0x84F370BC, 0, "Kprintf"},
	{0x5CE9838B, 0, "sceKernelDebugWrite"},
	{0x66253C4E, 0, "sceKernelRegisterDebugWrite"},
	{0xDBB5597F, 0, "sceKernelDebugRead"},
	{0xE6554FDA, 0, "sceKernelRegisterDebugRead"},
	{0xB9C643C9, 0, "sceKernelDebugEcho"},
	{0x7D1C74F0, 0, "sceKernelDebugEchoSet"},
	{0x24C32559, 0, "KDebugForKernel_24C32559"},
	{0xD636B827, 0, "sceKernelRemoveByDebugSection"},
	{0x5282DD5E, 0, "KDebugForKernel_5282DD5E"},
	{0x9F8703E4, 0, "KDebugForKernel_9F8703E4"},
	{0x333DCEC7, 0, "KDebugForKernel_333DCEC7"},
	{0xE892D9A1, 0, "KDebugForKernel_E892D9A1"},
	{0xA126F497, 0, "KDebugForKernel_A126F497"},
	{0xB7251823, 0, "sceKernelAcceptMbogoSig"},
};


#define SZ(a) sizeof(a)/sizeof(HLEFunction)

const HLEFunction pspeDebug[] = 
{
	{0xDEADBEAF, 0, "pspeDebugWrite"},
};


const HLEFunction sceUsb[] = 
{
	{0xae5de6af, 0, "sceUsbStart"},
	{0xc2464fa0, 0, "sceUsbStop"},
	{0xc21645a4, 0, "sceUsbGetState"},
	{0x4e537366, 0, "sceUsbGetDrvList"},
	{0x112cc951, 0, "sceUsbGetDrvState"},
	{0x586db82c, 0, "sceUsbActivate"},
	{0xc572a9c8, 0, "sceUsbDeactivate"},
	{0x5be0e002, 0, "sceUsbWaitState"},
	{0x1c360735, 0, "sceUsbWaitCancel"},
};
	

const HLEFunction sceLibFont[] = 
{
	{0x67f17ed7, 0, "sceFontNewLib"},	
	{0x574b6fbc, 0, "sceFontDoneLib"},
	{0x48293280, 0, "sceFontSetResolution"},	
	{0x27f6e642, 0, "sceFontGetNumFontList"},
	{0xbc75d85b, 0, "sceFontGetFontList"},	
	{0x099ef33c, 0, "sceFontFindOptimumFont"},	
	{0x681e61a7, 0, "sceFontFindFont"},	
	{0x2f67356a, 0, "sceFontCalcMemorySize"},	
	{0x5333322d, 0, "sceFontGetFontInfoByIndexNumber"},
	{0xa834319d, 0, "sceFontOpen"},	
	{0x57fcb733, 0, "sceFontOpenUserFile"},	
	{0xbb8e7fe6, 0, "sceFontOpenUserMemory"},	
	{0x3aea8cb6, 0, "sceFontClose"},	
	{0x0da7535e, 0, "sceFontGetFontInfo"},	
	{0xdcc80c2f, 0, "sceFontGetCharInfo"},	
	{0x5c3e4a9e, 0, "sceFontGetCharImageRect"},	
	{0x980f4895, 0, "sceFontGetCharGlyphImage"},	
	{0xca1e6945, 0, "sceFontGetCharGlyphImage_Clip"},
	{0x74b21701, 0, "sceFontPixelToPointH"},	
	{0xf8f0752e, 0, "sceFontPixelToPointV"},	
	{0x472694cd, 0, "sceFontPointToPixelH"},	
	{0x3c4b7e82, 0, "sceFontPointToPixelV"},	
	{0xee232411, 0, "sceFontSetAltCharacterCode"},
	{0xaa3de7b5, 0, "sceFontGetShadowInfo"}, 	 
	{0x48b06520, 0, "sceFontGetShadowImageRect"},
	{0x568be516, 0, "sceFontGetShadowGlyphImage"},
  {0x5dcf6858, 0, "sceFontGetShadowGlyphImage_Clip"},
	{0x02d7f94b, 0, "sceFontFlush"},

};

const HLEFunction sceUsbstor[] =
{
	{0x60066CFE, 0, "sceUsbstorGetStatus"},
};

const HLEFunction sceUsbstorBoot[] =
{
	{0xE58818A8, 0, "sceUsbstorBootSetCapacity"},
	{0x594BBF95, 0, "sceUsbstorBootSetLoadAddr"},
	{0x6D865ECD, 0, "sceUsbstorBootGetDataSize"},
	{0xA1119F0D, 0, "sceUsbstorBootSetStatus"},
	{0x1F080078, 0, "sceUsbstorBootRegisterNotify"},
	{0xA55C9E16, 0, "sceUsbstorBootUnregisterNotify"},
};

//OSD stuff? home button?
const HLEFunction sceImpose[] =
{
	{0x36aa6e91, 0, "sceImposeSetLanguageMode"},  // Seen
	{0x381bd9e7, 0, "sceImposeHomeButton"},
	{0x24fd7bcf, 0, "sceImposeGetLanguageMode"},
	{0x8c943191, 0, "sceImposeGetBatteryIconStatus"},
};

const HLEFunction sceOpenPSID[] = 
{
  {0xc69bebce, 0, "sceOpenPSID_c69bebce"},
};


const HLEModule moduleList[] = 
{
	{"FakeSysCalls", SZ(FakeSysCalls), FakeSysCalls},
  {"sceOpenPSID", SZ(sceOpenPSID), sceOpenPSID},
	{"UtilsForUser",SZ(UtilsForUser),UtilsForUser},
	{"KDebugForKernel",SZ(KDebugForKernel),KDebugForKernel},
	{"sceParseUri"},
	{"sceRtc",SZ(sceRtc),sceRtc},
	{"sceSAScore"},
	{"sceUsbstor",SZ(sceUsbstor),sceUsbstor},
	{"sceUsbstorBoot",SZ(sceUsbstorBoot),sceUsbstorBoot},
	{"sceUsb", SZ(sceUsb), sceUsb},
	{"SceBase64_Library"},
	{"sceCert_Loader"},
	{"SceFont_Library"},
	{"sceLibFont",SZ(sceLibFont),sceLibFont},
	{"sceNetApctl"},
	{"sceOpenPSID"},
	{"sceParseHttp"},
	{"sceSsl"},
	{"sceSIRCS_IrDA_Driver"},
	{"sceRtc"},
	{"sceImpose",SZ(sceImpose),sceImpose}, //r: [UNK:36aa6e91] : 08b2cd68		//305: [MIPS32 R4K 00000000 ]: Loader: [UNK:24fd7bcf] : 08b2cd70
	{"Pspnet_Scan"},
	{"Pspnet_Show_MacAddr"},
	{"pspeDebug", SZ(pspeDebug), pspeDebug},
	{"StdioForKernel", SZ(StdioForKernel), StdioForKernel},
	{"LoadCoreForKernel", SZ(LoadCoreForKernel), LoadCoreForKernel},
	{"IoFileMgrForKernel", SZ(IoFileMgrForKernel), IoFileMgrForKernel},
};

static const int numModules = sizeof(moduleList)/sizeof(HLEModule);

void RegisterAllModules() {
  Register_Kernel_Library();
  Register_ThreadManForUser();
  Register_LoadExecForUser();
  Register_SysMemUserForUser();
  Register_InterruptManager();
  Register_IoFileMgrForUser();
  Register_ModuleMgrForUser();
  Register_StdioForUser();

  Register_sceHprm();
  Register_sceCtrl();
  Register_sceDisplay();
  Register_sceAudio();
  Register_sceSasCore();
  Register_sceNet();
  Register_sceWlanDrv();
  Register_sceMpeg();
  Register_sceMp3();
  Register_sceHttp();
  Register_scePower();
  Register_sceSuspendForUser();
  Register_sceGe_user();
  Register_sceUmdUser();
  Register_sceDmac();
  Register_sceUtility();
  Register_sceAtrac3plus();
  Register_scePsmf();
  Register_scePsmfPlayer();

  for (int i = 0; i < numModules; i++)
  {
    RegisterModule(moduleList[i].name, moduleList[i].numFunctions, moduleList[i].funcTable);
  }
}

