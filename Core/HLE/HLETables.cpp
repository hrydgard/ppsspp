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

#include "sceAtrac.h"
#include "sceAudio.h"
#include "sceAudiocodec.h"
#include "sceCcc.h"
#include "sceChnnlsv.h"
#include "sceCtrl.h"
#include "sceDeflt.h"
#include "sceDisplay.h"
#include "sceDmac.h"
#include "sceFont.h"
#include "sceGameUpdate.h"
#include "sceGe.h"
#include "sceHeap.h"
#include "sceHprm.h"
#include "sceHttp.h"
#include "sceImpose.h"
#include "sceIo.h"
#include "sceJpeg.h"
#include "sceKernel.h"
#include "sceKernelEventFlag.h"
#include "sceKernelMemory.h"
#include "sceKernelInterrupt.h"
#include "sceKernelModule.h"
#include "sceKernelSemaphore.h"
#include "sceKernelThread.h"
#include "sceKernelTime.h"
#include "sceMd5.h"
#include "sceMp4.h"
#include "sceMp3.h"
#include "sceNet.h"
#include "sceNetAdhoc.h"
#include "sceNp.h"
#include "sceMpeg.h"
#include "sceOpenPSID.h"
#include "sceP3da.h"
#include "sceParseHttp.h"
#include "sceParseUri.h"
#include "scePauth.h"
#include "scePower.h"
#include "scePspNpDrm_user.h"
#include "scePsmf.h"
#include "sceRtc.h"
#include "sceSas.h"
#include "sceSsl.h"
#include "sceUmd.h"
#include "sceUsb.h"
#include "sceUtility.h"
#include "sceVaudio.h"
#include "sceMt19937.h"

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
	{0xC8186A58, WrapI_UIU<sceKernelUtilsMd5Digest>, "sceKernelUtilsMd5Digest"},
	{0x9E5C5086, WrapI_U<sceKernelUtilsMd5BlockInit>, "sceKernelUtilsMd5BlockInit"},
	{0x61E1E525, WrapI_UUI<sceKernelUtilsMd5BlockUpdate>, "sceKernelUtilsMd5BlockUpdate"},
	{0xB8D24E78, WrapI_UU<sceKernelUtilsMd5BlockResult>, "sceKernelUtilsMd5BlockResult"},
	{0x840259F1, WrapI_UIU<sceKernelUtilsSha1Digest>, "sceKernelUtilsSha1Digest"},
	{0xF8FCD5BA, WrapI_U<sceKernelUtilsSha1BlockInit>, "sceKernelUtilsSha1BlockInit"},
	{0x346F6DA8, WrapI_UUI<sceKernelUtilsSha1BlockUpdate>, "sceKernelUtilsSha1BlockUpdate"},
	{0x585F1C09, WrapI_UU<sceKernelUtilsSha1BlockResult>, "sceKernelUtilsSha1BlockResult"},
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
	{0xb29ddf9c, 0, "sceIoDopen"},
	{0xe3eb004c, 0, "sceIoDread"},
	{0xeb092469, 0, "sceIoDclose"},
	{0x109f50bc, 0, "sceIoOpen"},
	{0x6a638d83, 0, "sceIoRead"},
	{0x42ec03ac, 0, "sceIoWrite"},
	{0x68963324, 0, "sceIoLseek32"},
	{0x27eb27b8, 0, "sceIoLseek"},
	{0x810c4bc3, 0, "sceIoClose"},
	{0x779103a0, 0, "sceIoRename"},
	{0xf27a9c51, 0, "sceIoRemove"},
	{0x55f4717d, 0, "sceIoChdir"},
	{0x06a70004, 0, "sceIoMkdir"},
	{0x1117c65f, 0, "sceIoRmdir"},
	{0x54f5fb11, 0, "sceIoDevctl"},
	{0x63632449, 0, "sceIoIoctl"},
	{0xab96437f, 0, "sceIoSync"},
	{0xb2a628c1, 0, "sceIoAssign"},
	{0x6d08a871, 0, "sceIoUnassign"},
	{0xace946e8, 0, "sceIoGetstat"},
	{0xb8a740f4, 0, "sceIoChstat"},
	{0xa0b5a7c2, 0, "sceIoReadAsync"},
	{0x3251ea56, 0, "sceIoPollAsync"},
	{0xe23eec33, 0, "sceIoWaitAsync"},
	{0x35dbd746, 0, "sceIoWaitAsyncCB"},
	{0xbd17474f, 0, "IoFileMgrForKernel_BD17474F"},
	{0x76da16e3, 0, "IoFileMgrForKernel_76DA16E3"},
};
const HLEFunction StdioForKernel[] = 
{
	{0x98220F3E, 0, "sceKernelStdoutReopen"},
	{0xFB5380C5, 0, "sceKernelStderrReopen"},
	{0xcab439df, 0, "printf"},
	{0x2CCF071A, 0, "fdprintf"},
	{0xd97c8cb9, 0, "puts"},
	{0x172D316E, 0, "sceKernelStdin"},
	{0xA6BAB2E9, 0, "sceKernelStdout"},
	{0xF78BA90A, 0, "sceKernelStderr"},
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
	{0xb95fa50d, 0, "LoadCoreForKernel_B95FA50D"},
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
	{0x24C32559, 0, "sceKernelDipsw"},
	{0xD636B827, 0, "sceKernelRemoveByDebugSection"},
	{0x5282DD5E, 0, "sceKernelDipswSet"},
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
	Register_ThreadManForKernel();
	Register_LoadExecForUser();
	Register_UtilsForKernel();
	Register_SysMemUserForUser();
	Register_InterruptManager();
	Register_IoFileMgrForUser();
	Register_ModuleMgrForUser();
	Register_ModuleMgrForKernel();
	Register_StdioForUser();

	Register_sceHprm();
	Register_sceCcc();
	Register_sceCtrl();
	Register_sceDisplay();
	Register_sceAudio();
	Register_sceSasCore();
	Register_sceFont();
	Register_sceNet();
	Register_sceNetAdhoc();
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
	Register_sceHeap();

	for (int i = 0; i < numModules; i++)
	{
		RegisterModule(moduleList[i].name, moduleList[i].numFunctions, moduleList[i].funcTable);
	}

	// New modules have to be added at the end, or they will break savestates.
	Register_LoadExecForKernel();
	Register_SysMemForKernel();
	Register_sceMt19937();
}

