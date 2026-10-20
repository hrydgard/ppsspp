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

struct HLENidAlias {
	const char *module;
	u32 nid;
	u32 sameAs;
};

// Kernel NIDs were renumbered between firmware versions. These map an older firmware's NID to the one
// the function is implemented under here (6.60's), so firmware modules from that version resolve
// against the same tables. Found by matching the call sites in each version's own modules, with
// matching code in the exporting module where it's unchanged.
static const HLENidAlias hleNidAliases[] = {
	// 6.30-6.39, from popsman.prx (whose 6.30/6.31 version calls the semaphore functions).
	{ "InitForKernel", 0xEE67E450, 0x9D33A110 },  // InitForKernel_9D33A110
	{ "LoadExecForKernel", 0xFCD765C9, 0xC3474C2A },  // sceKernelExitVSHKernel
	{ "ModuleMgrForKernel", 0xE6BF3960, 0x3FF74DF1 },  // sceKernelStartModule
	{ "ModuleMgrForKernel", 0xFFB9B760, 0x939E4270 },  // sceKernelLoadModuleForKernel
	{ "SysMemForKernel", 0x25A760F0, 0x310802A9 },  // SysMemForKernel_310802A9
	{ "SysMemForKernel", 0x2CFE3B22, 0x8AE776AF },  // sceKernelMemset
	{ "SysMemForKernel", 0x3DE3C6DF, 0xC90B0992 },  // sceKernelGetUIDcontrolBlock
	{ "SysMemForKernel", 0x458A70B5, 0x07C586A1 },  // sceKernelGetModel
	{ "SysMemForKernel", 0x52B54B93, 0xF12A62F7 },
	{ "SysMemForKernel", 0xF0E0AB7A, 0xB4F00CB5 },  // sceKernelGetCompiledSdkVersion
	{ "SysMemForKernel", 0xF2FEAFD5, 0xFDC97D28 },
	{ "sceClockgen_driver", 0xA02BB1D3, 0xDAB6E612 },
	{ "sceCodec_driver", 0xB2EF6B19, 0x376399B6 },
	{ "sceCtrl_driver", 0xDEFAD580, 0x6C86AF22 },
	{ "sceDisplay_driver", 0x3DABE438, 0x03F16FD4 },
	{ "sceDisplay_driver", 0x3E4B1B28, 0xE38CA615 },
	{ "sceDisplay_driver", 0x8930B3A9, 0xE97E9718 },
	{ "sceDve_driver", 0xAA085C2A, 0x48005974 },
	{ "sceGe_driver", 0x2444EC4D, 0xD8633888 },  // sceGeEdramSetSize
	{ "sceGe_driver", 0x670F15ED, 0x5D28A52B },
	{ "sceGe_driver", 0xD8A53104, 0xAFE6C9EF },
	{ "sceGe_driver", 0xFEEC36F7, 0x4B9554EB },
	{ "sceHprm_driver", 0xADE4B1F4, 0xB2BEADB8 },
	{ "sceImpose_driver", 0x027F21C6, 0x28B97035 },
	{ "sceImpose_driver", 0x3BD9E51B, 0x133DB99A },
	{ "sceImpose_driver", 0x3FE74F52, 0xB497314D },  // sceImpose_driver_B497314D
	{ "sceImpose_driver", 0x44F17A7A, 0x0F067E16 },  // sceImposeChanges
	{ "sceImpose_driver", 0x4C4DF719, 0xDC3BECFF },  // sceImposeGetParam
	{ "sceImpose_driver", 0xEBC850AC, 0x5557F4E2 },  // sceImposeGetBatteryIconStatus
	{ "scePadSvc_driver", 0x2F9A534A, 0x0B3E6FD3 },
	{ "scePadSvc_driver", 0x773BA62B, 0x05C9C803 },
	{ "scePadSvc_driver", 0x830FC570, 0x7CAB5A3D },
	{ "sceRtc_driver", 0x99037763, 0xE7B3ABF4 },
	{ "sceRtc_driver", 0xFF530D9F, 0x4E267E02 },
	{ "sceSyscon_driver", 0x741110C5, 0x48448373 },
	{ "sceSyscon_driver", 0xC4931159, 0x9BC5E33B },
	{ "sceSysreg_driver", 0x3A98CABB, 0xC36775AD },  // sceSysreg_driver_C36775AD
	{ "sceSysreg_driver", 0x45596E3F, 0x0143E8A8 },  // sceSysregSemaTryLock660
	{ "sceSysreg_driver", 0x0CFD5A8A, 0xDB97C70E },  // sceSysregSemaUnlock660
	{ "sceSysreg_driver", 0x789597AB, 0x8B1DD83A },  // sceSysreg_driver_8B1DD83A
	{ "sceSysreg_driver", 0x86FFE348, 0x0AE8E549 },  // sceSysregAvcResetEnable660
	{ "sceSysreg_driver", 0x8E9E76AE, 0xB1751B06 },  // sceSysreg_driver_B1751B06
	{ "sceSysreg_driver", 0x9984A972, 0x4B4CCE80 },  // sceSysreg_driver_4B4CCE80
	{ "sceSysreg_driver", 0x9E80B4E2, 0x41B0337B },  // sceSysreg_driver_41B0337B
	{ "sceSysreg_driver", 0xB70793D7, 0x457FEBA9 },  // sceSysregMeResetEnable660
	{ "sceSysreg_driver", 0xD1999F94, 0x4634C9DC },  // sceSysreg_driver_4634C9DC
	{ "sceSysreg_driver", 0xD74D9041, 0x38527743 },  // sceSysregMeBusClockEnable660
	{ "sceSysreg_driver", 0xE2606FCB, 0x48F1C4AD },  // sceSysregMeResetDisable660
	// 6.20, from popsman.prx.
	{ "InitForKernel", 0x977A0F90, 0x9D33A110 },  // InitForKernel_9D33A110
	{ "LoadExecForKernel", 0x29413885, 0xC3474C2A },  // sceKernelExitVSHKernel
	{ "ModuleMgrForKernel", 0xDF8FFFAB, 0x3FF74DF1 },  // sceKernelStartModule
	{ "ModuleMgrForKernel", 0xE3CCC6EA, 0x939E4270 },  // sceKernelLoadModuleForKernel
	{ "sceClockgen_driver", 0xACC30ECE, 0xDAB6E612 },
	{ "sceCodec_driver", 0x794EE731, 0x376399B6 },
	{ "sceCtrl_driver", 0x7A6436DE, 0x6C86AF22 },
	{ "sceDisplay_driver", 0x0F82D90A, 0xE38CA615 },
	{ "sceDisplay_driver", 0xAAC50D73, 0x03F16FD4 },
	{ "sceDisplay_driver", 0xF4B7B531, 0xE97E9718 },
	{ "sceDve_driver", 0xA1401BA2, 0x48005974 },
	{ "sceGe_driver", 0x23E9F5FA, 0x4B9554EB },
	{ "sceGe_driver", 0x7E2381D2, 0xAFE6C9EF },
	{ "sceGe_driver", 0xD4D665C9, 0xD8633888 },  // sceGeEdramSetSize
	{ "sceGe_driver", 0xF489E74B, 0x5D28A52B },
	{ "sceHprm_driver", 0x62CDFB12, 0xB2BEADB8 },
	{ "sceImpose_driver", 0x25011AAB, 0x28B97035 },
	{ "sceImpose_driver", 0x4D849B71, 0x5557F4E2 },  // sceImposeGetBatteryIconStatus
	{ "sceImpose_driver", 0x5314204F, 0x133DB99A },
	{ "sceImpose_driver", 0x55864BD7, 0x0F067E16 },  // sceImposeChanges
	{ "sceImpose_driver", 0x8F1AC24D, 0xB497314D },  // sceImpose_driver_B497314D
	{ "sceImpose_driver", 0xC94AC8E2, 0xDC3BECFF },  // sceImposeGetParam
	{ "scePadSvc_driver", 0x1971BAE0, 0x7CAB5A3D },
	{ "scePadSvc_driver", 0x3B0567D6, 0x0B3E6FD3 },
	{ "scePadSvc_driver", 0x79B60777, 0x05C9C803 },
	{ "sceRtc_driver", 0x2F20DAF0, 0x4E267E02 },
	{ "sceRtc_driver", 0x43F38ED8, 0xE7B3ABF4 },
	{ "sceSyscon_driver", 0xB757C11D, 0x48448373 },
	{ "sceSyscon_driver", 0xF7E9487A, 0x9BC5E33B },
	{ "sceSysreg_driver", 0x119647A7, 0x0AE8E549 },  // sceSysregAvcResetEnable660
	{ "sceSysreg_driver", 0x2B3BE2C8, 0x38527743 },  // sceSysregMeBusClockEnable660
	{ "sceSysreg_driver", 0x2D858336, 0x48F1C4AD },  // sceSysregMeResetDisable660
	{ "sceSysreg_driver", 0x314A489C, 0xDB97C70E },  // sceSysregSemaUnlock660
	{ "sceSysreg_driver", 0x4643F826, 0x0143E8A8 },  // sceSysregSemaTryLock660
	{ "sceSysreg_driver", 0x47966D28, 0x41B0337B },  // sceSysreg_driver_41B0337B
	{ "sceSysreg_driver", 0x5D3DD0B3, 0x4634C9DC },  // sceSysreg_driver_4634C9DC
	{ "sceSysreg_driver", 0x94914740, 0x4B4CCE80 },  // sceSysreg_driver_4B4CCE80
	{ "sceSysreg_driver", 0x97EA6913, 0x457FEBA9 },  // sceSysregMeResetEnable660
	{ "sceSysreg_driver", 0x9EBB3C43, 0xC36775AD },  // sceSysreg_driver_C36775AD
	{ "sceSysreg_driver", 0xB733A263, 0xB1751B06 },  // sceSysreg_driver_B1751B06
	{ "sceSysreg_driver", 0xE7849809, 0x8B1DD83A },  // sceSysreg_driver_8B1DD83A
	{ "SysMemForKernel", 0x02AEA33F, 0x310802A9 },  // SysMemForKernel_310802A9
	{ "SysMemForKernel", 0xA4AC3934, 0xFDC97D28 },
	{ "SysMemForKernel", 0xB9796F69, 0xB4F00CB5 },  // sceKernelGetCompiledSdkVersion
	{ "SysMemForKernel", 0xF5780DAA, 0xC90B0992 },  // sceKernelGetUIDcontrolBlock
	{ "SysMemForKernel", 0xFEB5C72B, 0xF12A62F7 },
	// 5.55, from popsman.prx, whose code changed too much to match every import: only the ones used here,
	// each checked by its call site.
	{ "LoadExecForKernel", 0x9C9BB2D8, 0xC3474C2A },  // sceKernelExitVSHKernel
	{ "ModuleMgrForKernel", 0xBB8C8FDF, 0x3FF74DF1 },  // sceKernelStartModule
	{ "ModuleMgrForKernel", 0xC5A281C5, 0x939E4270 },  // sceKernelLoadModuleForKernel
	{ "SysMemForKernel", 0x2CFF6F90, 0xB4F00CB5 },  // sceKernelGetCompiledSdkVersion
	{ "SysMemForKernel", 0x950BCB31, 0xF12A62F7 },
	{ "SysMemForKernel", 0xD7E24299, 0xC90B0992 },  // sceKernelGetUIDcontrolBlock
	{ "sceGe_driver", 0x1F226BA9, 0xD8633888 },  // sceGeEdramSetSize
	{ "sceImpose_driver", 0xA516434B, 0x5557F4E2 },  // sceImposeGetBatteryIconStatus
	{ "sceSysreg_driver", 0x08E0EE7E, 0x38527743 },  // sceSysregMeBusClockEnable660
	{ "sceSysreg_driver", 0x0E6301D0, 0xDB97C70E },  // sceSysregSemaUnlock660
	{ "sceSysreg_driver", 0x20C9E2C4, 0x457FEBA9 },  // sceSysregMeResetEnable660
	{ "sceSysreg_driver", 0x410C74F7, 0x4634C9DC },  // sceSysreg_driver_4634C9DC
	{ "sceSysreg_driver", 0x461B8077, 0x0143E8A8 },  // sceSysregSemaTryLock660
	{ "sceSysreg_driver", 0x48869494, 0x41B0337B },  // sceSysreg_driver_41B0337B
	{ "sceSysreg_driver", 0x5F190A4E, 0x48F1C4AD },  // sceSysregMeResetDisable660
	{ "sceSysreg_driver", 0x663D5471, 0x0AE8E549 },  // sceSysregAvcResetEnable660
	{ "sceSysreg_driver", 0x823FFD7A, 0xB1751B06 },  // sceSysreg_driver_B1751B06
	{ "sceSysreg_driver", 0x923509A1, 0xC36775AD },  // sceSysreg_driver_C36775AD
	{ "sceSysreg_driver", 0xC7FC7669, 0x8B1DD83A },  // sceSysreg_driver_8B1DD83A
	{ "sceSysreg_driver", 0xE46A0890, 0x4B4CCE80 },  // sceSysreg_driver_4B4CCE80
};

u32 GetHLENidAlias(std::string_view moduleName, u32 nid) {
	for (const auto &alias : hleNidAliases) {
		if (alias.nid == nid && moduleName == alias.module) {
			return alias.sameAs;
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

	// Not ready to enable this due to apparent softlocks in Patapon 3.
	// Register_sceNpMatching2();
}
