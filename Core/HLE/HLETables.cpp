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
#include "Core/HLE/HLETables.h"
#include "Core/HLE/FunctionWrappers.h"

#include "sceAtrac.h"
#include "sceAudio.h"
#include "sceAudiocodec.h"
#include "sceAudioRouting.h"
#include "sceCcc.h"
#include "sceChnnlsv.h"
#include "sceCtrl.h"
#include "sceChkreg.h"
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
#include "sceKernelHeap.h"
#include "sceKernelMemory.h"
#include "sceKernelInterrupt.h"
#include "sceKernelModule.h"
#include "sceKernelSemaphore.h"
#include "sceKernelThread.h"
#include "sceKernelTime.h"
#include "sceMd5.h"
#include "sceMp4.h"
#include "sceAac.h"
#include "sceMp3.h"
#include "sceNet.h"
#include "sceNetAdhoc.h"
#include "sceNetAdhocMatching.h"
#include "sceNp.h"
#include "sceMpeg.h"
#include "sceMpegbase.h"
#include "sceOpenPSID.h"
#include "sceResmgr.h"
#include "sceP3da.h"
#include "sceParseHttp.h"
#include "sceParseUri.h"
#include "scePauth.h"
#include "scePower.h"
#include "scePspNpDrm_user.h"
#include "scePsmf.h"
#include "sceReg.h"
#include "sceRtc.h"
#include "sceSas.h"
#include "sceSircs.h"
#include "sceSsl.h"
#include "sceUmd.h"
#include "sceUsb.h"
#include "sceUsbAcc.h"
#include "sceUsbCam.h"
#include "sceUsbGps.h"
#include "sceUsbMic.h"
#include "sceUtility.h"
#include "sceVaudio.h"
#include "sceVshBridge.h"
#include "sceMt19937.h"
#include "sceSha256.h"
#include "sceAdler.h"
#include "sceSfmt19937.h"
#include "sceG729.h"
#include "KUBridge.h"
#include "PopsBoot.h"
#include "sceAmctrl.h"
#include "sceNetInet.h"
#include "sceNetResolver.h"
// #include "sceNp2.h"
#include "sceNet_lib.h"
#include "sceVideocodec.h"
#include "sceSysreg.h"

static const HLEFunction FakeSysCalls[] = {
	{NID_THREADRETURN, __KernelReturnFromThread, "__KernelReturnFromThread", 'x', ""},
	{NID_CALLBACKRETURN, __KernelReturnFromMipsCall, "__KernelReturnFromMipsCall", 'x', ""},
	{NID_INTERRUPTRETURN, __KernelReturnFromInterrupt, "__KernelReturnFromInterrupt", 'x', ""},
	{NID_EXTENDRETURN, __KernelReturnFromExtendStack, "__KernelReturnFromExtendStack", 'x', ""},
	{NID_MODULERETURN, __KernelReturnFromModuleFunc, "__KernelReturnFromModuleFunc", 'x', ""},
	{NID_IDLE, __KernelIdle, "_sceKernelIdle", 'x', ""},
	{NID_GPUREPLAY, &WrapI_V<__KernelGPUReplay>, "__KernelGPUReplay", 'x', ""},
	{NID_HLECALLRETURN, HLEReturnFromMipsCall, "HLEReturnFromMipsCall", 'x', ""},
};

static const HLEFunction UtilsForUser[] = {
	{0X91E4F6A7, &WrapU_V<sceKernelLibcClock>,                       "sceKernelLibcClock",                      'x', ""   },
	{0X27CC57F0, &WrapU_U<sceKernelLibcTime>,                        "sceKernelLibcTime",                       'x', "x"  },
	{0X71EC4271, &WrapU_UU<sceKernelLibcGettimeofday>,               "sceKernelLibcGettimeofday",               'x', "xx" },
	{0XBFA98062, &WrapI_UI<sceKernelDcacheInvalidateRange>,          "sceKernelDcacheInvalidateRange",          'i', "xi" },
	{0XC8186A58, &WrapI_UIU<sceKernelUtilsMd5Digest>,                "sceKernelUtilsMd5Digest",                 'i', "xix"},
	{0X9E5C5086, &WrapI_U<sceKernelUtilsMd5BlockInit>,               "sceKernelUtilsMd5BlockInit",              'i', "x"  },
	{0X61E1E525, &WrapI_UUI<sceKernelUtilsMd5BlockUpdate>,           "sceKernelUtilsMd5BlockUpdate",            'i', "xxi"},
	{0XB8D24E78, &WrapI_UU<sceKernelUtilsMd5BlockResult>,            "sceKernelUtilsMd5BlockResult",            'i', "xx" },
	{0X840259F1, &WrapI_UIU<sceKernelUtilsSha1Digest>,               "sceKernelUtilsSha1Digest",                'i', "xix"},
	{0XF8FCD5BA, &WrapI_U<sceKernelUtilsSha1BlockInit>,              "sceKernelUtilsSha1BlockInit",             'i', "x"  },
	{0X346F6DA8, &WrapI_UUI<sceKernelUtilsSha1BlockUpdate>,          "sceKernelUtilsSha1BlockUpdate",           'i', "xxi"},
	{0X585F1C09, &WrapI_UU<sceKernelUtilsSha1BlockResult>,           "sceKernelUtilsSha1BlockResult",           'i', "xx" },
	{0XE860E75E, &WrapU_UU<sceKernelUtilsMt19937Init>,               "sceKernelUtilsMt19937Init",               'x', "xx" },
	{0X06FB8A63, &WrapU_U<sceKernelUtilsMt19937UInt>,                "sceKernelUtilsMt19937UInt",               'x', "x"  },
	{0X37FB5C42, &WrapU_V<sceKernelGetGPI>,                          "sceKernelGetGPI",                         'x', ""   },
	{0X6AD345D7, &WrapV_U<sceKernelSetGPO>,                          "sceKernelSetGPO",                         'v', "x"  },
	{0X79D1C3FA, &WrapI_V<sceKernelDcacheWritebackAll>,              "sceKernelDcacheWritebackAll",             'i', ""   },
	{0XB435DEC5, &WrapI_V<sceKernelDcacheWritebackInvalidateAll>,    "sceKernelDcacheWritebackInvalidateAll",   'i', ""   },
	{0X3EE30821, &WrapI_UI<sceKernelDcacheWritebackRange>,           "sceKernelDcacheWritebackRange",           'i', "xi" },
	{0X34B9FA9E, &WrapI_UI<sceKernelDcacheWritebackInvalidateRange>, "sceKernelDcacheWritebackInvalidateRange", 'i', "xi" },
	{0XC2DF770E, &WrapI_UI<sceKernelIcacheInvalidateRange>,          "sceKernelIcacheInvalidateRange",          'i', "xi" },
	{0X80001C4C, nullptr,                                            "sceKernelDcacheProbe",                    '?', ""   },
	{0X16641D70, nullptr,                                            "sceKernelDcacheReadTag",                  '?', ""   },
	{0X4FD31C9D, nullptr,                                            "sceKernelIcacheProbe",                    '?', ""   },
	{0XFB05FAD0, nullptr,                                            "sceKernelIcacheReadTag",                  '?', ""   },
	{0X920F104A, &WrapU_V<sceKernelIcacheInvalidateAll>,             "sceKernelIcacheInvalidateAll",            'x', ""   }
};				   

static const HLEFunction LoadCoreForKernel[] = {
	{0XACE23476, nullptr,                                            "sceKernelCheckPspConfig",                 '?', ""   },
	{0X7BE1421C, nullptr,                                            "sceKernelCheckExecFile",                  '?', ""   },
	{0XBF983EF2, nullptr,                                            "sceKernelProbeExecutableObject",          '?', ""   },
	{0X7068E6BA, nullptr,                                            "sceKernelLoadExecutableObject",           '?', ""   },
	{0XB4D6FECC, nullptr,                                            "sceKernelApplyElfRelSection",             '?', ""   },
	{0X54AB2675, nullptr,                                            "sceKernelApplyPspRelSection",             '?', ""   },
	{0X2952F5AC, nullptr,                                            "sceKernelDcacheWBinvAll",                 '?', ""   },
	{0xD8779AC6, &WrapU_V<sceKernelIcacheClearAll>,                  "sceKernelIcacheClearAll",                 'x', "",       HLE_KERNEL_SYSCALL },
	{0X99A695F0, nullptr,                                            "sceKernelRegisterLibrary",                '?', ""   },
	{0X5873A31F, nullptr,                                            "sceKernelRegisterLibraryForUser",         '?', ""   },
	{0X0B464512, nullptr,                                            "sceKernelReleaseLibrary",                 '?', ""   },
	{0X9BAF90F6, nullptr,                                            "sceKernelCanReleaseLibrary",              '?', ""   },
	{0X0E760DBA, nullptr,                                            "sceKernelLinkLibraryEntries",             '?', ""   },
	{0X0DE1F600, nullptr,                                            "sceKernelLinkLibraryEntriesForUser",      '?', ""   },
	{0XDA1B09AA, nullptr,                                            "sceKernelUnLinkLibraryEntries",           '?', ""   },
	{0XC99DD47A, &WrapU_V<sceKernelQueryLoadCoreCB>,                 "sceKernelQueryLoadCoreCB",                'x', "",       HLE_KERNEL_SYSCALL },
	{0X616FCCCD, nullptr,                                            "sceKernelSetBootCallbackLevel",           '?', ""   },
	{0XF32A2940, nullptr,                                            "sceKernelModuleFromUID",                  '?', ""   },
	{0XCD0F3BAC, nullptr,                                            "sceKernelCreateModule",                   '?', ""   },
	{0X6B2371C2, nullptr,                                            "sceKernelDeleteModule",                   '?', ""   },
	{0X7320D964, nullptr,                                            "sceKernelModuleAssign",                   '?', ""   },
	{0X44B292AB, nullptr,                                            "sceKernelAllocModule",                    '?', ""   },
	{0XBD61D4D5, nullptr,                                            "sceKernelFreeModule",                     '?', ""   },
	{0XAE7C6E76, nullptr,                                            "sceKernelRegisterModule",                 '?', ""   },
	{0X74CF001A, nullptr,                                            "sceKernelReleaseModule",                  '?', ""   },
	{0XFB8AE27D, nullptr,                                            "sceKernelFindModuleByAddress",            '?', ""   },
	{0XCCE4A157, &WrapU_U<sceKernelFindModuleByUID>,                 "sceKernelFindModuleByUID",                'x', "x" ,     HLE_KERNEL_SYSCALL },
	{0X82CE54ED, nullptr,                                            "sceKernelModuleCount",                    '?', ""   },
	{0XC0584F0C, nullptr,                                            "sceKernelGetModuleList",                  '?', ""   },
	{0XCF8A41B1, &WrapU_C<sceKernelFindModuleByName>,                "sceKernelFindModuleByName",               'x', "s",      HLE_KERNEL_SYSCALL },
	{0XB95FA50D, nullptr,                                            "LoadCoreForKernel_B95FA50D",              '?', ""   },
	{0x696594C8, &WrapU_V<sceKernelQueryLoadCoreCB>,                 "sceKernelQueryLoadCoreCB660",             'x', "",       HLE_KERNEL_SYSCALL },
};


// sceKernelSm1ReferOperations() returns a pointer to a driver-registered "SM1 operations"
// table (set up via the sibling sceKernelSm1RegisterOperations(), also unimplemented here),
// or NULL if nothing has registered one.
static u32 sceKernelSm1ReferOperations() {
	return hleLogDebug(Log::sceKernel, 0);
}

// The kernel's debug printf. On hardware it formats the string and hands it to whatever
// sceKernelRegisterKprintfHandler() installed, which on a retail PSP means the serial port -
// so nothing a player ever sees, and games that call it are just leaving debug output in.
// We format it and put it in the log, which is the useful thing to do with it.
// Declared void in the SDK headers, so the return value doesn't matter, but 0 is what the
// firmware leaves in v0.
static u32 Kprintf(MIPSState *mips, u32 fmtAddr) {
	std::string formatted;
	if (!HLEFormatPrintf(mips, fmtAddr, 1, &formatted)) {
		return hleLogError(Log::Printf, 0, "bad format string or args");
	}
	// Each log line is already a line, so don't double space.
	if (!formatted.empty() && formatted.back() == '\n') {
		formatted.pop_back();
	}
	return hleLogInfo(Log::Printf, 0, "\"%s\"", formatted.c_str());
}

static const HLEFunction KDebugForKernel[] = {
	{0XE7A3874D, nullptr,                                            "sceKernelRegisterAssertHandler",          '?', ""   },
	{0X2FF4E9F9, nullptr,                                            "sceKernelAssert",                         '?', ""   },
	{0X9B868276, nullptr,                                            "sceKernelGetDebugPutchar",                '?', ""   },
	{0XE146606D, nullptr,                                            "sceKernelRegisterDebugPutchar",           '?', ""   },
	{0X7CEB2C09, &WrapU_V<sceKernelRegisterKprintfHandler>,          "sceKernelRegisterKprintfHandler",         'x', "",       HLE_KERNEL_SYSCALL },
	{0X84F370BC, &WrapU_U<Kprintf>,                                  "Kprintf",                                 'x', "x",      HLE_KERNEL_SYSCALL },
	{0X5CE9838B, nullptr,                                            "sceKernelDebugWrite",                     '?', ""   },
	{0X66253C4E, nullptr,                                            "sceKernelRegisterDebugWrite",             '?', ""   },
	{0XDBB5597F, nullptr,                                            "sceKernelDebugRead",                      '?', ""   },
	{0XE6554FDA, nullptr,                                            "sceKernelRegisterDebugRead",              '?', ""   },
	{0XB9C643C9, nullptr,                                            "sceKernelDebugEcho",                      '?', ""   },
	{0X7D1C74F0, nullptr,                                            "sceKernelDebugEchoSet",                   '?', ""   },
	{0X24C32559, nullptr,                                            "sceKernelDipsw",                          '?', ""   },
	{0XD636B827, nullptr,                                            "sceKernelRemoveByDebugSection",           '?', ""   },
	{0X5282DD5E, nullptr,                                            "sceKernelDipswSet",                       '?', ""   },
	{0X9F8703E4, nullptr,                                            "sceKernelDipswCpTime",                    '?', ""   },
	{0X333DCEC7, nullptr,                                            "sceKernelSm1RegisterOperations",          '?', ""   },
	{0XE892D9A1, &WrapU_V<sceKernelSm1ReferOperations>,              "sceKernelSm1ReferOperations",             'x', ""   },
	{0XA126F497, nullptr,                                            "KDebugForKernel_A126F497",                '?', ""   },
	{0XB7251823, nullptr,                                            "sceKernelAcceptMbogoSig",                 '?', ""   },
};

static const HLEFunction pspeDebug[] = {
	{0XDEADBEAF, nullptr,                                            "pspeDebugWrite",                          '?', ""   },
};

static const HLEModule moduleList[] = {
	{"FakeSysCalls", ARRAY_SIZE(FakeSysCalls), FakeSysCalls},
	{"UtilsForUser", ARRAY_SIZE(UtilsForUser), UtilsForUser},
	{"KDebugForKernel", ARRAY_SIZE(KDebugForKernel), KDebugForKernel},
	{"sceSAScore"},
	{"SceBase64_Library"},
	{"sceCert_Loader"},
	{"SceFont_Library"},
	{"sceNetApctl"},
	{"sceSIRCS_IrDA_Driver"},
	{"Pspnet_Scan"},
	{"Pspnet_Show_MacAddr"},
	{"pspeDebug", ARRAY_SIZE(pspeDebug), pspeDebug},
};

static const int numModules = ARRAY_SIZE(moduleList);

// Sony renumbered the kernel NIDs at these releases. We implement 6.60's; a module from an older
// firmware imports by its generation's NID, which GetHLENidAlias() maps to the first column. 0 where none
// is needed (unchanged, or not known).
//
// Each generation's NIDs come from the call sites in popsman.prx compared with the next newer version's,
// checked against the code of the module exporting them, and for sysreg against the registers each
// function touches (where popsman.prx was rewritten, 5.55 and 4.05, the call sites alone get some wrong).
static const struct {
	const char *module;
	u32 nids[8];  // 6.60-6.61, 6.30-6.39, 6.00-6.20, 5.01-5.55, 3.95-4.05, 3.80-3.90, 3.71-3.73, 2.80-3.52
} hleKernelNids[] = {
	{ "InitForKernel",      { 0x9D33A110, 0xEE67E450, 0x977A0F90, 0,          0          } },  // InitForKernel_9D33A110
	{ "LoadExecForKernel",  { 0xC3474C2A, 0xFCD765C9, 0x29413885, 0x9C9BB2D8, 0x63D88393 } },  // sceKernelExitVSHKernel
	{ "ModuleMgrForKernel", { 0x939E4270, 0xFFB9B760, 0xE3CCC6EA, 0xC5A281C5, 0x24C5ABC2 } },  // sceKernelLoadModuleForKernel
	{ "ModuleMgrForKernel", { 0x3FF74DF1, 0xE6BF3960, 0xDF8FFFAB, 0xBB8C8FDF, 0x63A3CAFB } },  // sceKernelStartModule
	{ "sceClockgen_driver", { 0xDAB6E612, 0xA02BB1D3, 0xACC30ECE, 0,          0          } },
	{ "sceCodec_driver",    { 0x376399B6, 0xB2EF6B19, 0x794EE731, 0,          0          } },
	{ "sceCtrl_driver",     { 0x6C86AF22, 0xDEFAD580, 0x7A6436DE, 0,          0          } },
	{ "sceDisplay_driver",  { 0x03F16FD4, 0x3DABE438, 0xAAC50D73, 0,          0          } },
	{ "sceDisplay_driver",  { 0xE38CA615, 0x3E4B1B28, 0x0F82D90A, 0,          0          } },
	{ "sceDisplay_driver",  { 0xE97E9718, 0x8930B3A9, 0xF4B7B531, 0,          0          } },
	{ "sceDve_driver",      { 0x48005974, 0xAA085C2A, 0xA1401BA2, 0,          0          } },
	{ "sceGe_driver",       { 0xD8633888, 0x2444EC4D, 0xD4D665C9, 0x1F226BA9, 0x1DDA69CE } },  // sceGeEdramSetSize
	{ "sceGe_driver",       { 0x4B9554EB, 0xFEEC36F7, 0x23E9F5FA, 0,          0          } },
	{ "sceGe_driver",       { 0x5D28A52B, 0x670F15ED, 0xF489E74B, 0,          0          } },
	{ "sceGe_driver",       { 0xAFE6C9EF, 0xD8A53104, 0x7E2381D2, 0,          0          } },
	{ "sceHprm_driver",     { 0xB2BEADB8, 0xADE4B1F4, 0x62CDFB12, 0,          0          } },
	{ "sceMeCore_driver",   { 0x5DFF5C50, 0x99E4DBFA, 0x3A2E60BB, 0x051C1601, 0x8988AD49, 0xD857CF93, 0xC287AD90, 0x47DB48C2 } },  // sceMeBootStart
	{ "sceImpose_driver",   { 0xB497314D, 0x3FE74F52, 0x8F1AC24D, 0,          0          } },  // sceImpose_driver_B497314D
	{ "sceImpose_driver",   { 0x0F067E16, 0x44F17A7A, 0x55864BD7, 0x0BBCA0BF, 0xCD1291FD } },  // sceImposeChanges
	{ "sceImpose_driver",   { 0x5557F4E2, 0xEBC850AC, 0x4D849B71, 0xA516434B, 0x869EB4A8 } },  // sceImposeGetBatteryIconStatus
	{ "sceImpose_driver",   { 0xDC3BECFF, 0x4C4DF719, 0xC94AC8E2, 0x4B02F047, 0x6F502C0A } },  // sceImposeGetParam
	{ "sceImpose_driver",   { 0x133DB99A, 0x3BD9E51B, 0x5314204F, 0,          0          } },
	{ "sceImpose_driver",   { 0x28B97035, 0x027F21C6, 0x25011AAB, 0,          0          } },
	{ "scePadSvc_driver",   { 0x05C9C803, 0x773BA62B, 0x79B60777, 0,          0          } },
	{ "scePadSvc_driver",   { 0x0B3E6FD3, 0x2F9A534A, 0x3B0567D6, 0,          0          } },
	{ "scePadSvc_driver",   { 0x7CAB5A3D, 0x830FC570, 0x1971BAE0, 0,          0          } },
	{ "scePower_driver",    { 0x469989AD, 0,          0xEBD177D6, 0,          0xEBD177D6 } },  // scePowerSetClockFrequency630
	{ "sceRtc_driver",      { 0x4E267E02, 0xFF530D9F, 0x2F20DAF0, 0,          0          } },
	{ "sceRtc_driver",      { 0xE7B3ABF4, 0x99037763, 0x43F38ED8, 0,          0          } },
	{ "sceSyscon_driver",   { 0x48448373, 0x741110C5, 0xB757C11D, 0,          0          } },
	{ "sceSyscon_driver",   { 0x9BC5E33B, 0xC4931159, 0xF7E9487A, 0,          0          } },
	{ "sceSysreg_driver",   { 0x41B0337B, 0x9E80B4E2, 0x47966D28, 0x48869494, 0xC4B60AB3 } },  // sceSysreg_driver_41B0337B
	{ "sceSysreg_driver",   { 0x4634C9DC, 0xD1999F94, 0x5D3DD0B3, 0x410C74F7, 0xE08034E0 } },  // sceSysreg_driver_4634C9DC
	{ "sceSysreg_driver",   { 0x4B4CCE80, 0x9984A972, 0x94914740, 0xE46A0890, 0x21797DCF } },  // sceSysreg_driver_4B4CCE80
	{ "sceSysreg_driver",   { 0x8B1DD83A, 0x789597AB, 0xE7849809, 0xC7FC7669, 0x643169E5 } },  // sceSysreg_driver_8B1DD83A
	{ "sceSysreg_driver",   { 0xB1751B06, 0x8E9E76AE, 0xB733A263, 0x823FFD7A, 0x67583EF1 } },  // sceSysreg_driver_B1751B06
	{ "sceSysreg_driver",   { 0xC36775AD, 0x3A98CABB, 0x9EBB3C43, 0x923509A1, 0x15574362 } },  // sceSysreg_driver_C36775AD
	{ "sceSysreg_driver",   { 0x0AE8E549, 0x86FFE348, 0x119647A7, 0x663D5471, 0xD80E8AAF } },  // sceSysregAvcResetEnable660
	{ "sceSysreg_driver",   { 0x38527743, 0xD74D9041, 0x2B3BE2C8, 0x08E0EE7E, 0xB60FBA15 } },  // sceSysregMeBusClockEnable660
	{ "sceSysreg_driver",   { 0x48F1C4AD, 0xE2606FCB, 0x2D858336, 0x5F190A4E, 0x8E6586AD } },  // sceSysregMeResetDisable660
	{ "sceSysreg_driver",   { 0x457FEBA9, 0xB70793D7, 0x97EA6913, 0x20C9E2C4, 0xA36C4E9E } },  // sceSysregMeResetEnable660
	{ "sceSysreg_driver",   { 0x0143E8A8, 0x45596E3F, 0x4643F826, 0x461B8077, 0xD83733C9 } },  // sceSysregSemaTryLock660
	{ "sceSysreg_driver",   { 0xDB97C70E, 0x0CFD5A8A, 0x314A489C, 0x0E6301D0, 0xE65B0490 } },  // sceSysregSemaUnlock660
	{ "SysMemForKernel",    { 0xB4F00CB5, 0xF0E0AB7A, 0xB9796F69, 0x2CFF6F90, 0x12720EDA } },  // sceKernelGetCompiledSdkVersion
	{ "SysMemForKernel",    { 0x07C586A1, 0x458A70B5, 0x864EBFD7, 0,          0          } },  // sceKernelGetModel
	{ "SysMemForKernel",    { 0xC90B0992, 0x3DE3C6DF, 0xF5780DAA, 0xD7E24299, 0x523E300A } },  // sceKernelGetUIDcontrolBlock
	{ "SysMemForKernel",    { 0x8AE776AF, 0x2CFE3B22, 0,          0,          0          } },  // sceKernelMemset
	{ "SysMemForKernel",    { 0x310802A9, 0x25A760F0, 0x02AEA33F, 0,          0          } },  // SysMemForKernel_310802A9
	{ "SysMemForKernel",    { 0xF12A62F7, 0x52B54B93, 0xFEB5C72B, 0x950BCB31, 0x5136926D } },
	{ "SysMemForKernel",    { 0xFDC97D28, 0xF2FEAFD5, 0xA4AC3934, 0,          0          } },
};

u32 GetHLENidAlias(std::string_view moduleName, u32 nid) {
	for (const auto &row : hleKernelNids) {
		for (int i = 1; i < (int)ARRAY_SIZE(row.nids); i++) {
			if (row.nids[i] == nid && moduleName == row.module) {
				return row.nids[0];
			}
		}
	}
	return 0;
}

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
	Register_sceNetResolver();
	Register_sceNetInet();
	Register_sceNetApctl();
	Register_sceNetAdhoc();
	Register_sceNetAdhocMatching();
	Register_sceNetAdhocDiscover();
	Register_sceNetAdhocctl();
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
	Register_sceAac();
	Register_scePauth();
	Register_sceNp();
	Register_sceNpCommerce2();
	Register_sceNpService();
	Register_sceNpAuth();
	Register_sceMd5();
	Register_sceJpeg();
	Register_sceAudiocodec();
	Register_sceHeap();

	for (int i = 0; i < numModules; i++) {
		RegisterHLEModule(moduleList[i].name, moduleList[i].numFunctions, moduleList[i].funcTable);
	}

	// IMPORTANT: New modules have to be added at the end, or they will break savestates.

	Register_StdioForKernel();
	RegisterHLEModule("LoadCoreForKernel", ARRAY_SIZE(LoadCoreForKernel), LoadCoreForKernel);
	Register_IoFileMgrForKernel();
	Register_LoadExecForKernel();
	Register_SysMemForKernel();
	Register_sceMt19937();
	Register_SysclibForKernel();
	Register_sceCtrl_driver();
	Register_sceDisplay_driver();
	Register_sceMpegbase();
	Register_sceUsbGps();
	Register_sceLibFttt();
	Register_sceSha256();
	Register_sceAdler();
	Register_sceSfmt19937();
	Register_sceAudioRouting();
	Register_sceUsbCam();
	Register_sceG729();
	Register_sceNetUpnp();
	Register_sceNetIfhandle();
	Register_KUBridge();
	Register_sceUsbAcc();
	Register_sceUsbMic();
	Register_sceOpenPSID_driver();
	Register_semaphore();
	Register_sceDdrdb();
	Register_mp4msv();
	Register_InterruptManagerForKernel();
	Register_sceSircs();
	Register_sceNet_lib();
	Register_sceReg();
	Register_sceRtc_driver();
	Register_scePower_driver();
	Register_sceImpose_driver();
	Register_sceHprm_driver();
	Register_sceChkreg();
	Register_sceVshBridge();
	Register_sceResmgr();
	Register_sceVideocodec();

	// add new modules here.
	Register_SystemCtrlForKernel();
	Register_sceGe_driver();
	Register_sceMePower_driver();
	Register_SystemCtrlForUser();
	Register_ExceptionManagerForKernel();
	Register_sceSysreg_driver();
	Register_sceSysEventForKernel();
	Register_sceAmctrl_driver();
	Register_InitForKernel();
	Register_PopsBootCalls();
	Register_sceVaudio_driver();
	Register_sceNetApctl_lib();
	Register_sceMeCore_driver();

	// Not ready to enable this due to apparent softlocks in Patapon 3.
	// Register_sceNpMatching2();
}
