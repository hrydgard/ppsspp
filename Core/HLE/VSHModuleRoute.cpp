// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

#include "Core/HLE/VSHModuleRoute.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

#include "Common/Crypto/sha256.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"

namespace {

constexpr VSHFirmwareProfile VSH_FIRMWARE_PROFILE_661_02G {
	"psp-2000-661-02g",
	660,
	"02g",
	"flash0/vsh/module/vshmain.prx",
	397158,
	"dd283103f78db14a8bd08c8aa03ecc01807850baed54fb77b3c38ba7ffff712f",
	"flash0/vsh/etc/index_02g.dat",
	496,
	"edd253bd015da11fdef6f008bd1cd400c4c3fc7f2547779759adf5c6073fdc2c",
};

// Order is part of the boot contract. These entries replace the two ad-hoc
// preload arrays that originally lived in sceKernelModule.cpp.
constexpr VSHModuleRouteEntry VSH_BOOT_MODULE_ROUTES[] = {
	{"flash0:/kd/dmacman.prx", "sceDMAManager", 0x7085E9CF, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/systimer.prx", "sceSystimer", 0xA19EC846, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/memlmd_01g.prx", "sceMemlmd", 0x951BE2FA, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/loadexec_01g.prx", "sceLoadExec", 0x4C568F26, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/lowio.prx", "sceLowIO_Driver", 0x821DB6D5, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/idstorage.prx", "sceIdStorage_Service", 0xF9BA0E2D, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/syscon.prx", "sceSYSCON_Driver", 0xE65C7304, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/rtc.prx", "sceRTC_Service", 0x5197D47A, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	// Sony's registry service owns system.ireg/system.dreg in Direct VSH.
	{"flash0:/kd/registry.prx", "sceRegistry_Service", 0x72A495D4, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, true},
	{"flash0:/kd/wlan.prx", "sceWlan_Driver", 0xF87A2BC6, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/wlanfirm_01g.prx", "sceWlanFirmMagpie_driver", 0x2B32AABD, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/utility.prx", "sceUtility_Driver", 0x4CCDC1C0, VSHModuleRoute::RealPrx, VSHModuleStage::BootDriver, false},
	{"flash0:/kd/vshbridge.prx", "sceVshBridge_Driver", 0x4C0D3A9A, VSHModuleRoute::RealPrxWithHooks, VSHModuleStage::UiFoundation, true},
	{"flash0:/vsh/module/paf.prx", "scePaf_Module", 0x05FD4661, VSHModuleRoute::RealPrx, VSHModuleStage::UiFoundation, true},
	{"flash0:/vsh/module/common_gui.prx", "sceVshCommonGui_Module", 0xF57F73F0, VSHModuleRoute::RealPrx, VSHModuleStage::UiFoundation, true},
	{"flash0:/vsh/module/common_util.prx", "sceVshCommonUtil_Module", 0x8E65BDEB, VSHModuleRoute::RealPrx, VSHModuleStage::UiFoundation, true},
};

// Names are corroborated against Jpcsp cd20cf31 and uOFW. The dispositions
// capture the measured Direct VSH boundary, not just whether another emulator
// happens to return zero for a NID.
constexpr VSHImportClassification VSH_IMPORT_CLASSIFICATIONS[] = {
	{"InterruptManagerForKernel", 0x58DD8978, "sceKernelRegisterIntrHandler", VSHImportDisposition::IntentionalUnresolved, "measured+uofw+jpcsp"},
	{"InterruptManagerForKernel", 0xF987B1F0, "sceKernelReleaseIntrHandler", VSHImportDisposition::IntentionalUnresolved, "measured+uofw+jpcsp"},
	{"InterruptManagerForKernel", 0x4D6E7305, "sceKernelEnableIntr", VSHImportDisposition::IntentionalUnresolved, "measured+uofw+jpcsp"},
	{"InterruptManagerForKernel", 0xD774BA45, "sceKernelDisableIntr", VSHImportDisposition::IntentionalUnresolved, "measured+uofw+jpcsp"},
	{"InterruptManagerForKernel", 0xFFA8B183, "sceKernelRegisterSubIntrHandler", VSHImportDisposition::HostHleImplemented, "uofw+ppsspp-subintr-manager"},
	{"InterruptManagerForKernel", 0xFB8E22EC, "sceKernelEnableSubIntr", VSHImportDisposition::HostHleImplemented, "uofw+ppsspp-subintr-manager"},

	{"ThreadManForKernel", 0xB7D098C6, "sceKernelCreateMutex", VSHImportDisposition::HostHleImplemented, "uofw+ppsspp-kernel-mutex"},
	{"ThreadManForKernel", 0xB011B11F, "sceKernelLockMutex", VSHImportDisposition::HostHleImplemented, "uofw+ppsspp-kernel-mutex"},
	{"ThreadManForKernel", 0x6B30100F, "sceKernelUnlockMutex", VSHImportDisposition::HostHleImplemented, "uofw+ppsspp-kernel-mutex"},
	{"ThreadManForKernel", 0xD979E9BF, "sceKernelAllocateFpl", VSHImportDisposition::IntentionalUnresolved, "measured+uofw+pspautotests"},
	{"SysMemForKernel", 0xC90B0992, "sceKernelGetUIDcontrolBlock", VSHImportDisposition::HostStateRequired, "uofw+jpcsp"},
	{"SysMemForKernel", 0x1AB50974, "sceKernelJointMemoryBlock", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0x22A114DC, "sceKernelMemset32", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0x53D50AC2, "sceKernelPartitionTotalMemSize", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"SysMemForKernel", 0x7158CE7E, "sceKernelAllocPartitionMemory", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0xB4F00CB5, "sceKernelGetCompiledSdkVersion", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0xC1A26C6F, "sceKernelFreePartitionMemory", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0xE860BE8F, "sceKernelQueryMemoryBlockInfo", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemForKernel", 0xEF29061C, "sceKernelGetGameInfo", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"SysMemUserForUser", 0x2A3E5280, "sceKernelQueryMemoryInfo", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp+pafmini-6.61"},
	{"InitForKernel", 0x9F9AE99C, "sceKernelSetInitCallback", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"LoadExecForKernel", 0x1F08547A, "sceKernelInvokeExitCallback", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"LoadExecForKernel", 0xB57D0DEC, "sceKernelCheckExitCallback", VSHImportDisposition::HostHleImplemented, "uofw"},

	{"sceSysEventForKernel", 0xCD9E4BB5, "sceKernelRegisterSysEventHandler", VSHImportDisposition::HostHleImplemented, "uofw+host-lifecycle"},
	{"sceSysEventForKernel", 0xD7D3FDCD, "sceKernelUnregisterSysEventHandler", VSHImportDisposition::HostHleImplemented, "uofw+host-lifecycle"},
	{"sceSuspendForKernel", 0x91A77137, "sceKernelRegisterSuspendHandler", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp+serialized-host-registry"},
	{"sceSuspendForKernel", 0xB43D1A8C, "sceKernelRegisterResumeHandler", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp+serialized-host-registry"},
	{"sceSuspendForKernel", 0xEADB1BD7, "sceKernelPowerLock", VSHImportDisposition::HostHleImplemented, "pspsdk+uofw+jpcsp"},
	{"sceSuspendForKernel", 0x3AEE7261, "sceKernelPowerUnlock", VSHImportDisposition::HostHleImplemented, "pspsdk+uofw+jpcsp"},

	{"sceDisplay_driver", 0x996881D2, "sceDisplay_driver_996881D2", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceDisplay_driver", 0x117C3E2C, "sceDisplayEnable", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"sceDisplay_driver", 0x33B620AF, "sceDisplayDisable", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"sceDisplay_driver", 0x3E17FE8D, "sceDisplaySetFirmwareLayer", VSHImportDisposition::HostHleImplemented, "native-6.61+prxtool"},
	{"sceGe_driver", 0x547EC5F0, "sceGeEdramGetHwSize", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"KDebugForKernel", 0x86010FCB, "sceKernelDipsw", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"KDebugForKernel", 0x47570AC5, "sceKernelIsToolMode", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"KDebugForKernel", 0xACF427DC, "sceKernelIsDevelopmentToolMode", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceBSMan", 0x46ACDAE3, "sceBSMan_46ACDAE3", VSHImportDisposition::HostHleImplemented, "jpcsp-6.60-retail-profile"},
	{"sceBSMan", 0x5A5BF52F, "sceBSMan_5A5BF52F", VSHImportDisposition::HostStateRequired, "psplibdoc+02g-hardware-query"},
	{"sceBSMan", 0x93274AD7, "sceBSMan_93274AD7", VSHImportDisposition::HostStateRequired, "psplibdoc+02g-hardware-query"},
	{"sceBSMan", 0xED295515, "sceBSMan_ED295515", VSHImportDisposition::HostStateRequired, "psplibdoc+02g-hardware-query"},

	{"scePadSvc_driver", 0x05C9C803, "scePadSvc_driver_05C9C803", VSHImportDisposition::IntentionalUnresolved, "pops-only-not-direct-vsh-game"},
	{"scePadSvc_driver", 0x0B3E6FD3, "scePadSvc_driver_0B3E6FD3", VSHImportDisposition::IntentionalUnresolved, "pops-only-not-direct-vsh-game"},
	{"scePadSvc_driver", 0x7CAB5A3D, "scePadSvc_driver_7CAB5A3D", VSHImportDisposition::IntentionalUnresolved, "pops-only-not-direct-vsh-game"},
	{"sceBtdun_lib", 0x412DA4A1, "sceBtdun_lib_412DA4A1", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0x523FA5A5, "sceBtdun_lib_523FA5A5", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0x5BED7B20, "sceBtdun_lib_5BED7B20", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0x5C91E089, "sceBtdun_lib_5C91E089", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0x966A10F5, "sceBtdun_lib_966A10F5", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0xBC01BAC1, "sceBtdun_lib_BC01BAC1", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceBtdun_lib", 0xDDA6D0A5, "sceBtdun_lib_DDA6D0A5", VSHImportDisposition::IntentionalUnresolved, "psp-go-bluetooth-absent-on-02g"},
	{"sceFatfs_driver", 0x3991C1A9, "sceFatfs_driver_3991C1A9", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceFatfs_driver", 0x5DEB8124, "sceFatfs_driver_5DEB8124", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceFatfs_driver", 0xC8537E11, "sceFatfs_driver_C8537E11", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceEFlash_driver", 0x8411F4ED, "sceEFlash_driver_8411F4ED", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceEFlash_driver", 0x9065E889, "sceEFlash_driver_9065E889", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceEFlash_driver", 0x966AB475, "sceEFlash_driver_966AB475", VSHImportDisposition::IntentionalUnresolved, "psp-go-eflash-absent-on-02g"},
	{"sceLed_driver", 0xEA24BE03, "sceLedSetMode", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"ModuleMgrForKernel", 0x939E4270, "sceKernelLoadModuleForKernel", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"ModuleMgrForKernel", 0xD4EE2D26, "sceKernelLoadModuleToBlock", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceCtrl_driver", 0xF6E94EA3, "sceCtrlSetSamplingMode", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceCtrl_driver", 0x1809B9FC, "sceCtrlGetButtonIntercept", VSHImportDisposition::HostHleImplemented, "uofw+native-6.61"},
	{"sceCtrl_driver", 0x2BA616AF, "sceCtrlPeekBufferPositive", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceCtrl_driver", 0xDF53E160, "sceCtrlSetSpecialButtonCallback", VSHImportDisposition::HostHleImplemented, "uofw"},
	{"sceCtrl_driver", 0xF8346777, "sceCtrlSetButtonIntercept", VSHImportDisposition::HostHleImplemented, "uofw+native-6.61"},

	{"sceUmd", 0x816E656B, "sceUmdSetSuspendResumeMode", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceMSAudio_driver", 0x543DBDD7, "sceMSAudio_driver_543DBDD7", VSHImportDisposition::CompatibilityNoOpCandidate, "jpcsp"},
	{"sceMeCore_driver", 0x5DFF5C50, "sceMeBootStart", VSHImportDisposition::CompatibilityNoOpCandidate, "uofw+jpcsp"},
	{"sceMePower_driver", 0x1862B784, "sceMePower_driver_1862B784", VSHImportDisposition::HostStateRequired, "uofw+jpcsp"},
	{"sceMePower_driver", 0xB37562AA, "sceMePowerControlAvcPower", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceMePower_driver", 0xE9F69ACF, "sceMePower_driver_E9F69ACF", VSHImportDisposition::HostStateRequired, "uofw+jpcsp"},
	{"sceIdStorage_driver", 0x6FE062D1, "sceIdStorageLookup", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceIdStorage_driver", 0xEB00C509, "sceIdStorageReadLeaf", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceVaudio_driver", 0x03B6807D, "sceVaudioChReserve", VSHImportDisposition::HostHleImplemented, "pspautotests+jpcsp"},
	{"sceVaudio_driver", 0x8986295E, "sceVaudioOutputBlocking", VSHImportDisposition::HostHleImplemented, "pspautotests+jpcsp"},
	{"UtilsForKernel", 0x39FFB756, "UtilsForKernel_39FFB756", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"UtilsForKernel", 0xA6B0A6B8, "UtilsForKernel_A6B0A6B8", VSHImportDisposition::HostHleImplemented, "uofw+jpcsp"},
	{"sceMgr_driver", 0x2B68BFD5, "sceMgr_driver_2B68BFD5", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x7B11D7AD, "sceMgr_driver_7B11D7AD", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x5BABFFAE, "sceMgr_driver_5BABFFAE", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x84B044C7, "sceMgr_driver_84B044C7", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x45EA1DB5, "sceMgr_driver_45EA1DB5", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x7F37BECF, "sceMgr_driver_7F37BECF", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x09C11491, "sceMgr_driver_09C11491", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x20610CDF, "sceMgr_driver_20610CDF", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x401F71DC, "sceMgr_driver_401F71DC", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x5A63B6A4, "sceMgr_driver_5A63B6A4", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x864EA078, "sceMgr_driver_864EA078", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0x6B4C5BC5, "sceMgr_driver_6B4C5BC5", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"sceMgr_driver", 0xCEE87932, "sceMgr_driver_CEE87932", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"ThreadManForKernel", 0xE7282CB6, "sceKernelAllocateFplCB", VSHImportDisposition::HostHleImplemented, "pspautotests+jpcsp"},
	{"ThreadManForKernel", 0xF6414A71, "sceKernelFreeFpl", VSHImportDisposition::HostHleImplemented, "pspautotests+jpcsp"},
	{"sceMSAudio_driver", 0x19474552, "sceMSAudio_driver_19474552", VSHImportDisposition::HostHleImplemented, "jpcsp"},
	{"vsh", 0xFC5BB3A0, "vsh_FC5BB3A0", VSHImportDisposition::HostStateRequired, "native-6.61-trace"},
	{"vsh", 0xFAB1C740, "vsh_FAB1C740", VSHImportDisposition::HostStateRequired, "native-6.61-trace"},
	{"sceMpegbase", 0x492B5E4B, "sceMpegBaseCscInit", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0xC01EC829, "sceVideocodecOpen", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x4F160BF4, "sceVideocodecReleaseEDRAM", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x2D31F5B1, "sceVideocodecGetEDRAM", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x17099F0A, "sceVideocodecInit", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x26927D19, "sceVideocodecGetVersion", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x745A7B7A, "sceVideocodecSetMemory", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x893B32B1, "sceVideocodec_893B32B1", VSHImportDisposition::CompatibilityNoOpCandidate, "jpcsp"},
	{"sceVideocodec", 0xDBA273FA, "sceVideocodecDecode", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0xA2F0564E, "sceVideocodecStop", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x307E6E1C, "sceVideocodecDelete", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceVideocodec", 0x627B7D42, "sceVideocodecGetSEI", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceMpegbase", 0x0530BE4E, "sceMpegbase_0530BE4E", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceMpegbase", 0x91929A21, "sceMpegBaseCscAvc", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceMpegbase", 0x304882E1, "sceMpegBaseCscAvcRange", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceMpegbase", 0xAC9E717E, "sceMpegbase_AC9E717E", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
	{"sceMpegbase", 0x7AC0321A, "sceMpegBaseYCrCbCopy", VSHImportDisposition::HostHleImplemented, "psplibdoc+jpcsp"},
};

struct ModuleAuditRecord {
	const VSHModuleRouteEntry *entry = nullptr;
	bool attempted = false;
	bool loaded = false;
	bool started = false;
	bool fake = false;
	int loadResult = 0;
	int startResult = 0;
	std::string actualModuleName;
	u32 crc = 0;
};

struct UnresolvedAuditRecord {
	std::string importingModule;
	std::string importLibrary;
	u32 nid = 0;
	VSHUnresolvedKind kind = VSHUnresolvedKind::NoProvider;
	u64 loadHits = 0;
	u64 runtimeHits = 0;
	bool resolved = false;
};

struct ResolvedImportAuditRecord {
	std::string importingModule;
	std::string importLibrary;
	u32 nid = 0;
	VSHImportOwner owner = VSHImportOwner::HostHle;
	std::string provider;
	bool rejectedPlaceholder = false;
	u64 linkHits = 0;
};

struct ModuleLifecycleAuditRecord {
	u64 sequence = 0;
	std::string path;
	int moduleId = -1;
	std::string moduleName;
	u32 crc = 0;
	VSHModuleRoute route = VSHModuleRoute::Unsupported;
	bool fake = false;
	bool identityBounded = false;
	bool identityMatched = true;
	bool startAttempted = false;
	bool started = false;
	bool startCompleted = false;
	int startResult = 0;
	int entryResult = 0;
	bool stopped = false;
	int stopResult = 0;
	bool unloaded = false;
	int unloadResult = 0;
};

struct ModuleLoadFailureAuditRecord {
	std::string path;
	int result = 0;
	u64 attempts = 0;
};

struct HleOverrideAuditRecord {
	std::string exportingModule;
	std::string exportLibrary;
	u32 nid = 0;
	u64 hits = 0;
};

std::mutex auditMutex;
std::vector<ModuleAuditRecord> moduleAudit;
std::vector<UnresolvedAuditRecord> unresolvedAudit;
std::vector<ResolvedImportAuditRecord> resolvedImportAudit;
std::vector<ModuleLifecycleAuditRecord> moduleLifecycleAudit;
std::vector<ModuleLoadFailureAuditRecord> moduleLoadFailureAudit;
std::vector<HleOverrideAuditRecord> hleOverrideAudit;
u64 nextModuleSequence = 1;

void EnsureAuditInitializedLocked() {
	if (!moduleAudit.empty()) {
		return;
	}
	moduleAudit.reserve(std::size(VSH_BOOT_MODULE_ROUTES));
	for (const auto &entry : VSH_BOOT_MODULE_ROUTES) {
		moduleAudit.push_back({&entry});
	}
}

ModuleAuditRecord *FindModuleAuditLocked(const VSHModuleRouteEntry &entry) {
	EnsureAuditInitializedLocked();
	for (auto &record : moduleAudit) {
		if (record.entry == &entry || !strcmp(record.entry->path, entry.path)) {
			return &record;
		}
	}
	return nullptr;
}

std::string NormalizePspPath(std::string_view path) {
	std::string normalized;
	normalized.reserve(path.size());
	for (char c : path) {
		if (c == ':') {
			continue;
		}
		if (c == '\\') {
			c = '/';
		}
		normalized.push_back((char)std::tolower((unsigned char)c));
	}
	return normalized;
}

const VSHModuleRouteEntry *FindBootRoute(std::string_view path, std::string_view moduleName) {
	const std::string normalizedPath = NormalizePspPath(path);
	// A concrete flash path is authoritative. Utility frontends can reuse a
	// boot module name while still being a distinct PRX identity.
	for (const auto &entry : VSH_BOOT_MODULE_ROUTES) {
		if (!normalizedPath.empty() && normalizedPath == NormalizePspPath(entry.path)) {
			return &entry;
		}
	}
	if (startsWith(normalizedPath, "flash0/"))
		return nullptr;

	// host0 aliases do not retain their flash path, so module identity is the
	// only stable fallback for those loads.
	for (const auto &entry : VSH_BOOT_MODULE_ROUTES) {
		if (entry.moduleName && !moduleName.empty() && equalsNoCase(entry.moduleName, moduleName))
			return &entry;
	}
	return nullptr;
}

ModuleLifecycleAuditRecord *FindLifecycleModuleLocked(int moduleId) {
	for (auto &record : moduleLifecycleAudit) {
		if (record.moduleId == moduleId) {
			return &record;
		}
	}
	return nullptr;
}

bool LifecycleModuleHasHostHooks(const ModuleLifecycleAuditRecord &module) {
	for (const auto &resolved : resolvedImportAudit) {
		if (resolved.owner == VSHImportOwner::HostHle && equalsNoCase(resolved.importingModule, module.moduleName)) {
			return true;
		}
	}
	return false;
}

const char *UnresolvedKindName(VSHUnresolvedKind kind) {
	switch (kind) {
	case VSHUnresolvedKind::KnownHleModuleMissingNid:
		return "known-hle-missing-nid";
	case VSHUnresolvedKind::NoProvider:
		return "no-provider";
	default:
		return "unknown";
	}
}

const char *ImportOwnerName(VSHImportOwner owner) {
	switch (owner) {
	case VSHImportOwner::HostHle: return "host-hle";
	case VSHImportOwner::RealPrx: return "real-prx";
	default: return "unknown";
	}
}

bool ValidateProfileFile(const Path &path, const char *relativePath, u64 expectedSize, const char *expectedSha256, std::string *errorString) {
	if (!File::Exists(path)) {
		if (errorString) {
			*errorString = "missing profile file " + std::string(relativePath);
		}
		return false;
	}

	const u64 actualSize = File::GetFileSize(path);
	if (actualSize != expectedSize) {
		if (errorString) {
			*errorString = StringFromFormat("profile file %s has size %llu, expected %llu", relativePath,
				(unsigned long long)actualSize, (unsigned long long)expectedSize);
		}
		return false;
	}

	std::string data;
	if (!File::ReadBinaryFileToString(path, &data)) {
		if (errorString) {
			*errorString = "could not read profile file " + std::string(relativePath);
		}
		return false;
	}

	sha256_context context;
	u8 digest[32];
	sha256_starts(&context);
	sha256_update(&context, reinterpret_cast<const u8 *>(data.data()), (u32)data.size());
	sha256_finish(&context, digest);
	std::string actualSha256;
	actualSha256.reserve(sizeof(digest) * 2);
	for (u8 value : digest) {
		actualSha256 += StringFromFormat("%02x", value);
	}
	if (!equalsNoCase(actualSha256, expectedSha256)) {
		if (errorString) {
			*errorString = "profile file " + std::string(relativePath) + " does not match the 6.61 02g fingerprint";
		}
		return false;
	}

	return true;
}

}  // namespace

const VSHFirmwareProfile &VSHGetFirmwareProfile() {
	return VSH_FIRMWARE_PROFILE_661_02G;
}

bool VSHValidateFirmwareProfile(std::string *errorString) {
	const auto &profile = VSHGetFirmwareProfile();
	const Path primaryModule = g_Config.nandRootDirectory / profile.primaryModulePath;
	const Path index = g_Config.nandRootDirectory / profile.indexPath;
	if (!ValidateProfileFile(primaryModule, profile.primaryModulePath, profile.primaryModuleSize, profile.primaryModuleSha256, errorString)) {
		return false;
	}
	if (!ValidateProfileFile(index, profile.indexPath, profile.indexSize, profile.indexSha256, errorString)) {
		return false;
	}
	if (errorString) {
		errorString->clear();
	}
	return true;
}

const VSHModuleRouteEntry *VSHGetBootModuleRoutes(size_t *count) {
	if (count) {
		*count = std::size(VSH_BOOT_MODULE_ROUTES);
	}
	return VSH_BOOT_MODULE_ROUTES;
}

const char *VSHModuleRouteName(VSHModuleRoute route) {
	switch (route) {
	case VSHModuleRoute::RealPrx: return "real-prx";
	case VSHModuleRoute::HostHle: return "host-hle";
	case VSHModuleRoute::RealPrxWithHooks: return "real-prx-with-hooks";
	case VSHModuleRoute::Unsupported: return "unsupported";
	default: return "unknown";
	}
}

const char *VSHModuleStageName(VSHModuleStage stage) {
	switch (stage) {
	case VSHModuleStage::BootDriver: return "boot-driver";
	case VSHModuleStage::UiFoundation: return "ui-foundation";
	default: return "unknown";
	}
}

bool VSHRouteForcesRealModule(std::string_view moduleName) {
	for (const auto &entry : VSH_BOOT_MODULE_ROUTES) {
		if (!entry.moduleName) {
			continue;
		}
		if ((entry.route == VSHModuleRoute::RealPrx || entry.route == VSHModuleRoute::RealPrxWithHooks) && equalsNoCase(moduleName, entry.moduleName)) {
			return true;
		}
	}
	return false;
}

const VSHImportClassification *VSHFindImportClassification(std::string_view library, u32 nid) {
	for (const auto &classification : VSH_IMPORT_CLASSIFICATIONS) {
		if (classification.nid == nid && equalsNoCase(classification.library, library)) {
			return &classification;
		}
	}
	return nullptr;
}

const char *VSHImportDispositionName(VSHImportDisposition disposition) {
	switch (disposition) {
	case VSHImportDisposition::HostHleImplemented: return "host-hle-implemented";
	case VSHImportDisposition::HostHleCandidate: return "host-hle-candidate";
	case VSHImportDisposition::HostStateRequired: return "host-state-required";
	case VSHImportDisposition::CompatibilityNoOpCandidate: return "compat-noop-candidate";
	case VSHImportDisposition::IntentionalUnresolved: return "intentional-unresolved";
	case VSHImportDisposition::Unclassified: return "unclassified";
	default: return "unknown";
	}
}

void VSHRouteAuditReset() {
	std::lock_guard<std::mutex> guard(auditMutex);
	moduleAudit.clear();
	unresolvedAudit.clear();
	resolvedImportAudit.clear();
	moduleLifecycleAudit.clear();
	moduleLoadFailureAudit.clear();
	hleOverrideAudit.clear();
	nextModuleSequence = 1;
	EnsureAuditInitializedLocked();
}

void VSHRouteAuditRecordModuleAttempt(const VSHModuleRouteEntry &entry) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindModuleAuditLocked(entry)) {
		record->attempted = true;
	}
}

void VSHRouteAuditRecordModuleLoad(const VSHModuleRouteEntry &entry, int moduleId, std::string_view actualModuleName, u32 crc, bool isFake) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindModuleAuditLocked(entry)) {
		record->attempted = true;
		record->loaded = moduleId >= 0;
		record->loadResult = moduleId;
		record->actualModuleName = actualModuleName;
		record->crc = crc;
		record->fake = isFake;
	}
}

void VSHRouteAuditRecordModuleLoadFailure(const VSHModuleRouteEntry &entry, int result) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindModuleAuditLocked(entry)) {
		record->attempted = true;
		record->loaded = false;
		record->loadResult = result;
	}
}

void VSHRouteAuditRecordModuleStart(const VSHModuleRouteEntry &entry, int result) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindModuleAuditLocked(entry)) {
		record->startResult = result;
		record->started = result >= 0;
	}
}

void VSHRouteAuditRecordUnresolved(std::string_view importingModule, std::string_view importLibrary, u32 nid, VSHUnresolvedKind kind, bool runtimeHit) {
	std::lock_guard<std::mutex> guard(auditMutex);
	for (auto &record : unresolvedAudit) {
		if (record.nid == nid && record.kind == kind && record.importingModule == importingModule && record.importLibrary == importLibrary) {
			record.resolved = false;
			if (runtimeHit) {
				record.runtimeHits++;
			} else {
				record.loadHits++;
			}
			return;
		}
	}
	UnresolvedAuditRecord record;
	record.importingModule = importingModule;
	record.importLibrary = importLibrary;
	record.nid = nid;
	record.kind = kind;
	record.loadHits = runtimeHit ? 0 : 1;
	record.runtimeHits = runtimeHit ? 1 : 0;
	unresolvedAudit.push_back(std::move(record));
}

void VSHRouteAuditRecordResolved(std::string_view importingModule, std::string_view importLibrary, u32 nid, VSHImportOwner owner, std::string_view provider, bool rejectedPlaceholder) {
	std::lock_guard<std::mutex> guard(auditMutex);
	for (auto &record : unresolvedAudit) {
		if (record.nid == nid && record.importingModule == importingModule && record.importLibrary == importLibrary) {
			record.resolved = true;
		}
	}
	for (auto &record : resolvedImportAudit) {
		if (record.nid == nid && record.importingModule == importingModule && record.importLibrary == importLibrary) {
			record.owner = owner;
			record.provider = provider;
			record.rejectedPlaceholder = record.rejectedPlaceholder || rejectedPlaceholder;
			record.linkHits++;
			return;
		}
	}
	ResolvedImportAuditRecord record;
	record.importingModule = importingModule;
	record.importLibrary = importLibrary;
	record.nid = nid;
	record.owner = owner;
	record.provider = provider;
	record.rejectedPlaceholder = rejectedPlaceholder;
	record.linkHits = 1;
	resolvedImportAudit.push_back(std::move(record));
}

void VSHRouteAuditRecordLifecycleLoad(std::string_view path, int moduleId, std::string_view moduleName, u32 crc, bool isFake) {
	std::lock_guard<std::mutex> guard(auditMutex);
	ModuleLifecycleAuditRecord *record = FindLifecycleModuleLocked(moduleId);
	if (!record) {
		ModuleLifecycleAuditRecord created;
		created.sequence = nextModuleSequence++;
		created.moduleId = moduleId;
		moduleLifecycleAudit.push_back(std::move(created));
		record = &moduleLifecycleAudit.back();
	}
	record->path = path;
	record->moduleName = moduleName;
	record->crc = crc;
	record->fake = isFake;

	const VSHModuleRouteEntry *bootRoute = FindBootRoute(path, moduleName);
	if (bootRoute) {
		record->route = bootRoute->route;
		record->identityBounded = true;
		record->identityMatched = equalsNoCase(moduleName, bootRoute->moduleName) && crc == bootRoute->expectedCrc;
		if (isFake != (bootRoute->route == VSHModuleRoute::HostHle)) {
			record->identityMatched = false;
		}
	} else {
		record->route = isFake ? VSHModuleRoute::HostHle : VSHModuleRoute::RealPrx;
		record->identityBounded = equalsNoCase(moduleName, "vsh_module");
		record->identityMatched = true;
	}
}

void VSHRouteAuditRecordLifecycleLoadFailure(std::string_view path, int result) {
	std::lock_guard<std::mutex> guard(auditMutex);
	for (auto &record : moduleLoadFailureAudit) {
		if (record.result == result && record.path == path) {
			record.attempts++;
			return;
		}
	}
	ModuleLoadFailureAuditRecord record;
	record.path = path;
	record.result = result;
	record.attempts = 1;
	moduleLoadFailureAudit.push_back(std::move(record));
}

void VSHRouteAuditRecordLifecycleStart(int moduleId, int result, bool completed) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindLifecycleModuleLocked(moduleId)) {
		if (completed) {
			record->startCompleted = true;
			record->entryResult = result;
		} else {
			record->startAttempted = true;
			record->startResult = result;
			record->started = record->started || result >= 0;
		}
	}
}

void VSHRouteAuditRecordLifecycleStop(int moduleId, int result) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindLifecycleModuleLocked(moduleId)) {
		record->stopped = true;
		record->stopResult = result;
	}
}

void VSHRouteAuditRecordLifecycleUnload(int moduleId, int result) {
	std::lock_guard<std::mutex> guard(auditMutex);
	if (auto *record = FindLifecycleModuleLocked(moduleId)) {
		record->unloaded = result >= 0;
		record->unloadResult = result;
	}
}

void VSHRouteAuditRecordHleOverride(std::string_view exportingModule, std::string_view exportLibrary, u32 nid) {
	std::lock_guard<std::mutex> guard(auditMutex);
	for (auto &record : hleOverrideAudit) {
		if (record.nid == nid && record.exportingModule == exportingModule && record.exportLibrary == exportLibrary) {
			record.hits++;
			return;
		}
	}
	HleOverrideAuditRecord record;
	record.exportingModule = exportingModule;
	record.exportLibrary = exportLibrary;
	record.nid = nid;
	record.hits = 1;
	hleOverrideAudit.push_back(std::move(record));
}

std::string VSHRouteAuditReport() {
	std::lock_guard<std::mutex> guard(auditMutex);
	EnsureAuditInitializedLocked();

	u64 attempted = 0;
	u64 loaded = 0;
	u64 started = 0;
	u64 failed = 0;
	u64 fake = 0;
	u64 routeMismatches = 0;
	for (const auto &record : moduleAudit) {
		attempted += record.attempted;
		loaded += record.loaded;
		started += record.started;
		failed += record.attempted && (!record.loaded || !record.started);
		fake += record.fake;
		if (record.loaded) {
			if (record.fake != (record.entry->route == VSHModuleRoute::HostHle) ||
				!equalsNoCase(record.actualModuleName, record.entry->moduleName) || record.crc != record.entry->expectedCrc) {
				routeMismatches++;
			}
		}
	}

	u64 lifecycleReal = 0;
	u64 lifecycleFake = 0;
	u64 lifecycleStarted = 0;
	u64 lifecycleStartCompleted = 0;
	u64 lifecycleStopped = 0;
	u64 lifecycleUnloaded = 0;
	u64 lifecycleIdentityMismatches = 0;
	u64 lifecycleIdentityBounded = 0;
	u64 lifecycleHooked = 0;
	for (const auto &record : moduleLifecycleAudit) {
		lifecycleReal += !record.fake;
		lifecycleFake += record.fake;
		lifecycleStarted += record.started;
		lifecycleStartCompleted += record.startCompleted;
		lifecycleStopped += record.stopped;
		lifecycleUnloaded += record.unloaded;
		lifecycleIdentityBounded += record.identityBounded;
		lifecycleIdentityMismatches += record.identityBounded && !record.identityMatched;
		lifecycleHooked += LifecycleModuleHasHostHooks(record);
	}
	u64 lifecycleLoadFailureAttempts = 0;
	for (const auto &record : moduleLoadFailureAudit) {
		lifecycleLoadFailureAttempts += record.attempts;
	}

	u64 unresolvedLoadHits = 0;
	u64 unresolvedRuntimeHits = 0;
	u64 unresolvedRuntimeSites = 0;
	u64 unresolvedRuntimeClassifiedSites = 0;
	u64 unresolvedRuntimeUnclassifiedSites = 0;
	u64 unresolvedCurrentSites = 0;
	for (const auto &record : unresolvedAudit) {
		unresolvedLoadHits += record.loadHits;
		unresolvedRuntimeHits += record.runtimeHits;
		unresolvedRuntimeSites += record.runtimeHits != 0;
		unresolvedCurrentSites += !record.resolved;
		if (record.runtimeHits != 0) {
			if (VSHFindImportClassification(record.importLibrary, record.nid)) {
				unresolvedRuntimeClassifiedSites++;
			} else {
				unresolvedRuntimeUnclassifiedSites++;
			}
		}
	}
	u64 hostHleSites = 0;
	u64 realPrxSites = 0;
	u64 placeholderRejectedSites = 0;
	for (const auto &record : resolvedImportAudit) {
		hostHleSites += record.owner == VSHImportOwner::HostHle;
		realPrxSites += record.owner == VSHImportOwner::RealPrx;
		placeholderRejectedSites += record.rejectedPlaceholder;
	}

	std::ostringstream report;
	report << "vsh-route-audit profile=" << VSHGetFirmwareProfile().id
		<< " modules=" << attempted << '/' << std::size(VSH_BOOT_MODULE_ROUTES)
		<< " loaded=" << loaded
		<< " started=" << started
		<< " failed=" << failed
		<< " fake=" << fake
		<< " route-mismatch=" << routeMismatches
		<< " lifecycle-loaded=" << moduleLifecycleAudit.size()
		<< " lifecycle-real=" << lifecycleReal
		<< " lifecycle-fake=" << lifecycleFake
		<< " lifecycle-started=" << lifecycleStarted
		<< " lifecycle-start-complete=" << lifecycleStartCompleted
		<< " lifecycle-stopped=" << lifecycleStopped
		<< " lifecycle-unloaded=" << lifecycleUnloaded
		<< " lifecycle-hooked=" << lifecycleHooked
		<< " lifecycle-identity-bounded=" << lifecycleIdentityBounded
		<< " lifecycle-identity-mismatch=" << lifecycleIdentityMismatches
		<< " load-failure-sites=" << moduleLoadFailureAudit.size()
		<< " load-failure-attempts=" << lifecycleLoadFailureAttempts
		<< " hle-overrides-real=" << hleOverrideAudit.size()
		<< " import-host-hle=" << hostHleSites
		<< " import-real-prx=" << realPrxSites
		<< " placeholder-real=" << placeholderRejectedSites
		<< " unresolved-sites=" << unresolvedAudit.size()
		<< " unresolved-current=" << unresolvedCurrentSites
		<< " unresolved-load-hits=" << unresolvedLoadHits
		<< " unresolved-runtime-sites=" << unresolvedRuntimeSites
		<< " unresolved-runtime-hits=" << unresolvedRuntimeHits
		<< " runtime-classified=" << unresolvedRuntimeClassifiedSites
		<< " runtime-unclassified=" << unresolvedRuntimeUnclassifiedSites;
	return report.str();
}

void VSHRouteAuditLogSummary(const char *phase) {
	const std::string report = VSHRouteAuditReport();
	NOTICE_LOG(Log::sceModule, "%s phase=%s", report.c_str(), phase ? phase : "unknown");

	std::lock_guard<std::mutex> guard(auditMutex);
	for (const auto &record : moduleAudit) {
		if (!record.attempted) {
			continue;
		}
		INFO_LOG(Log::sceModule,
			"vsh-route module path=%s requested=%s stage=%s required=%d expected=%s expected-crc=%08x actual=%s crc=%08x loaded=%d started=%d fake=%d load-result=%d start-result=%d",
			record.entry->path,
			VSHModuleRouteName(record.entry->route),
			VSHModuleStageName(record.entry->stage),
			record.entry->required,
			record.entry->moduleName,
			record.entry->expectedCrc,
			record.actualModuleName.empty() ? "unknown" : record.actualModuleName.c_str(),
			record.crc,
			record.loaded,
			record.started,
			record.fake,
			record.loadResult,
			record.startResult);
	}

	std::vector<const ModuleLifecycleAuditRecord *> orderedModules;
	orderedModules.reserve(moduleLifecycleAudit.size());
	for (const auto &record : moduleLifecycleAudit) {
		orderedModules.push_back(&record);
	}
	std::sort(orderedModules.begin(), orderedModules.end(), [](const auto *left, const auto *right) {
		return left->sequence < right->sequence;
	});
	for (const auto *record : orderedModules) {
		u64 hostImports = 0;
		u64 realImports = 0;
		u64 currentUnresolved = 0;
		u64 runtimeUnresolved = 0;
		u64 hleOverrides = 0;
		for (const auto &resolved : resolvedImportAudit) {
			if (equalsNoCase(resolved.importingModule, record->moduleName)) {
				hostImports += resolved.owner == VSHImportOwner::HostHle;
				realImports += resolved.owner == VSHImportOwner::RealPrx;
			}
		}
		for (const auto &unresolved : unresolvedAudit) {
			if (equalsNoCase(unresolved.importingModule, record->moduleName)) {
				currentUnresolved += !unresolved.resolved;
				runtimeUnresolved += !unresolved.resolved && unresolved.runtimeHits != 0;
			}
		}
		for (const auto &overridden : hleOverrideAudit) {
			if (equalsNoCase(overridden.exportingModule, record->moduleName)) {
				hleOverrides++;
			}
		}
		NOTICE_LOG(Log::sceModule,
			"vsh-module seq=%llu uid=%d name=%s path=\"%s\" route=%s crc=%08x identity=%s fake=%d start-attempted=%d started=%d start-complete=%d start-result=%d entry-result=%d stopped=%d stop-result=%d unloaded=%d unload-result=%d imports-hle=%llu imports-real=%llu imports-unresolved=%llu runtime-unresolved=%llu exports-overridden-by-hle=%llu",
			(unsigned long long)record->sequence, record->moduleId,
			record->moduleName.empty() ? "unknown" : record->moduleName.c_str(),
			record->path.empty() ? "unknown" : record->path.c_str(), VSHModuleRouteName(record->route), record->crc,
			record->identityBounded ? (record->identityMatched ? "match" : "mismatch") : "observed", record->fake,
			record->startAttempted, record->started, record->startCompleted, record->startResult, record->entryResult,
			record->stopped, record->stopResult, record->unloaded, record->unloadResult,
			(unsigned long long)hostImports, (unsigned long long)realImports,
			(unsigned long long)currentUnresolved, (unsigned long long)runtimeUnresolved,
			(unsigned long long)hleOverrides);
	}

	std::vector<const ModuleLoadFailureAuditRecord *> orderedFailures;
	orderedFailures.reserve(moduleLoadFailureAudit.size());
	for (const auto &record : moduleLoadFailureAudit) {
		orderedFailures.push_back(&record);
	}
	std::sort(orderedFailures.begin(), orderedFailures.end(), [](const auto *left, const auto *right) {
		return std::tie(left->path, left->result) < std::tie(right->path, right->result);
	});
	for (const auto *record : orderedFailures) {
		NOTICE_LOG(Log::sceModule, "vsh-module-failure path=\"%s\" result=%08x attempts=%llu",
			record->path.c_str(), (u32)record->result, (unsigned long long)record->attempts);
	}
	std::vector<const UnresolvedAuditRecord *> orderedUnresolved;
	orderedUnresolved.reserve(unresolvedAudit.size());
	for (const auto &record : unresolvedAudit) {
		orderedUnresolved.push_back(&record);
	}
	std::sort(orderedUnresolved.begin(), orderedUnresolved.end(), [](const auto *left, const auto *right) {
		return std::tie(left->importingModule, left->importLibrary, left->nid) < std::tie(right->importingModule, right->importLibrary, right->nid);
	});
	for (const auto *record : orderedUnresolved) {
		const VSHImportClassification *classification = VSHFindImportClassification(record->importLibrary, record->nid);
		const char *functionName = classification ? classification->functionName : "unknown";
		const char *disposition = classification ? VSHImportDispositionName(classification->disposition) : VSHImportDispositionName(VSHImportDisposition::Unclassified);
		const char *evidence = classification ? classification->evidence : "none";
		if (record->runtimeHits != 0 && !record->resolved) {
			NOTICE_LOG(Log::sceModule,
				"vsh-unresolved caller=%s library=%s nid=%08x function=%s disposition=%s evidence=%s kind=%s load-hits=%llu runtime-hits=%llu",
				record->importingModule.c_str(), record->importLibrary.c_str(), record->nid,
				functionName, disposition, evidence, UnresolvedKindName(record->kind),
				(unsigned long long)record->loadHits, (unsigned long long)record->runtimeHits);
		} else if (!record->resolved) {
			INFO_LOG(Log::sceModule,
				"vsh-unresolved caller=%s library=%s nid=%08x function=%s disposition=%s evidence=%s kind=%s load-hits=%llu runtime-hits=%llu",
				record->importingModule.c_str(), record->importLibrary.c_str(), record->nid,
				functionName, disposition, evidence, UnresolvedKindName(record->kind),
				(unsigned long long)record->loadHits, (unsigned long long)record->runtimeHits);
		}
	}

	std::vector<const ResolvedImportAuditRecord *> orderedResolved;
	orderedResolved.reserve(resolvedImportAudit.size());
	for (const auto &record : resolvedImportAudit) {
		orderedResolved.push_back(&record);
	}
	std::sort(orderedResolved.begin(), orderedResolved.end(), [](const auto *left, const auto *right) {
		return std::tie(left->importingModule, left->importLibrary, left->nid) < std::tie(right->importingModule, right->importLibrary, right->nid);
	});
	for (const auto *record : orderedResolved) {
		const VSHImportClassification *classification = VSHFindImportClassification(record->importLibrary, record->nid);
		const char *functionName = classification ? classification->functionName : "known-resolved-import";
		const char *disposition = classification ? VSHImportDispositionName(classification->disposition) : "not-classified-required";
		if (classification || record->rejectedPlaceholder) {
			NOTICE_LOG(Log::sceModule,
				"vsh-resolved caller=%s library=%s nid=%08x function=%s owner=%s provider=%s disposition=%s placeholder-rejected=%d link-hits=%llu",
				record->importingModule.c_str(), record->importLibrary.c_str(), record->nid, functionName,
				ImportOwnerName(record->owner), record->provider.c_str(), disposition, record->rejectedPlaceholder,
				(unsigned long long)record->linkHits);
		} else {
			INFO_LOG(Log::sceModule,
				"vsh-resolved caller=%s library=%s nid=%08x function=%s owner=%s provider=%s disposition=%s placeholder-rejected=%d link-hits=%llu",
				record->importingModule.c_str(), record->importLibrary.c_str(), record->nid, functionName,
				ImportOwnerName(record->owner), record->provider.c_str(), disposition, record->rejectedPlaceholder,
				(unsigned long long)record->linkHits);
		}
	}

	std::vector<const HleOverrideAuditRecord *> orderedOverrides;
	orderedOverrides.reserve(hleOverrideAudit.size());
	for (const auto &record : hleOverrideAudit) {
		orderedOverrides.push_back(&record);
	}
	std::sort(orderedOverrides.begin(), orderedOverrides.end(), [](const auto *left, const auto *right) {
		return std::tie(left->exportingModule, left->exportLibrary, left->nid) < std::tie(right->exportingModule, right->exportLibrary, right->nid);
	});
	for (const auto *record : orderedOverrides) {
		INFO_LOG(Log::sceModule,
			"vsh-hle-override exporter=%s library=%s nid=%08x owner=host-hle real-export-suppressed=1 hits=%llu",
			record->exportingModule.c_str(), record->exportLibrary.c_str(), record->nid,
			(unsigned long long)record->hits);
	}
}
