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
#include "Common/Data/Format/JSONReader.h"
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
#include "Common/Thread/ThreadUtil.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/System/OSD.h"
#include "Core/Config.h"
#include "Core/ConfigSettings.h"
#include "Core/ConfigValues.h"
#include "Core/Loaders.h"
#include "Core/KeyMap.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Instance.h"
#include "GPU/Common/FramebufferManagerCommon.h"

// TODO: Find a better place for this.
http::RequestManager g_DownloadManager;

Config g_Config;

static bool jitForcedOff;

// Not in Config.h because it's #included a lot.
struct ConfigPrivate {
	std::mutex recentIsosLock;
	std::mutex recentIsosThreadLock;
	std::thread recentIsosThread;
	bool recentIsosThreadPending = false;

	void ResetRecentIsosThread();
	void SetRecentIsosThread(std::function<void()> f);
};

#ifdef _DEBUG
static const char * const logSectionName = "LogDebug";
#else
static const char * const logSectionName = "Log";
#endif

static bool TryUpdateSavedPath(Path *path);

std::string GPUBackendToString(GPUBackend backend) {
	switch (backend) {
	case GPUBackend::OPENGL:
		return "OPENGL";
	case GPUBackend::DIRECT3D9:
		return "DIRECT3D9";
	case GPUBackend::DIRECT3D11:
		return "DIRECT3D11";
	case GPUBackend::VULKAN:
		return "VULKAN";
	}
	// Intentionally not a default so we get a warning.
	return "INVALID";
}

GPUBackend GPUBackendFromString(std::string_view backend) {
	if (!equalsNoCase(backend, "OPENGL") || backend == "0")
		return GPUBackend::OPENGL;
	if (!equalsNoCase(backend, "DIRECT3D9") || backend == "1")
		return GPUBackend::DIRECT3D9;
	if (!equalsNoCase(backend, "DIRECT3D11") || backend == "2")
		return GPUBackend::DIRECT3D11;
	if (!equalsNoCase(backend, "VULKAN") || backend == "3")
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
		mapping.GetKeys("LangRegionNames", keys);

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
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(RISCV64)
	if (System_GetPropertyBool(SYSPROP_CAN_JIT))
		return (int)CPUCore::JIT;
	return (int)CPUCore::IR_INTERPRETER;
#else
	return (int)CPUCore::IR_INTERPRETER;
#endif
}

static bool DefaultCodeGen() {
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(RISCV64)
	return true;
#else
	return false;
#endif
}

static bool DefaultVSync() {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(UWP)
	// Previously we didn't allow turning off vsync/FIFO on Android. Let's set the default accordingly.
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

static float DefaultUISaturation() {
	return IsVREnabled() ? 1.5f : 1.0f;
}

static const ConfigSetting generalSettings[] = {
	ConfigSetting("FirstRun", &g_Config.bFirstRun, true, CfgFlag::DEFAULT),
	ConfigSetting("RunCount", &g_Config.iRunCount, 0, CfgFlag::DEFAULT),
	ConfigSetting("Enable Logging", &g_Config.bEnableLogging, true, CfgFlag::DEFAULT),
	ConfigSetting("AutoRun", &g_Config.bAutoRun, true, CfgFlag::DEFAULT),
	ConfigSetting("Browse", &g_Config.bBrowse, false, CfgFlag::DEFAULT),
	ConfigSetting("IgnoreBadMemAccess", &g_Config.bIgnoreBadMemAccess, true, CfgFlag::DEFAULT),
	ConfigSetting("CurrentDirectory", &g_Config.currentDirectory, "", CfgFlag::DEFAULT),
	ConfigSetting("ShowDebuggerOnLoad", &g_Config.bShowDebuggerOnLoad, false, CfgFlag::DEFAULT),
	ConfigSetting("ShowImDebugger", &g_Config.bShowImDebugger, false, CfgFlag::DONT_SAVE),
	ConfigSetting("CheckForNewVersion", &g_Config.bCheckForNewVersion, true, CfgFlag::DEFAULT),
	ConfigSetting("Language", &g_Config.sLanguageIni, &DefaultLangRegion, CfgFlag::DEFAULT),
	ConfigSetting("ForceLagSync2", &g_Config.bForceLagSync, false, CfgFlag::PER_GAME),
	ConfigSetting("DiscordRichPresence", &g_Config.bDiscordRichPresence, false, CfgFlag::DEFAULT),
	ConfigSetting("UISound", &g_Config.bUISound, false, CfgFlag::DEFAULT),

	ConfigSetting("DisableHTTPS", &g_Config.bDisableHTTPS, false, CfgFlag::DONT_SAVE),
	ConfigSetting("AutoLoadSaveState", &g_Config.iAutoLoadSaveState, 0, CfgFlag::PER_GAME),
	ConfigSetting("EnableCheats", &g_Config.bEnableCheats, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("EnablePlugins", &g_Config.bEnablePlugins, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("CwCheatRefreshRate", &g_Config.iCwCheatRefreshIntervalMs, 77, CfgFlag::PER_GAME),
	ConfigSetting("CwCheatScrollPosition", &g_Config.fCwCheatScrollPosition, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("GameListScrollPosition", &g_Config.fGameListScrollPosition, 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("DebugOverlay", &g_Config.iDebugOverlay, 0, CfgFlag::DONT_SAVE),
	ConfigSetting("DefaultTab", &g_Config.iDefaultTab, 0, CfgFlag::DEFAULT),

	ConfigSetting("ScreenshotMode", &g_Config.iScreenshotMode, 0, CfgFlag::DEFAULT),
	ConfigSetting("ScreenshotsAsPNG", &g_Config.bScreenshotsAsPNG, false, CfgFlag::PER_GAME),
	ConfigSetting("UseFFV1", &g_Config.bUseFFV1, false, CfgFlag::DEFAULT),
	ConfigSetting("DumpFrames", &g_Config.bDumpFrames, false, CfgFlag::DEFAULT),
	ConfigSetting("DumpVideoOutput", &g_Config.bDumpVideoOutput, false, CfgFlag::DEFAULT),
	ConfigSetting("DumpAudio", &g_Config.bDumpAudio, false, CfgFlag::DEFAULT),
	ConfigSetting("SaveLoadResetsAVdumping", &g_Config.bSaveLoadResetsAVdumping, false, CfgFlag::DEFAULT),
	ConfigSetting("StateSlot", &g_Config.iCurrentStateSlot, 0, CfgFlag::PER_GAME),
	ConfigSetting("EnableStateUndo", &g_Config.bEnableStateUndo, &DefaultEnableStateUndo, CfgFlag::PER_GAME),
	ConfigSetting("StateLoadUndoGame", &g_Config.sStateLoadUndoGame, "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveGame", &g_Config.sStateUndoLastSaveGame, "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveSlot", &g_Config.iStateUndoLastSaveSlot, -5, CfgFlag::DEFAULT), // Start with an "invalid" value
	ConfigSetting("RewindSnapshotInterval", &g_Config.iRewindSnapshotInterval, 0, CfgFlag::PER_GAME),

	ConfigSetting("ShowOnScreenMessage", &g_Config.bShowOnScreenMessages, true, CfgFlag::DEFAULT),
	ConfigSetting("ShowRegionOnGameIcon", &g_Config.bShowRegionOnGameIcon, false, CfgFlag::DEFAULT),
	ConfigSetting("ShowIDOnGameIcon", &g_Config.bShowIDOnGameIcon, false, CfgFlag::DEFAULT),
	ConfigSetting("GameGridScale", &g_Config.fGameGridScale, 1.0, CfgFlag::DEFAULT),
	ConfigSetting("GridView1", &g_Config.bGridView1, true, CfgFlag::DEFAULT),
	ConfigSetting("GridView2", &g_Config.bGridView2, true, CfgFlag::DEFAULT),
	ConfigSetting("GridView3", &g_Config.bGridView3, false, CfgFlag::DEFAULT),
	ConfigSetting("RightAnalogUp", &g_Config.iRightAnalogUp, 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDown", &g_Config.iRightAnalogDown, 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogLeft", &g_Config.iRightAnalogLeft, 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogRight", &g_Config.iRightAnalogRight, 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogPress", &g_Config.iRightAnalogPress, 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogCustom", &g_Config.bRightAnalogCustom, false, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDisableDiagonal", &g_Config.bRightAnalogDisableDiagonal, false, CfgFlag::PER_GAME),
	ConfigSetting("SwipeUp", &g_Config.iSwipeUp, 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeDown", &g_Config.iSwipeDown, 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeLeft", &g_Config.iSwipeLeft, 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeRight", &g_Config.iSwipeRight, 0, CfgFlag::PER_GAME),
	ConfigSetting("SwipeSensitivity", &g_Config.fSwipeSensitivity, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("SwipeSmoothing", &g_Config.fSwipeSmoothing, 0.3f, CfgFlag::PER_GAME),
	ConfigSetting("DoubleTapGesture", &g_Config.iDoubleTapGesture, 0, CfgFlag::PER_GAME),
	ConfigSetting("GestureControlEnabled", &g_Config.bGestureControlEnabled, false, CfgFlag::PER_GAME),
	ConfigSetting("TouchGliding", &g_Config.bTouchGliding, false, CfgFlag::PER_GAME),

	// "default" means let emulator decide, "" means disable.
	ConfigSetting("ReportingHost", &g_Config.sReportHost, "default", CfgFlag::DEFAULT),
	ConfigSetting("AutoSaveSymbolMap", &g_Config.bAutoSaveSymbolMap, false, CfgFlag::PER_GAME),
	ConfigSetting("CacheFullIsoInRam", &g_Config.bCacheFullIsoInRam, false, CfgFlag::PER_GAME),
	ConfigSetting("RemoteISOPort", &g_Config.iRemoteISOPort, 0, CfgFlag::DEFAULT),
	ConfigSetting("LastRemoteISOServer", &g_Config.sLastRemoteISOServer, "", CfgFlag::DEFAULT),
	ConfigSetting("LastRemoteISOPort", &g_Config.iLastRemoteISOPort, 0, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOManualConfig", &g_Config.bRemoteISOManual, false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteShareOnStartup", &g_Config.bRemoteShareOnStartup, false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSubdir", &g_Config.sRemoteISOSubdir, "/", CfgFlag::DEFAULT),
	ConfigSetting("RemoteDebuggerOnStartup", &g_Config.bRemoteDebuggerOnStartup, false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteTab", &g_Config.bRemoteTab, false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSharedDir", &g_Config.sRemoteISOSharedDir, "", CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOShareType", &g_Config.iRemoteISOShareType, (int)RemoteISOShareType::RECENT, CfgFlag::DEFAULT),
	ConfigSetting("AskForExitConfirmationAfterSeconds", &g_Config.iAskForExitConfirmationAfterSeconds, 60, CfgFlag::PER_GAME),

#ifdef __ANDROID__
	ConfigSetting("ScreenRotation", &g_Config.iScreenRotation, ROTATION_AUTO_HORIZONTAL),
#endif
	ConfigSetting("InternalScreenRotation", &g_Config.iInternalScreenRotation, ROTATION_LOCKED_HORIZONTAL, CfgFlag::PER_GAME),

	ConfigSetting("BackgroundAnimation", &g_Config.iBackgroundAnimation, 1, CfgFlag::DEFAULT),
	ConfigSetting("TransparentBackground", &g_Config.bTransparentBackground, true, CfgFlag::DEFAULT),
	ConfigSetting("UITint", &g_Config.fUITint, 0.0, CfgFlag::DEFAULT),
	ConfigSetting("UISaturation", &g_Config.fUISaturation, &DefaultUISaturation, CfgFlag::DEFAULT),

#if defined(USING_WIN_UI)
	ConfigSetting("TopMost", &g_Config.bTopMost, false, CfgFlag::DEFAULT),
	ConfigSetting("PauseOnLostFocus", &g_Config.bPauseOnLostFocus, false, CfgFlag::PER_GAME),
#endif

#if !defined(MOBILE_DEVICE)
	ConfigSetting("WindowX", &g_Config.iWindowX, -1, CfgFlag::DEFAULT), // -1 tells us to center the window.
	ConfigSetting("WindowY", &g_Config.iWindowY, -1, CfgFlag::DEFAULT),
	ConfigSetting("WindowWidth", &g_Config.iWindowWidth, 0, CfgFlag::DEFAULT),   // 0 will be automatically reset later (need to do the AdjustWindowRect dance).
	ConfigSetting("WindowHeight", &g_Config.iWindowHeight, 0, CfgFlag::DEFAULT),
#endif

	ConfigSetting("PauseWhenMinimized", &g_Config.bPauseWhenMinimized, false, CfgFlag::PER_GAME),
	ConfigSetting("PauseExitsEmulator", &g_Config.bPauseExitsEmulator, false, CfgFlag::DONT_SAVE),
	ConfigSetting("PauseMenuExitsEmulator", &g_Config.bPauseMenuExitsEmulator, false, CfgFlag::DONT_SAVE),

	ConfigSetting("DumpDecryptedEboots", &g_Config.bDumpDecryptedEboot, false, CfgFlag::PER_GAME),
	ConfigSetting("FullscreenOnDoubleclick", &g_Config.bFullscreenOnDoubleclick, true, CfgFlag::DONT_SAVE),
	ConfigSetting("ShowMenuBar", &g_Config.bShowMenuBar, true, CfgFlag::DEFAULT),

	ConfigSetting("MemStickInserted", &g_Config.bMemStickInserted, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("LoadPlugins", &g_Config.bLoadPlugins, true, CfgFlag::PER_GAME),
	ConfigSetting("NotificationPos", &g_Config.iNotificationPos, (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::DEFAULT),

	ConfigSetting("IgnoreCompatSettings", &g_Config.sIgnoreCompatSettings, "", CfgFlag::PER_GAME | CfgFlag::REPORT),

	ConfigSetting("RunBehindPauseMenu", &g_Config.bRunBehindPauseMenu, false, CfgFlag::DEFAULT),

	ConfigSetting("ShowGPOLEDs", &g_Config.bShowGPOLEDs, false, CfgFlag::PER_GAME),

	ConfigSetting("UIScaleFactor", &g_Config.iUIScaleFactor, false, CfgFlag::DEFAULT),
};

static bool DefaultSasThread() {
	return cpu_info.num_cores > 1;
}

static const ConfigSetting achievementSettings[] = {
	// Core settings
	ConfigSetting("AchievementsEnable", &g_Config.bAchievementsEnable, true, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsEnableRAIntegration", &g_Config.bAchievementsEnableRAIntegration, false, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsChallengeMode", &g_Config.bAchievementsHardcoreMode, true, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsEncoreMode", &g_Config.bAchievementsEncoreMode, false, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnofficial", &g_Config.bAchievementsUnofficial, false, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLogBadMemReads", &g_Config.bAchievementsLogBadMemReads, false, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsSaveStateInHardcoreMode", &g_Config.bAchievementsSaveStateInHardcoreMode, false, CfgFlag::DEFAULT),

	// Achievements login info. Note that password is NOT stored, only a login token.
	// And that login token is stored separately from the ini, see NativeSaveSecret, but it can also be loaded
	// from the ini if manually entered (useful when testing various builds on Android).
	ConfigSetting("AchievementsToken", &g_Config.sAchievementsToken, "", CfgFlag::DONT_SAVE),
	ConfigSetting("AchievementsUserName", &g_Config.sAchievementsUserName, "", CfgFlag::DEFAULT),

	// Customizations
	ConfigSetting("AchievementsSoundEffects", &g_Config.bAchievementsSoundEffects, true, CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnlockAudioFile", &g_Config.sAchievementsUnlockAudioFile, "", CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardSubmitAudioFile", &g_Config.sAchievementsLeaderboardSubmitAudioFile, "", CfgFlag::DEFAULT),

	ConfigSetting("AchievementsLeaderboardTrackerPos", &g_Config.iAchievementsLeaderboardTrackerPos, (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardStartedOrFailedPos", &g_Config.iAchievementsLeaderboardStartedOrFailedPos, (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardSubmittedPos", &g_Config.iAchievementsLeaderboardSubmittedPos, (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsProgressPos", &g_Config.iAchievementsProgressPos, (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsChallengePos", &g_Config.iAchievementsChallengePos, (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnlockedPos", &g_Config.iAchievementsUnlockedPos, (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
};

static const ConfigSetting cpuSettings[] = {
	ConfigSetting("CPUCore", &g_Config.iCpuCore, &DefaultCpuCore, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SeparateSASThread", &g_Config.bSeparateSASThread, &DefaultSasThread, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("IOTimingMethod", &g_Config.iIOTimingMethod, IOTIMING_FAST, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FastMemoryAccess", &g_Config.bFastMemory, true, CfgFlag::PER_GAME),
	ConfigSetting("FunctionReplacements", &g_Config.bFuncReplacements, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("HideSlowWarnings", &g_Config.bHideSlowWarnings, false, CfgFlag::DEFAULT),
	ConfigSetting("HideStateWarnings", &g_Config.bHideStateWarnings, false, CfgFlag::DEFAULT),
	ConfigSetting("PreloadFunctions", &g_Config.bPreloadFunctions, false, CfgFlag::PER_GAME),
	ConfigSetting("JitDisableFlags", &g_Config.uJitDisableFlags, (uint32_t)0, CfgFlag::PER_GAME),
	ConfigSetting("CPUSpeed", &g_Config.iLockedCPUSpeed, 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
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

#if PPSSPP_PLATFORM(WINDOWS)
	// If no Vulkan, use Direct3D 11 on Windows 8+ (most importantly 10.)
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
#if PPSSPP_API(D3D9)
		if (!failed.count(GPUBackend::DIRECT3D9)) {
			return (int)GPUBackend::DIRECT3D9;
		}
#endif

		// They've all failed.  Let them try the default - or on Android, OpenGL.
		sFailedGPUBackends += ",ALL";
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
	if (backend == GPUBackend::DIRECT3D11 || backend == GPUBackend::DIRECT3D9)
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

static int FastForwardModeFromString(const std::string &s) {
	if (!strcasecmp(s.c_str(), "CONTINUOUS"))
		return (int)FastForwardMode::CONTINUOUS;
	if (!strcasecmp(s.c_str(), "SKIP_FLIP"))
		return (int)FastForwardMode::SKIP_FLIP;
	return DefaultFastForwardMode();
}

static std::string FastForwardModeToString(int v) {
	switch (FastForwardMode(v)) {
	case FastForwardMode::CONTINUOUS:
		return "CONTINUOUS";
	case FastForwardMode::SKIP_FLIP:
		return "SKIP_FLIP";
	}
	return "CONTINUOUS";
}

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

static const ConfigSetting graphicsSettings[] = {
	ConfigSetting("EnableCardboardVR", &g_Config.bEnableCardboardVR, false, CfgFlag::PER_GAME),
	ConfigSetting("CardboardScreenSize", &g_Config.iCardboardScreenSize, 50, CfgFlag::PER_GAME),
	ConfigSetting("CardboardXShift", &g_Config.iCardboardXShift, 0, CfgFlag::PER_GAME),
	ConfigSetting("CardboardYShift", &g_Config.iCardboardYShift, 0, CfgFlag::PER_GAME),
	ConfigSetting("iShowStatusFlags", &g_Config.iShowStatusFlags, 0, CfgFlag::PER_GAME),
	ConfigSetting("GraphicsBackend", &g_Config.iGPUBackend, &DefaultGPUBackend, &GPUBackendTranslator::To, &GPUBackendTranslator::From, CfgFlag::DEFAULT | CfgFlag::REPORT),
#if PPSSPP_PLATFORM(ANDROID) && PPSSPP_ARCH(ARM64)
	ConfigSetting("CustomDriver", &g_Config.sCustomDriver, "", CfgFlag::DEFAULT),
#endif
	ConfigSetting("DisabledGraphicsBackends", &g_Config.sDisabledGPUBackends, "", CfgFlag::DEFAULT),
	ConfigSetting("VulkanDevice", &g_Config.sVulkanDevice, "", CfgFlag::DEFAULT),
#ifdef _WIN32
	ConfigSetting("D3D11Device", &g_Config.sD3D11Device, "", CfgFlag::DEFAULT),
#endif
	ConfigSetting("CameraDevice", &g_Config.sCameraDevice, "", CfgFlag::DEFAULT),
	ConfigSetting("CameraMirrorHorizontal", &g_Config.bCameraMirrorHorizontal, false, CfgFlag::DEFAULT),
	ConfigSetting("AndroidFramerateMode", &g_Config.iDisplayFramerateMode, 1, CfgFlag::DEFAULT),
	ConfigSetting("VendorBugChecksEnabled", &g_Config.bVendorBugChecksEnabled, true, CfgFlag::DONT_SAVE),
	ConfigSetting("UseGeometryShader", &g_Config.bUseGeometryShader, false, CfgFlag::PER_GAME),
	ConfigSetting("SkipBufferEffects", &g_Config.bSkipBufferEffects, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("DisableRangeCulling", &g_Config.bDisableRangeCulling, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("DepthRasterMode", &g_Config.iDepthRasterMode, &DefaultDepthRaster, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SoftwareRenderer", &g_Config.bSoftwareRendering, false, CfgFlag::PER_GAME),
	ConfigSetting("SoftwareRendererJit", &g_Config.bSoftwareRenderingJit, true, CfgFlag::PER_GAME),
	ConfigSetting("HardwareTransform", &g_Config.bHardwareTransform, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SoftwareSkinning", &g_Config.bSoftwareSkinning, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TextureFiltering", &g_Config.iTexFiltering, 1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("Smart2DTexFiltering", &g_Config.bSmart2DTexFiltering, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("InternalResolution", &g_Config.iInternalResolution, &DefaultInternalResolution, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("AndroidHwScale", &g_Config.iAndroidHwScale, &DefaultAndroidHwScale, CfgFlag::DEFAULT),
	ConfigSetting("HighQualityDepth", &g_Config.bHighQualityDepth, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FrameSkip", &g_Config.iFrameSkip, 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FrameSkipType", &g_Config.iFrameSkipType, 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("AutoFrameSkip", &g_Config.bAutoFrameSkip, IsVREnabled(), CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("StereoRendering", &g_Config.bStereoRendering, false, CfgFlag::PER_GAME),
	ConfigSetting("StereoToMonoShader", &g_Config.sStereoToMonoShader, "RedBlue", CfgFlag::PER_GAME),
	ConfigSetting("FrameRate", &g_Config.iFpsLimit1, 0, CfgFlag::PER_GAME),
	ConfigSetting("FrameRate2", &g_Config.iFpsLimit2, -1, CfgFlag::PER_GAME),
	ConfigSetting("AnalogFrameRate", &g_Config.iAnalogFpsLimit, 240, CfgFlag::PER_GAME),
	ConfigSetting("UnthrottlingMode", &g_Config.iFastForwardMode, &DefaultFastForwardMode, &FastForwardModeToString, &FastForwardModeFromString, CfgFlag::PER_GAME),
#if defined(USING_WIN_UI)
	ConfigSetting("RestartRequired", &g_Config.bRestartRequired, false, CfgFlag::DONT_SAVE),
#endif

	// Most low-performance (and many high performance) mobile GPUs do not support aniso anyway so defaulting to 4 is fine.
	ConfigSetting("AnisotropyLevel", &g_Config.iAnisotropyLevel, 4, CfgFlag::PER_GAME),
	ConfigSetting("MultiSampleLevel", &g_Config.iMultiSampleLevel, 0, CfgFlag::PER_GAME),  // Number of samples is 1 << iMultiSampleLevel

	ConfigSetting("TextureBackoffCache", &g_Config.bTextureBackoffCache, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VertexDecJit", &g_Config.bVertexDecoderJit, &DefaultCodeGen, CfgFlag::DONT_SAVE | CfgFlag::REPORT),

#ifndef MOBILE_DEVICE
	ConfigSetting("FullScreen", &g_Config.bFullScreen, false, CfgFlag::DEFAULT),
	ConfigSetting("FullScreenMulti", &g_Config.bFullScreenMulti, false, CfgFlag::DEFAULT),
#endif

#if PPSSPP_PLATFORM(IOS)
	ConfigSetting("AppSwitchMode", &g_Config.iAppSwitchMode, (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR, CfgFlag::DEFAULT),
#endif

	ConfigSetting("BufferFiltering", &g_Config.iDisplayFilter, SCALE_LINEAR, CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetX", &g_Config.fDisplayOffsetX, 0.5f, CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetY", &g_Config.fDisplayOffsetY, 0.5f, CfgFlag::PER_GAME),
	ConfigSetting("DisplayScale", &g_Config.fDisplayScale, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("DisplayIntegerScale", &g_Config.bDisplayIntegerScale, false, CfgFlag::PER_GAME),
	ConfigSetting("DisplayAspectRatio", &g_Config.fDisplayAspectRatio, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("DisplayStretch", &g_Config.bDisplayStretch, false, CfgFlag::PER_GAME),
	ConfigSetting("DisplayCropTo16x9", &g_Config.bDisplayCropTo16x9, true, CfgFlag::PER_GAME),

	ConfigSetting("ImmersiveMode", &g_Config.bImmersiveMode, true, CfgFlag::PER_GAME),
	ConfigSetting("SustainedPerformanceMode", &g_Config.bSustainedPerformanceMode, false, CfgFlag::PER_GAME),
	ConfigSetting("IgnoreScreenInsets", &g_Config.bIgnoreScreenInsets, true, CfgFlag::DEFAULT),

	ConfigSetting("ReplaceTextures", &g_Config.bReplaceTextures, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("SaveNewTextures", &g_Config.bSaveNewTextures, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("IgnoreTextureFilenames", &g_Config.bIgnoreTextureFilenames, false, CfgFlag::PER_GAME),

	ConfigSetting("TexScalingLevel", &g_Config.iTexScalingLevel, 1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexScalingType", &g_Config.iTexScalingType, 0, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexDeposterize", &g_Config.bTexDeposterize, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TexHardwareScaling", &g_Config.bTexHardwareScaling, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VSync", &g_Config.bVSync, &DefaultVSync, CfgFlag::PER_GAME),
	ConfigSetting("BloomHack", &g_Config.iBloomHack, 0, CfgFlag::PER_GAME | CfgFlag::REPORT),

	// Not really a graphics setting...
	ConfigSetting("SplineBezierQuality", &g_Config.iSplineBezierQuality, 2, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("HardwareTessellation", &g_Config.bHardwareTessellation, false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("TextureShader", &g_Config.sTextureShaderName, "Off", CfgFlag::PER_GAME),
	ConfigSetting("ShaderChainRequires60FPS", &g_Config.bShaderChainRequires60FPS, false, CfgFlag::PER_GAME),

	ConfigSetting("SkipGPUReadbackMode", &g_Config.iSkipGPUReadbackMode, false, CfgFlag::PER_GAME | CfgFlag::REPORT),

	ConfigSetting("GfxDebugOutput", &g_Config.bGfxDebugOutput, false, CfgFlag::DONT_SAVE),
	ConfigSetting("LogFrameDrops", &g_Config.bLogFrameDrops, false, CfgFlag::DEFAULT),

	ConfigSetting("InflightFrames", &g_Config.iInflightFrames, 3, CfgFlag::DEFAULT),
	ConfigSetting("RenderDuplicateFrames", &g_Config.bRenderDuplicateFrames, false, CfgFlag::PER_GAME),

	ConfigSetting("MultiThreading", &g_Config.bRenderMultiThreading, true, CfgFlag::DEFAULT),

	ConfigSetting("ShaderCache", &g_Config.bShaderCache, true, CfgFlag::DONT_SAVE),  // Doesn't save. Ini-only.
	ConfigSetting("GpuLogProfiler", &g_Config.bGpuLogProfiler, false, CfgFlag::DEFAULT),

	ConfigSetting("UberShaderVertex", &g_Config.bUberShaderVertex, true, CfgFlag::DEFAULT),
	ConfigSetting("UberShaderFragment", &g_Config.bUberShaderFragment, true, CfgFlag::DEFAULT),

	ConfigSetting("DisplayRefreshRate", &g_Config.iDisplayRefreshRate, g_Config.iDisplayRefreshRate, CfgFlag::PER_GAME),
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

static const ConfigSetting soundSettings[] = {
	ConfigSetting("Enable", &g_Config.bEnableSound, true, CfgFlag::PER_GAME),
	ConfigSetting("AudioBackend", &g_Config.iAudioBackend, 0, CfgFlag::PER_GAME),
	ConfigSetting("ExtraAudioBuffering", &g_Config.bExtraAudioBuffering, false, CfgFlag::DEFAULT),

	// Legacy volume settings, these get auto upgraded through default handlers on the new settings. NOTE: Must be before the new ones in the order here.
	// The default settings here are still relevant, they will get propagated into the new ones.
	ConfigSetting("GlobalVolume", &g_Config.iLegacyGameVolume, VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("ReverbVolume", &g_Config.iLegacyReverbVolume, VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("AchievementSoundVolume", &g_Config.iLegacyAchievementVolume, 6, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),

	// Current volume settings.
	ConfigSetting("GameVolume", &g_Config.iGameVolume, &DefaultGameVolume, CfgFlag::PER_GAME),
	ConfigSetting("ReverbRelativeVolume", &g_Config.iReverbVolume, &DefaultReverbVolume, CfgFlag::PER_GAME),
	ConfigSetting("AltSpeedRelativeVolume", &g_Config.iAltSpeedVolume, VOLUMEHI_FULL, CfgFlag::PER_GAME),
	ConfigSetting("AchievementVolume", &g_Config.iAchievementVolume, &DefaultAchievementVolume, CfgFlag::PER_GAME),
	ConfigSetting("UIVolume", &g_Config.iUIVolume, 75, CfgFlag::DEFAULT),

	ConfigSetting("AudioDevice", &g_Config.sAudioDevice, "", CfgFlag::DEFAULT),
	ConfigSetting("AutoAudioDevice", &g_Config.bAutoAudioDevice, true, CfgFlag::DEFAULT),
	ConfigSetting("AudioMixWithOthers", &g_Config.bAudioMixWithOthers, true, CfgFlag::DEFAULT),
	ConfigSetting("AudioRespectSilentMode", &g_Config.bAudioRespectSilentMode, false, CfgFlag::DEFAULT),
	ConfigSetting("UseExperimentalAtrac", &g_Config.bUseExperimentalAtrac, false, CfgFlag::DEFAULT),
};

static bool DefaultShowTouchControls() {
	switch (System_GetPropertyInt(SYSPROP_DEVICE_TYPE)) {
	case DEVICE_TYPE_MOBILE:
		return !KeyMap::HasBuiltinController(System_GetProperty(SYSPROP_NAME));
	default:
		return false;
	}
}

static bool DefaultShowPauseButton() {
	switch (System_GetPropertyInt(SYSPROP_DEVICE_TYPE)) {
	case DEVICE_TYPE_MOBILE:
	case DEVICE_TYPE_DESKTOP:
		return true;
	case DEVICE_TYPE_VR:
	case DEVICE_TYPE_TV:
		return false;
	default:
		return false;
	}
}

static const float defaultControlScale = 1.15f;
static const ConfigTouchPos defaultTouchPosShow = { -1.0f, -1.0f, defaultControlScale, true };
static const ConfigTouchPos defaultTouchPosHide = { -1.0f, -1.0f, defaultControlScale, false };

static const ConfigSetting controlSettings[] = {
	ConfigSetting("HapticFeedback", &g_Config.bHapticFeedback, false, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchCross", &g_Config.bShowTouchCross, true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchCircle", &g_Config.bShowTouchCircle, true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchSquare", &g_Config.bShowTouchSquare, true, CfgFlag::PER_GAME),
	ConfigSetting("ShowTouchTriangle", &g_Config.bShowTouchTriangle, true, CfgFlag::PER_GAME),

	ConfigSetting("Custom0Mapping", "Custom0Image", "Custom0Shape", "Custom0Toggle", "Custom0Repeat", &g_Config.CustomButton[0], {0, 0, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom1Mapping", "Custom1Image", "Custom1Shape", "Custom1Toggle", "Custom1Repeat", &g_Config.CustomButton[1], {0, 1, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom2Mapping", "Custom2Image", "Custom2Shape", "Custom2Toggle", "Custom2Repeat", &g_Config.CustomButton[2], {0, 2, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom3Mapping", "Custom3Image", "Custom3Shape", "Custom3Toggle", "Custom3Repeat", &g_Config.CustomButton[3], {0, 3, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom4Mapping", "Custom4Image", "Custom4Shape", "Custom4Toggle", "Custom4Repeat", &g_Config.CustomButton[4], {0, 4, 0, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom5Mapping", "Custom5Image", "Custom5Shape", "Custom5Toggle", "Custom5Repeat", &g_Config.CustomButton[5], {0, 0, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom6Mapping", "Custom6Image", "Custom6Shape", "Custom6Toggle", "Custom6Repeat", &g_Config.CustomButton[6], {0, 1, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom7Mapping", "Custom7Image", "Custom7Shape", "Custom7Toggle", "Custom7Repeat", &g_Config.CustomButton[7], {0, 2, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom8Mapping", "Custom8Image", "Custom8Shape", "Custom8Toggle", "Custom8Repeat", &g_Config.CustomButton[8], {0, 3, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom9Mapping", "Custom9Image", "Custom9Shape", "Custom9Toggle", "Custom9Repeat", &g_Config.CustomButton[9], {0, 4, 1, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom10Mapping", "Custom10Image", "Custom10Shape", "Custom10Toggle", "Custom10Repeat", &g_Config.CustomButton[10], {0, 0, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom11Mapping", "Custom11Image", "Custom11Shape", "Custom11Toggle", "Custom11Repeat", &g_Config.CustomButton[11], {0, 1, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom12Mapping", "Custom12Image", "Custom12Shape", "Custom12Toggle", "Custom12Repeat", &g_Config.CustomButton[12], {0, 2, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom13Mapping", "Custom13Image", "Custom13Shape", "Custom13Toggle", "Custom13Repeat", &g_Config.CustomButton[13], {0, 3, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom14Mapping", "Custom14Image", "Custom14Shape", "Custom14Toggle", "Custom14Repeat", &g_Config.CustomButton[14], {0, 4, 2, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom15Mapping", "Custom15Image", "Custom15Shape", "Custom15Toggle", "Custom15Repeat", &g_Config.CustomButton[15], {0, 0, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom16Mapping", "Custom16Image", "Custom16Shape", "Custom16Toggle", "Custom16Repeat", &g_Config.CustomButton[16], {0, 1, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom17Mapping", "Custom17Image", "Custom17Shape", "Custom17Toggle", "Custom17Repeat", &g_Config.CustomButton[17], {0, 2, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom18Mapping", "Custom18Image", "Custom18Shape", "Custom18Toggle", "Custom18Repeat", &g_Config.CustomButton[18], {0, 3, 9, false, false}, CfgFlag::PER_GAME),
	ConfigSetting("Custom19Mapping", "Custom19Image", "Custom19Shape", "Custom19Toggle", "Custom19Repeat", &g_Config.CustomButton[19], {0, 4, 9, false, false}, CfgFlag::PER_GAME),
	// Combo keys are something else, but I don't want to break the config backwards compatibility so these will stay wrongly named.
	ConfigSetting("fcombo0X", "fcombo0Y", "comboKeyScale0", "ShowComboKey0", &g_Config.touchCustom[0], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo1X", "fcombo1Y", "comboKeyScale1", "ShowComboKey1", &g_Config.touchCustom[1], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo2X", "fcombo2Y", "comboKeyScale2", "ShowComboKey2", &g_Config.touchCustom[2], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo3X", "fcombo3Y", "comboKeyScale3", "ShowComboKey3", &g_Config.touchCustom[3], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo4X", "fcombo4Y", "comboKeyScale4", "ShowComboKey4", &g_Config.touchCustom[4], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo5X", "fcombo5Y", "comboKeyScale5", "ShowComboKey5", &g_Config.touchCustom[5], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo6X", "fcombo6Y", "comboKeyScale6", "ShowComboKey6", &g_Config.touchCustom[6], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo7X", "fcombo7Y", "comboKeyScale7", "ShowComboKey7", &g_Config.touchCustom[7], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo8X", "fcombo8Y", "comboKeyScale8", "ShowComboKey8", &g_Config.touchCustom[8], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo9X", "fcombo9Y", "comboKeyScale9", "ShowComboKey9", &g_Config.touchCustom[9], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo10X", "fcombo10Y", "comboKeyScale10", "ShowComboKey10", &g_Config.touchCustom[10], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo11X", "fcombo11Y", "comboKeyScale11", "ShowComboKey11", &g_Config.touchCustom[11], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo12X", "fcombo12Y", "comboKeyScale12", "ShowComboKey12", &g_Config.touchCustom[12], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo13X", "fcombo13Y", "comboKeyScale13", "ShowComboKey13", &g_Config.touchCustom[13], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo14X", "fcombo14Y", "comboKeyScale14", "ShowComboKey14", &g_Config.touchCustom[14], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo15X", "fcombo15Y", "comboKeyScale15", "ShowComboKey15", &g_Config.touchCustom[15], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo16X", "fcombo16Y", "comboKeyScale16", "ShowComboKey16", &g_Config.touchCustom[16], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo17X", "fcombo17Y", "comboKeyScale17", "ShowComboKey17", &g_Config.touchCustom[17], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo18X", "fcombo18Y", "comboKeyScale18", "ShowComboKey18", &g_Config.touchCustom[18], defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("fcombo19X", "fcombo19Y", "comboKeyScale19", "ShowComboKey19", &g_Config.touchCustom[19], defaultTouchPosHide, CfgFlag::PER_GAME),

	// A win32 user seeing touch controls is likely using PPSSPP on a tablet. There it makes
	// sense to default this to on.
	ConfigSetting("ShowTouchPause", &g_Config.bShowTouchPause, &DefaultShowPauseButton, CfgFlag::DEFAULT),
#if defined(USING_WIN_UI)
	ConfigSetting("IgnoreWindowsKey", &g_Config.bIgnoreWindowsKey, false, CfgFlag::PER_GAME),
#endif

	ConfigSetting("ShowTouchControls", &g_Config.bShowTouchControls, &DefaultShowTouchControls, CfgFlag::PER_GAME),

	// ConfigSetting("KeyMapping", &g_Config.iMappingMap, 0),

#ifdef MOBILE_DEVICE
	ConfigSetting("TiltBaseAngleY", &g_Config.fTiltBaseAngleY, 0.9f, CfgFlag::PER_GAME),
	ConfigSetting("TiltInvertX", &g_Config.bInvertTiltX, false, CfgFlag::PER_GAME),
	ConfigSetting("TiltInvertY", &g_Config.bInvertTiltY, false, CfgFlag::PER_GAME),
	ConfigSetting("TiltSensitivityX", &g_Config.iTiltSensitivityX, 60, CfgFlag::PER_GAME),
	ConfigSetting("TiltSensitivityY", &g_Config.iTiltSensitivityY, 60, CfgFlag::PER_GAME),
	ConfigSetting("TiltAnalogDeadzoneRadius", &g_Config.fTiltAnalogDeadzoneRadius, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("TiltInverseDeadzone", &g_Config.fTiltInverseDeadzone, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("TiltCircularDeadzone", &g_Config.bTiltCircularDeadzone, true, CfgFlag::PER_GAME),
	ConfigSetting("TiltInputType", &g_Config.iTiltInputType, 0, CfgFlag::PER_GAME),
#endif

	ConfigSetting("DisableDpadDiagonals", &g_Config.bDisableDpadDiagonals, false, CfgFlag::PER_GAME),
	ConfigSetting("GamepadOnlyFocused", &g_Config.bGamepadOnlyFocused, false, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonStyle", &g_Config.iTouchButtonStyle, 1, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonOpacity", &g_Config.iTouchButtonOpacity, 65, CfgFlag::PER_GAME),
	ConfigSetting("TouchButtonHideSeconds", &g_Config.iTouchButtonHideSeconds, 20, CfgFlag::PER_GAME),
	ConfigSetting("AutoCenterTouchAnalog", &g_Config.bAutoCenterTouchAnalog, false, CfgFlag::PER_GAME),
	ConfigSetting("StickyTouchDPad", &g_Config.bStickyTouchDPad, false, CfgFlag::PER_GAME),

	// Snap touch control position
	ConfigSetting("TouchSnapToGrid", &g_Config.bTouchSnapToGrid, false, CfgFlag::PER_GAME),
	ConfigSetting("TouchSnapGridSize", &g_Config.iTouchSnapGridSize, 64, CfgFlag::PER_GAME),

	// -1.0f means uninitialized, set in GamepadEmu::CreatePadLayout().
	ConfigSetting("ActionButtonSpacing2", &g_Config.fActionButtonSpacing, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("ActionButtonCenterX", "ActionButtonCenterY", "ActionButtonScale", nullptr, &g_Config.touchActionButtonCenter, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("DPadX", "DPadY", "DPadScale", "ShowTouchDpad", &g_Config.touchDpad, defaultTouchPosShow, CfgFlag::PER_GAME),

	// Note: these will be overwritten if DPadRadius is set.
	ConfigSetting("DPadSpacing", &g_Config.fDpadSpacing, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("StartKeyX", "StartKeyY", "StartKeyScale", "ShowTouchStart", &g_Config.touchStartKey, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("SelectKeyX", "SelectKeyY", "SelectKeyScale", "ShowTouchSelect", &g_Config.touchSelectKey, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("UnthrottleKeyX", "UnthrottleKeyY", "UnthrottleKeyScale", "ShowTouchUnthrottle", &g_Config.touchFastForwardKey, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("LKeyX", "LKeyY", "LKeyScale", "ShowTouchLTrigger", &g_Config.touchLKey, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RKeyX", "RKeyY", "RKeyScale", "ShowTouchRTrigger", &g_Config.touchRKey, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("AnalogStickX", "AnalogStickY", "AnalogStickScale", "ShowAnalogStick", &g_Config.touchAnalogStick, defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogStickX", "RightAnalogStickY", "RightAnalogStickScale", "ShowRightAnalogStick", &g_Config.touchRightAnalogStick, defaultTouchPosHide, CfgFlag::PER_GAME),

	ConfigSetting("AnalogDeadzone", &g_Config.fAnalogDeadzone, 0.15f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogInverseDeadzone", &g_Config.fAnalogInverseDeadzone, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogSensitivity", &g_Config.fAnalogSensitivity, 1.1f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogIsCircular", &g_Config.bAnalogIsCircular, false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogAutoRotSpeed", &g_Config.fAnalogAutoRotSpeed, 8.0f, CfgFlag::PER_GAME),

	ConfigSetting("AnalogLimiterDeadzone", &g_Config.fAnalogLimiterDeadzone, 0.6f, CfgFlag::DEFAULT),
	ConfigSetting("AnalogTriggerThreshold", &g_Config.fAnalogTriggerThreshold, 0.75f, CfgFlag::DEFAULT),

	ConfigSetting("AllowMappingCombos", &g_Config.bAllowMappingCombos, false, CfgFlag::DEFAULT),
	ConfigSetting("StrictComboOrder", &g_Config.bStrictComboOrder, false, CfgFlag::DEFAULT),

	ConfigSetting("LeftStickHeadScale", &g_Config.fLeftStickHeadScale, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("RightStickHeadScale", &g_Config.fRightStickHeadScale, 1.0f, CfgFlag::PER_GAME),
	ConfigSetting("HideStickBackground", &g_Config.bHideStickBackground, false, CfgFlag::PER_GAME),

	ConfigSetting("UseMouse", &g_Config.bMouseControl, false, CfgFlag::PER_GAME),
	ConfigSetting("MapMouse", &g_Config.bMapMouse, false, CfgFlag::PER_GAME),
	ConfigSetting("ConfineMap", &g_Config.bMouseConfine, false, CfgFlag::PER_GAME),
	ConfigSetting("MouseSensitivity", &g_Config.fMouseSensitivity, 0.1f, CfgFlag::PER_GAME),
	ConfigSetting("MouseSmoothing", &g_Config.fMouseSmoothing, 0.9f, CfgFlag::PER_GAME),
	ConfigSetting("MouseWheelUpDelayMs", &g_Config.iMouseWheelUpDelayMs, 80, CfgFlag::PER_GAME),

	ConfigSetting("SystemControls", &g_Config.bSystemControls, true, CfgFlag::DEFAULT),
	ConfigSetting("RapidFileInterval", &g_Config.iRapidFireInterval, 5, CfgFlag::DEFAULT),

	ConfigSetting("AnalogGesture", &g_Config.bAnalogGesture, false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogGestureSensibility", &g_Config.fAnalogGestureSensibility, 1.0f, CfgFlag::PER_GAME),
};

static const ConfigSetting networkSettings[] = {
	ConfigSetting("EnableWlan", &g_Config.bEnableWlan, false, CfgFlag::PER_GAME),
	ConfigSetting("EnableAdhocServer", &g_Config.bEnableAdhocServer, false, CfgFlag::PER_GAME),
	ConfigSetting("proAdhocServer", &g_Config.proAdhocServer, "socom.cc", CfgFlag::PER_GAME),
	ConfigSetting("PortOffset", &g_Config.iPortOffset, 10000, CfgFlag::PER_GAME),
	ConfigSetting("PrimaryDNSServer", &g_Config.sInfrastructureDNSServer, "67.222.156.250", CfgFlag::PER_GAME),
	ConfigSetting("MinTimeout", &g_Config.iMinTimeout, 0, CfgFlag::PER_GAME),
	ConfigSetting("ForcedFirstConnect", &g_Config.bForcedFirstConnect, false, CfgFlag::PER_GAME),
	ConfigSetting("EnableUPnP", &g_Config.bEnableUPnP, false, CfgFlag::PER_GAME),
	ConfigSetting("UPnPUseOriginalPort", &g_Config.bUPnPUseOriginalPort, false, CfgFlag::PER_GAME),
	ConfigSetting("InfrastructureUsername", &g_Config.sInfrastructureUsername, &DefaultInfrastructureUsername, CfgFlag::PER_GAME),
	ConfigSetting("InfrastructureAutoDNS", &g_Config.bInfrastructureAutoDNS, true, CfgFlag::PER_GAME),
	ConfigSetting("AllowSavestateWhileConnected", &g_Config.bAllowSavestateWhileConnected, false, CfgFlag::DONT_SAVE),
	ConfigSetting("DontDownloadInfraJson", &g_Config.bDontDownloadInfraJson, false, CfgFlag::DONT_SAVE),

	ConfigSetting("EnableNetworkChat", &g_Config.bEnableNetworkChat, false, CfgFlag::PER_GAME),
	ConfigSetting("ChatButtonPosition", &g_Config.iChatButtonPosition, (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME),
	ConfigSetting("ChatScreenPosition", &g_Config.iChatScreenPosition, (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME),
	ConfigSetting("EnableQuickChat", &g_Config.bEnableQuickChat, true, CfgFlag::PER_GAME),
	ConfigSetting("QuickChat1", &g_Config.sQuickChat0, "Quick Chat 1", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat2", &g_Config.sQuickChat1, "Quick Chat 2", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat3", &g_Config.sQuickChat2, "Quick Chat 3", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat4", &g_Config.sQuickChat3, "Quick Chat 4", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat5", &g_Config.sQuickChat4, "Quick Chat 5", CfgFlag::PER_GAME),
};

static const ConfigSetting systemParamSettings[] = {
	ConfigSetting("PSPModel", &g_Config.iPSPModel, PSP_MODEL_SLIM, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("PSPFirmwareVersion", &g_Config.iFirmwareVersion, PSP_DEFAULT_FIRMWARE, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("NickName", &g_Config.sNickName, "PPSSPP", CfgFlag::PER_GAME),
	ConfigSetting("MacAddress", &g_Config.sMACAddress, "", CfgFlag::PER_GAME),
	ConfigSetting("GameLanguage", &g_Config.iLanguage, -1, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("ParamTimeFormat", &g_Config.iTimeFormat, PSP_SYSTEMPARAM_TIME_FORMAT_24HR, CfgFlag::PER_GAME),
	ConfigSetting("ParamDateFormat", &g_Config.iDateFormat, PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD, CfgFlag::PER_GAME),
	ConfigSetting("TimeZone", &g_Config.iTimeZone, 0, CfgFlag::PER_GAME),
	ConfigSetting("DayLightSavings", &g_Config.bDayLightSavings, (bool) PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD, CfgFlag::PER_GAME),
	ConfigSetting("ButtonPreference", &g_Config.iButtonPreference, PSP_SYSTEMPARAM_BUTTON_CROSS, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("LockParentalLevel", &g_Config.iLockParentalLevel, 0, CfgFlag::PER_GAME),
	ConfigSetting("WlanAdhocChannel", &g_Config.iWlanAdhocChannel, PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC, CfgFlag::PER_GAME),
#if defined(USING_WIN_UI) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH)
	ConfigSetting("BypassOSKWithKeyboard", &g_Config.bBypassOSKWithKeyboard, false, CfgFlag::PER_GAME),
#endif
	ConfigSetting("WlanPowerSave", &g_Config.bWlanPowerSave, (bool) PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF, CfgFlag::PER_GAME),
	ConfigSetting("EncryptSave", &g_Config.bEncryptSave, true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("MemStickSize", &g_Config.iMemStickSizeGB, 16, CfgFlag::DEFAULT),
};

static const ConfigSetting debuggerSettings[] = {
	ConfigSetting("DisasmWindowX", &g_Config.iDisasmWindowX, -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowY", &g_Config.iDisasmWindowY, -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowW", &g_Config.iDisasmWindowW, -1, CfgFlag::DEFAULT),
	ConfigSetting("DisasmWindowH", &g_Config.iDisasmWindowH, -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowX", &g_Config.iGEWindowX, -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowY", &g_Config.iGEWindowY, -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowW", &g_Config.iGEWindowW, -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowH", &g_Config.iGEWindowH, -1, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsBL", &g_Config.uGETabsLeft, (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsBR", &g_Config.uGETabsRight, (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("GEWindowTabsTR", &g_Config.uGETabsTopRight, (uint32_t)0, CfgFlag::DEFAULT),
	ConfigSetting("ConsoleWindowX", &g_Config.iConsoleWindowX, -1, CfgFlag::DEFAULT),
	ConfigSetting("ConsoleWindowY", &g_Config.iConsoleWindowY, -1, CfgFlag::DEFAULT),
	ConfigSetting("FontWidth", &g_Config.iFontWidth, 8, CfgFlag::DEFAULT),
	ConfigSetting("FontHeight", &g_Config.iFontHeight, 12, CfgFlag::DEFAULT),
	ConfigSetting("DisplayStatusBar", &g_Config.bDisplayStatusBar, true, CfgFlag::DEFAULT),
	ConfigSetting("ShowBottomTabTitles",&g_Config.bShowBottomTabTitles, true, CfgFlag::DEFAULT),
	ConfigSetting("ShowDeveloperMenu", &g_Config.bShowDeveloperMenu, false, CfgFlag::DEFAULT),
	ConfigSetting("SkipDeadbeefFilling", &g_Config.bSkipDeadbeefFilling, false, CfgFlag::DEFAULT),
	ConfigSetting("FuncHashMap", &g_Config.bFuncHashMap, false, CfgFlag::DEFAULT),
	ConfigSetting("SkipFuncHashMap", &g_Config.sSkipFuncHashMap, "", CfgFlag::DEFAULT),
	ConfigSetting("MemInfoDetailed", &g_Config.bDebugMemInfoDetailed, false, CfgFlag::DEFAULT),
};

static const ConfigSetting jitSettings[] = {
	ConfigSetting("DiscardRegsOnJRRA", &g_Config.bDiscardRegsOnJRRA, false, CfgFlag::DONT_SAVE | CfgFlag::REPORT),
};

static const ConfigSetting upgradeSettings[] = {
	ConfigSetting("UpgradeMessage", &g_Config.upgradeMessage, "", CfgFlag::DEFAULT),
	ConfigSetting("UpgradeVersion", &g_Config.upgradeVersion, "", CfgFlag::DEFAULT),
	ConfigSetting("DismissedVersion", &g_Config.dismissedVersion, "", CfgFlag::DEFAULT),
};

static const ConfigSetting themeSettings[] = {
	ConfigSetting("ThemeName", &g_Config.sThemeName, "Default", CfgFlag::DEFAULT),
};


static const ConfigSetting vrSettings[] = {
	ConfigSetting("VREnable", &g_Config.bEnableVR, true, CfgFlag::PER_GAME),
	ConfigSetting("VREnable6DoF", &g_Config.bEnable6DoF, false, CfgFlag::PER_GAME),
	ConfigSetting("VREnableStereo", &g_Config.bEnableStereo, false, CfgFlag::PER_GAME),
	ConfigSetting("VRForce72Hz", &g_Config.bForce72Hz, true, CfgFlag::PER_GAME),
	ConfigSetting("VRForce", &g_Config.bForceVR, false, CfgFlag::DEFAULT),
	ConfigSetting("VRImmersiveMode", &g_Config.bEnableImmersiveVR, true, CfgFlag::PER_GAME),
	ConfigSetting("VRManualForceVR", &g_Config.bManualForceVR, false, CfgFlag::PER_GAME),
	ConfigSetting("VRPassthrough", &g_Config.bPassthrough, false, CfgFlag::PER_GAME),
	ConfigSetting("VRRescaleHUD", &g_Config.bRescaleHUD, true, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraDistance", &g_Config.fCameraDistance, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraHeight", &g_Config.fCameraHeight, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraSide", &g_Config.fCameraSide, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraPitch", &g_Config.fCameraPitch, 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCanvasDistance", &g_Config.fCanvasDistance, 12.0f, CfgFlag::DEFAULT),
	ConfigSetting("VRCanvas3DDistance", &g_Config.fCanvas3DDistance, 3.0f, CfgFlag::DEFAULT),
	ConfigSetting("VRFieldOfView", &g_Config.fFieldOfViewPercentage, 100.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRHeadUpDisplayScale", &g_Config.fHeadUpDisplayScale, 0.3f, CfgFlag::PER_GAME),
};

static const ConfigSectionSettings sections[] = {
	{"General", generalSettings, ARRAY_SIZE(generalSettings)},
	{"CPU", cpuSettings, ARRAY_SIZE(cpuSettings)},
	{"Graphics", graphicsSettings, ARRAY_SIZE(graphicsSettings)},
	{"Sound", soundSettings, ARRAY_SIZE(soundSettings)},
	{"Control", controlSettings, ARRAY_SIZE(controlSettings)},
	{"SystemParam", systemParamSettings, ARRAY_SIZE(systemParamSettings)},
	{"Network", networkSettings, ARRAY_SIZE(networkSettings)},
	{"Debugger", debuggerSettings, ARRAY_SIZE(debuggerSettings)},
	{"JIT", jitSettings, ARRAY_SIZE(jitSettings)},
	{"Upgrade", upgradeSettings, ARRAY_SIZE(upgradeSettings)},
	{"Theme", themeSettings, ARRAY_SIZE(themeSettings)},
	{"VR", vrSettings, ARRAY_SIZE(vrSettings)},
	{"Achievements", achievementSettings, ARRAY_SIZE(achievementSettings)},
};

const size_t numSections = ARRAY_SIZE(sections);

static void IterateSettings(IniFile &iniFile, std::function<void(Section *section, const ConfigSetting &setting)> func) {
	for (size_t i = 0; i < numSections; ++i) {
		Section *section = iniFile.GetOrCreateSection(sections[i].section);
		for (size_t j = 0; j < sections[i].settingsCount; j++) {
			func(section, sections[i].settings[j]);
		}
	}
}

static void IterateSettings(std::function<void(const ConfigSetting &setting)> func) {
	for (size_t i = 0; i < numSections; ++i) {
		for (size_t j = 0; j < sections[i].settingsCount; j++) {
			func(sections[i].settings[j]);
		}
	}
}

void ConfigPrivate::ResetRecentIsosThread() {
	std::lock_guard<std::mutex> guard(recentIsosThreadLock);
	if (recentIsosThreadPending && recentIsosThread.joinable())
		recentIsosThread.join();
}

void ConfigPrivate::SetRecentIsosThread(std::function<void()> f) {
	std::lock_guard<std::mutex> guard(recentIsosThreadLock);
	if (recentIsosThreadPending && recentIsosThread.joinable())
		recentIsosThread.join();
	recentIsosThread = std::thread(f);
	recentIsosThreadPending = true;
}

Config::Config() {
	private_ = new ConfigPrivate();
}

Config::~Config() {
	if (bUpdatedInstanceCounter) {
		ShutdownInstanceCounter();
	}
	private_->ResetRecentIsosThread();
	delete private_;
}

void Config::LoadLangValuesMapping() {
	IniFile mapping;
	mapping.LoadFromVFS(g_VFS, "langregion.ini");
	std::vector<std::string> keys;
	mapping.GetKeys("LangRegionNames", keys);

	std::map<std::string, int> langCodeMapping;
	langCodeMapping["JAPANESE"] = PSP_SYSTEMPARAM_LANGUAGE_JAPANESE;
	langCodeMapping["ENGLISH"] = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	langCodeMapping["FRENCH"] = PSP_SYSTEMPARAM_LANGUAGE_FRENCH;
	langCodeMapping["SPANISH"] = PSP_SYSTEMPARAM_LANGUAGE_SPANISH;
	langCodeMapping["GERMAN"] = PSP_SYSTEMPARAM_LANGUAGE_GERMAN;
	langCodeMapping["ITALIAN"] = PSP_SYSTEMPARAM_LANGUAGE_ITALIAN;
	langCodeMapping["DUTCH"] = PSP_SYSTEMPARAM_LANGUAGE_DUTCH;
	langCodeMapping["PORTUGUESE"] = PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE;
	langCodeMapping["RUSSIAN"] = PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN;
	langCodeMapping["KOREAN"] = PSP_SYSTEMPARAM_LANGUAGE_KOREAN;
	langCodeMapping["CHINESE_TRADITIONAL"] = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL;
	langCodeMapping["CHINESE_SIMPLIFIED"] = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED;

	const Section *langRegionNames = mapping.GetOrCreateSection("LangRegionNames");
	const Section *systemLanguage = mapping.GetOrCreateSection("SystemLanguage");

	for (size_t i = 0; i < keys.size(); i++) {
		std::string langName;
		langRegionNames->Get(keys[i], &langName, "ERROR");
		std::string langCode;
		systemLanguage->Get(keys[i], &langCode, "ENGLISH");
		int iLangCode = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		if (langCodeMapping.find(langCode) != langCodeMapping.end())
			iLangCode = langCodeMapping[langCode];
		langValuesMapping_[keys[i]] = std::make_pair(langName, iLangCode);
	}
}

const std::map<std::string, std::pair<std::string, int>, std::less<>> &Config::GetLangValuesMapping() {
	if (langValuesMapping_.empty()) {
		LoadLangValuesMapping();
	}
	return langValuesMapping_;
}

void Config::Reload() {
	reload_ = true;
	Load();
	reload_ = false;
}

// Call this if you change the search path (such as when changing memstick directory. can't
// really think of any other legit uses).
void Config::UpdateIniLocation(const char *iniFileName, const char *controllerIniFilename) {
	const bool useIniFilename = iniFileName != nullptr && strlen(iniFileName) > 0;
	const char *ppssppIniFilename = IsVREnabled() ? "ppssppvr.ini" : "ppsspp.ini";
	bool exists;
	iniFilename_ = FindConfigFile(useIniFilename ? iniFileName : ppssppIniFilename, &exists);
	const bool useControllerIniFilename = controllerIniFilename != nullptr && strlen(controllerIniFilename) > 0;
	const char *controlsIniFilename = IsVREnabled() ? "controlsvr.ini" : "controls.ini";
	controllerIniFilename_ = FindConfigFile(useControllerIniFilename ? controllerIniFilename : controlsIniFilename, &exists);
}

bool Config::LoadAppendedConfig() {
	IniFile iniFile;
	if (!iniFile.Load(appendedConfigFileName_)) {
		ERROR_LOG(Log::Loader, "Failed to read appended config '%s'.", appendedConfigFileName_.c_str());
		return false;
	}

	IterateSettings(iniFile, [&iniFile](Section *section, const ConfigSetting &setting) {
		if (iniFile.Exists(section->name().c_str(), setting.iniKey_))
			setting.Get(section);
	});

	INFO_LOG(Log::Loader, "Loaded appended config '%s'.", appendedConfigFileName_.c_str());

	Save("Loaded appended config"); // Let's prevent reset
	return true;
}

void Config::SetAppendedConfigIni(const Path &path) {
	appendedConfigFileName_ = path;
}

void Config::UpdateAfterSettingAutoFrameSkip() {
	if (bAutoFrameSkip && iFrameSkip == 0) {
		iFrameSkip = 1;
	}
	
	if (bAutoFrameSkip && bSkipBufferEffects) {
		bSkipBufferEffects = false;
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
		ERROR_LOG(Log::Loader, "Failed to read '%s'. Setting config to default.", iniFilename_.c_str());
		// Continue anyway to initialize the config.
	}

	IterateSettings(iniFile, [](Section *section, const ConfigSetting &setting) {
		setting.Get(section);
	});

	iRunCount++;

	// For iOS, issue #19211
	TryUpdateSavedPath(&currentDirectory);

	// This check is probably not really necessary here anyway, you can always
	// press Home or Browse if you're in a bad directory.
	if (!File::Exists(currentDirectory))
		currentDirectory = defaultCurrentDirectory;

	Section *log = iniFile.GetOrCreateSection(logSectionName);

	bool debugDefaults = false;
#ifdef _DEBUG
	debugDefaults = true;
#endif
	g_logManager.LoadConfig(log, debugDefaults);

	Section *recent = iniFile.GetOrCreateSection("Recent");
	recent->Get("MaxRecent", &iMaxRecent, 60);

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
		private_->ResetRecentIsosThread();
		std::lock_guard<std::mutex> guard(private_->recentIsosLock);
		recentIsos.clear();
		for (int i = 0; i < iMaxRecent; i++) {
			char keyName[64];
			std::string fileName;

			snprintf(keyName, sizeof(keyName), "FileName%d", i);
			if (recent->Get(keyName, &fileName, "") && !fileName.empty()) {
				recentIsos.push_back(fileName);
			}
		}
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

	// Check for an old dpad setting (very obsolete)
	Section *control = iniFile.GetSection("Control");
	if (control) {
		float f;
		control->Get("DPadRadius", &f, 0.0f);
		if (f > 0.0f) {
			ResetControlLayout();
		}
	}

	// Force JIT setting to a valid value for the current system configuration.
	if (!System_GetPropertyBool(SYSPROP_CAN_JIT)) {
		if (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR) {
			g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
		}
	}

	const char *gitVer = PPSSPP_GIT_VERSION;
	Version installed(gitVer);
	Version upgrade(upgradeVersion);
	const bool versionsValid = installed.IsValid() && upgrade.IsValid();

	// Do this regardless of iRunCount to prevent a silly bug where one might use an older
	// build of PPSSPP, receive an upgrade notice, then start a newer version, and still receive the upgrade notice,
	// even if said newer version is >= the upgrade found online.
	if ((dismissedVersion == upgradeVersion) || (versionsValid && (installed >= upgrade))) {
		upgradeMessage.clear();
	}

	// Check for new version on every 10 runs.
	// Sometimes the download may not be finished when the main screen shows (if the user dismisses the
	// splash screen quickly), but then we'll just show the notification next time instead, we store the
	// upgrade number in the ini.
	if (iRunCount % 10 == 0 && bCheckForNewVersion) {
		const char *versionUrl = "http://www.ppsspp.org/version.json";
		const char *acceptMime = "application/json, text/*; q=0.9, */*; q=0.8";
		g_DownloadManager.StartDownloadWithCallback(versionUrl, Path(), http::RequestFlags::Default, &DownloadCompletedCallback, "version", acceptMime);
	}

	INFO_LOG(Log::Loader, "Loading controller config: %s", controllerIniFilename_.c_str());
	bSaveSettings = true;

	LoadStandardControllerIni();

	//so this is all the way down here to overwrite the controller settings
	//sadly it won't benefit from all the "version conversion" going on up-above
	//but these configs shouldn't contain older versions anyhow
	if (bGameSpecific) {
		loadGameConfig(gameId_, gameIdTitle_);
	}

	CleanRecent();

	PostLoadCleanup(false);

	INFO_LOG(Log::Loader, "Config loaded: '%s' (%0.1f ms)", iniFilename_.c_str(), (time_now_d() - startTime) * 1000.0);
}

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
		saveGameConfig(gameId_, gameIdTitle_);

		PreSaveCleanup(false);

		CleanRecent();
		IniFile iniFile;
		if (!iniFile.Load(iniFilename_)) {
			WARN_LOG(Log::Loader, "Likely saving config for first time - couldn't read ini '%s'", iniFilename_.c_str());
		}

		// Need to do this somewhere...
		bFirstRun = false;

		IterateSettings(iniFile, [&](Section *section, const ConfigSetting &setting) {
			if (!bGameSpecific || !setting.PerGame()) {
				setting.Set(section);
			}
		});

		Section *recent = iniFile.GetOrCreateSection("Recent");
		recent->Set("MaxRecent", iMaxRecent);

		private_->ResetRecentIsosThread();
		for (int i = 0; i < iMaxRecent; i++) {
			char keyName[64];
			snprintf(keyName, sizeof(keyName), "FileName%d", i);
			std::lock_guard<std::mutex> guard(private_->recentIsosLock);
			if (i < (int)recentIsos.size()) {
				recent->Set(keyName, recentIsos[i]);
			} else {
				recent->Delete(keyName); // delete the nonexisting FileName
			}
		}

		Section *pinnedPaths = iniFile.GetOrCreateSection("PinnedPaths");
		pinnedPaths->Clear();
		for (size_t i = 0; i < vPinnedPaths.size(); ++i) {
			char keyName[64];
			snprintf(keyName, sizeof(keyName), "Path%d", (int)i);
			pinnedPaths->Set(keyName, vPinnedPaths[i]);
		}

		if (!bGameSpecific) {
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

		if (!bGameSpecific) //otherwise we already did this in saveGameConfig()
		{
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

		PostSaveCleanup(false);
	} else {
		INFO_LOG(Log::Loader, "Not saving config");
	}

	return true;
}

// A lot more cleanup tasks should be moved into here, and some of these are severely outdated.
void Config::PostLoadCleanup(bool gameSpecific) {
	// Override ppsspp.ini JIT value to prevent crashing
	jitForcedOff = DefaultCpuCore() != (int)CPUCore::JIT && (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR);
	if (jitForcedOff) {
		g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
	}

	// This caps the exponent 4 (so 16x.)
	if (iAnisotropyLevel > 4) {
		iAnisotropyLevel = 4;
	}

	// Set a default MAC, and correct if it's an old format.
	if (sMACAddress.length() != 17)
		sMACAddress = CreateRandMAC();

	if (g_Config.bAutoFrameSkip && g_Config.bSkipBufferEffects) {
		g_Config.bSkipBufferEffects = false;
	}

	// Automatically silence secondary instances. Could be an option I guess, but meh.
	if (PPSSPP_ID > 1) {
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

	// Convert old volume settings.

}

void Config::PreSaveCleanup(bool gameSpecific) {
	if (jitForcedOff) {
		// If we forced jit off and it's still set to IR, change it back to jit.
		if (g_Config.iCpuCore == (int)CPUCore::IR_INTERPRETER)
			g_Config.iCpuCore = (int)CPUCore::JIT;
	}
}

void Config::PostSaveCleanup(bool gameSpecific) {
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

// Use for debugging the version check without messing with the server
#if 0
#define PPSSPP_GIT_VERSION "v0.0.1-gaaaaaaaaa"
#endif

void Config::DownloadCompletedCallback(http::Request &download) {
	if (download.ResultCode() != 200) {
		ERROR_LOG(Log::Loader, "Failed to download %s: %d", download.url().c_str(), download.ResultCode());
		return;
	}
	std::string data;
	download.buffer().TakeAll(&data);
	if (data.empty()) {
		ERROR_LOG(Log::Loader, "Version check: Empty data from server!");
		return;
	}

	json::JsonReader reader(data.c_str(), data.size());
	const json::JsonGet root = reader.root();
	if (!root) {
		ERROR_LOG(Log::Loader, "Failed to parse json");
		return;
	}

	std::string version;
	root.getString("version", &version);

	const char *gitVer = PPSSPP_GIT_VERSION;
	Version installed(gitVer);
	Version upgrade(version);
	Version dismissed(g_Config.dismissedVersion);

	if (!installed.IsValid()) {
		ERROR_LOG(Log::Loader, "Version check: Local version string invalid. Build problems? %s", PPSSPP_GIT_VERSION);
		return;
	}
	if (!upgrade.IsValid()) {
		ERROR_LOG(Log::Loader, "Version check: Invalid server version: %s", version.c_str());
		return;
	}

	if (installed >= upgrade) {
		INFO_LOG(Log::Loader, "Version check: Already up to date, erasing any upgrade message");
		g_Config.upgradeMessage.clear();
		g_Config.upgradeVersion = upgrade.ToString();
		g_Config.dismissedVersion.clear();
		return;
	}

	if (installed < upgrade && dismissed != upgrade) {
		g_Config.upgradeMessage = "New version of PPSSPP available!";
		g_Config.upgradeVersion = upgrade.ToString();
		g_Config.dismissedVersion.clear();
	}
}

void Config::DismissUpgrade() {
	g_Config.dismissedVersion = g_Config.upgradeVersion;
}

void Config::AddRecent(const std::string &file) {
	// Don't bother with this if the user disabled recents (it's -1).
	if (iMaxRecent <= 0)
		return;

	// We'll add it back below.  This makes sure it's at the front, and only once.
	RemoveRecent(file);

	private_->ResetRecentIsosThread();
	std::lock_guard<std::mutex> guard(private_->recentIsosLock);
	const std::string filename = File::ResolvePath(file);
	recentIsos.insert(recentIsos.begin(), filename);
	if ((int)recentIsos.size() > iMaxRecent)
		recentIsos.resize(iMaxRecent);
}

void Config::RemoveRecent(const std::string &file) {
	// Don't bother with this if the user disabled recents (it's -1).
	if (iMaxRecent <= 0)
		return;

	private_->ResetRecentIsosThread();
	std::lock_guard<std::mutex> guard(private_->recentIsosLock);
	
	const std::string filename = File::ResolvePath(file);
	auto iter = std::remove_if(recentIsos.begin(), recentIsos.end(), [filename](const auto &str) {
		const std::string recent = File::ResolvePath(str);
		return filename == recent;
	});
	// remove_if is weird.
	recentIsos.erase(iter, recentIsos.end());
}

// On iOS, the path to the app documents directory changes on each launch.
// Example path:
// /var/mobile/Containers/Data/Application/0E0E89DE-8D8E-485A-860C-700D8BC87B86/Documents/PSP/GAME/SuicideBarbie
// The GUID part changes on each launch.
static bool TryUpdateSavedPath(Path *path) {
#if PPSSPP_PLATFORM(IOS)
	INFO_LOG(Log::Loader, "Original path: %s", path->c_str());
	std::string pathStr = path->ToString();

	const std::string_view applicationRoot = "/var/mobile/Containers/Data/Application/";
	if (startsWith(pathStr, applicationRoot)) {
		size_t documentsPos = pathStr.find("/Documents/");
		if (documentsPos == std::string::npos) {
			return false;
		}
		std::string memstick = g_Config.memStickDirectory.ToString();
		size_t memstickDocumentsPos = memstick.find("/Documents");  // Note: No trailing slash, or we won't find it.
		*path = Path(memstick.substr(0, memstickDocumentsPos) + pathStr.substr(documentsPos));
		return true;
	} else {
		// Path can't be auto-updated.
		return false;
	}
#else
	return false;
#endif
}

void Config::CleanRecent() {
	private_->SetRecentIsosThread([this] {
		SetCurrentThreadName("RecentISOs");

		AndroidJNIThreadContext jniContext;  // destructor detaches

		double startTime = time_now_d();

		std::vector<std::string> recent;
		{
			std::lock_guard<std::mutex> guard(private_->recentIsosLock);
			recent = recentIsos;
		}
		
		std::vector<std::string> cleanedRecent;
		if (recentIsos.empty()) {
			INFO_LOG(Log::Loader, "No recents list found.");
		}

		for (size_t i = 0; i < recent.size(); i++) {
			bool exists = false;
			Path path = Path(recent[i]);
			switch (path.Type()) {
			case PathType::CONTENT_URI:
			case PathType::NATIVE:
				exists = File::Exists(path);
				if (!exists) {
					if (TryUpdateSavedPath(&path)) {
						exists = File::Exists(path);
						INFO_LOG(Log::Loader, "Exists=%d when checking updated path: %s", exists, path.c_str());
					}
				}
				break;
			default:
				FileLoader *loader = ConstructFileLoader(path);
				exists = loader->ExistsFast();
				delete loader;
				break;
			}

			if (exists) {
				std::string pathStr = path.ToString();
				// Make sure we don't have any redundant items.
				auto duplicate = std::find(cleanedRecent.begin(), cleanedRecent.end(), pathStr);
				if (duplicate == cleanedRecent.end()) {
					cleanedRecent.push_back(pathStr);
				}
			} else {
				DEBUG_LOG(Log::Loader, "Removed %s from recent. errno=%d", path.c_str(), errno);
			}
		}

		double recentTime = time_now_d() - startTime;
		if (recentTime > 0.1) {
			INFO_LOG(Log::System, "CleanRecent took %0.2f", recentTime);
		}

		std::lock_guard<std::mutex> guard(private_->recentIsosLock);
		recentIsos = cleanedRecent;
	});
}

std::vector<std::string> Config::RecentIsos() const {
	std::lock_guard<std::mutex> guard(private_->recentIsosLock);
	return recentIsos;
}

bool Config::HasRecentIsos() const {
	std::lock_guard<std::mutex> guard(private_->recentIsosLock);
	return !recentIsos.empty();
}

void Config::ClearRecentIsos() {
	private_->ResetRecentIsosThread();
	std::lock_guard<std::mutex> guard(private_->recentIsosLock);
	recentIsos.clear();
}

void Config::SetSearchPath(const Path &searchPath) {
	searchPath_ = searchPath;
}

const Path Config::FindConfigFile(const std::string &baseFilename, bool *exists) {
	// Don't search for an absolute path.
	if (baseFilename.size() > 1 && baseFilename[0] == '/') {
		Path path(baseFilename);
		*exists = File::Exists(path);
		return path;
	}
#ifdef _WIN32
	if (baseFilename.size() > 3 && baseFilename[1] == ':' && (baseFilename[2] == '/' || baseFilename[2] == '\\')) {
		Path path(baseFilename);
		*exists = File::Exists(path);
		return path;
	}
#endif

	Path filename = searchPath_ / baseFilename;
	if (File::Exists(filename)) {
		*exists = true;
		return filename;
	}
	*exists = false;
	// Make sure at least the directory it's supposed to be in exists.
	Path parent = filename.NavigateUp();

	// We try to create the path and ignore if it fails (already exists).
	if (parent != GetSysDirectory(DIRECTORY_SYSTEM)) {
		File::CreateFullPath(parent);
	}
	return filename;
}

void Config::RestoreDefaults(RestoreSettingsBits whatToRestore) {
	if (bGameSpecific) {
		// TODO: This should be possible to do in a cleaner way.
		deleteGameConfig(gameId_);
		createGameConfig(gameId_);
		Load();
	} else {
		if (whatToRestore & RestoreSettingsBits::SETTINGS) {
			IterateSettings([](const ConfigSetting &setting) {
				setting.RestoreToDefault();
			});
		}

		if (whatToRestore & RestoreSettingsBits::CONTROLS) {
			KeyMap::RestoreDefault();
		}

		if (whatToRestore & RestoreSettingsBits::RECENT) {
			ClearRecentIsos();
			currentDirectory = defaultCurrentDirectory;
		}
	}
}

bool Config::hasGameConfig(const std::string &pGameId) {
	bool exists = false;
	Path fullIniFilePath = getGameConfigFile(pGameId, &exists);
	return exists;
}

void Config::changeGameSpecific(const std::string &pGameId, const std::string &title) {
	if (!reload_)
		Save("changeGameSpecific");
	gameId_ = pGameId;
	gameIdTitle_ = title;
	bGameSpecific = !pGameId.empty();
}

bool Config::createGameConfig(const std::string &pGameId) {
	bool exists;
	Path fullIniFilePath = getGameConfigFile(pGameId, &exists);

	if (exists) {
		INFO_LOG(Log::System, "Game config already exists");
		return false;
	}

	File::CreateEmptyFile(fullIniFilePath);
	return true;
}

bool Config::deleteGameConfig(const std::string& pGameId) {
	bool exists;
	Path fullIniFilePath = Path(getGameConfigFile(pGameId, &exists));

	if (exists) {
		File::Delete(fullIniFilePath);
	}
	return true;
}

Path Config::getGameConfigFile(const std::string &pGameId, bool *exists) {
	const char *ppssppIniFilename = IsVREnabled() ? "_ppssppvr.ini" : "_ppsspp.ini";
	std::string iniFileName = pGameId + ppssppIniFilename;
	Path iniFileNameFull = FindConfigFile(iniFileName, exists);

	return iniFileNameFull;
}

bool Config::saveGameConfig(const std::string &pGameId, const std::string &title) {
	if (pGameId.empty()) {
		return false;
	}

	bool exists;
	Path fullIniFilePath = getGameConfigFile(pGameId, &exists);

	IniFile iniFile;

	Section *top = iniFile.GetOrCreateSection("");
	top->AddComment(StringFromFormat("Game config for %s - %s", pGameId.c_str(), title.c_str()));

	PreSaveCleanup(true);

	IterateSettings(iniFile, [](Section *section, const ConfigSetting &setting) {
		if (setting.PerGame()) {
			setting.Set(section);
		}
	});

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

	PostSaveCleanup(true);
	return true;
}

bool Config::loadGameConfig(const std::string &pGameId, const std::string &title) {
	bool exists;
	Path iniFileNameFull = getGameConfigFile(pGameId, &exists);
	if (!exists) {
		DEBUG_LOG(Log::Loader, "No game-specific settings found in %s. Using global defaults.", iniFileNameFull.c_str());
		return false;
	}

	changeGameSpecific(pGameId, title);
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

	IterateSettings(iniFile, [](Section *section, const ConfigSetting &setting) {
		if (setting.PerGame()) {
			setting.Get(section);
		}
	});

	KeyMap::LoadFromIni(iniFile);

	if (!appendedConfigFileName_.ToString().empty() &&
		std::find(appendedConfigUpdatedGames_.begin(), appendedConfigUpdatedGames_.end(), pGameId) == appendedConfigUpdatedGames_.end()) {

		LoadAppendedConfig();
		appendedConfigUpdatedGames_.push_back(pGameId);
	}

	PostLoadCleanup(true);
	return true;
}

void Config::unloadGameConfig() {
	if (bGameSpecific) {
		changeGameSpecific();

		IniFile iniFile;
		iniFile.Load(iniFilename_);

		// Reload game specific settings back to standard.
		IterateSettings(iniFile, [](Section *section, const ConfigSetting &setting) {
			if (setting.PerGame()) {
				setting.Get(section);
			}
		});

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
		PostLoadCleanup(true);
	}
}

void Config::LoadStandardControllerIni() {
	IniFile controllerIniFile;
	if (!controllerIniFile.Load(controllerIniFilename_)) {
		ERROR_LOG(Log::Loader, "Failed to read %s. Setting controller config to default.", controllerIniFilename_.c_str());
		KeyMap::RestoreDefault();
	} else {
		// Continue anyway to initialize the config. It will just restore the defaults.
		KeyMap::LoadFromIni(controllerIniFile);
	}
}

void Config::ResetControlLayout() {
	auto reset = [](ConfigTouchPos &pos) {
		pos.x = defaultTouchPosShow.x;
		pos.y = defaultTouchPosShow.y;
		pos.scale = defaultTouchPosShow.scale;
	};
	reset(g_Config.touchActionButtonCenter);
	g_Config.fActionButtonSpacing = 1.0f;
	reset(g_Config.touchDpad);
	g_Config.fDpadSpacing = 1.0f;
	reset(g_Config.touchStartKey);
	reset(g_Config.touchSelectKey);
	reset(g_Config.touchFastForwardKey);
	reset(g_Config.touchLKey);
	reset(g_Config.touchRKey);
	reset(g_Config.touchAnalogStick);
	reset(g_Config.touchRightAnalogStick);
	for (int i = 0; i < CUSTOM_BUTTON_COUNT; i++) {
		reset(g_Config.touchCustom[i]);
	}
	g_Config.fLeftStickHeadScale = 1.0f;
	g_Config.fRightStickHeadScale = 1.0f;
}

void Config::GetReportingInfo(UrlEncoder &data) const {
	for (size_t i = 0; i < numSections; ++i) {
		const std::string prefix = std::string("config.") + sections[i].section;
		for (size_t j = 0; j < sections[i].settingsCount; j++) {
			sections[i].settings[j].ReportSetting(data, prefix);
		}
	}
}

bool Config::IsPortrait() const {
	return (iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180) && !bSkipBufferEffects;
}

int Config::GetPSPLanguage() {
	if (g_Config.iLanguage == -1) {
		const auto &langValuesMapping = GetLangValuesMapping();
		auto iter = langValuesMapping.find(g_Config.sLanguageIni);
		if (iter != langValuesMapping.end()) {
			return iter->second.second;
		} else {
			// Fallback to English
			return PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		}
	} else {
		return g_Config.iLanguage;
	}
}

void PlayTimeTracker::Start(const std::string &gameId) {
	if (gameId.empty()) {
		return;
	}
	VERBOSE_LOG(Log::System, "GameTimeTracker::Start(%s)", gameId.c_str());

	auto iter = tracker_.find(std::string(gameId));
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
	tracker_[gameId] = playTime;
}

void PlayTimeTracker::Stop(const std::string &gameId) {
	if (gameId.empty()) {
		return;
	}

	VERBOSE_LOG(Log::System, "GameTimeTracker::Stop(%s)", gameId.c_str());

	auto iter = tracker_.find(std::string(gameId));
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

void PlayTimeTracker::Load(const Section *section) {
	tracker_.clear();

	auto map = section->ToMap();

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

bool PlayTimeTracker::GetPlayedTimeString(const std::string &gameId, std::string *str) const {
	auto ga = GetI18NCategory(I18NCat::GAME);

	auto iter = tracker_.find(gameId);
	if (iter == tracker_.end()) {
		return false;
	}

	int totalSeconds = iter->second.totalTimePlayed;
	int seconds = totalSeconds % 60;
	totalSeconds /= 60;
	int minutes = totalSeconds % 60;
	totalSeconds /= 60;
	int hours = totalSeconds;

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
