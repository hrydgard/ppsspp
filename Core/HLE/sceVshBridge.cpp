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
#include "Core/HLE/sceVshBridge.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelModule.h"

// sceVshBridge is the HLE surface of flash0:/kd/vshbridge.prx, a kernel-mode module that
// exposes a bunch of otherwise-kernel-only functionality (controller reads, LoadExec
// variants, audio/ME/display bits, MagicGate memory stick audio, etc.) to the VSH, which
// mostly runs in user mode. Names and signatures are known (from JPCSP/psplibdoc), even
// though the functions themselves aren't implemented yet - functions genuinely only known by
// NID are named sceVshBridge_<NID>, matching JPCSP's own naming for those. A handful (marked
// below) just forward straight to the equivalent user-mode PPSSPP function, matching what
// JPCSP itself does for those same few.

static int vshKernelLoadModuleVSH(const char *fileName, int flags, u32 optionAddr) {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadModuleVSHByID() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshChkregCheckRegion() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshChkregGetPsCode() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_C949966C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMePowerControlAvcPower() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshUmdManTerm() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7C1658F2() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshDisplaySetHoldMode() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshImposeGetStatus() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshImposeSetStatus() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshImposeGetParam() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshImposeSetParam() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshImposeChanges() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshRtcSetConf() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshRtcSetCurrentTick() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioFormatICV() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshIoDevctl() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshIdStorageLookup() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshAudioSetFrequency() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioInit() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioEnd() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioAuth() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioCheckICV() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioCheckICVn() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioDeauth() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_14877197() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_5BBB35E4() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_B27C593F() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_0D2CEAD2() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_D120667D() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_D907B6AA() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioInvalidateICV() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7A63BE73() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_222A18C4() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_04310D7C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioReadMACList() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioGetInitialEKB() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMSAudioGetICVInfo() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshVaudioOutputBlocking() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshVaudioChReserve() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshVaudioChRelease() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshAudioSRCOutputBlocking() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshAudioSRCChReserve() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshAudioSRCChRelease() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMeRpcLock() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshMeRpcUnlock() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferPlain0() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferPlain() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecFromHost() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExec() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHDiscUpdater() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHDisk() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferVSHUsbWlan() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHMs1() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHMs2() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHMs3() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelExitVSHVSH() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferVSHPlain() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelDipswSet() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelDipswClear() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecVSHDiscDebug() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferVSHUsbWlanDebug() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshKernelLoadExecBufferVSHFromHost() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int vshLflashFatfmtStartFatfmt(int numberParameters, u32 parametersAddr) {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

// The following are only known by NID - imported by the real vsh_module (firmware 6.6x) but
// not documented anywhere we could find, including JPCSP.
static int sceVshBridge_01730088() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_0543156C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_0C0D5913() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_0D684A0B() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_0D7A4FE4() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_12B07B05() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_1D5C579F() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_21C243FE() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_21D4D038() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_27BDA326() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_27CD418C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_29CDFFBA() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_2EBD2323() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_3785D08B() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_3A46C639() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_3C90E435() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_3D30FEB6() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_582B5281() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_59197BE8() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_5B7F3339() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_5E0F5543() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_5E5AF7A2() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_5F35E8FE() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_63047647() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_63E69956() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7423151D() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_74DBE57E() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_791FCD43() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_79B916E1() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7A90D816() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7B14CE2B() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7D1C13B5() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_7E117907() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_81682A40() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_837C457A() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_88C35487() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_9056DE3A() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_9347D693() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_9427C909() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_9940D95C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_A29B5A33() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_AAB9A9EF() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_ABB84565() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_B8B07CAF() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_C51A6C26() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_CCD27632() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_CD1A2C46() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_D39DE400() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_D47041CA() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_D7D7E7B6() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_DB7C3D5A() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_E533E98C() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static int sceVshBridge_EBC3A334() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

const HLEFunction sceVshBridge[] = {
	{0XA5628F0D, &WrapI_CIU<vshKernelLoadModuleVSH>,                 "vshKernelLoadModuleVSH",                 'i', "six" },
	{0X41C54ADF, &WrapI_V<vshKernelLoadModuleVSHByID>,                "vshKernelLoadModuleVSHByID",             'i', ""    },
	{0XC9626587, &WrapI_UUUU<sceKernelLoadModuleBufferUsbWlan>,       "vshKernelLoadModuleBufferVSH",           'i', "xxxx"},
	{0X5C2983C2, &WrapI_V<vshChkregCheckRegion>,                      "vshChkregCheckRegion",                   'i', ""    },
	{0X61001D64, &WrapI_V<vshChkregGetPsCode>,                        "vshChkregGetPsCode",                     'i', ""    },
	{0XC949966C, &WrapI_V<sceVshBridge_C949966C>,                     "sceVshBridge_C949966C",                  'i', ""    },
	{0X5D4213EA, &WrapI_V<vshMePowerControlAvcPower>,                 "vshMePowerControlAvcPower",              'i', ""    },
	{0XC6395C03, &WrapI_UU<sceCtrlReadBufferPositive>,                "vshCtrlReadBufferPositive",              'i', "xx"  },
	{0X0163A8E7, &WrapI_V<vshUmdManTerm>,                             "vshUmdManTerm",                          'i', ""    },
	{0X7C1658F2, &WrapI_V<sceVshBridge_7C1658F2>,                     "sceVshBridge_7C1658F2",                  'i', ""    },
	{0X0E10922A, &WrapI_V<vshDisplaySetHoldMode>,                     "vshDisplaySetHoldMode",                  'i', ""    },
	{0XCA719C34, &WrapI_V<vshImposeGetStatus>,                        "vshImposeGetStatus",                     'i', ""    },
	{0X4E4E4DA3, &WrapI_V<vshImposeSetStatus>,                        "vshImposeSetStatus",                     'i', ""    },
	{0X639C3CB3, &WrapI_V<vshImposeGetParam>,                         "vshImposeGetParam",                      'i', ""    },
	{0X4A596D2D, &WrapI_V<vshImposeSetParam>,                         "vshImposeSetParam",                      'i', ""    },
	{0X5894C339, &WrapI_V<vshImposeChanges>,                          "vshImposeChanges",                       'i', ""    },
	{0X0FA48729, &WrapI_V<vshRtcSetConf>,                             "vshRtcSetConf",                          'i', ""    },
	{0X16415246, &WrapI_V<vshRtcSetCurrentTick>,                      "vshRtcSetCurrentTick",                   'i', ""    },
	{0X5350C073, &WrapI_V<vshMSAudioFormatICV>,                       "vshMSAudioFormatICV",                    'i', ""    },
	{0X2380DC08, &WrapI_V<vshIoDevctl>,                               "vshIoDevctl",                            'i', ""    },
	{0X4DB43867, &WrapI_V<vshIdStorageLookup>,                        "vshIdStorageLookup",                     'i', ""    },
	{0X7B67394E, &WrapI_V<vshAudioSetFrequency>,                      "vshAudioSetFrequency",                   'i', ""    },
	{0XCE32CBEF, &WrapI_V<vshMSAudioInit>,                            "vshMSAudioInit",                         'i', ""    },
	{0XE5DA5E95, &WrapI_V<vshMSAudioEnd>,                             "vshMSAudioEnd",                          'i', ""    },
	{0X6CAEB765, &WrapI_V<vshMSAudioAuth>,                            "vshMSAudioAuth",                         'i', ""    },
	{0X53BFD101, &WrapI_V<vshMSAudioCheckICV>,                        "vshMSAudioCheckICV",                     'i', ""    },
	{0XE174218C, &WrapI_V<vshMSAudioCheckICVn>,                       "vshMSAudioCheckICVn",                    'i', ""    },
	{0X7EA32357, &WrapI_V<vshMSAudioDeauth>,                          "vshMSAudioDeauth",                       'i', ""    },
	{0X14877197, &WrapI_V<sceVshBridge_14877197>,                     "sceVshBridge_14877197",                  'i', ""    },
	{0X5BBB35E4, &WrapI_V<sceVshBridge_5BBB35E4>,                     "sceVshBridge_5BBB35E4",                  'i', ""    },
	{0XB27C593F, &WrapI_V<sceVshBridge_B27C593F>,                     "sceVshBridge_B27C593F",                  'i', ""    },
	{0X0D2CEAD2, &WrapI_V<sceVshBridge_0D2CEAD2>,                     "sceVshBridge_0D2CEAD2",                  'i', ""    },
	{0XD120667D, &WrapI_V<sceVshBridge_D120667D>,                     "sceVshBridge_D120667D",                  'i', ""    },
	{0XD907B6AA, &WrapI_V<sceVshBridge_D907B6AA>,                     "sceVshBridge_D907B6AA",                  'i', ""    },
	{0XD46D4528, &WrapI_V<vshMSAudioInvalidateICV>,                   "vshMSAudioInvalidateICV",                'i', ""    },
	{0X7A63BE73, &WrapI_V<sceVshBridge_7A63BE73>,                     "sceVshBridge_7A63BE73",                  'i', ""    },
	{0X222A18C4, &WrapI_V<sceVshBridge_222A18C4>,                     "sceVshBridge_222A18C4",                  'i', ""    },
	{0X04310D7C, &WrapI_V<sceVshBridge_04310D7C>,                     "sceVshBridge_04310D7C",                  'i', ""    },
	{0X4E61C72E, &WrapI_V<vshMSAudioReadMACList>,                     "vshMSAudioReadMACList",                  'i', ""    },
	{0XE2DD0A81, &WrapI_V<vshMSAudioGetInitialEKB>,                   "vshMSAudioGetInitialEKB",                'i', ""    },
	{0X6396ACBD, &WrapI_V<vshMSAudioGetICVInfo>,                      "vshMSAudioGetICVInfo",                   'i', ""    },
	{0X274BB6AE, &WrapI_V<vshVaudioOutputBlocking>,                   "vshVaudioOutputBlocking",                'i', ""    },
	{0X8C440581, &WrapI_V<vshVaudioChReserve>,                        "vshVaudioChReserve",                     'i', ""    },
	{0X07EC5661, &WrapI_V<vshVaudioChRelease>,                        "vshVaudioChRelease",                     'i', ""    },
	{0X3B3D9F5D, &WrapI_V<vshAudioSRCOutputBlocking>,                 "vshAudioSRCOutputBlocking",              'i', ""    },
	{0XB7F233A2, &WrapI_V<vshAudioSRCChReserve>,                      "vshAudioSRCChReserve",                   'i', ""    },
	{0XC58D0939, &WrapI_V<vshAudioSRCChRelease>,                      "vshAudioSRCChRelease",                   'i', ""    },
	{0XCBDA2613, &WrapI_V<vshMeRpcLock>,                              "vshMeRpcLock",                           'i', ""    },
	{0XA7F0E8E0, &WrapI_V<vshMeRpcUnlock>,                            "vshMeRpcUnlock",                         'i', ""    },
	{0X98B4117E, &WrapI_V<vshKernelLoadExecBufferPlain0>,             "vshKernelLoadExecBufferPlain0",          'i', ""    },
	{0X8399A8AA, &WrapI_V<vshKernelLoadExecBufferPlain>,              "vshKernelLoadExecBufferPlain",           'i', ""    },
	{0XE614F45F, &WrapI_V<vshKernelLoadExecFromHost>,                 "vshKernelLoadExecFromHost",              'i', ""    },
	{0XEEFB02BB, &WrapI_V<vshKernelLoadExec>,                         "vshKernelLoadExec",                      'i', ""    },
	{0X9929DDA5, &WrapV_V<sceKernelExitGame>,                         "vshKernelExitVSH",                       'v', ""    },
	{0XB7C46DCA, &WrapI_V<vshKernelLoadExecVSHDiscUpdater>,           "vshKernelLoadExecVSHDiscUpdater",        'i', ""    },
	{0XF4873F4D, &WrapI_V<vshKernelLoadExecVSHDisk>,                  "vshKernelLoadExecVSHDisk",               'i', ""    },
	{0X83528906, &WrapI_V<vshKernelLoadExecBufferVSHUsbWlan>,         "vshKernelLoadExecBufferVSHUsbWlan",      'i', ""    },
	{0XF35BFB7D, &WrapI_V<vshKernelLoadExecVSHMs1>,                   "vshKernelLoadExecVSHMs1",                'i', ""    },
	{0X97FB006F, &WrapI_V<vshKernelLoadExecVSHMs2>,                   "vshKernelLoadExecVSHMs2",                'i', ""    },
	{0X029EF6C9, &WrapI_V<vshKernelLoadExecVSHMs3>,                   "vshKernelLoadExecVSHMs3",                'i', ""    },
	{0X40716012, &WrapI_V<vshKernelExitVSHVSH>,                       "vshKernelExitVSHVSH",                    'i', ""    },
	{0X88DA81A5, &WrapI_V<vshKernelLoadExecBufferVSHPlain>,           "vshKernelLoadExecBufferVSHPlain",        'i', ""    },
	{0XC1D3AE95, &WrapI_V<vshKernelDipswSet>,                         "vshKernelDipswSet",                      'i', ""    },
	{0XD08C1FBE, &WrapI_V<vshKernelDipswClear>,                       "vshKernelDipswClear",                    'i', ""    },
	{0X04AEC74C, &WrapI_V<vshKernelLoadExecVSHDiscDebug>,             "vshKernelLoadExecVSHDiscDebug",          'i', ""    },
	{0X68BE3316, &WrapI_V<vshKernelLoadExecBufferVSHUsbWlanDebug>,    "vshKernelLoadExecBufferVSHUsbWlanDebug", 'i', ""    },
	{0X88BD8364, &WrapI_V<vshKernelLoadExecBufferVSHFromHost>,        "vshKernelLoadExecBufferVSHFromHost",     'i', ""    },
	{0X74DA9D25, &WrapI_IU<vshLflashFatfmtStartFatfmt>,               "vshLflashFatfmtStartFatfmt",             'i', "ix"  },
	{0X01730088, &WrapI_V<sceVshBridge_01730088>,   "sceVshBridge_01730088",          'i', ""    },
	{0X0543156C, &WrapI_V<sceVshBridge_0543156C>,   "sceVshBridge_0543156C",          'i', ""    },
	{0X0C0D5913, &WrapI_V<sceVshBridge_0C0D5913>,   "sceVshBridge_0C0D5913",          'i', ""    },
	{0X0D684A0B, &WrapI_V<sceVshBridge_0D684A0B>,   "sceVshBridge_0D684A0B",          'i', ""    },
	{0X0D7A4FE4, &WrapI_V<sceVshBridge_0D7A4FE4>,   "sceVshBridge_0D7A4FE4",          'i', ""    },
	{0X12B07B05, &WrapI_V<sceVshBridge_12B07B05>,   "sceVshBridge_12B07B05",          'i', ""    },
	{0X1D5C579F, &WrapI_V<sceVshBridge_1D5C579F>,   "sceVshBridge_1D5C579F",          'i', ""    },
	{0X21C243FE, &WrapI_V<sceVshBridge_21C243FE>,   "sceVshBridge_21C243FE",          'i', ""    },
	{0X21D4D038, &WrapI_V<sceVshBridge_21D4D038>,   "sceVshBridge_21D4D038",          'i', ""    },
	{0X27BDA326, &WrapI_V<sceVshBridge_27BDA326>,   "sceVshBridge_27BDA326",          'i', ""    },
	{0X27CD418C, &WrapI_V<sceVshBridge_27CD418C>,   "sceVshBridge_27CD418C",          'i', ""    },
	{0X29CDFFBA, &WrapI_V<sceVshBridge_29CDFFBA>,   "sceVshBridge_29CDFFBA",          'i', ""    },
	{0X2EBD2323, &WrapI_V<sceVshBridge_2EBD2323>,   "sceVshBridge_2EBD2323",          'i', ""    },
	{0X3785D08B, &WrapI_V<sceVshBridge_3785D08B>,   "sceVshBridge_3785D08B",          'i', ""    },
	{0X3A46C639, &WrapI_V<sceVshBridge_3A46C639>,   "sceVshBridge_3A46C639",          'i', ""    },
	{0X3C90E435, &WrapI_V<sceVshBridge_3C90E435>,   "sceVshBridge_3C90E435",          'i', ""    },
	{0X3D30FEB6, &WrapI_V<sceVshBridge_3D30FEB6>,   "sceVshBridge_3D30FEB6",          'i', ""    },
	{0X582B5281, &WrapI_V<sceVshBridge_582B5281>,   "sceVshBridge_582B5281",          'i', ""    },
	{0X59197BE8, &WrapI_V<sceVshBridge_59197BE8>,   "sceVshBridge_59197BE8",          'i', ""    },
	{0X5B7F3339, &WrapI_V<sceVshBridge_5B7F3339>,   "sceVshBridge_5B7F3339",          'i', ""    },
	{0X5E0F5543, &WrapI_V<sceVshBridge_5E0F5543>,   "sceVshBridge_5E0F5543",          'i', ""    },
	{0X5E5AF7A2, &WrapI_V<sceVshBridge_5E5AF7A2>,   "sceVshBridge_5E5AF7A2",          'i', ""    },
	{0X5F35E8FE, &WrapI_V<sceVshBridge_5F35E8FE>,   "sceVshBridge_5F35E8FE",          'i', ""    },
	{0X63047647, &WrapI_V<sceVshBridge_63047647>,   "sceVshBridge_63047647",          'i', ""    },
	{0X63E69956, &WrapI_V<sceVshBridge_63E69956>,   "sceVshBridge_63E69956",          'i', ""    },
	{0X7423151D, &WrapI_V<sceVshBridge_7423151D>,   "sceVshBridge_7423151D",          'i', ""    },
	{0X74DBE57E, &WrapI_V<sceVshBridge_74DBE57E>,   "sceVshBridge_74DBE57E",          'i', ""    },
	{0X791FCD43, &WrapI_V<sceVshBridge_791FCD43>,   "sceVshBridge_791FCD43",          'i', ""    },
	{0X79B916E1, &WrapI_V<sceVshBridge_79B916E1>,   "sceVshBridge_79B916E1",          'i', ""    },
	{0X7A90D816, &WrapI_V<sceVshBridge_7A90D816>,   "sceVshBridge_7A90D816",          'i', ""    },
	{0X7B14CE2B, &WrapI_V<sceVshBridge_7B14CE2B>,   "sceVshBridge_7B14CE2B",          'i', ""    },
	{0X7D1C13B5, &WrapI_V<sceVshBridge_7D1C13B5>,   "sceVshBridge_7D1C13B5",          'i', ""    },
	{0X7E117907, &WrapI_V<sceVshBridge_7E117907>,   "sceVshBridge_7E117907",          'i', ""    },
	{0X81682A40, &WrapI_V<sceVshBridge_81682A40>,   "sceVshBridge_81682A40",          'i', ""    },
	{0X837C457A, &WrapI_V<sceVshBridge_837C457A>,   "sceVshBridge_837C457A",          'i', ""    },
	{0X88C35487, &WrapI_V<sceVshBridge_88C35487>,   "sceVshBridge_88C35487",          'i', ""    },
	{0X9056DE3A, &WrapI_V<sceVshBridge_9056DE3A>,   "sceVshBridge_9056DE3A",          'i', ""    },
	{0X9347D693, &WrapI_V<sceVshBridge_9347D693>,   "sceVshBridge_9347D693",          'i', ""    },
	{0X9427C909, &WrapI_V<sceVshBridge_9427C909>,   "sceVshBridge_9427C909",          'i', ""    },
	{0X9940D95C, &WrapI_V<sceVshBridge_9940D95C>,   "sceVshBridge_9940D95C",          'i', ""    },
	{0XA29B5A33, &WrapI_V<sceVshBridge_A29B5A33>,   "sceVshBridge_A29B5A33",          'i', ""    },
	{0XAAB9A9EF, &WrapI_V<sceVshBridge_AAB9A9EF>,   "sceVshBridge_AAB9A9EF",          'i', ""    },
	{0XABB84565, &WrapI_V<sceVshBridge_ABB84565>,   "sceVshBridge_ABB84565",          'i', ""    },
	{0XB8B07CAF, &WrapI_V<sceVshBridge_B8B07CAF>,   "sceVshBridge_B8B07CAF",          'i', ""    },
	{0XC51A6C26, &WrapI_V<sceVshBridge_C51A6C26>,   "sceVshBridge_C51A6C26",          'i', ""    },
	{0XCCD27632, &WrapI_V<sceVshBridge_CCD27632>,   "sceVshBridge_CCD27632",          'i', ""    },
	{0XCD1A2C46, &WrapI_V<sceVshBridge_CD1A2C46>,   "sceVshBridge_CD1A2C46",          'i', ""    },
	{0XD39DE400, &WrapI_V<sceVshBridge_D39DE400>,   "sceVshBridge_D39DE400",          'i', ""    },
	{0XD47041CA, &WrapI_V<sceVshBridge_D47041CA>,   "sceVshBridge_D47041CA",          'i', ""    },
	{0XD7D7E7B6, &WrapI_V<sceVshBridge_D7D7E7B6>,   "sceVshBridge_D7D7E7B6",          'i', ""    },
	{0XDB7C3D5A, &WrapI_V<sceVshBridge_DB7C3D5A>,   "sceVshBridge_DB7C3D5A",          'i', ""    },
	{0XE533E98C, &WrapI_V<sceVshBridge_E533E98C>,   "sceVshBridge_E533E98C",          'i', ""    },
	{0XEBC3A334, &WrapI_V<sceVshBridge_EBC3A334>,   "sceVshBridge_EBC3A334",          'i', ""    },
};

void Register_sceVshBridge() {
	RegisterHLEModule("sceVshBridge", ARRAY_SIZE(sceVshBridge), sceVshBridge);
}
