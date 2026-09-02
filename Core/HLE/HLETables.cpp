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
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Common/Data/Text/Parsers.h"

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
#include "sceNetInet.h"
#include "sceNetResolver.h"
// #include "sceNp2.h"
#include "sceNet_lib.h"

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
	{0XC99DD47A, nullptr,                                            "sceKernelQueryLoadCoreCB",                '?', ""   },
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
};


// sceKernelSm1ReferOperations() returns a pointer to a driver-registered "SM1 operations"
// table (set up via the sibling sceKernelSm1RegisterOperations(), also unimplemented here),
// or NULL if nothing has registered one.
static u32 sceKernelSm1ReferOperations() {
	return hleLogDebug(Log::sceKernel, 0);
}

// Retail firmware DIP switches are clear. The 6.60 NID is used by wlan.prx;
// keeping range validation matches Jpcsp and avoids treating a bad query as a
// successful hardware probe.
static int sceKernelDipsw(int bit) {
	if (bit < 0 || bit >= 64) {
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid DIP switch %d", bit);
	}
	return hleLogDebug(Log::sceKernel, 0, "bit=%d", bit);
}

static int sceKernelIsDVDMode() {
	return hleLogDebug(Log::sceKernel, 0);
}

static int sceKernelIsToolMode() {
	// The selected 6.61 retail firmware profile has neither the tool-mode nor
	// development-tool DIP switches set. utility.prx uses these results to
	// decide whether an applet partition may be filled with a diagnostic poison
	// word, so leaving v0 unchanged here corrupts every later utility frontend.
	return hleLogDebug(Log::sceKernel, 0);
}

static int sceKernelApplicationType() {
	// uOFW and Jpcsp both model VSH as 0x100.  Keep the established 0x200
	// response for game-resident native utilities, including savedata.
	return hleLogDebug(Log::sceKernel, __KernelIsRunningVSH() ? 0x100 : 0x200);
}

static int sceKernelBootFrom() {
	// vshmain is a flash boot even when loadexec reached it by exiting a game.
	// Non-VSH callers retain PPSSPP's established Memory Stick response.
	return hleLogDebug(Log::sceKernel, __KernelIsRunningVSH() ? 0x00 : 0x40);
}

static int sceKernelSetInitCallback(u32 callback, u32 flag, u32 statusAddr) {
	if (!Memory::IsValid4AlignedAddress(callback)) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "callback=%08x", callback);
	}
	// Init has already completed when a game-resident service such as impose is
	// loaded.  uOFW's InitForKernel implementation executes late callbacks
	// immediately as callback((void *)1, 0, NULL), rather than queueing them in
	// Init's boot list.  The queued MIPS call retains the importing module's GP.
	const u32 args[3] = { 1, 0, 0 };
	hleEnqueueCall(callback, ARRAY_SIZE(args), args);
	if (statusAddr != 0) {
		if (!Memory::IsValid4AlignedAddress(statusAddr)) {
			return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "status=%08x", statusAddr);
		}
		Memory::WriteUnchecked_U32(0, statusAddr);
	}
	return hleLogDebug(Log::sceKernel, 0, "callback=%08x flag=%u", callback, flag);
}

static int sceKernelInitDiscImage() {
	return hleLogDebug(Log::sceKernel, 0);
}

static int sceCodecSelectVolumeTable() {
	return hleLogDebug(Log::sceAudio, 0);
}

static int sceKernelRegisterSysEventHandler(u32 handlerAddr) {
	return hleLogDebug(Log::sceKernel, 0, "handler=%08x", handlerAddr);
}

static int sceKernelUnregisterSysEventHandler(u32 handlerAddr) {
	return hleLogDebug(Log::sceKernel, 0, "handler=%08x", handlerAddr);
}

static int sceUmdManRegisterImposeCallBack(u32 callbackAddr) {
	return hleLogDebug(Log::sceIo, 0, "callback=%08x", callbackAddr);
}

// PPSSPP has no host LED device. Accepting a valid firmware request is a
// complete local model: the state is visual-only and has no guest-readable
// feedback path.
static int sceLedSetMode(int led, int mode, u32 configAddr) {
	return hleLogDebug(Log::HLE, 0, "led=%d mode=%d config=%08x", led, mode, configAddr);
}

static const HLEFunction KDebugForKernel[] = {
	{0XE7A3874D, nullptr,                                            "sceKernelRegisterAssertHandler",          '?', ""   },
	{0X2FF4E9F9, nullptr,                                            "sceKernelAssert",                         '?', ""   },
	{0X9B868276, nullptr,                                            "sceKernelGetDebugPutchar",                '?', ""   },
	{0XE146606D, nullptr,                                            "sceKernelRegisterDebugPutchar",           '?', ""   },
	{0X7CEB2C09, &WrapU_V<sceKernelRegisterKprintfHandler>,          "sceKernelRegisterKprintfHandler",         'x', "",       HLE_KERNEL_SYSCALL },
	{0X84F370BC, &WrapI_C<sceKernelPrintf>,                          "Kprintf",                                 'i', "s",      HLE_KERNEL_SYSCALL },
	{0X5CE9838B, nullptr,                                            "sceKernelDebugWrite",                     '?', ""   },
	{0X66253C4E, nullptr,                                            "sceKernelRegisterDebugWrite",             '?', ""   },
	{0XDBB5597F, nullptr,                                            "sceKernelDebugRead",                      '?', ""   },
	{0XE6554FDA, nullptr,                                            "sceKernelRegisterDebugRead",              '?', ""   },
	{0XB9C643C9, nullptr,                                            "sceKernelDebugEcho",                      '?', ""   },
	{0X7D1C74F0, nullptr,                                            "sceKernelDebugEchoSet",                   '?', ""   },
	{0X24C32559, &WrapI_I<sceKernelDipsw>,                           "sceKernelDipsw",                          'i', "i",      HLE_KERNEL_SYSCALL },
	{0XD636B827, nullptr,                                            "sceKernelRemoveByDebugSection",           '?', ""   },
	{0X5282DD5E, nullptr,                                            "sceKernelDipswSet",                       '?', ""   },
	{0X9F8703E4, nullptr,                                            "sceKernelDipswCpTime",                    '?', ""   },
	{0X333DCEC7, nullptr,                                            "sceKernelSm1RegisterOperations",          '?', ""   },
	{0XE892D9A1, &WrapU_V<sceKernelSm1ReferOperations>,              "sceKernelSm1ReferOperations",             'x', ""   },
	{0XA126F497, nullptr,                                            "KDebugForKernel_A126F497",                '?', ""   },
	{0XB7251823, nullptr,                                            "sceKernelAcceptMbogoSig",                 '?', ""   },
	{0X86010FCB, &WrapI_I<sceKernelDipsw>,                           "sceKernelDipsw",                          'i', "i",      HLE_KERNEL_SYSCALL },
	{0XB41E2430, &WrapI_V<sceKernelIsDVDMode>,                       "sceKernelIsDVDMode",                      'i', "",       HLE_KERNEL_SYSCALL },
	{0X47570AC5, &WrapI_V<sceKernelIsToolMode>,                      "sceKernelIsToolMode",                     'i', "",       HLE_KERNEL_SYSCALL },
	{0XACF427DC, &WrapI_V<sceKernelIsToolMode>,                      "sceKernelIsDevelopmentToolMode",          'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction InitForKernel[] = {
	{0X27932388, &WrapI_V<sceKernelBootFrom>,                        "sceKernelBootFrom",                       'i', "",       HLE_KERNEL_SYSCALL },
	{0X7233B5BC, &WrapI_V<sceKernelApplicationType>,                 "sceKernelApplicationType",                'i', "",       HLE_KERNEL_SYSCALL },
	{0X9F9AE99C, &WrapI_UUU<sceKernelSetInitCallback>,               "sceKernelSetInitCallback",                'i', "xxx",    HLE_KERNEL_SYSCALL },
	{0XA18A4A8B, &WrapI_V<sceKernelInitDiscImage>,                   "sceKernelInitDiscImage",                  'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceCodec_driver[] = {
	{0X261C6EE8, &WrapI_V<sceCodecSelectVolumeTable>,                "sceCodecSetOutputVolume",                'i', "",       HLE_KERNEL_SYSCALL },
	{0X856E7487, &WrapI_V<sceCodecSelectVolumeTable>,                "sceCodecOutputEnable",                   'i', "",       HLE_KERNEL_SYSCALL },
	{0XE4456BC3, &WrapI_V<sceCodecSelectVolumeTable>,                "sceCodecSelectVolumeTable",               'i', "",       HLE_KERNEL_SYSCALL },
	{0XE61A4623, &WrapI_V<sceCodecSelectVolumeTable>,                "sceCodec_driver_E61A4623",                'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceSysEventForKernel[] = {
	{0XCD9E4BB5, &WrapI_U<sceKernelRegisterSysEventHandler>,         "sceKernelRegisterSysEventHandler",        'i', "x",      HLE_KERNEL_SYSCALL },
	{0XD7D3FDCD, &WrapI_U<sceKernelUnregisterSysEventHandler>,       "sceKernelUnregisterSysEventHandler",      'i', "x",      HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceUmdMan_driver[] = {
	{0X80D31D5D, &WrapI_U<sceUmdManRegisterImposeCallBack>,          "sceUmdManRegisterImposeCallBack",         'i', "x",      HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceLed_driver[] = {
	{0XEA24BE03, &WrapI_IIU<sceLedSetMode>,                          "sceLedSetMode",                           'i', "iix",    HLE_KERNEL_SYSCALL },
};

// PPSSPP executes media work on host codec/audio services rather than the
// physical Media Engine. These firmware bridge calls therefore complete the
// corresponding host-side state transition synchronously. The reached return
// values and signatures match the pinned Jpcsp implementation and uOFW.
static int sceMSAudio_driver_543DBDD7() {
	return hleLogDebug(Log::sceAudio, 0);
}

static int sceMSAudio_driver_19474552(u32 resultAddr) {
	if (!Memory::IsValid4AlignedAddress(resultAddr)) {
		return hleLogError(Log::sceAudio, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "result=%08x", resultAddr);
	}
	Memory::WriteUnchecked_U32(0, resultAddr);
	return hleLogDebug(Log::sceAudio, 0, "result=%08x", resultAddr);
}

static int sceMSAudio_driver_59B4EE6D(int unknown) {
	return hleLogDebug(Log::sceAudio, 0, "unknown=%d", unknown);
}

static int sceMSAudio_driver_B7DB5AC6() {
	return hleLogDebug(Log::sceAudio, 0);
}

static int sceMeBootStart(int unknown) {
	return hleLogDebug(Log::ME, 0, "unknown=%d", unknown);
}

static int sceMePowerSetCpuGranularity(int numerator, int denominator) {
	return hleLogDebug(Log::ME, 0, "cpu granularity=%d/%d", numerator, denominator);
}

static int sceMePowerSetBusGranularity(int numerator, int denominator) {
	return hleLogDebug(Log::ME, 0, "bus granularity=%d/%d", numerator, denominator);
}

static int sceMePowerControlAvcPower(int power) {
	// The AVC block is implemented by the host video decoder.  Firmware still
	// expects this ME power-domain call to succeed when a game preview starts.
	return hleLogDebug(Log::ME, 0, "power=%d", power);
}

static int sceIdStorageReadLeafForVsh(int key, u32 bufferAddr) {
	if (!Memory::IsValidRange(bufferAddr, 512)) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	Memory::Memset(bufferAddr, 0, 512, "IdStorageLeaf");
	switch (key) {
	case 0x0044: {
		u8 mac[6]{};
		if (!ParseMacAddress(g_Config.sMACAddress, mac)) {
			return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ERROR, "invalid configured MAC address");
		}
		Memory::Memcpy(bufferAddr, mac, sizeof(mac), "IdStorageMacAddress");
		return hleLogDebug(Log::sceNet, 0, "MAC leaf");
	}
	case 0x0045:
		// WLAN firmware identity for generation 02g, matching Jpcsp's hardware
		// service model. This keeps the real WLAN firmware consumer off NAND.
		Memory::WriteUnchecked_U16(0x1002, bufferAddr);
		Memory::WriteUnchecked_U8(0x01, bufferAddr + 2);
		return hleLogDebug(Log::sceNet, 0, "WLAN firmware leaf");
	default:
		return hleLogWarning(Log::sceNet, SCE_KERNEL_ERROR_ERROR, "unsupported IdStorage leaf %04x", key);
	}
}

static int sceIdStorageLookupForVsh(int key, int offset, u32 bufferAddr, u32 length) {
	if (offset < 0 || offset > 512 || length > (u32)(512 - offset) ||
		!Memory::IsValidRange(bufferAddr, length)) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	u8 leaf[512]{};
	switch (key) {
	case 0x0044:
		if (!ParseMacAddress(g_Config.sMACAddress, leaf)) {
			return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ERROR, "invalid configured MAC address");
		}
		break;
	case 0x0045:
		leaf[0] = 0x02;
		leaf[1] = 0x10;
		leaf[2] = 0x01;
		break;
	default:
		return hleLogWarning(Log::sceNet, SCE_KERNEL_ERROR_ERROR, "unsupported IdStorage lookup %04x", key);
	}
	Memory::Memcpy(bufferAddr, leaf + offset, length, "IdStorageLookup");
	return hleLogDebug(Log::sceNet, 0, "key=%04x offset=%d length=%d", key, offset, length);
}

static const HLEFunction sceIdStorage_driver[] = {
	{0X6FE062D1, &WrapI_IIUU<sceIdStorageLookupForVsh>, "sceIdStorageLookup",   'i', "iixx", HLE_KERNEL_SYSCALL},
	{0XEB00C509, &WrapI_IU<sceIdStorageReadLeafForVsh>, "sceIdStorageReadLeaf", 'i', "ix",   HLE_KERNEL_SYSCALL},
};

static int sceMgr_driver_2B68BFD5() {
	return hleLogDebug(Log::ME, 0);
}

static int sceMgr_driver_7B11D7AD() {
	return hleLogDebug(Log::ME, 0);
}

static int sceMgr_driver_5BABFFAE(u32 stateAddr) {
	return hleLogDebug(Log::ME, 0, "state=%08x", stateAddr);
}

static int sceMgr_driver_84B044C7() {
	return hleLogDebug(Log::ME, 0);
}

static int sceMgr_driver_45EA1DB5() {
	return hleLogDebug(Log::ME, 0);
}

static int sceMgr_driver_7F37BECF() {
	return hleLogDebug(Log::ME, 0);
}

static int sceMgrDriverNoop() {
	return hleLogDebug(Log::ME, 0);
}

static const HLEFunction sceMSAudio_driver[] = {
	{0X543DBDD7, &WrapI_V<sceMSAudio_driver_543DBDD7>,               "sceMSAudio_driver_543DBDD7",              'i', "",       HLE_KERNEL_SYSCALL },
	{0X19474552, &WrapI_U<sceMSAudio_driver_19474552>,                "sceMSAudio_driver_19474552",              'i', "p",      HLE_KERNEL_SYSCALL },
	{0X59B4EE6D, &WrapI_I<sceMSAudio_driver_59B4EE6D>,               "sceMSAudio_driver_59B4EE6D",              'i', "i",      HLE_KERNEL_SYSCALL },
	{0XB7DB5AC6, &WrapI_V<sceMSAudio_driver_B7DB5AC6>,               "sceMSAudio_driver_B7DB5AC6",              'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceMeCore_driver[] = {
	{0X5DFF5C50, &WrapI_I<sceMeBootStart>,                           "sceMeBootStart",                          'i', "i",      HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceMePower_driver[] = {
	{0X1862B784, &WrapI_II<sceMePowerSetCpuGranularity>,             "sceMePower_driver_1862B784",              'i', "ii",     HLE_KERNEL_SYSCALL },
	{0XB37562AA, &WrapI_I<sceMePowerControlAvcPower>,                "sceMePowerControlAvcPower",               'i', "i",      HLE_KERNEL_SYSCALL },
	{0XE9F69ACF, &WrapI_II<sceMePowerSetBusGranularity>,             "sceMePower_driver_E9F69ACF",              'i', "ii",     HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceMgr_driver[] = {
	{0X2B68BFD5, &WrapI_V<sceMgr_driver_2B68BFD5>,                   "sceMgr_driver_2B68BFD5",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X09C11491, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_09C11491",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X20610CDF, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_20610CDF",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X401F71DC, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_401F71DC",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X45EA1DB5, &WrapI_V<sceMgr_driver_45EA1DB5>,                   "sceMgr_driver_45EA1DB5",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X5A63B6A4, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_5A63B6A4",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X5BABFFAE, &WrapI_U<sceMgr_driver_5BABFFAE>,                   "sceMgr_driver_5BABFFAE",                  'i', "x",      HLE_KERNEL_SYSCALL },
	{0X6B4C5BC5, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_6B4C5BC5",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X7B11D7AD, &WrapI_V<sceMgr_driver_7B11D7AD>,                   "sceMgr_driver_7B11D7AD",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X7F37BECF, &WrapI_V<sceMgr_driver_7F37BECF>,                   "sceMgr_driver_7F37BECF",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X84B044C7, &WrapI_V<sceMgr_driver_84B044C7>,                   "sceMgr_driver_84B044C7",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0X864EA078, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_864EA078",                  'i', "",       HLE_KERNEL_SYSCALL },
	{0XCEE87932, &WrapI_V<sceMgrDriverNoop>,                         "sceMgr_driver_CEE87932",                  'i', "",       HLE_KERNEL_SYSCALL },
};

static int sceBSManGetRetailProfile(u32 bufferAddr) {
	if (!Memory::IsValidRange(bufferAddr, 11))
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	// Retail 02g profile values from the failed hybrid attempt's Jpcsp 6.60 comparison.
	Memory::WriteUnchecked_U16(126, bufferAddr + 0);
	Memory::WriteUnchecked_U16(0, bufferAddr + 2);
	Memory::WriteUnchecked_U16(5, bufferAddr + 4);
	Memory::WriteUnchecked_U8(0x08, bufferAddr + 6);
	Memory::WriteUnchecked_U8(0x00, bufferAddr + 7);
	Memory::WriteUnchecked_U8(0x46, bufferAddr + 8);
	Memory::WriteUnchecked_U8(0x00, bufferAddr + 9);
	Memory::WriteUnchecked_U8(0x00, bufferAddr + 10);
	return hleLogDebug(Log::sceKernel, 0);
}

static const HLEFunction sceBSMan[] = {
	{0X46ACDAE3, &WrapI_U<sceBSManGetRetailProfile>,                 "sceBSMan_46ACDAE3",                       'i', "p",      HLE_KERNEL_SYSCALL },
};

static int sceFirmwareServiceNoop() {
	return hleLogDebug(Log::HLE, 0);
}

static int sceFirmwareServiceNoop1(u32 value) {
	return hleLogDebug(Log::HLE, 0, "value=%08x", value);
}

static int sceFirmwareServiceNoop3(u32 value0, u32 value1, u32 value2) {
	return hleLogDebug(Log::HLE, 0, "values=%08x,%08x,%08x", value0, value1, value2);
}

// Native impose imports these optional physical-device services. The hybrid
// model has no corresponding host hardware, so expose deterministic neutral state.
static const HLEFunction sceLcdc_driver[] = {
	{0X451FE1A1, &WrapI_V<sceFirmwareServiceNoop>,                   "sceLcdc_driver_451FE1A1",                 'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction scePspNpDrm_driver[] = {
	{0X2D88879A, &WrapI_V<sceFirmwareServiceNoop>,                   "sceNpDrmSetDebugMode",                    'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceAudio_driver[] = {
	{0X5182B550, &WrapI_V<sceFirmwareServiceNoop>,                   "sceAudio_driver_5182B550",                'i', "",       HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceDve_driver[] = {
	{0X253B69B6, &WrapI_UUU<sceFirmwareServiceNoop3>,                "sceDve_driver_253B69B6",                  'i', "xxx",    HLE_KERNEL_SYSCALL },
	{0X77832653, &WrapI_U<sceFirmwareServiceNoop1>,                  "sceDve_driver_77832653",                  'i', "x",      HLE_KERNEL_SYSCALL },
	{0XED631526, &WrapI_U<sceFirmwareServiceNoop1>,                  "sceDve_driver_ED631526",                  'i', "x",      HLE_KERNEL_SYSCALL },
};

static const HLEFunction sceNp9660_driver[] = {
	{0X8EF69DC6, &WrapI_U<sceFirmwareServiceNoop1>,                  "sceNp9660_driver_8EF69DC6",               'i', "x",      HLE_KERNEL_SYSCALL },
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
	{"sceBSMan", ARRAY_SIZE(sceBSMan), sceBSMan},
	{"sceLcdc_driver", ARRAY_SIZE(sceLcdc_driver), sceLcdc_driver},
	{"scePspNpDrm_driver", ARRAY_SIZE(scePspNpDrm_driver), scePspNpDrm_driver},
	{"sceAudio_driver", ARRAY_SIZE(sceAudio_driver), sceAudio_driver},
	{"sceDve_driver", ARRAY_SIZE(sceDve_driver), sceDve_driver},
	{"sceNp9660_driver", ARRAY_SIZE(sceNp9660_driver), sceNp9660_driver},
};

static const int numModules = ARRAY_SIZE(moduleList);

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

	// add new modules here.
	Register_sceGe_driver();
	RegisterHLEModule("sceLed_driver", ARRAY_SIZE(sceLed_driver), sceLed_driver);
	Register_sceVaudio_driver();
	RegisterHLEModule("sceMSAudio_driver", ARRAY_SIZE(sceMSAudio_driver), sceMSAudio_driver);
	RegisterHLEModule("sceMeCore_driver", ARRAY_SIZE(sceMeCore_driver), sceMeCore_driver);
	RegisterHLEModule("sceMePower_driver", ARRAY_SIZE(sceMePower_driver), sceMePower_driver);
	RegisterHLEModule("sceIdStorage_driver", ARRAY_SIZE(sceIdStorage_driver), sceIdStorage_driver);
	RegisterHLEModule("InitForKernel", ARRAY_SIZE(InitForKernel), InitForKernel);
	RegisterHLEModule("sceCodec_driver", ARRAY_SIZE(sceCodec_driver), sceCodec_driver);
	RegisterHLEModule("sceSysEventForKernel", ARRAY_SIZE(sceSysEventForKernel), sceSysEventForKernel);
	RegisterHLEModule("sceUmdMan_driver", ARRAY_SIZE(sceUmdMan_driver), sceUmdMan_driver);
	RegisterHLEModule("sceMgr_driver", ARRAY_SIZE(sceMgr_driver), sceMgr_driver);
	Register_vsh();
	Register_sceVideocodec();

	// Not ready to enable this due to apparent softlocks in Patapon 3.
	// Register_sceNpMatching2();
}
