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
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "ppsspp_config.h"

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/URL.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log/LogManager.h"
#include "Common/Math/CrossSIMD.h"
#include "Common/OSVersion.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/System/Request.h"
#include "Core/Config.h"
#include "Core/ConfigSettings.h"
#include "Core/ConfigValues.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Instance.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Util/PathUtil.h"

#include "GPU/Common/FramebufferManagerCommon.h"

// TODO: Find a better place for this.
http::RequestManager g_DownloadManager;

Config g_Config;

static bool jitForcedOff;

#ifdef _DEBUG
static const std::string_view logSectionName = "LogDebug";
#else
static const std::string_view logSectionName = "Log";
#endif

static const std::vector<std::string_view> defaultProAdhocServerList = {
	"socom.cc", "psp.gameplayer.club",  // TODO: Add some saved recent history too?
};

std::string GPUBackendToString(GPUBackend backend) {
	switch (backend) {
	case GPUBackend::OPENGL:
		return "OPENGL";
	case GPUBackend::DIRECT3D11:
		return "DIRECT3D11";
	case GPUBackend::VULKAN:
		return "VULKAN";
	}
	// Intentionally not a default so we get a warning.
	return "INVALID";
}

GPUBackend GPUBackendFromString(std::string_view backend) {
	if (equalsNoCase(backend, "OPENGL") || backend == "0")
		return GPUBackend::OPENGL;
	if (equalsNoCase(backend, "DIRECT3D11") || backend == "2")
		return GPUBackend::DIRECT3D11;
	if (equalsNoCase(backend, "VULKAN") || backend == "3")
		return GPUBackend::VULKAN;
	return GPUBackend::OPENGL;
}

std::string DefaultLangRegion() {
	// Unfortunate default.  There's no need to use bFirstRun, since this is only a default.
	static std::string defaultLangRegion = "en_US";
	std::string langRegion = System_GetProperty(SYSPROP_LANGREGION);
	if (g_i18nrepo.IniExists(langRegion)) {
		defaultLangRegion = langRegion;
	} else if (langRegion.length() >= 3) {
		// Don't give up.  Let's try a fuzzy match - so nl_BE can match nl_NL.
		IniFile mapping;
		mapping.LoadFromVFS(g_VFS, "langregion.ini");
		std::vector<std::string> keys;
		Section *section = mapping.GetSection("LangRegionNames");
		if (section) {
			section->GetKeys(&keys);
		}
		for (const std::string &key : keys) {
			if (startsWithNoCase(key, langRegion)) {
				// Exact submatch, or different case.  Let's use it.
				defaultLangRegion = key;
				break;
			} else if (startsWithNoCase(key, langRegion.substr(0, 3))) {
				// Best so far.
				defaultLangRegion = key;
			}
		}
	}

	return defaultLangRegion;
}

static int DefaultDepthRaster() {
#ifdef CROSSSIMD_SLOW
	// No SIMD acceleration for the depth rasterizer.
	// Default to off.
	return (int)DepthRasterMode::OFF;
#endif

// For 64-bit ARM and x86 with SIMD, enable depth raster.
#if PPSSPP_ARCH(ARM64_NEON) || PPSSPP_ARCH(SSE2)

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	return (int)DepthRasterMode::LOW_QUALITY;
#else
	return (int)DepthRasterMode::DEFAULT;
#endif

#else

	// 32-bit ARM or no SIMD, the depth raster will be too slow.
	return (int)DepthRasterMode::OFF;

#endif
}

std::string CreateRandMAC() {
	std::stringstream randStream;
	srand(time(nullptr));
	for (int i = 0; i < 6; i++) {
		u32 value = rand() % 256;
		if (i == 0) {
			// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
			value &= 0xfc;
		}
		if (value <= 15)
			randStream << '0' << std::hex << value;
		else
			randStream << std::hex << value;
		if (i < 5) {
			randStream << ':'; //we need a : between every octet
		}
	}
	return randStream.str();
}

static int DefaultCpuCore() {
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(RISCV64) || PPSSPP_ARCH(LOONGARCH64)
	if (System_GetPropertyBool(SYSPROP_CAN_JIT))
		return (int)CPUCore::JIT;
	return (int)CPUCore::IR_INTERPRETER;
#else
	return (int)CPUCore::IR_INTERPRETER;
#endif
}

static bool DefaultCodeGen() {
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(RISCV64) || PPSSPP_ARCH(LOONGARCH64)
	return true;
#else
	return false;
#endif
}

static bool DefaultEnableStateUndo() {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	// Off on mobile to save disk space.
	return false;
#endif
	return true;
}

static float DefaultGameGridScale() {
#if PPSSPP_PLATFORM(IOS)
	return 1.25f;
#else
	return 1.0f;
#endif
}

static float DefaultUISaturation() {
	return IsVREnabled() ? 1.5f : 1.0f;
}

static int DefaultUIScaleFactor() {
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC)
	return -1;
#else
	return 0;
#endif
}

static int DefaultScreenRotation() {
#if PPSSPP_PLATFORM(ANDROID)
	return ROTATION_LOCKED_HORIZONTAL;
#else
	return ROTATION_AUTO;
#endif
}

#define SETTING(a, x) &a, &a.x
#define SETTING_IDX(a, x, i) &a, &a.x[i]

// All relative to g_Config.
static const ConfigSetting generalSettings[] = {
	ConfigSetting("FirstRun", SETTING(g_Config, bFirstRun), true, CfgFlag::DEFAULT),
	ConfigSetting("RunCount", SETTING(g_Config, iRunCount), 0, CfgFlag::DEFAULT),
	ConfigSetting("Enable Logging", SETTING(g_Config, bEnableLogging), true, CfgFlag::PER_GAME),
	ConfigSetting("FileLogging", SETTING(g_Config, bEnableFileLogging), false, CfgFlag::PER_GAME),
	ConfigSetting("AutoRun", SETTING(g_Config, bAutoRun), true, CfgFlag::DEFAULT),
	ConfigSetting("IgnoreBadMemAccess", SETTING(g_Config, bIgnoreBadMemAccess), true, CfgFlag::DEFAULT),
	ConfigSetting("CurrentDirectory", SETTING(g_Config, currentDirectory), "", CfgFlag::DEFAULT),
	ConfigSetting("ShowDebuggerOnLoad", SETTING(g_Config, bShowDebuggerOnLoad), false, CfgFlag::DEFAULT),
	ConfigSetting("ShowImDebugger", SETTING(g_Config, bShowImDebugger), false, CfgFlag::DONT_SAVE),
	ConfigSetting("CheckForNewVersion", SETTING(g_Config, bCheckForNewVersion), true, CfgFlag::DEFAULT),
	ConfigSetting("Language", SETTING(g_Config, sLanguageIni), &DefaultLangRegion, CfgFlag::DEFAULT),
	ConfigSetting("ForceLagSync2", SETTING(g_Config, bForceLagSync), false, CfgFlag::PER_GAME),
	ConfigSetting("DiscordRichPresence", SETTING(g_Config, bDiscordRichPresence), false, CfgFlag::DEFAULT),
	ConfigSetting("UISound", SETTING(g_Config, bUISound), false, CfgFlag::DEFAULT),

	ConfigSetting("DisableHTTPS", SETTING(g_Config, bDisableHTTPS), false, CfgFlag::DONT_SAVE),
	ConfigSetting("AutoLoadSaveState", SETTING(g_Config, iAutoLoadSaveState), 0, CfgFlag::PER_GAME),
	ConfigSetting("EnableCheats", SETTING(g_Config, bEnableCheats), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("EnablePlugins", SETTING(g_Config, bEnablePlugins), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("CwCheatRefreshRate", SETTING(g_Config, iCwCheatRefreshIntervalMs), 77, CfgFlag::PER_GAME),
	ConfigSetting("CwCheatScrollPosition", SETTING(g_Config, fCwCheatScrollPosition), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("GameListScrollPosition", SETTING(g_Config, fGameListScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("HomebrewScrollPosition", SETTING(g_Config, fHomebrewScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("RemoteScrollPosition", SETTING(g_Config, fRemoteScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("DebugOverlay", SETTING(g_Config, iDebugOverlay), 0, CfgFlag::DONT_SAVE),
	ConfigSetting("DefaultTab", SETTING(g_Config, iDefaultTab), 0, CfgFlag::DEFAULT),
	ConfigSetting("DisableHLEFlags", SETTING(g_Config, iDisableHLE), 0, CfgFlag::PER_GAME),
	ConfigSetting("ForceEnableHLEFlags", SETTING(g_Config, iForceEnableHLE), 0, CfgFlag::PER_GAME),

	ConfigSetting("ScreenshotMode", SETTING(g_Config, iScreenshotMode), 0, CfgFlag::DEFAULT),
	ConfigSetting("ScreenshotsAsPNG", SETTING(g_Config, bScreenshotsAsPNG), false, CfgFlag::PER_GAME),
	ConfigSetting("UseFFV1", SETTING(g_Config, bUseFFV1), false, CfgFlag::DEFAULT),
	ConfigSetting("DumpFrames", SETTING(g_Config, bDumpFrames), false, CfgFlag::DEFAULT),
	ConfigSetting("DumpVideoOutput", SETTING(g_Config, bDumpVideoOutput), false, CfgFlag::DEFAULT),
	ConfigSetting("DumpAudio", SETTING(g_Config, bDumpAudio), false, CfgFlag::DEFAULT),
	ConfigSetting("SaveLoadResetsAVdumping", SETTING(g_Config, bSaveLoadResetsAVdumping), false, CfgFlag::DEFAULT),
	ConfigSetting("StateSlot", SETTING(g_Config, iCurrentStateSlot), 0, CfgFlag::PER_GAME),
	ConfigSetting("EnableStateUndo", SETTING(g_Config, bEnableStateUndo), &DefaultEnableStateUndo, CfgFlag::PER_GAME),
	ConfigSetting("StateLoadUndoGame", SETTING(g_Config, sStateLoadUndoGame), "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveGame", SETTING(g_Config, sStateUndoLastSaveGame), "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveSlot", SETTING(g_Config, iStateUndoLastSaveSlot), -5, CfgFlag::DEFAULT), // Start with an "invalid" value
	ConfigSetting("RewindSnapshotInterval", SETTING(g_Config, iRewindSnapshotInterval), 0, CfgFlag::PER_GAME),
	ConfigSetting("SaveStateSlotCount", SETTING(g_Config, iSaveStateSlotCount), 5, CfgFlag::DEFAULT),

	ConfigSetting("ShowRegionOnGameIcon", SETTING(g_Config, bShowRegionOnGameIcon), false, CfgFlag::DEFAULT),
	ConfigSetting("ShowIDOnGameIcon", SETTING(g_Config, bShowIDOnGameIcon), false, CfgFlag::DEFAULT),
	ConfigSetting("GameGridScale", SETTING(g_Config, fGameGridScale), &DefaultGameGridScale, CfgFlag::DEFAULT),
	ConfigSetting("GridView1", SETTING(g_Config, bGridView1), true, CfgFlag::DEFAULT),
	ConfigSetting("GridView2", SETTING(g_Config, bGridView2), true, CfgFlag::DEFAULT),
	ConfigSetting("GridView3", SETTING(g_Config, bGridView3), false, CfgFlag::DEFAULT),
	ConfigSetting("GridView4", SETTING(g_Config, bGridView4), false, CfgFlag::DEFAULT),
	ConfigSetting("RightAnalogUp", SETTING(g_Config, iRightAnalogUp), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDown", SETTING(g_Config, iRightAnalogDown), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogLeft", SETTING(g_Config, iRightAnalogLeft), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogRight", SETTING(g_Config, iRightAnalogRight), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogPress", SETTING(g_Config, iRightAnalogPress), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogCustom", SETTING(g_Config, bRightAnalogCustom), false, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDisableDiagonal", SETTING(g_Config, bRightAnalogDisableDiagonal), false, CfgFlag::PER_GAME),

	ConfigSetting("TouchGliding", SETTING(g_Config, bTouchGliding), false, CfgFlag::PER_GAME),

	// "default" means let emulator decide, "" means disable.
	ConfigSetting("ReportingHost", SETTING(g_Config, sReportHost), "default", CfgFlag::DEFAULT),
	ConfigSetting("AutoSaveSymbolMap", SETTING(g_Config, bAutoSaveSymbolMap), false, CfgFlag::PER_GAME),
	ConfigSetting("CompressSymbols", SETTING(g_Config, bCompressSymbols), true, CfgFlag::DEFAULT),
	ConfigSetting("CacheFullIsoInRam", SETTING(g_Config, bCacheFullIsoInRam), false, CfgFlag::PER_GAME),
	ConfigSetting("RemoteISOPort", SETTING(g_Config, iRemoteISOPort), 0, CfgFlag::DEFAULT),
	ConfigSetting("LastRemoteISOServer", SETTING(g_Config, sLastRemoteISOServer), "", CfgFlag::DEFAULT),
	ConfigSetting("LastRemoteISOPort", SETTING(g_Config, iLastRemoteISOPort), 0, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOManualConfig", SETTING(g_Config, bRemoteISOManual), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteShareOnStartup", SETTING(g_Config, bRemoteShareOnStartup), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSubdir", SETTING(g_Config, sRemoteISOSubdir), "/", CfgFlag::DEFAULT),
	ConfigSetting("RemoteDebuggerOnStartup", SETTING(g_Config, bRemoteDebuggerOnStartup), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteDebuggerLocal", SETTING(g_Config, bRemoteDebuggerLocal), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteTab", SETTING(g_Config, bRemoteTab), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSharedDir", SETTING(g_Config, sRemoteISOSharedDir), "", CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOShareType", SETTING(g_Config, iRemoteISOShareType), (int)RemoteISOShareType::RECENT, CfgFlag::DEFAULT),
	ConfigSetting("AskForExitConfirmationAfterSeconds", SETTING(g_Config, iAskForExitConfirmationAfterSeconds), 300, CfgFlag::PER_GAME),

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	ConfigSetting("ScreenRotation", SETTING(g_Config, iScreenRotation), &DefaultScreenRotation, CfgFlag::DEFAULT),
#endif

	ConfigSetting("BackgroundAnimation", SETTING(g_Config, iBackgroundAnimation), 1, CfgFlag::DEFAULT),
	ConfigSetting("TransparentBackground", SETTING(g_Config, bTransparentBackground), true, CfgFlag::DEFAULT),
	ConfigSetting("UITint", SETTING(g_Config, fUITint), 0.0, CfgFlag::DEFAULT),
	ConfigSetting("UISaturation", SETTING(g_Config, fUISaturation), &DefaultUISaturation, CfgFlag::DEFAULT),

#if defined(USING_WIN_UI)
	ConfigSetting("TopMost", SETTING(g_Config, bTopMost), false, CfgFlag::DEFAULT),
	ConfigSetting("PauseOnLostFocus", SETTING(g_Config, bPauseOnLostFocus), false, CfgFlag::PER_GAME),
#endif

#if !defined(MOBILE_DEVICE)
	ConfigSetting("WindowX", SETTING(g_Config, iWindowX), -1, CfgFlag::DEFAULT), // -1 tells us to center the window.
	ConfigSetting("WindowY", SETTING(g_Config, iWindowY), -1, CfgFlag::DEFAULT),
	ConfigSetting("WindowWidth", SETTING(g_Config, iWindowWidth), 0, CfgFlag::DEFAULT),   // 0 will be automatically reset later (need to do the AdjustWindowRect dance).
	ConfigSetting("WindowHeight", SETTING(g_Config, iWindowHeight), 0, CfgFlag::DEFAULT),
	ConfigSetting("WindowSizeState", SETTING(g_Config, iWindowSizeState), (int)WindowSizeState::Normal, CfgFlag::DEFAULT),
	ConfigSetting("ShrinkIfWindowSmall", SETTING(g_Config, bShrinkIfWindowSmall), false, CfgFlag::DEFAULT),
#endif

	ConfigSetting("PauseWhenMinimized", SETTING(g_Config, bPauseWhenMinimized), false, CfgFlag::PER_GAME),
	ConfigSetting("PauseExitsEmulator", SETTING(g_Config, bPauseExitsEmulator), false, CfgFlag::DONT_SAVE),
	ConfigSetting("PauseMenuExitsEmulator", SETTING(g_Config, bPauseMenuExitsEmulator), false, CfgFlag::DONT_SAVE),

	ConfigSetting("DumpFileTypes", SETTING(g_Config, iDumpFileTypes), 0, CfgFlag::PER_GAME),

	ConfigSetting("FullscreenOnDoubleclick", SETTING(g_Config, bFullscreenOnDoubleclick), true, CfgFlag::DONT_SAVE),
	ConfigSetting("ShowMenuBar", SETTING(g_Config, bShowMenuBar), true, CfgFlag::DEFAULT),

	ConfigSetting("MemStickInserted", SETTING(g_Config, bMemStickInserted), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("LoadPlugins", SETTING(g_Config, bLoadPlugins), true, CfgFlag::PER_GAME),
	ConfigSetting("NotificationPos", SETTING(g_Config, iNotificationPos), (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::DEFAULT),

	ConfigSetting("IgnoreCompatSettings", SETTING(g_Config, sIgnoreCompatSettings), "", CfgFlag::PER_GAME | CfgFlag::REPORT),

	ConfigSetting("RunBehindPauseMenu", SETTING(g_Config, bRunBehindPauseMenu), false, CfgFlag::DEFAULT),

	ConfigSetting("ShowGPOLEDs", SETTING(g_Config, bShowGPOLEDs), false, CfgFlag::PER_GAME),

	ConfigSetting("UIScaleFactor", SETTING(g_Config, iUIScaleFactor), &DefaultUIScaleFactor, CfgFlag::DEFAULT),

	ConfigSetting("VulkanDisableImplicitLayers", SETTING(g_Config, bVulkanDisableImplicitLayers), false, CfgFlag::DEFAULT),
};

static bool DefaultSasThread() {
	return cpu_info.num_cores > 1;
}

static const ConfigSetting achievementSettings[] = {
	// Core settings
	ConfigSetting("AchievementsEnable", SETTING(g_Config, bAchievementsEnable), false, CfgFlag::PER_GAME),
	ConfigSetting("AchievementsEnableRAIntegration", SETTING(g_Config, bAchievementsEnableRAIntegration), false, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsChallengeMode", SETTING(g_Config, bAchievementsHardcoreMode), true, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsEncoreMode", SETTING(g_Config, bAchievementsEncoreMode), false, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnofficial", SETTING(g_Config, bAchievementsUnofficial), false, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLogBadMemReads", SETTING(g_Config, bAchievementsLogBadMemReads), false, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsSaveStateInHardcoreMode", SETTING(g_Config, bAchievementsSaveStateInHardcoreMode), false, CfgFlag::DEFAULT),

	// Achievements login info. Note that password is NOT stored, only a login token.
	// And that login token is stored separately from the ini, see NativeSaveSecret, but it can also be loaded
	// from the ini if manually entered (useful when testing various builds on Android).
	ConfigSetting("AchievementsToken", SETTING(g_Config, sAchievementsToken), "", CfgFlag::DONT_SAVE),
	ConfigSetting("AchievementsUserName", SETTING(g_Config, sAchievementsUserName), "", CfgFlag::DEFAULT),
	ConfigSetting("AchievementsHost", SETTING(g_Config, sAchievementsHost), "", CfgFlag::DEFAULT),

	// Customizations
	ConfigSetting("AchievementsSoundEffects", SETTING(g_Config, bAchievementsSoundEffects), true, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnlockAudioFile", SETTING(g_Config, sAchievementsUnlockAudioFile), "", CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardSubmitAudioFile", SETTING(g_Config, sAchievementsLeaderboardSubmitAudioFile), "", CfgFlag::DEFAULT),

	ConfigSetting("AchievementsLeaderboardTrackerPos", SETTING(g_Config, iAchievementsLeaderboardTrackerPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardStartedOrFailedPos", SETTING(g_Config, iAchievementsLeaderboardStartedOrFailedPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardSubmittedPos", SETTING(g_Config, iAchievementsLeaderboardSubmittedPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsProgressPos", SETTING(g_Config, iAchievementsProgressPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsChallengePos", SETTING(g_Config, iAchievementsChallengePos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnlockedPos", SETTING(g_Config, iAchievementsUnlockedPos), (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
};

static const ConfigSetting cpuSettings[] = {
	ConfigSetting("CPUCore", SETTING(g_Config, iCpuCore), &DefaultCpuCore, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SeparateSASThread", SETTING(g_Config, bSeparateSASThread), &DefaultSasThread, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("IOTimingMethod", SETTING(g_Config, iIOTimingMethod), IOTIMING_FAST, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FastMemoryAccess", SETTING(g_Config, bFastMemory), true, CfgFlag::PER_GAME),
	ConfigSetting("FunctionReplacements", SETTING(g_Config, bFuncReplacements), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("HideSlowWarnings", SETTING(g_Config, bHideSlowWarnings), false, CfgFlag::DEFAULT),
	ConfigSetting("HideStateWarnings", SETTING(g_Config, bHideStateWarnings), false, CfgFlag::DEFAULT),
	ConfigSetting("JitDisableFlags", SETTING(g_Config, uJitDisableFlags), (uint32_t)0, CfgFlag::PER_GAME),
	ConfigSetting("CPUSpeed", SETTING(g_Config, iLockedCPUSpeed), 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
};

static int DefaultInternalResolution() {
	// Auto on Windows and Linux, 2x on large screens and iOS, 1x elsewhere.
#if defined(USING_WIN_UI) || defined(USING_QT_UI)
	return 0;
#elif PPSSPP_PLATFORM(IOS)
	return 2;
#else
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {
		return 4;
	}
	int longestDisplaySide = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES));
	int scale = longestDisplaySide >= 1000 ? 2 : 1;
	INFO_LOG(Log::G3D, "Longest display side: %d pixels. Choosing scale %d", longestDisplaySide, scale);
	return scale;
#endif
}

static int DefaultFastForwardMode() {
#if PPSSPP_PLATFORM(ANDROID) || defined(USING_QT_UI) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS)
	return (int)FastForwardMode::SKIP_FLIP;
#else
	return (int)FastForwardMode::CONTINUOUS;
#endif
}

static int DefaultAndroidHwScale() {
#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19 || System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
		// Arbitrary cutoff at Kitkat - modern devices are usually powerful enough that hw scaling
		// doesn't really help very much and mostly causes problems. See #11151
		return 0;
	}

	// Get the real resolution as passed in during startup, not dp_xres and stuff
	int xres = System_GetPropertyInt(SYSPROP_DISPLAY_XRES);
	int yres = System_GetPropertyInt(SYSPROP_DISPLAY_YRES);

	if (xres <= 960) {
		// Smaller than the PSP*2, let's go native.
		return 0;
	} else if (xres <= 480 * 3) {  // 720p xres
		// Small-ish screen, we should default to 2x
		return 2 + 1;
	} else {
		// Large or very large screen. Default to 3x psp resolution.
		return 3 + 1;
	}
	return 0;
#else
	return 1;
#endif
}

// See issue 14439. Should possibly even block these devices from selecting VK.
const char * const vulkanDefaultBlacklist[] = {
	"Sony:BRAVIA VH1",
};

static int DefaultGPUBackend() {
	if (IsVREnabled()) {
		return (int)GPUBackend::OPENGL;
	}

#if PPSSPP_PLATFORM(UWP)
	return (int)GPUBackend::DIRECT3D11;
#elif PPSSPP_PLATFORM(WINDOWS)
	// On Win10, there's a good chance Vulkan will work by default.
	if (IsWin10OrHigher()) {
		return (int)GPUBackend::VULKAN;
	}
	// On older Windows, to be safe, use Direct3D 11.
	if (IsWin8OrHigher()) {
		return (int)GPUBackend::DIRECT3D11;
	}
#elif PPSSPP_PLATFORM(ANDROID)
	// Check blacklist.
	for (size_t i = 0; i < ARRAY_SIZE(vulkanDefaultBlacklist); i++) {
		if (System_GetProperty(SYSPROP_NAME) == vulkanDefaultBlacklist[i]) {
			return (int)GPUBackend::OPENGL;
		}
	}

	// Default to Vulkan only on Oreo 8.1 (level 27) devices or newer, and only
	// on ARM64 and x86-64. Drivers before, and on other archs, are generally too
	// unreliable to default to (with some exceptions, of course).
#if PPSSPP_ARCH(64BIT)
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 27) {
		return (int)GPUBackend::VULKAN;
	}
#else
	// There are some newer devices that benefit from Vulkan as default, but are 32-bit. Example: Redmi 9A.
	// Let's only allow the very newest generation though.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		return (int)GPUBackend::VULKAN;
	}
#endif

#elif PPSSPP_PLATFORM(MAC)

#if PPSSPP_ARCH(ARM64)
	return (int)GPUBackend::VULKAN;
#else
	// On Intel (generally older Macs) default to OpenGL.
	return (int)GPUBackend::OPENGL;
#endif

#elif PPSSPP_PLATFORM(IOS_APP_STORE)
	return (int)GPUBackend::VULKAN;
#endif

	// TODO: On some additional Linux platforms, we should also default to Vulkan.
	return (int)GPUBackend::OPENGL;
}

int Config::NextValidBackend() {
	std::vector<std::string> split;
	std::set<GPUBackend> failed;

	SplitString(sFailedGPUBackends, ',', split);
	for (const auto &str : split) {
		if (!str.empty() && str != "ALL") {
			failed.insert(GPUBackendFromString(str));
		}
	}

	// Count these as "failed" too so we don't pick them.
	SplitString(sDisabledGPUBackends, ',', split);
	for (const auto &str : split) {
		if (!str.empty()) {
			failed.insert(GPUBackendFromString(str));
		}
	}

	if (failed.count((GPUBackend)iGPUBackend)) {
		ERROR_LOG(Log::Loader, "Graphics backend failed for %d, trying another", iGPUBackend);

#if !PPSSPP_PLATFORM(UWP)
		if (!failed.count(GPUBackend::VULKAN) && VulkanMayBeAvailable()) {
			return (int)GPUBackend::VULKAN;
		}
#endif
#if PPSSPP_PLATFORM(WINDOWS)
		if (!failed.count(GPUBackend::DIRECT3D11) && IsWin7OrHigher()) {
			return (int)GPUBackend::DIRECT3D11;
		}
#endif
#if PPSSPP_API(ANY_GL)
		if (!failed.count(GPUBackend::OPENGL)) {
			return (int)GPUBackend::OPENGL;
		}
#endif

		// They've all failed.  Let them try the default - or on Android, OpenGL.
		if (sFailedGPUBackends.find(",ALL") == std::string::npos) {
			sFailedGPUBackends += ",ALL";
		}
		ERROR_LOG(Log::Loader, "All graphics backends failed");
#if PPSSPP_PLATFORM(ANDROID)
		return (int)GPUBackend::OPENGL;
#else
		return DefaultGPUBackend();
#endif
	}

	return iGPUBackend;
}

bool Config::IsBackendEnabled(GPUBackend backend) {
	std::vector<std::string> split;

	SplitString(sDisabledGPUBackends, ',', split);
	for (const auto &str : split) {
		if (str.empty())
			continue;
		auto match = GPUBackendFromString(str);
		if (match == backend)
			return false;
	}

#if PPSSPP_PLATFORM(UWP)
	if (backend != GPUBackend::DIRECT3D11)
		return false;
#elif PPSSPP_PLATFORM(SWITCH)
	if (backend != GPUBackend::OPENGL)
		return false;
#elif PPSSPP_PLATFORM(WINDOWS)
	if (backend == GPUBackend::DIRECT3D11 && !IsVistaOrHigher())
		return false;
#else
	if (backend == GPUBackend::DIRECT3D11)
		return false;
#endif

#if !PPSSPP_API(ANY_GL)
	if (backend == GPUBackend::OPENGL)
		return false;
#endif
	if (backend == GPUBackend::VULKAN && !VulkanMayBeAvailable())
		return false;
	return true;
}

template <typename T, std::string (*FTo)(T), T (*FFrom)(std::string_view)>
struct ConfigTranslator {
	static std::string To(int v) {
		return StringFromInt(v) + " (" + FTo(T(v)) + ")";
	}

	static int From(const std::string &v) {
		int result;
		if (TryParse(v, &result)) {
			return result;
		}
		return (int)FFrom(v);
	}
};

typedef ConfigTranslator<GPUBackend, GPUBackendToString, GPUBackendFromString> GPUBackendTranslator;

static std::string DefaultInfrastructureUsername() {
	// If the user has already picked a Nickname that satisfies the rules and is not "PPSSPP",
	// let's use that.
	// NOTE: This type of dependency means that network settings must be AFTER system settings in sections[].
	if (g_Config.sNickName != "PPSSPP" &&
		!g_Config.sNickName.empty() &&
		g_Config.sNickName == SanitizeString(g_Config.sNickName, StringRestriction::AlphaNumDashUnderscore, 3, 16)) {
		return g_Config.sNickName;
	}

	// Otherwise let's leave it empty, which will result in login failure and a warning.
	return std::string();
}

bool DisplayLayoutConfig::InternalRotationIsPortrait() const {
	return (iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180) && !g_Config.bSkipBufferEffects;
}

bool DisplayLayoutConfig::ResetToDefault(std::string_view blockName) {
	static const DisplayLayoutConfig defaultLayout = DisplayLayoutConfig();
	*this = defaultLayout;
	if (endsWith(blockName, ".Portrait")) {
		// TODO: On mobile, where the aspect is fixed, we should use the screen size to compute this properly,
		// so the screen almost touches the top edge.
		fDisplayOffsetY = 0.25f;
		bImmersiveMode = false;
	}
	return true;
}

// These were previously part of Graphics.
// It's instantiated into g_Config.displayLayoutLandscape and g_Config.displayLayoutPortrait.
// Defaults are set directly on the struct declaration (and adjusted per instance in ResetToDefault above).
static const ConfigSetting displayLayoutSettings[] = {
	ConfigSetting("BufferFiltering", SETTING(g_Config.displayLayoutLandscape, iDisplayFilter), CfgFlag::PER_GAME),
	ConfigSetting("DisplayStretch", SETTING(g_Config.displayLayoutLandscape, bDisplayStretch), CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetX", SETTING(g_Config.displayLayoutLandscape, fDisplayOffsetX), CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetY", SETTING(g_Config.displayLayoutLandscape, fDisplayOffsetY), CfgFlag::PER_GAME),
	ConfigSetting("DisplayScale", SETTING(g_Config.displayLayoutLandscape, fDisplayScale), CfgFlag::PER_GAME),
	ConfigSetting("DisplayIntegerScale", SETTING(g_Config.displayLayoutLandscape, bDisplayIntegerScale), CfgFlag::PER_GAME),
	ConfigSetting("DisplayAspectRatio", SETTING(g_Config.displayLayoutLandscape, fDisplayAspectRatio), CfgFlag::PER_GAME),
	ConfigSetting("IgnoreScreenInsets", SETTING(g_Config.displayLayoutLandscape, bIgnoreScreenInsets), CfgFlag::PER_GAME),
	ConfigSetting("InternalScreenRotation", SETTING(g_Config.displayLayoutLandscape, iInternalScreenRotation), CfgFlag::PER_GAME),
	ConfigSetting("RotateControlsWithScreen", SETTING(g_Config.displayLayoutLandscape, bRotateControlsWithScreen), CfgFlag::PER_GAME),
	ConfigSetting("EnableCardboardVR", SETTING(g_Config.displayLayoutLandscape, bEnableCardboardVR), CfgFlag::PER_GAME),
	ConfigSetting("CardboardScreenSize", SETTING(g_Config.displayLayoutLandscape, iCardboardScreenSize), CfgFlag::PER_GAME),
	ConfigSetting("CardboardXShift", SETTING(g_Config.displayLayoutLandscape, iCardboardXShift), CfgFlag::PER_GAME),
	ConfigSetting("CardboardYShift", SETTING(g_Config.displayLayoutLandscape, iCardboardYShift), CfgFlag::PER_GAME),
	ConfigSetting("ImmersiveMode", SETTING(g_Config.displayLayoutLandscape, bImmersiveMode), CfgFlag::PER_GAME),
};

bool GestureControlConfig::ResetToDefault(std::string_view blockName) {
	static const GestureControlConfig defaultLayout;
	*this = defaultLayout;
	// TODO: Check blockname to set defaults differently for secondary zone(s).
	return true;
}

// These were previously mostly part of General (which was wrong).
static const ConfigSetting gestureControlSettings[] = {
	ConfigSetting("SwipeUp", SETTING(g_Config.gestureControls[0], iSwipeUp), 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeDown", SETTING(g_Config.gestureControls[0], iSwipeDown), 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeLeft", SETTING(g_Config.gestureControls[0], iSwipeLeft), 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeRight", SETTING(g_Config.gestureControls[0], iSwipeRight), 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeSensitivity", SETTING(g_Config.gestureControls[0], fSwipeSensitivity), 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("SwipeSmoothing", SETTING(g_Config.gestureControls[0], fSwipeSmoothing), 0.3f, CfgFlag::PER_GAME),
	ConfigSetting("DoubleTapGesture", SETTING(g_Config.gestureControls[0], iDoubleTapGesture), 0, CfgFlag::PER_GAME),
	ConfigSetting("GestureControlEnabled", SETTING(g_Config.gestureControls[0], bGestureControlEnabled), false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogGesture", SETTING(g_Config.gestureControls[0], bAnalogGesture), false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogGestureSensibility", SETTING(g_Config.gestureControls[0], fAnalogGestureSensitivity), 1.0f, CfgFlag::PER_GAME),
};

static const ConfigSetting graphicsSettings[] = {
	ConfigSetting("iShowStatusFlags", SETTING(g_Config, iShowStatusFlags), 0, CfgFlag::PER_GAME),
	ConfigSetting("GraphicsBackend", SETTING(g_Config, iGPUBackend), &DefaultGPUBackend, &GPUBackendTranslator::To, &GPUBackendTranslator::From, CfgFlag::DEFAULT | CfgFlag::REPORT),
#if PPSSPP_PLATFORM(ANDROID) && PPSSPP_ARCH(ARM64)
	ConfigSetting("CustomDriver", SETTING(g_Config, sCustomDriver), "", CfgFlag::DEFAULT),
#endif
	ConfigSetting("DisabledGraphicsBackends", SETTING(g_Config, sDisabledGPUBackends), "", CfgFlag::DEFAULT),
	ConfigSetting("VulkanDevice", SETTING(g_Config, sVulkanDevice), "", CfgFlag::DEFAULT),
#ifdef _WIN32
	ConfigSetting("D3D11Device", SETTING(g_Config, sD3D11Device), "", CfgFlag::DEFAULT),
#endif
	ConfigSetting("CameraDevice", SETTING(g_Config, sCameraDevice), "", CfgFlag::DEFAULT),
	ConfigSetting("CameraMirrorHorizontal", SETTING(g_Config, bCameraMirrorHorizontal), false, CfgFlag::DEFAULT),
	ConfigSetting("AndroidFramerateMode", SETTING(g_Config, iDisplayFramerateMode), 1, CfgFlag::DEFAULT),
	ConfigSetting("VendorBugChecksEnabled", SETTING(g_Config, bVendorBugChecksEnabled), true, CfgFlag::DONT_SAVE),
	ConfigSetting("UseGeometryShader", SETTING(g_Config, bUseGeometryShader), false, CfgFlag::PER_GAME),
	ConfigSetting("SkipBufferEffects", SETTING(g_Config, bSkipBufferEffects), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("DisableRangeCulling", SETTING(g_Config, bDisableRangeCulling), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("DepthRasterMode", SETTING(g_Config, iDepthRasterMode), &DefaultDepthRaster, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SoftwareRenderer", SETTING(g_Config, bSoftwareRendering), false, CfgFlag::PER_GAME),
	ConfigSetting("SoftwareRendererJit", SETTING(g_Config, bSoftwareRenderingJit), true, CfgFlag::PER_GAME),
	ConfigSetting("HardwareTransform", SETTING(g_Config, bHardwareTransform), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SoftwareSkinning", SETTING(g_Config, bSoftwareSkinning), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TextureFiltering", SETTING(g_Config, iTexFiltering), 1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("Smart2DTexFiltering", SETTING(g_Config, bSmart2DTexFiltering), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("InternalResolution", SETTING(g_Config, iInternalResolution), &DefaultInternalResolution, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("AndroidHwScale", SETTING(g_Config, iAndroidHwScale), &DefaultAndroidHwScale, CfgFlag::DEFAULT),
	ConfigSetting("HighQualityDepth", SETTING(g_Config, bHighQualityDepth), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FrameSkip", SETTING(g_Config, iFrameSkip), 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("AutoFrameSkip", SETTING(g_Config, bAutoFrameSkip), IsVREnabled(), CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("StereoRendering", SETTING(g_Config, bStereoRendering), false, CfgFlag::PER_GAME),
	ConfigSetting("StereoToMonoShader", SETTING(g_Config, sStereoToMonoShader), "RedBlue", CfgFlag::PER_GAME),
	ConfigSetting("FrameRate", SETTING(g_Config, iFpsLimit1), 0, CfgFlag::PER_GAME),
	ConfigSetting("FrameRate2", SETTING(g_Config, iFpsLimit2), -1, CfgFlag::PER_GAME),
	ConfigSetting("AnalogFrameRate", SETTING(g_Config, iAnalogFpsLimit), 240, CfgFlag::PER_GAME),
#if defined(USING_WIN_UI)
	ConfigSetting("RestartRequired", SETTING(g_Config, bRestartRequired), false, CfgFlag::DONT_SAVE),
#endif

	// Most low-performance (and many high performance) mobile GPUs do not support aniso anyway so defaulting to 4 is fine.
	ConfigSetting("AnisotropyLevel", SETTING(g_Config, iAnisotropyLevel), 4, CfgFlag::PER_GAME),
	ConfigSetting("MultiSampleLevel", SETTING(g_Config, iMultiSampleLevel), 0, CfgFlag::PER_GAME),  // Number of samples is 1 << iMultiSampleLevel

	ConfigSetting("TextureBackoffCache", SETTING(g_Config, bTextureBackoffCache), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VertexDecJit", SETTING(g_Config, bVertexDecoderJit), &DefaultCodeGen, CfgFlag::DONT_SAVE | CfgFlag::REPORT),

#ifndef MOBILE_DEVICE
	ConfigSetting("FullScreen", SETTING(g_Config, bFullScreen), false, CfgFlag::DEFAULT),
	ConfigSetting("FullScreenMulti", SETTING(g_Config, bFullScreenMulti), false, CfgFlag::DEFAULT),
#endif

#if PPSSPP_PLATFORM(IOS)
	ConfigSetting("AppSwitchMode", SETTING(g_Config, iAppSwitchMode), (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR, CfgFlag::DEFAULT),
#endif

	ConfigSetting("DisplayCropTo16x9", SETTING(g_Config, bDisplayCropTo16x9), true, CfgFlag::PER_GAME),

	ConfigSetting("SustainedPerformanceMode", SETTING(g_Config, bSustainedPerformanceMode), false, CfgFlag::PER_GAME),

	ConfigSetting("ReplaceTextures", SETTING(g_Config, bReplaceTextures), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SaveNewTextures", SETTING(g_Config, bSaveNewTextures), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("IgnoreTextureFilenames", SETTING(g_Config, bIgnoreTextureFilenames), false, CfgFlag::PER_GAME),
	ConfigSetting("ReplacementTextureLoadSpeed", SETTING(g_Config, iReplacementTextureLoadSpeed), 0, CfgFlag::PER_GAME),

	ConfigSetting("TexScalingLevel", SETTING(g_Config, iTexScalingLevel), 1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexScalingType", SETTING(g_Config, iTexScalingType), 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexDeposterize", SETTING(g_Config, bTexDeposterize), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexHardwareScaling", SETTING(g_Config, bTexHardwareScaling), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VerticalSync", SETTING(g_Config, bVSync), true, CfgFlag::PER_GAME),
	ConfigSetting("LowLatencyPresent", SETTING(g_Config, bLowLatencyPresent), true, CfgFlag::PER_GAME),
	ConfigSetting("BloomHack", SETTING(g_Config, iBloomHack), 0, CfgFlag::PER_GAME | CfgFlag::REPORT),

	// Not really a graphics setting...
	ConfigSetting("SplineBezierQuality", SETTING(g_Config, iSplineBezierQuality), 2, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("HardwareTessellation", SETTING(g_Config, bHardwareTessellation), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TextureShader", SETTING(g_Config, sTextureShaderName), "Off", CfgFlag::PER_GAME),
	ConfigSetting("ShaderChainRequires60FPS", SETTING(g_Config, bShaderChainRequires60FPS), false, CfgFlag::PER_GAME),

	ConfigSetting("SkipGPUReadbackMode", SETTING(g_Config, iSkipGPUReadbackMode), false, CfgFlag::PER_GAME | CfgFlag::REPORT),

	ConfigSetting("GfxDebugOutput", SETTING(g_Config, bGfxDebugOutput), false, CfgFlag::DONT_SAVE),
	ConfigSetting("LogFrameDrops", SETTING(g_Config, bLogFrameDrops), false, CfgFlag::DEFAULT),

	ConfigSetting("InflightFrames", SETTING(g_Config, iInflightFrames), 3, CfgFlag::DEFAULT),
	ConfigSetting("RenderDuplicateFrames", SETTING(g_Config, bRenderDuplicateFrames), false, CfgFlag::PER_GAME),

	ConfigSetting("MultiThreading", SETTING(g_Config, bRenderMultiThreading), true, CfgFlag::DEFAULT),

	ConfigSetting("ShaderCache", SETTING(g_Config, bShaderCache), true, CfgFlag::DEFAULT),
	ConfigSetting("GpuLogProfiler", SETTING(g_Config, bGpuLogProfiler), false, CfgFlag::DEFAULT),

	ConfigSetting("UberShaderVertex", SETTING(g_Config, bUberShaderVertex), true, CfgFlag::DEFAULT),
	ConfigSetting("UberShaderFragment", SETTING(g_Config, bUberShaderFragment), true, CfgFlag::DEFAULT),

	ConfigSetting("DisplayRefreshRate", SETTING(g_Config, iDisplayRefreshRate), g_Config.iDisplayRefreshRate, CfgFlag::PER_GAME),
};

static int LegacyVolumeToNewVolume(int legacy, int max) {
	float multiplier = Volume10ToMultiplier(legacy);
	return std::clamp(MultiplierToVolume100(multiplier), 0, max);
}

static int DefaultGameVolume() {
	return LegacyVolumeToNewVolume(g_Config.iLegacyGameVolume, 100);
}

static int DefaultReverbVolume() {
	return LegacyVolumeToNewVolume(g_Config.iLegacyReverbVolume, 200);
}

static int DefaultAchievementVolume() {
	// NOTE: The old achievemnt volume was a straight percentage so it doesn't convert
	// the same as the others.
	return MultiplierToVolume100((float)g_Config.iLegacyAchievementVolume / 10.0f);
}

static int DefaultGamePreviewVolume() {
	// This was previously controlled by the UI volume, so transfer the default value over.
	return g_Config.iUIVolume;
}

static const ConfigSetting soundSettings[] = {
	ConfigSetting("Enable", SETTING(g_Config, bEnableSound), true, CfgFlag::PER_GAME),
	ConfigSetting("ExtraAudioBuffering", SETTING(g_Config, bExtraAudioBuffering), false, CfgFlag::DEFAULT),
	ConfigSetting("AudioBufferSize", SETTING(g_Config, iSDLAudioBufferSize), 256, CfgFlag::DEFAULT),

	ConfigSetting("FillAudioGaps", SETTING(g_Config, bFillAudioGaps), true, CfgFlag::DEFAULT),
	ConfigSetting("AudioSyncMode", SETTING(g_Config, iAudioPlaybackMode), (int)AudioSyncMode::CLASSIC_PITCH, CfgFlag::DEFAULT),

	// Legacy volume settings, these get auto upgraded through default handlers on the new settings. NOTE: Must be before the new ones in the order here.
	// The default settings here are still relevant, they will get propagated into the new ones.
	// In 1.21 or so, let's get rid of these.
	ConfigSetting("GlobalVolume", SETTING(g_Config, iLegacyGameVolume), VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("ReverbVolume", SETTING(g_Config, iLegacyReverbVolume), VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("AchievementSoundVolume", SETTING(g_Config, iLegacyAchievementVolume), 6, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),

	// Current volume settings.
	ConfigSetting("GameVolume", SETTING(g_Config, iGameVolume), &DefaultGameVolume, CfgFlag::PER_GAME),
	ConfigSetting("ReverbRelativeVolume", SETTING(g_Config, iReverbVolume), &DefaultReverbVolume, CfgFlag::PER_GAME),
	ConfigSetting("AltSpeedRelativeVolume", SETTING(g_Config, iAltSpeedVolume), VOLUMEHI_FULL, CfgFlag::PER_GAME),
	ConfigSetting("AchievementVolume", SETTING(g_Config, iAchievementVolume), &DefaultAchievementVolume, CfgFlag::PER_GAME),
	ConfigSetting("UIVolume", SETTING(g_Config, iUIVolume), 75, CfgFlag::DEFAULT),
	ConfigSetting("GamePreviewVolume", SETTING(g_Config, iGamePreviewVolume), &DefaultGamePreviewVolume, CfgFlag::DEFAULT),

	ConfigSetting("AudioDevice", SETTING(g_Config, sAudioDevice), "", CfgFlag::DEFAULT),
	ConfigSetting("AutoAudioDevice", SETTING(g_Config, bAutoAudioDevice), true, CfgFlag::DEFAULT),
	ConfigSetting("AudioMixWithOthers", SETTING(g_Config, bAudioMixWithOthers), true, CfgFlag::DEFAULT),
	ConfigSetting("AudioRespectSilentMode", SETTING(g_Config, bAudioRespectSilentMode), false, CfgFlag::DEFAULT),
	ConfigSetting("UseOldAtrac", SETTING(g_Config, bUseOldAtrac), false, CfgFlag::DEFAULT),
};

static bool DefaultShowTouchControls() {
	switch (System_GetPropertyInt(SYSPROP_DEVICE_TYPE)) {
	case DEVICE_TYPE_MOBILE:
		return !KeyMap::HasBuiltinController(System_GetProperty(SYSPROP_NAME));
	default:
		return false;
	}
}

static const float defaultControlScale = 1.15f;
static const ConfigTouchPos defaultTouchPosShow = { -1.0f, -1.0f, defaultControlScale, true };
static const ConfigTouchPos defaultTouchPosHide = { -1.0f, -1.0f, defaultControlScale, false };

void TouchControlConfig::ResetLayout() {
	// reset puts the settings in a state so they'll then get properly reinitialized in InitPadLayout.
	// Intentionally don't modify 'show' here, this is only done in ResetToDefault.
	auto reset = [](ConfigTouchPos *pos) {
		pos->x = defaultTouchPosShow.x;
		pos->y = defaultTouchPosShow.y;
		pos->scale = defaultTouchPosShow.scale;
	};
	reset(&touchActionButtonCenter);
	fActionButtonSpacing = 1.0f;
	reset(&touchDpad);
	fDpadSpacing = 1.0f;
	reset(&touchStartKey);
	reset(&touchSelectKey);
	reset(&touchFastForwardKey);
	reset(&touchLKey);
	reset(&touchRKey);
	reset(&touchAnalogStick);
	reset(&touchRightAnalogStick);
	reset(&touchPauseKey);

	for (int i = 0; i < CUSTOM_BUTTON_COUNT; i++) {
		reset(&touchCustom[i]);
	}
	fLeftStickHeadScale = 1.0f;
	fRightStickHeadScale = 1.0f;
}

bool TouchControlConfig::ResetToDefault(std::string_view blockName) {
	static const TouchControlConfig defaults = TouchControlConfig();
	*this = defaults;

	switch (System_GetPropertyInt(SYSPROP_DEVICE_TYPE)) {
	case DEVICE_TYPE_MOBILE:
	case DEVICE_TYPE_DESKTOP:
		touchPauseKey.show = true;
		break;
	case DEVICE_TYPE_VR:
	case DEVICE_TYPE_TV:
		touchPauseKey.show = false;
		break;
	}

	return true;
}

static const ConfigSetting touchControlSettings[] = {
	ConfigSetting("ShowTouchCross", SETTING(g_Config.touchControlsLandscape, bShowTouchCross), true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchCircle", SETTING(g_Config.touchControlsLandscape, bShowTouchCircle), true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchSquare", SETTING(g_Config.touchControlsLandscape, bShowTouchSquare), true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchTriangle", SETTING(g_Config.touchControlsLandscape, bShowTouchTriangle), true, CfgFlag::PER_GAME),

	// Combo keys are something else, but I don't want to break the config backwards compatibility so these will stay wrongly named.
	ConfigSetting("fcombo0X", "fcombo0Y", "comboKeyScale0", "ShowComboKey0", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 0), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo1X", "fcombo1Y", "comboKeyScale1", "ShowComboKey1", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 1), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo2X", "fcombo2Y", "comboKeyScale2", "ShowComboKey2", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 2), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo3X", "fcombo3Y", "comboKeyScale3", "ShowComboKey3", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 3), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo4X", "fcombo4Y", "comboKeyScale4", "ShowComboKey4", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 4), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo5X", "fcombo5Y", "comboKeyScale5", "ShowComboKey5", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 5), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo6X", "fcombo6Y", "comboKeyScale6", "ShowComboKey6", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 6), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo7X", "fcombo7Y", "comboKeyScale7", "ShowComboKey7", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 7), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo8X", "fcombo8Y", "comboKeyScale8", "ShowComboKey8", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 8), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo9X", "fcombo9Y", "comboKeyScale9", "ShowComboKey9", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 9), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo10X", "fcombo10Y", "comboKeyScale10", "ShowComboKey10", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 10), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo11X", "fcombo11Y", "comboKeyScale11", "ShowComboKey11", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 11), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo12X", "fcombo12Y", "comboKeyScale12", "ShowComboKey12", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 12), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo13X", "fcombo13Y", "comboKeyScale13", "ShowComboKey13", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 13), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo14X", "fcombo14Y", "comboKeyScale14", "ShowComboKey14", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 14), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo15X", "fcombo15Y", "comboKeyScale15", "ShowComboKey15", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 15), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo16X", "fcombo16Y", "comboKeyScale16", "ShowComboKey16", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 16), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo17X", "fcombo17Y", "comboKeyScale17", "ShowComboKey17", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 17), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo18X", "fcombo18Y", "comboKeyScale18", "ShowComboKey18", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 18), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo19X", "fcombo19Y", "comboKeyScale19", "ShowComboKey19", SETTING_IDX(g_Config.touchControlsLandscape, touchCustom, 19), defaultTouchPosHide, CfgFlag::PER_GAME),

	// -1.0f means uninitialized, set in GamepadEmu::CreatePadLayout().
	ConfigSetting("ActionButtonSpacing2", SETTING(g_Config.touchControlsLandscape, fActionButtonSpacing), 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("ActionButtonCenterX", "ActionButtonCenterY", "ActionButtonScale", nullptr, SETTING(g_Config.touchControlsLandscape, touchActionButtonCenter), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("DPadX", "DPadY", "DPadScale", "ShowTouchDpad", SETTING(g_Config.touchControlsLandscape, touchDpad), defaultTouchPosShow, CfgFlag::PER_GAME),

	// Note: these will be overwritten if DPadRadius is set.
	ConfigSetting("DPadSpacing", SETTING(g_Config.touchControlsLandscape, fDpadSpacing), 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("StartKeyX", "StartKeyY", "StartKeyScale", "ShowTouchStart", SETTING(g_Config.touchControlsLandscape, touchStartKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("SelectKeyX", "SelectKeyY", "SelectKeyScale", "ShowTouchSelect", SETTING(g_Config.touchControlsLandscape, touchSelectKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("UnthrottleKeyX", "UnthrottleKeyY", "UnthrottleKeyScale", "ShowTouchUnthrottle", SETTING(g_Config.touchControlsLandscape, touchFastForwardKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("LKeyX", "LKeyY", "LKeyScale", "ShowTouchLTrigger", SETTING(g_Config.touchControlsLandscape, touchLKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RKeyX", "RKeyY", "RKeyScale", "ShowTouchRTrigger", SETTING(g_Config.touchControlsLandscape, touchRKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("PauseKeyX", "PauseKeyY", "PauseKeyScale", "ShowTouchPause", SETTING(g_Config.touchControlsLandscape, touchPauseKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("AnalogStickX", "AnalogStickY", "AnalogStickScale", "ShowAnalogStick", SETTING(g_Config.touchControlsLandscape, touchAnalogStick), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogStickX", "RightAnalogStickY", "RightAnalogStickScale", "ShowRightAnalogStick", SETTING(g_Config.touchControlsLandscape, touchRightAnalogStick), defaultTouchPosHide, CfgFlag::PER_GAME),

	ConfigSetting("LeftStickHeadScale", SETTING(g_Config.touchControlsLandscape, fLeftStickHeadScale), CfgFlag::PER_GAME),
	ConfigSetting("RightStickHeadScale", SETTING(g_Config.touchControlsLandscape, fRightStickHeadScale), CfgFlag::PER_GAME),
	ConfigSetting("HideStickBackground", SETTING(g_Config.touchControlsLandscape, bHideStickBackground), CfgFlag::PER_GAME),
};

static const ConfigSetting controlSettings[] = {
	ConfigSetting("HapticFeedback", SETTING(g_Config, bHapticFeedback), false, CfgFlag::PER_GAME),
	
#if defined(USING_WIN_UI)
	ConfigSetting("IgnoreWindowsKey", SETTING(g_Config, bIgnoreWindowsKey), false, CfgFlag::PER_GAME),
#endif

	ConfigSetting("ShowTouchControls", SETTING(g_Config, bShowTouchControls), &DefaultShowTouchControls, CfgFlag::PER_GAME),

	// ConfigSetting("KeyMapping", SETTING(g_Config, iMappingMap), 0),
	ConfigSetting("Custom0Mapping", "Custom0Image", "Custom0Shape", "Custom0Toggle", "Custom0Repeat", SETTING_IDX(g_Config, CustomButton, 0), {0, 0, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom1Mapping", "Custom1Image", "Custom1Shape", "Custom1Toggle", "Custom1Repeat", SETTING_IDX(g_Config, CustomButton, 1), {0, 1, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom2Mapping", "Custom2Image", "Custom2Shape", "Custom2Toggle", "Custom2Repeat", SETTING_IDX(g_Config, CustomButton, 2), {0, 2, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom3Mapping", "Custom3Image", "Custom3Shape", "Custom3Toggle", "Custom3Repeat", SETTING_IDX(g_Config, CustomButton, 3), {0, 3, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom4Mapping", "Custom4Image", "Custom4Shape", "Custom4Toggle", "Custom4Repeat", SETTING_IDX(g_Config, CustomButton, 4), {0, 4, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom5Mapping", "Custom5Image", "Custom5Shape", "Custom5Toggle", "Custom5Repeat", SETTING_IDX(g_Config, CustomButton, 5), {0, 0, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom6Mapping", "Custom6Image", "Custom6Shape", "Custom6Toggle", "Custom6Repeat", SETTING_IDX(g_Config, CustomButton, 6), {0, 1, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom7Mapping", "Custom7Image", "Custom7Shape", "Custom7Toggle", "Custom7Repeat", SETTING_IDX(g_Config, CustomButton, 7), {0, 2, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom8Mapping", "Custom8Image", "Custom8Shape", "Custom8Toggle", "Custom8Repeat", SETTING_IDX(g_Config, CustomButton, 8), {0, 3, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom9Mapping", "Custom9Image", "Custom9Shape", "Custom9Toggle", "Custom9Repeat", SETTING_IDX(g_Config, CustomButton, 9), {0, 4, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom10Mapping", "Custom10Image", "Custom10Shape", "Custom10Toggle", "Custom10Repeat", SETTING_IDX(g_Config, CustomButton, 10), {0, 0, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom11Mapping", "Custom11Image", "Custom11Shape", "Custom11Toggle", "Custom11Repeat", SETTING_IDX(g_Config, CustomButton, 11), {0, 1, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom12Mapping", "Custom12Image", "Custom12Shape", "Custom12Toggle", "Custom12Repeat", SETTING_IDX(g_Config, CustomButton, 12), {0, 2, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom13Mapping", "Custom13Image", "Custom13Shape", "Custom13Toggle", "Custom13Repeat", SETTING_IDX(g_Config, CustomButton, 13), {0, 3, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom14Mapping", "Custom14Image", "Custom14Shape", "Custom14Toggle", "Custom14Repeat", SETTING_IDX(g_Config, CustomButton, 14), {0, 4, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom15Mapping", "Custom15Image", "Custom15Shape", "Custom15Toggle", "Custom15Repeat", SETTING_IDX(g_Config, CustomButton, 15), {0, 0, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom16Mapping", "Custom16Image", "Custom16Shape", "Custom16Toggle", "Custom16Repeat", SETTING_IDX(g_Config, CustomButton, 16), {0, 1, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom17Mapping", "Custom17Image", "Custom17Shape", "Custom17Toggle", "Custom17Repeat", SETTING_IDX(g_Config, CustomButton, 17), {0, 2, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom18Mapping", "Custom18Image", "Custom18Shape", "Custom18Toggle", "Custom18Repeat", SETTING_IDX(g_Config, CustomButton, 18), {0, 3, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom19Mapping", "Custom19Image", "Custom19Shape", "Custom19Toggle", "Custom19Repeat", SETTING_IDX(g_Config, CustomButton, 19), {0, 4, 9, false, false}, CfgFlag::PER_GAME),

	ConfigSetting("TiltBaseAngleY", SETTING(g_Config, fTiltBaseAngleY), 0.9f, CfgFlag::PER_GAME),
	ConfigSetting("TiltInvertX", SETTING(g_Config, bInvertTiltX), false, CfgFlag::PER_GAME),
	ConfigSetting("TiltInvertY", SETTING(g_Config, bInvertTiltY), false, CfgFlag::PER_GAME),
	ConfigSetting("TiltSensitivityX", SETTING(g_Config, iTiltSensitivityX), 60, CfgFlag::PER_GAME),
	ConfigSetting("TiltSensitivityY", SETTING(g_Config, iTiltSensitivityY), 60, CfgFlag::PER_GAME),
	ConfigSetting("TiltAnalogDeadzoneRadius", SETTING(g_Config, fTiltAnalogDeadzoneRadius), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("TiltInverseDeadzone", SETTING(g_Config, fTiltInverseDeadzone), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("TiltCircularDeadzone", SETTING(g_Config, bTiltCircularDeadzone), true, CfgFlag::PER_GAME),
	ConfigSetting("TiltInputType", SETTING(g_Config, iTiltInputType), 1, CfgFlag::PER_GAME),
	ConfigSetting("TiltInputEnabled", SETTING(g_Config, bTiltInputEnabled), false, CfgFlag::PER_GAME),

	ConfigSetting("DisableDpadDiagonals", SETTING(g_Config, bDisableDpadDiagonals), false, CfgFlag::PER_GAME),
	ConfigSetting("GamepadOnlyFocused", SETTING(g_Config, bGamepadOnlyFocused), false, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonStyle", SETTING(g_Config, iTouchButtonStyle), 1, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonOpacity", SETTING(g_Config, iTouchButtonOpacity), 65, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonHideSeconds", SETTING(g_Config, iTouchButtonHideSeconds), 20, CfgFlag::PER_GAME),
	ConfigSetting("AutoCenterTouchAnalog", SETTING(g_Config, bAutoCenterTouchAnalog), false, CfgFlag::PER_GAME),
	ConfigSetting("StickyTouchDPad", SETTING(g_Config, bStickyTouchDPad), false, CfgFlag::PER_GAME),

	// Snap touch control position
	ConfigSetting("TouchSnapToGrid", SETTING(g_Config, bTouchSnapToGrid), false, CfgFlag::PER_GAME),
	ConfigSetting("TouchSnapGridSize", SETTING(g_Config, iTouchSnapGridSize), 64, CfgFlag::PER_GAME),

	ConfigSetting("AnalogDeadzone", SETTING(g_Config, fAnalogDeadzone), 0.15f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogInverseDeadzone", SETTING(g_Config, fAnalogInverseDeadzone), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogSensitivity", SETTING(g_Config, fAnalogSensitivity), 1.1f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogIsCircular", SETTING(g_Config, bAnalogIsCircular), false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogAutoRotSpeed", SETTING(g_Config, fAnalogAutoRotSpeed), 8.0f, CfgFlag::PER_GAME),

	ConfigSetting("AnalogLimiterDeadzone", SETTING(g_Config, fAnalogLimiterDeadzone), 0.6f, CfgFlag::DEFAULT),
	ConfigSetting("AnalogTriggerThreshold", SETTING(g_Config, fAnalogTriggerThreshold), 0.75f, CfgFlag::DEFAULT),

	ConfigSetting("AllowMappingCombos", SETTING(g_Config, bAllowMappingCombos), false, CfgFlag::DEFAULT),
	ConfigSetting("StrictComboOrder", SETTING(g_Config, bStrictComboOrder), false, CfgFlag::DEFAULT),

	ConfigSetting("UseMouse", SETTING(g_Config, bMouseControl), false, CfgFlag::PER_GAME),
	ConfigSetting("ConfineMap", SETTING(g_Config, bMouseConfine), false, CfgFlag::PER_GAME),
	ConfigSetting("MouseSensitivity", SETTING(g_Config, fMouseSensitivity), 0.1f, CfgFlag::PER_GAME),
	ConfigSetting("MouseSmoothing", SETTING(g_Config, fMouseSmoothing), 0.9f, CfgFlag::PER_GAME),
	ConfigSetting("MouseWheelUpDelayMs", SETTING(g_Config, iMouseWheelUpDelayMs), 80, CfgFlag::PER_GAME),

	ConfigSetting("SystemControls", SETTING(g_Config, bSystemControls), true, CfgFlag::DEFAULT),
	ConfigSetting("RapidFileInterval", SETTING(g_Config, iRapidFireInterval), 5, CfgFlag::DEFAULT),
	
	// Touch layout selection for swap layout feature
	ConfigSetting("TouchLayoutSelection", SETTING(g_Config, iTouchLayoutSelection), 1, CfgFlag::PER_GAME),
};

static const ConfigSetting networkSettings[] = {
	ConfigSetting("EnableWlan", SETTING(g_Config, bEnableWlan), false, CfgFlag::PER_GAME),
	ConfigSetting("EnableAdhocServer", SETTING(g_Config, bEnableAdhocServer), false, CfgFlag::PER_GAME),
	ConfigSetting("proAdhocServer", SETTING(g_Config, sProAdhocServer), "socom.cc", CfgFlag::PER_GAME),
	ConfigSetting("UseServerRelay", SETTING(g_Config, bUseServerRelay), false, CfgFlag::PER_GAME),
	ConfigSetting("proAdhocServerList", SETTING(g_Config, proAdhocServerList), &defaultProAdhocServerList, CfgFlag::DEFAULT),
	ConfigSetting("PortOffset", SETTING(g_Config, iPortOffset), 10000, CfgFlag::PER_GAME),
	ConfigSetting("PrimaryDNSServer", SETTING(g_Config, sInfrastructureDNSServer), "67.222.156.250", CfgFlag::PER_GAME),
	ConfigSetting("MinTimeout", SETTING(g_Config, iMinTimeout), 0, CfgFlag::PER_GAME),
	ConfigSetting("ForcedFirstConnect", SETTING(g_Config, bForcedFirstConnect), false, CfgFlag::PER_GAME),
	ConfigSetting("EnableUPnP", SETTING(g_Config, bEnableUPnP), false, CfgFlag::PER_GAME),
	ConfigSetting("UPnPUseOriginalPort", SETTING(g_Config, bUPnPUseOriginalPort), false, CfgFlag::PER_GAME),
	ConfigSetting("InfrastructureUsername", SETTING(g_Config, sInfrastructureUsername), &DefaultInfrastructureUsername, CfgFlag::PER_GAME),
	ConfigSetting("InfrastructureAutoDNS", SETTING(g_Config, bInfrastructureAutoDNS), true, CfgFlag::PER_GAME),
	ConfigSetting("AllowSavestateWhileConnected", SETTING(g_Config, bAllowSavestateWhileConnected), false, CfgFlag::DONT_SAVE),
	ConfigSetting("AllowSpeedControlWhileConnected", SETTING(g_Config, bAllowSpeedControlWhileConnected), false, CfgFlag::PER_GAME),
	ConfigSetting("DontDownloadInfraJson", SETTING(g_Config, bDontDownloadInfraJson), false, CfgFlag::DONT_SAVE),

	ConfigSetting("EnableNetworkChat", SETTING(g_Config, bEnableNetworkChat), false, CfgFlag::PER_GAME),
	ConfigSetting("ChatButtonPosition", SETTING(g_Config, iChatButtonPosition), (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME),
	ConfigSetting("ChatScreenPosition", SETTING(g_Config, iChatScreenPosition), (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME),
	ConfigSetting("EnableQuickChat", SETTING(g_Config, bEnableQuickChat), true, CfgFlag::PER_GAME),
	ConfigSetting("QuickChat1", SETTING(g_Config, sQuickChat[0]), "Quick Chat 1", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat2", SETTING(g_Config, sQuickChat[1]), "Quick Chat 2", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat3", SETTING(g_Config, sQuickChat[2]), "Quick Chat 3", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat4", SETTING(g_Config, sQuickChat[3]), "Quick Chat 4", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat5", SETTING(g_Config, sQuickChat[4]), "Quick Chat 5", CfgFlag::PER_GAME),
};

static const ConfigSetting systemParamSettings[] = {
	ConfigSetting("PSPModel", SETTING(g_Config, iPSPModel), PSP_MODEL_SLIM, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("PSPFirmwareVersion", SETTING(g_Config, iFirmwareVersion), PSP_DEFAULT_FIRMWARE, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("NickName", SETTING(g_Config, sNickName), "PPSSPP", CfgFlag::PER_GAME),
	ConfigSetting("MacAddress", SETTING(g_Config, sMACAddress), "", CfgFlag::PER_GAME),
	ConfigSetting("GameLanguage", SETTING(g_Config, iLanguage), -1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("ParamTimeFormat", SETTING(g_Config, iTimeFormat), PSP_SYSTEMPARAM_TIME_FORMAT_24HR, CfgFlag::PER_GAME),
	ConfigSetting("ParamDateFormat", SETTING(g_Config, iDateFormat), PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD, CfgFlag::PER_GAME),
	ConfigSetting("TimeZone", SETTING(g_Config, iTimeZone), 0, CfgFlag::PER_GAME),
	ConfigSetting("DayLightSavings", SETTING(g_Config, bDayLightSavings), (bool) PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD, CfgFlag::PER_GAME),
	ConfigSetting("ButtonPreference", SETTING(g_Config, iButtonPreference), PSP_SYSTEMPARAM_BUTTON_CROSS, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("LockParentalLevel", SETTING(g_Config, iLockParentalLevel), 0, CfgFlag::PER_GAME),
	ConfigSetting("WlanAdhocChannel", SETTING(g_Config, iWlanAdhocChannel), PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC, CfgFlag::PER_GAME),
#if defined(USING_WIN_UI) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH)
	ConfigSetting("BypassOSKWithKeyboard", SETTING(g_Config, bBypassOSKWithKeyboard), false, CfgFlag::PER_GAME),
#endif
	ConfigSetting("WlanPowerSave", SETTING(g_Config, bWlanPowerSave), (bool) PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF, CfgFlag::PER_GAME),
	ConfigSetting("EncryptSave", SETTING(g_Config, bEncryptSave), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("MemStickSize", SETTING(g_Config, iMemStickSizeGB), 16, CfgFlag::DEFAULT),
};

static const ConfigSetting debuggerSettings[] = {
	ConfigSetting("DisasmWindowX", SETTING(g_Config, iDisasmWindowX), -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowY", SETTING(g_Config, iDisasmWindowY), -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowW", SETTING(g_Config, iDisasmWindowW), -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowH", SETTING(g_Config, iDisasmWindowH), -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowX", SETTING(g_Config, iGEWindowX), -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowY", SETTING(g_Config, iGEWindowY), -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowW", SETTING(g_Config, iGEWindowW), -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowH", SETTING(g_Config, iGEWindowH), -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsBL", SETTING(g_Config, uGETabsLeft), (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsBR", SETTING(g_Config, uGETabsRight), (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsTR", SETTING(g_Config, uGETabsTopRight), (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("ConsoleWindowX", SETTING(g_Config, iConsoleWindowX), -1, CfgFlag::DEFAULT),
	ConfigSetting("ConsoleWindowY", SETTING(g_Config, iConsoleWindowY), -1, CfgFlag::DEFAULT),
	ConfigSetting("FontWidth", SETTING(g_Config, iFontWidth), 8, CfgFlag::DEFAULT),
	ConfigSetting("FontHeight", SETTING(g_Config, iFontHeight), 12, CfgFlag::DEFAULT),
	ConfigSetting("DisplayStatusBar", SETTING(g_Config, bDisplayStatusBar), true, CfgFlag::DEFAULT),
	ConfigSetting("ShowBottomTabTitles",SETTING(g_Config, bShowBottomTabTitles), true, CfgFlag::DEFAULT),
	ConfigSetting("ShowDeveloperMenu", SETTING(g_Config, bShowDeveloperMenu), false, CfgFlag::DEFAULT),
	ConfigSetting("SkipDeadbeefFilling", SETTING(g_Config, bSkipDeadbeefFilling), false, CfgFlag::DEFAULT),
	ConfigSetting("FuncHashMap", SETTING(g_Config, bFuncHashMap), false, CfgFlag::DEFAULT),
	ConfigSetting("SkipFuncHashMap", SETTING(g_Config, sSkipFuncHashMap), "", CfgFlag::DEFAULT),
	ConfigSetting("MemInfoDetailed", SETTING(g_Config, bDebugMemInfoDetailed), false, CfgFlag::DEFAULT),
};

static const ConfigSetting jitSettings[] = {
	ConfigSetting("DiscardRegsOnJRRA", SETTING(g_Config, bDiscardRegsOnJRRA), false, CfgFlag::DONT_SAVE | CfgFlag::REPORT),
};

static const ConfigSetting themeSettings[] = {
	ConfigSetting("ThemeName", SETTING(g_Config, sThemeName), "Default", CfgFlag::DEFAULT),
};


static const ConfigSetting vrSettings[] = {
	ConfigSetting("VREnable", SETTING(g_Config, bEnableVR), true, CfgFlag::PER_GAME),
	ConfigSetting("VREnable6DoF", SETTING(g_Config, bEnable6DoF), false, CfgFlag::PER_GAME),
	ConfigSetting("VREnableStereo", SETTING(g_Config, bEnableStereo), false, CfgFlag::PER_GAME),
	ConfigSetting("VRForce72Hz", SETTING(g_Config, bForce72Hz), true, CfgFlag::PER_GAME),
	ConfigSetting("VRForce", SETTING(g_Config, bForceVR), false, CfgFlag::DEFAULT),
	ConfigSetting("VRImmersiveMode", SETTING(g_Config, bEnableImmersiveVR), true, CfgFlag::PER_GAME),
	ConfigSetting("VRManualForceVR", SETTING(g_Config, bManualForceVR), false, CfgFlag::PER_GAME),
	ConfigSetting("VRPassthrough", SETTING(g_Config, bPassthrough), false, CfgFlag::PER_GAME),
	ConfigSetting("VRRescaleHUD", SETTING(g_Config, bRescaleHUD), true, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraDistance", SETTING(g_Config, fCameraDistance), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraHeight", SETTING(g_Config, fCameraHeight), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraSide", SETTING(g_Config, fCameraSide), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraPitch", SETTING(g_Config, fCameraPitch), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCanvasDistance", SETTING(g_Config, fCanvasDistance), 12.0f, CfgFlag::DEFAULT),
	ConfigSetting("VRCanvas3DDistance", SETTING(g_Config, fCanvas3DDistance), 3.0f, CfgFlag::DEFAULT),
	ConfigSetting("VRFieldOfView", SETTING(g_Config, fFieldOfViewPercentage), 100.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRHeadUpDisplayScale", SETTING(g_Config, fHeadUpDisplayScale), 0.3f, CfgFlag::PER_GAME),
};

// The first column says what structure the parameters are relative to.
static const ConfigSectionMeta g_sectionMeta[] = {
	{ &g_Config, generalSettings, ARRAY_SIZE(generalSettings), "General" },
	{ &g_Config, cpuSettings, ARRAY_SIZE(cpuSettings), "CPU" },
	{ &g_Config, graphicsSettings, ARRAY_SIZE(graphicsSettings), "Graphics" },
	{ &g_Config, soundSettings, ARRAY_SIZE(soundSettings), "Sound" },
	{ &g_Config, controlSettings, ARRAY_SIZE(controlSettings), "Control" },
	{ &g_Config, systemParamSettings, ARRAY_SIZE(systemParamSettings), "SystemParam" },
	{ &g_Config, networkSettings, ARRAY_SIZE(networkSettings), "Network" },
	{ &g_Config, debuggerSettings, ARRAY_SIZE(debuggerSettings), "Debugger" },
	{ &g_Config, jitSettings, ARRAY_SIZE(jitSettings), "JIT" },
	{ &g_Config, themeSettings, ARRAY_SIZE(themeSettings), "Theme" },
	{ &g_Config, vrSettings, ARRAY_SIZE(vrSettings), "VR" },
	{ &g_Config, achievementSettings, ARRAY_SIZE(achievementSettings), "Achievements" },
	{ &g_Config.displayLayoutLandscape, displayLayoutSettings, ARRAY_SIZE(displayLayoutSettings), "DisplayLayout.Landscape", "Graphics" },  // We read the old settings from [Graphics], since most people played in landscape before.
	{ &g_Config.displayLayoutPortrait, displayLayoutSettings, ARRAY_SIZE(displayLayoutSettings), "DisplayLayout.Portrait"},  // These we don't want to read from the old settings, since for most people, those settings will be bad.
	{ &g_Config.touchControlsLandscape, touchControlSettings, ARRAY_SIZE(touchControlSettings), "TouchControls.Landscape", "Control" },  // We read the old settings from [Control], since most people played in landscape before.
	{ &g_Config.touchControlsPortrait, touchControlSettings, ARRAY_SIZE(touchControlSettings), "TouchControls.Portrait"},  // These we don't want to read from the old settings, since for most people, those settings will be bad.
	{ &g_Config.gestureControls[0], gestureControlSettings, ARRAY_SIZE(gestureControlSettings), "GestureControls.Left", "General"},  // We read the old settings from [General], since most of them used to be there (except the analog stuff).
	{ &g_Config.gestureControls[1], gestureControlSettings, ARRAY_SIZE(gestureControlSettings), "GestureControls.Right", "General"},  // We read the old settings from [General], since most of them used to be there (except the analog stuff).
};

ConfigBlock *GetConfigBlockForSection(std::string_view sectionName) {
	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		if (equals(meta.section, sectionName)) {
			return meta.configBlock;
		}
	}
	return nullptr;
}

const size_t numSections = ARRAY_SIZE(g_sectionMeta);

std::map<const void *, std::pair<const ConfigBlock *, const ConfigSetting *>> &Config::getPtrLUT() {
	static std::map<const void *, std::pair<const ConfigBlock *, const ConfigSetting *>> lut;
	return lut;
}

int Config::GetDefaultValueInt(int *configSetting) {
	const auto &lut = getPtrLUT();
	auto it = lut.find(configSetting);
	if (it != lut.end()) {
		const ConfigBlock *block = it->second.first;
		if (block->CanResetToDefault()) {
			// TODO: Support this.
			_dbg_assert_(false);
			return NO_DEFAULT_INT;
		}
		const ConfigSetting *setting = it->second.second;
		return setting->GetDefaultInt();
	}
	return NO_DEFAULT_INT;
}

void Config::Init() {
	// Initialize the pointer->setting lookup map.
	auto &ref = getPtrLUT();
	for (size_t i = 0; i < numSections; ++i) {
		ConfigBlock *configBlock = g_sectionMeta[i].configBlock;
		for (size_t j = 0; j < g_sectionMeta[i].settingsCount; j++) {
			const void *ptr = g_sectionMeta[i].settings[j].GetVoidPtr(configBlock);
			ref[ptr] = std::make_pair(configBlock, &g_sectionMeta[i].settings[j]);
		}
	}
	_dbg_assert_(ref.size() > 1);
}

Config::~Config() {
	if (bUpdatedInstanceCounter) {
		ShutdownInstanceCounter();
	}
}

void Config::Reload() {
	inReload_ = true;
	Load();
	inReload_ = false;
}

// Call this if you change the search path (such as when changing memstick directory. can't
// really think of any other legit uses).
void Config::UpdateIniLocation(const char *iniFileName, const char *controllerIniFilename) {
	const bool useIniFilename = iniFileName != nullptr && strlen(iniFileName) > 0;
	const char *ppssppIniFilename = IsVREnabled() ? "ppssppvr.ini" : "ppsspp.ini";
	bool exists;
	iniFilename_ = FindConfigFile(searchPath_, useIniFilename ? iniFileName : ppssppIniFilename, &exists);
	const bool useControllerIniFilename = controllerIniFilename != nullptr && strlen(controllerIniFilename) > 0;
	const char *controlsIniFilename = IsVREnabled() ? "controlsvr.ini" : "controls.ini";
	controllerIniFilename_ = FindConfigFile(searchPath_, useControllerIniFilename ? controllerIniFilename : controlsIniFilename, &exists);
}

bool Config::LoadAppendedConfig() {
	IniFile iniFile;
	if (!iniFile.Load(appendedConfigFileName_)) {
		ERROR_LOG(Log::Loader, "Failed to read appended config '%s'.", appendedConfigFileName_.c_str());
		return false;
	}

	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		Section *section = iniFile.GetSection(meta.section);
		if (!section) {
			continue;
		}
		for (size_t j = 0; j < meta.settingsCount; j++) {
			meta.settings[j].ReadFromIniSection(meta.configBlock, section, false);
		}
	}

	INFO_LOG(Log::Loader, "Loaded appended config '%s'.", appendedConfigFileName_.c_str());

	Save("Loaded appended config"); // Let's prevent reset
	return true;
}

void Config::UpdateAfterSettingAutoFrameSkip() {
	if (bAutoFrameSkip && bSkipBufferEffects) {
		bSkipBufferEffects = false;
	}
}

void Config::ReadAllSettings(const IniFile &iniFile) {
	// Read settings. Note, configblocks can now support their own defaulting mechanism.
	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		const Section *section = iniFile.GetSection(meta.section);
		ConfigBlock *configBlock = meta.configBlock;
		// Not found? Try the fallback (to upgrade settings that have been moved from old sections).
		if (!section && !meta.fallbackSectionName.empty()) {
			section = iniFile.GetSection(meta.fallbackSectionName);
			// NOTE: it's tempting to update the configBlock here, but that's not what we want to do!
			// We just want to read from a different section in the ini file, we still want to read into
			// the same configBlock.
		}
		// If section is still null, we'll handle that gracefully by resetting to defaults.
		bool applyDefaultPerSetting = true;
		if (configBlock->ResetToDefault(meta.section)) {
			applyDefaultPerSetting = false;
		}
		for (size_t j = 0; j < meta.settingsCount; j++) {
			const ConfigSetting &setting = meta.settings[j];
			setting.ReadFromIniSection(configBlock, section, applyDefaultPerSetting);
		}
	}
}

void Config::Load(const char *iniFileName, const char *controllerIniFilename) {
	double startTime = time_now_d();

	if (!bUpdatedInstanceCounter) {
		InitInstanceCounter();
		bUpdatedInstanceCounter = true;
	}

	g_DownloadManager.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

	UpdateIniLocation(iniFileName, controllerIniFilename);

	INFO_LOG(Log::Loader, "Loading config: %s", iniFilename_.c_str());
	bSaveSettings = true;

	IniFile iniFile;
	if (!iniFile.Load(iniFilename_)) {
		WARN_LOG(Log::Loader, "Failed to read '%s'. Setting main config to default.", iniFilename_.c_str());
		// Continue anyway to initialize the config.
	}

	ReadAllSettings(iniFile);

	iRunCount++;

	// For iOS, issue #19211
	TryUpdateSavedPath(&currentDirectory);

	Section *log = iniFile.GetOrCreateSection(logSectionName);

	g_logManager.LoadConfig(log);

	Section *recent = iniFile.GetOrCreateSection("Recent");
	iMaxRecent = 60;
	recent->Get("MaxRecent", &iMaxRecent);

	// Fix issue from switching from uint (hex in .ini) to int (dec)
	// -1 is okay, though. We'll just ignore recent stuff if it is.
	if (iMaxRecent == 0)
		iMaxRecent = 60;

	// Fix JIT setting if no longer available.
	if (!System_GetPropertyBool(SYSPROP_CAN_JIT)) {
		if (iCpuCore == (int)CPUCore::JIT || iCpuCore == (int)CPUCore::JIT_IR) {
			WARN_LOG(Log::Loader, "Forcing JIT off due to unavailablility");
			iCpuCore = (int)CPUCore::IR_INTERPRETER;
		}
	}

	if (iMaxRecent > 0) {
		g_recentFiles.Load(recent, iMaxRecent);
		g_recentFiles.Clean();
	}

	// Time tracking
	Section *playTime = iniFile.GetOrCreateSection("PlayTime");
	playTimeTracker_.Load(playTime);

	auto pinnedPaths = iniFile.GetOrCreateSection("PinnedPaths")->ToMap();
	vPinnedPaths.clear();
	for (const auto &[_, value] : pinnedPaths) {
		// Unpin paths that are deleted automatically.
		const std::string &path = value;
		if (startsWith(path, "http://") || startsWith(path, "https://") || File::Exists(Path(path))) {
			vPinnedPaths.push_back(File::ResolvePath(path));
		}
	}

	// Default values for post process shaders
	bool postShadersInitialized = iniFile.HasSection("PostShaderList");
	Section *postShaderChain = iniFile.GetOrCreateSection("PostShaderList");
	Section *postShaderSetting = iniFile.GetOrCreateSection("PostShaderSetting");
	if (IsVREnabled() && !postShadersInitialized) {
		postShaderChain->Set("PostShader1", "ColorCorrection");
		postShaderSetting->Set("ColorCorrectionSettingCurrentValue1", 1.0f);
		postShaderSetting->Set("ColorCorrectionSettingCurrentValue2", 1.5f);
		postShaderSetting->Set("ColorCorrectionSettingCurrentValue3", 1.1f);
		postShaderSetting->Set("ColorCorrectionSettingCurrentValue4", 1.0f);
	}

	// Load post process shader values
	mPostShaderSetting.clear();
	for (const auto &[key, value] : postShaderSetting->ToMap()) {
		mPostShaderSetting[key] = std::stof(value);
	}

	const Section *hostOverrideSetting = iniFile.GetOrCreateSection("HostAliases");
	// TODO: relocate me before PR
	mHostToAlias = hostOverrideSetting->ToMap();

	// Load post process shader names
	vPostShaderNames.clear();
	for (const auto& it : postShaderChain->ToMap()) {
		if (it.second != "Off")
			vPostShaderNames.push_back(it.second);
	}

	// Force JIT setting to a valid value for the current system configuration.
	if (!System_GetPropertyBool(SYSPROP_CAN_JIT)) {
		if (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR) {
			g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
		}
	}

	INFO_LOG(Log::Loader, "Loading controller config: %s", controllerIniFilename_.c_str());
	bSaveSettings = true;

	LoadStandardControllerIni();

	// So this is all the way down here to overwrite the controller settings
	// sadly it won't benefit from all the "version conversion" going on up-above
	// but these configs shouldn't contain older versions anyhow
	_dbg_assert_(!IsGameSpecific());

	// Ensure layout2 defaults are initialized from layout1 so swapping is visible
	EnsureSecondaryLayoutsInitialized();

	PostLoadCleanup();

	INFO_LOG(Log::Loader, "Config loaded: '%s' (%0.1f ms)", iniFilename_.c_str(), (time_now_d() - startTime) * 1000.0);
}

bool Config::ShouldSaveSetting(const void *ptr) const {
	return std::find(settingsNotToSave_.begin(), settingsNotToSave_.end(), ptr) == settingsNotToSave_.end();
}

// If we're in game specific mode, we need to:
// * Save the game-specific settings to the game-specific ini file.
// * Then, save the NON-game-specific settings ONLY to the regular ini file!
bool Config::Save(const char *saveReason) {
	double startTime = time_now_d();
	if (!IsFirstInstance()) {
		// TODO: Should we allow saving config if started from a different directory?
		// How do we tell?
		WARN_LOG(Log::Loader, "Not saving config - secondary instances don't.");
		// Don't want to retry or something.
		return true;
	}

	if (!iniFilename_.empty() && g_Config.bSaveSettings) {
		if (IsGameSpecific()) {
			// Save just the game-specific settings to the game-specific ini.
			SaveGameConfig(gameId_, "");  // we don't pass a title, it was stored in the ini the first time.
		}

		PreSaveCleanup();

		g_recentFiles.Clean();
		IniFile iniFile;
		if (!iniFile.Load(iniFilename_)) {
			WARN_LOG(Log::Loader, "Likely saving config for first time - couldn't read ini '%s'", iniFilename_.c_str());
		}

		// Need to do this somewhere...
		bFirstRun = false;

		// Do the writing.
		for (const ConfigSectionMeta &meta : g_sectionMeta) {
			Section *section = iniFile.GetOrCreateSection(meta.section);
			ConfigBlock *configBlock = meta.configBlock;
			for (size_t j = 0; j < meta.settingsCount; j++) {
				if (IsGameSpecific() && (meta.settings[j].Flags() & CfgFlag::PER_GAME)) {
					// Skip per-game settings in non-game-specific ini.
					continue;
				}
				if (!ShouldSaveSetting(meta.settings[j].GetVoidPtr(configBlock))) {
					// Skip settings marked as "don't save".
					INFO_LOG(Log::System, "Not saving setting '%.*s' as marked as don't save.", STR_VIEW(meta.settings[j].IniKey()));
					continue;
				}
				meta.settings[j].WriteToIniSection(configBlock, section);
			}
		}

		Section *recent = iniFile.GetOrCreateSection("Recent");
		recent->Set("MaxRecent", iMaxRecent);
		g_recentFiles.Save(recent, iMaxRecent);

		Section *pinnedPaths = iniFile.GetOrCreateSection("PinnedPaths");
		pinnedPaths->Clear();
		for (size_t i = 0; i < vPinnedPaths.size(); ++i) {
			char keyName[64];
			snprintf(keyName, sizeof(keyName), "Path%d", (int)i);
			pinnedPaths->Set(keyName, vPinnedPaths[i]);
		}

		if (!IsGameSpecific()) {
			// These settings can be game specific, and so are handled in SaveGameConfig().

			Section *postShaderSetting = iniFile.GetOrCreateSection("PostShaderSetting");
			postShaderSetting->Clear();
			for (const auto &[k, v] : mPostShaderSetting) {
				postShaderSetting->Set(k, v);
			}
			Section *postShaderChain = iniFile.GetOrCreateSection("PostShaderList");
			postShaderChain->Clear();
			for (size_t i = 0; i < vPostShaderNames.size(); ++i) {
				char keyName[64];
				snprintf(keyName, sizeof(keyName), "PostShader%d", (int)i+1);
				postShaderChain->Set(keyName, vPostShaderNames[i]);
			}
		}

		// TODO: relocate me before PR
		Section *hostOverrideSetting = iniFile.GetOrCreateSection("HostAliases");
		hostOverrideSetting->Clear();
		for (auto& it : mHostToAlias) {
			hostOverrideSetting->Set(it.first.c_str(), it.second.c_str());
		}

		Section *control = iniFile.GetOrCreateSection("Control");
		control->Delete("DPadRadius");

		Section *log = iniFile.GetOrCreateSection(logSectionName);
		g_logManager.SaveConfig(log);

		// Time tracking
		Section *playTime = iniFile.GetOrCreateSection("PlayTime");
		playTimeTracker_.Save(playTime);

		if (!iniFile.Save(iniFilename_)) {
			ERROR_LOG(Log::Loader, "Error saving config (%s) - can't write ini '%s'", saveReason, iniFilename_.c_str());
			return false;
		}
		INFO_LOG(Log::Loader, "Config saved (%s): '%s' (%0.1f ms)", saveReason, iniFilename_.c_str(), (time_now_d() - startTime) * 1000.0);

		if (!IsGameSpecific()) {
			// These settings can be game specific, and so are handled in SaveGameConfig().
			IniFile controllerIniFile;
			if (!controllerIniFile.Load(controllerIniFilename_)) {
				ERROR_LOG(Log::Loader, "Error saving controller config - can't read ini first '%s'", controllerIniFilename_.c_str());
			}
			KeyMap::SaveToIni(controllerIniFile);
			if (!controllerIniFile.Save(controllerIniFilename_)) {
				ERROR_LOG(Log::Loader, "Error saving config - can't write ini '%s'", controllerIniFilename_.c_str());
				return false;
			}
			INFO_LOG(Log::Loader, "Controller config saved: %s", controllerIniFilename_.c_str());
		}

		PostSaveCleanup();
	} else {
		INFO_LOG(Log::Loader, "Not saving config");
	}

	return true;
}

// A lot more cleanup tasks should be moved into here, and some of these are severely outdated.
void Config::PostLoadCleanup() {
	// Override ppsspp.ini JIT value to prevent crashing
	jitForcedOff = DefaultCpuCore() != (int)CPUCore::JIT && (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR);
	if (jitForcedOff) {
		g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
	}

	// This caps the aniso level exponent to 4 (so 16x.). No hardware supports more anyway.
	iAnisotropyLevel = std::clamp(iAnisotropyLevel, 0, 4);

	if (iGPUBackend == 1) {  // d3d9, no longer supported. Fall back to D3D11.
		iGPUBackend = (int)GPUBackend::DIRECT3D11;
	} else if (iGPUBackend < 0 || iGPUBackend > 3) {
		iGPUBackend = (int)DefaultGPUBackend();
	}

#if PPSSPP_PLATFORM(UWP)
	// Enforce D3D11.
	iGPUBackend = (int)GPUBackend::DIRECT3D11;
#endif

	// Set a default MAC, and correct if it's an old format.
	if (sMACAddress.length() != 17)
		sMACAddress = CreateRandMAC();

	if (g_Config.bAutoFrameSkip && g_Config.bSkipBufferEffects) {
		g_Config.bSkipBufferEffects = false;
	}

	// Automatically silence secondary instances. Could be an option I guess, but meh.
	if (PPSSPP_ID > 1) {
		NOTICE_LOG(Log::Audio, "Secondary instance %d - silencing audio", (int)PPSSPP_ID);
		g_Config.iGameVolume = 0;
	}

	// Automatically switch away from deprecated setting value.
	if (iTexScalingLevel <= 0) {
		iTexScalingLevel = 1;
	}

	// Remove a legacy value.
	if (g_Config.sCustomDriver == "Default") {
		g_Config.sCustomDriver = "";
	}

	// Squash unsupported screen rotations.
	if (g_Config.iScreenRotation == ROTATION_AUTO_HORIZONTAL) {
		g_Config.iScreenRotation = ROTATION_LOCKED_HORIZONTAL;
	} else if (g_Config.iScreenRotation == ROTATION_LOCKED_VERTICAL180) {
		g_Config.iScreenRotation = ROTATION_LOCKED_VERTICAL;
	}

	// Clamp save state slot count to somewhat sane limits.
	g_Config.iSaveStateSlotCount = std::clamp(g_Config.iSaveStateSlotCount, 1, 100);
}

void Config::PreSaveCleanup() {
	if (jitForcedOff) {
		// If we forced jit off and it's still set to IR, change it back to jit.
		if (g_Config.iCpuCore == (int)CPUCore::IR_INTERPRETER)
			g_Config.iCpuCore = (int)CPUCore::JIT;
	}
}

void Config::PostSaveCleanup() {
	if (jitForcedOff) {
		// Force JIT off again just in case Config::Save() is called without exiting PPSSPP.
		if (g_Config.iCpuCore == (int)CPUCore::JIT)
			g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
	}
}

void Config::NotifyUpdatedCpuCore() {
	if (jitForcedOff && g_Config.iCpuCore == (int)CPUCore::IR_INTERPRETER) {
		// No longer forced off, the user set it to IR jit.
		jitForcedOff = false;
	}
}

void Config::SetSearchPath(const Path &searchPath) {
	searchPath_ = searchPath;
}

void Config::RestoreDefaults(RestoreSettingsBits whatToRestore, bool log) {
	if (IsGameSpecific()) {
		// TODO: This could be done in a cleaner way.
		DeleteGameConfig(gameId_);
		CreateGameConfig(gameId_);
		Load();
	} else {
		if (whatToRestore & RestoreSettingsBits::SETTINGS) {
			// Read settings. Note, ConfigBlocks can now support their own defaulting mechanism.
			for (const auto &meta : g_sectionMeta) {
				ConfigBlock *configBlock = meta.configBlock;
				bool applyDefaultPerSetting = true;
				if (!configBlock->ResetToDefault(meta.section)) {
					// Reset the settings one by one.
					for (size_t j = 0; j < meta.settingsCount; j++) {
						meta.settings[j].RestoreToDefault(configBlock, log);
					}
				}
			}
		}

		if (whatToRestore & RestoreSettingsBits::CONTROLS) {
			KeyMap::RestoreDefault();
		}

		if (whatToRestore & RestoreSettingsBits::RECENT) {
			g_recentFiles.Clear();
			currentDirectory = defaultCurrentDirectory;
		}
	}
}

bool Config::HasGameConfig(std::string_view gameId) {
	if (gameId.empty()) {
		return false;
	}
	bool exists = false;
	Path fullIniFilePath = GetGameConfigFilePath(searchPath_, gameId, &exists);
	return exists;
}

bool Config::CreateGameConfig(std::string_view gameId) {
	if (gameId.empty()) {
		return false;
	}
	bool exists;
	Path fullIniFilePath = GetGameConfigFilePath(searchPath_, gameId, &exists);

	if (exists) {
		INFO_LOG(Log::System, "Game config already exists");
		return false;
	}

	File::CreateEmptyFile(fullIniFilePath);
	return true;
}

bool Config::DeleteGameConfig(std::string_view gameId) {
	bool exists = false;
	Path fullIniFilePath = GetGameConfigFilePath(searchPath_, gameId, &exists);

	if (exists) {
		if (System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN)) {
			System_MoveToTrash(fullIniFilePath);
		} else {
			File::Delete(fullIniFilePath);
		}
	}
	return true;
}

bool Config::SaveGameConfig(const std::string &gameId, std::string_view titleForComment) {
	if (gameId.empty()) {
		return false;
	}

	if (gameId_.empty()) {
		INFO_LOG(Log::G3D, "Switching to game-specific mode for saving config: %s", gameId.c_str());
		gameId_ = gameId;
	}

	bool exists;
	Path fullIniFilePath = GetGameConfigFilePath(searchPath_, gameId, &exists);

	IniFile iniFile;

	// Just like regular configs, we should load and save, in order to preserve things like comments.
	iniFile.Load(fullIniFilePath);

	Section *top = iniFile.GetOrCreateSection("");
	if (top->Lines().empty() && !titleForComment.empty()) {
		top->AddComment(StringFromFormat("Game config for %s - %.*s", gameId.c_str(), STR_VIEW(titleForComment)));
	}

	PreSaveCleanup();

	// Do all the actual saving.
	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		Section *section = iniFile.GetOrCreateSection(meta.section);
		ConfigBlock *configBlock = meta.configBlock;
		for (size_t j = 0; j < meta.settingsCount; j++) {
			if (meta.settings[j].PerGame()) {
				meta.settings[j].WriteToIniSection(configBlock, section);
			}
		}
	}

	Section *postShaderSetting = iniFile.GetOrCreateSection("PostShaderSetting");
	postShaderSetting->Clear();
	for (const auto &[k, v] : mPostShaderSetting) {
		postShaderSetting->Set(k, v);
	}

	Section *postShaderChain = iniFile.GetOrCreateSection("PostShaderList");
	postShaderChain->Clear();
	for (size_t i = 0; i < vPostShaderNames.size(); ++i) {
		char keyName[64];
		snprintf(keyName, sizeof(keyName), "PostShader%d", (int)i+1);
		postShaderChain->Set(keyName, vPostShaderNames[i]);
	}

	KeyMap::SaveToIni(iniFile);
	iniFile.Save(fullIniFilePath);

	INFO_LOG(Log::Loader, "Game-specific config saved: '%s'", fullIniFilePath.c_str());

	PostSaveCleanup();
	return true;
}

bool Config::LoadGameConfig(const std::string &gameId) {
	bool exists;
	Path iniFileNameFull = GetGameConfigFilePath(searchPath_, gameId, &exists);
	if (!exists) {
		// Bail if there's no game-specific config.
		DEBUG_LOG(Log::Loader, "No game-specific settings found in %s. Using global defaults.", iniFileNameFull.c_str());
		return false;
	}

	// Switch to game specific mode, if we're not in it.
	if (gameId_.empty()) {
		INFO_LOG(Log::Loader, "Switching to game specific mode before load: %s", gameId.c_str());
		gameId_ = gameId;
	}

	IniFile iniFile;
	iniFile.Load(iniFileNameFull);

	auto postShaderSetting = iniFile.GetOrCreateSection("PostShaderSetting")->ToMap();
	mPostShaderSetting.clear();
	for (const auto &[k, v] : postShaderSetting) {
		float value = 0.0f;
		if (sscanf(v.c_str(), "%f", &value)) {
			mPostShaderSetting[k] = value;
		} else {
			WARN_LOG(Log::Loader, "Invalid float value string for param %s: '%s'", k.c_str(), v.c_str());
		}
	}

	auto postShaderChain = iniFile.GetOrCreateSection("PostShaderList")->ToMap();
	vPostShaderNames.clear();
	for (const auto &[_, v] : postShaderChain) {
		if (v != "Off")
			vPostShaderNames.push_back(v);
	}

	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		Section *section = iniFile.GetSection(meta.section);
		ConfigBlock *configBlock = meta.configBlock;
		// Not found? Try the fallback (to upgrade settings that have been moved from old sections).
		if (!section && !meta.fallbackSectionName.empty()) {
			section = iniFile.GetSection(meta.fallbackSectionName);
			// NOTE: it's tempting to update the configBlock here, but that's not what we want to do!
			// We just want to read from a different section in the ini file, we still want to read into
			// the same configBlock.
		}
		for (size_t j = 0; j < meta.settingsCount; j++) {
			meta.settings[j].ReadFromIniSection(configBlock, section, false);
		}
	}

	KeyMap::LoadFromIni(iniFile);

	if (!appendedConfigFileName_.ToString().empty() &&
		std::find(appendedConfigUpdatedGames_.begin(), appendedConfigUpdatedGames_.end(), gameId) == appendedConfigUpdatedGames_.end()) {

		LoadAppendedConfig();
		appendedConfigUpdatedGames_.push_back(gameId);
	}

	PostLoadCleanup();

	DEBUG_LOG(Log::Loader, "Game-specific config loaded: %s", gameId_.c_str());
	return true;
}

void Config::UnloadGameConfig() {
	_dbg_assert_(IsGameSpecific());

	// Leave game-specific mode.
	gameId_.clear();

	// Reload all settings from the main ini file.
	IniFile iniFile;
	iniFile.Load(iniFilename_);

	ReadAllSettings(iniFile);

	auto postShaderSetting = iniFile.GetOrCreateSection("PostShaderSetting")->ToMap();
	mPostShaderSetting.clear();
	for (const auto &[k, v] : postShaderSetting) {
		mPostShaderSetting[k] = std::stof(v);
	}

	auto postShaderChain = iniFile.GetOrCreateSection("PostShaderList")->ToMap();
	vPostShaderNames.clear();
	for (const auto &[k, v] : postShaderChain) {
		if (v != "Off")
			vPostShaderNames.push_back(v);
	}

	LoadStandardControllerIni();
	PostLoadCleanup();
}

void Config::LoadStandardControllerIni() {
	IniFile controllerIniFile;
	if (!controllerIniFile.Load(controllerIniFilename_)) {
		WARN_LOG(Log::Loader, "Failed to read '%s'. Setting controller config to default.", controllerIniFilename_.c_str());
		KeyMap::RestoreDefault();
	} else {
		// Continue anyway to initialize the config. It will just restore the defaults.
		KeyMap::LoadFromIni(controllerIniFile);
	}
}

void Config::GetReportingInfo(UrlEncoder &data) const {
	for (const ConfigSectionMeta &meta : g_sectionMeta) {
		const std::string prefix = join("config.", meta.section);
		ConfigBlock *configBlock = meta.configBlock;
		for (size_t j = 0; j < meta.settingsCount; j++) {
			meta.settings[j].ReportSetting(configBlock, data, prefix);
		}
	}
}

void PlayTimeTracker::Start(std::string_view gameId) {
	if (gameId.empty()) {
		return;
	}
	VERBOSE_LOG(Log::System, "GameTimeTracker::Start(%.*s)", STR_VIEW(gameId));

	auto iter = tracker_.find(gameId);
	if (iter != tracker_.end()) {
		if (iter->second.startTime == 0.0) {
			iter->second.lastTimePlayed = time_now_unix_utc();
			iter->second.startTime = time_now_d();
		}
		return;
	}

	PlayTime playTime;
	playTime.lastTimePlayed = time_now_unix_utc();
	playTime.totalTimePlayed = 0.0;
	playTime.startTime = time_now_d();
	tracker_[std::string(gameId)] = playTime;
}

void PlayTimeTracker::Stop(std::string_view gameId) {
	if (gameId.empty()) {
		return;
	}

	VERBOSE_LOG(Log::System, "GameTimeTracker::Stop(%.*s)", STR_VIEW(gameId));

	auto iter = tracker_.find(gameId);
	if (iter != tracker_.end()) {
		if (iter->second.startTime != 0.0) {
			iter->second.totalTimePlayed += time_now_d() - iter->second.startTime;
			iter->second.startTime = 0.0;
		}
		iter->second.lastTimePlayed = time_now_unix_utc();
		return;
	}

	// Shouldn't happen, ignore this case.
	WARN_LOG(Log::System, "GameTimeTracker::Stop called without corresponding GameTimeTracker::Start");
}

void PlayTimeTracker::Reset(std::string_view gameId) {
	auto iter = tracker_.find(gameId);
	if (iter != tracker_.end()) {
		iter->second.lastTimePlayed = 0;
		iter->second.totalTimePlayed = 0;
		iter->second.startTime = 0.0;
	}
}

void PlayTimeTracker::Load(const Section *section) {
	tracker_.clear();

	const auto map = section->ToMap();

	for (const auto &iter : map) {
		const std::string &value = iter.second;
		// Parse the string.
		PlayTime gameTime{};
		if (2 == sscanf(value.c_str(), "%d,%llu", &gameTime.totalTimePlayed, (long long *)&gameTime.lastTimePlayed)) {
			tracker_[iter.first] = gameTime;
		}
	}
}

void PlayTimeTracker::Save(Section *section) {
	for (auto &iter : tracker_) {
		std::string formatted = StringFromFormat("%d,%llu", iter.second.totalTimePlayed, iter.second.lastTimePlayed);
		section->Set(iter.first, formatted);
	}
}

bool PlayTimeTracker::GetPlayedTimeString(std::string_view gameId, std::string *str) const {
	auto ga = GetI18NCategory(I18NCat::GAME);

	auto iter = tracker_.find(gameId);
	if (iter == tracker_.end()) {
		return false;
	}

	int totalSeconds = iter->second.totalTimePlayed;
	const int seconds = totalSeconds % 60;
	totalSeconds /= 60;
	const int minutes = totalSeconds % 60;
	totalSeconds /= 60;
	const int hours = totalSeconds;

	*str = ApplySafeSubstitutions(ga->T("Time Played: %1h %2m %3s"), hours, minutes, seconds);
	return true;
}

// This matches exactly the old shift-based curve.
float Volume10ToMultiplier(int volume) {
	// Allow muting entirely.
	if (volume <= 0) {
		return 0.0f;
	}
	return powf(2.0f, (float)(volume - 10));
}

// NOTE: This is used for new volume parameters.
// It uses a more intuitive-feeling curve.
float Volume100ToMultiplier(int volume) {
	// Switch to linear above the 1.0f point.
	if (volume > 100) {
		return volume / 100.0f;
	}
	return powf(volume * 0.01f, 1.75f);
}

// Used for migration from the old settings.
int MultiplierToVolume100(float multiplier) {
	// Switch to linear above the 1.0f point.
	if (multiplier > 1.0f) {
		return multiplier * 100;
	}
	return (int)(powf(multiplier, 1.0f / 1.75f) * 100.f + 0.5f);
}

float UIScaleFactorToMultiplier(int factor) {
	return powf(2.0f, (float)factor / 8.0f);
}
void Config::SwapTouchControlsLayouts(DeviceOrientation orientation) {
	if (orientation == DeviceOrientation::Portrait) {
		std::swap(touchControlsPortrait, touchControlsPortraitLayout2);
	} else {
		std::swap(touchControlsLandscape, touchControlsLandscapeLayout2);
	}
	// Note: do NOT auto-save here. Caller should persist if desired.
}

void Config::EnsureSecondaryLayoutsInitialized() {
	auto IsConfigured = [](const TouchControlConfig &c) {
		// If any position has been set (x >= 0), or any custom button is visible,
		// consider the layout configured.
		auto posSet = [](const ConfigTouchPos &p) { return p.x >= 0.0f || p.y >= 0.0f; };

		if (posSet(c.touchActionButtonCenter) || posSet(c.touchDpad) || posSet(c.touchStartKey) ||
			posSet(c.touchSelectKey) || posSet(c.touchFastForwardKey) || posSet(c.touchLKey) ||
			posSet(c.touchRKey) || posSet(c.touchAnalogStick) || posSet(c.touchRightAnalogStick) ||
			posSet(c.touchPauseKey)) {
			return true;
		}

		for (size_t i = 0; i < TouchControlConfig::CUSTOM_BUTTON_COUNT; ++i) {
			if (c.touchCustom[i].show)
				return true;
			if (posSet(c.touchCustom[i]))
				return true;
		}

		return false;
	};

	if (!IsConfigured(touchControlsPortraitLayout2)) {
		// Use preset defaults for the secondary layout instead of copying
		// the primary layout. This avoids unintentionally duplicating
		// user-customized primary layouts into layout 2.
		touchControlsPortraitLayout2.ResetToDefault("TouchControls.PortraitLayout2");
	}
	if (!IsConfigured(touchControlsLandscapeLayout2)) {
		touchControlsLandscapeLayout2.ResetToDefault("TouchControls.LandscapeLayout2");
	}
}