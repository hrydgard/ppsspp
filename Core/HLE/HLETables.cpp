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

#include "HLETables.h"

#include "sceCtrl.h"
#include "sceDisplay.h"
#include "sceHttp.h"
#include "sceAtrac.h"
#include "sceIo.h"
#include "sceHprm.h"
#include "scePower.h"
#include "sceFont.h"
#include "sceNet.h"
#include "sceMpeg.h"
#include "sceImpose.h"
#include "sceGe.h"
#include "scePsmf.h"
#include "sceRtc.h"
#include "sceSas.h"
#include "sceUmd.h"
#include "sceDmac.h"
#include "sceRtc.h"
#include "sceOpenPSID.h"
#include "sceKernel.h"
#include "sceKernelEventFlag.h"
#include "sceKernelMemory.h"
#include "sceKernelInterrupt.h"
#include "sceKernelModule.h"
#include "sceKernelSemaphore.h"
#include "sceKernelThread.h"
#include "sceKernelTime.h"
#include "sceAudio.h"
#include "sceUtility.h"
#include "sceParseUri.h"
#include "sceSsl.h"
#include "sceParseHttp.h"
#include "sceVaudio.h"
#include "sceUsb.h"
#include "sceChnnlsv.h"
#include "scePspNpDrm_user.h"
#include "sceP3da.h"
#include "sceGameUpdate.h"
#include "sceDeflt.h"
#include "sceMp4.h"
#include "sceMp3.h"
#include "scePauth.h"
#include "sceNp.h"
#include "sceMd5.h"
#include "sceJpeg.h"
#include "sceAudiocodec.h"

#define N(s) s

//\*\*\ found\:\ {[a-zA-Z]*}\ {0x[a-zA-Z0-9]*}\ \*\*
//{FID(\2),0,N("\1")},

//Metal Gear Acid modules:
//kjfs
//sound
//zlibdec
const HLEFunction FakeSysCalls[] = {
	{NID_THREADRETURN, __KernelReturnFromThread, "__KernelReturnFromThread"},
	{NID_CALLBACKRETURN, __KernelReturnFromMipsCall, "__KernelReturnFromMipsCall"},
	{NID_INTERRUPTRETURN, __KernelReturnFromInterrupt, "__KernelReturnFromInterrupt"},
	{NID_EXTENDRETURN, __KernelReturnFromExtendStack, "__KernelReturnFromExtendStack"},
	{NID_MODULERETURN, __KernelReturnFromModuleFunc, "__KernelReturnFromModuleFunc"},
	{NID_IDLE, __KernelIdle, "_sceKernelIdle"},
};

const HLEFunction UtilsForUser[] = 
{
	{0x91E4F6A7, WrapU_V<sceKernelLibcClock>, "sceKernelLibcClock"},
	{0x27CC57F0, WrapU_U<sceKernelLibcTime>, "sceKernelLibcTime"},
	{0x71EC4271, WrapU_UU<sceKernelLibcGettimeofday>, "sceKernelLibcGettimeofday"},
	{0xBFA98062, WrapI_UI<sceKernelDcacheInvalidateRange>, "sceKernelDcacheInvalidateRange"},
	{0xC8186A58, 0, "sceKernelUtilsMd5Digest"},
	{0x9E5C5086, 0, "sceKernelUtilsMd5BlockInit"},
	{0x61E1E525, 0, "sceKernelUtilsMd5BlockUpdate"},
	{0xB8D24E78, 0, "sceKernelUtilsMd5BlockResult"},
	{0x840259F1, 0, "sceKernelUtilsSha1Digest"},
	{0xF8FCD5BA, 0, "sceKernelUtilsSha1BlockInit"},
	{0x346F6DA8, 0, "sceKernelUtilsSha1BlockUpdate"},
	{0x585F1C09, 0, "sceKernelUtilsSha1BlockResult"},
	{0xE860E75E, WrapU_UU<sceKernelUtilsMt19937Init>, "sceKernelUtilsMt19937Init"},
	{0x06FB8A63, WrapU_U<sceKernelUtilsMt19937UInt>, "sceKernelUtilsMt19937UInt"},
	{0x37FB5C42, WrapU_V<sceKernelGetGPI>, "sceKernelGetGPI"},
	{0x6AD345D7, WrapV_U<sceKernelSetGPO>, "sceKernelSetGPO"},
	{0x79D1C3FA, WrapI_V<sceKernelDcacheWritebackAll>, "sceKernelDcacheWritebackAll"},
	{0xB435DEC5, WrapI_V<sceKernelDcacheWritebackInvalidateAll>, "sceKernelDcacheWritebackInvalidateAll"},
	{0x3EE30821, WrapI_UI<sceKernelDcacheWritebackRange>, "sceKernelDcacheWritebackRange"},
	{0x34B9FA9E, WrapI_UI<sceKernelDcacheWritebackInvalidateRange>, "sceKernelDcacheWritebackInvalidateRange"},
	{0xC2DF770E, WrapI_UI<sceKernelIcacheInvalidateRange>, "sceKernelIcacheInvalidateRange"},
	{0x80001C4C, 0, "sceKernelDcacheProbe"},
	{0x16641D70, 0, "sceKernelDcacheReadTag"},
	{0x4FD31C9D, 0, "sceKernelIcacheProbe"},
	{0xFB05FAD0, 0, "sceKernelIcacheReadTag"},
	{0x920f104a, WrapU_V<sceKernelIcacheInvalidateAll>, "sceKernelIcacheInvalidateAll"}
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
	{0x54AB2675, 0, "sceKernelApplyPspRelSection"},
	{0x2952F5AC, 0, "sceKernelDcacheWBinvAll"},
	{0xD8779AC6, WrapU_V<sceKernelIcacheClearAll>, "sceKernelIcacheClearAll"},
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
	{0xCF8A41B1, WrapU_C<sceKernelFindModuleByName>,"sceKernelFindModuleByName"},
};


const HLEFunction KDebugForKernel[] = 
{
	{0xE7A3874D, 0, "sceKernelRegisterAssertHandler"},
	{0x2FF4E9F9, 0, "sceKernelAssert"},
	{0x9B868276, 0, "sceKernelGetDebugPutchar"},
	{0xE146606D, 0, "sceKernelRegisterDebugPutchar"},
	{0x7CEB2C09, WrapU_V<sceKernelRegisterKprintfHandler>, "sceKernelRegisterKprintfHandler"},
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


const HLEModule moduleList[] = 
{
	{"FakeSysCalls", SZ(FakeSysCalls), FakeSysCalls},
	{"UtilsForUser",SZ(UtilsForUser),UtilsForUser},
	{"KDebugForKernel",SZ(KDebugForKernel),KDebugForKernel},
	{"sceSAScore"},
	{"SceBase64_Library"},
	{"sceCert_Loader"},
	{"SceFont_Library"},
	{"sceNetApctl"},
	{"sceSIRCS_IrDA_Driver"},
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
	Register_sceFont();
	Register_sceNet();
	Register_sceRtc();
	Register_sceWlanDrv();
	Register_sceMpeg();
	Register_sceMp3();
	Register_sceHttp();
	Register_scePower();
	Register_sceImpose();
	Register_sceSuspendForUser();
	Register_sceGe_user();
	Register_sceUmdUser();
	Register_sceDmac();
	Register_sceUtility();
	Register_sceAtrac3plus();
	Register_scePsmf();
	Register_scePsmfPlayer();
	Register_sceOpenPSID();
	Register_sceParseUri();
	Register_sceSsl();
	Register_sceParseHttp();
	Register_sceVaudio();
	Register_sceUsb();
	Register_sceChnnlsv();
	Register_sceNpDrm();
	Register_sceP3da();
	Register_sceGameUpdate();
	Register_sceDeflt();
	Register_sceMp4();
	Register_scePauth();
	Register_sceNp();
	Register_sceNpCommerce2();
	Register_sceNpService();
	Register_sceNpAuth();
	Register_sceMd5();
	Register_sceJpeg();
	Register_sceAudiocodec();

	for (int i = 0; i < numModules; i++)
	{
		RegisterModule(moduleList[i].name, moduleList[i].numFunctions, moduleList[i].funcTable);
	}
}

