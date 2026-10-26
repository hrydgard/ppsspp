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
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <set>
#include <type_traits>
#include <sstream>
#include <thread>

#include "ppsspp_config.h"

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/URL.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Format/JSONReader.h"
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
#include "Core/Reporting.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
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

std::string GPUBackendToString(GPUBackend backend) {
	switch (backend) {
	case GPUBackend::OPENGL:
		return "OPENGL";
	case GPUBackend::DIRECT3D11:
		return "DIRECT3D11";
	case GPUBackend::VULKAN:
		return "VULKAN";
	case GPUBackend::METAL:
		return "METAL";
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
	if (equalsNoCase(backend, "METAL") || backend == "4")
		return GPUBackend::METAL;
	return GPUBackend::OPENGL;
}

std::string DefaultLangRegion() {
	// Unfortunate default.  There's no need to use bFirstRun, since this is only a default.
	static std::string defaultLangRegion = "en_US";
	std::string langRegion = System_GetProperty(SYSPROP_LANGREGION);
	if (!langRegion.empty() && g_i18nrepo.IniExists(langRegion)) {
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

int DefaultDepthRaster() {
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

// The old GridView bools, read as ints so a missing key (-1) can be told apart from false.
static int LegacyGridViewFromString(const std::string &value) {
	return equalsNoCase(value, "True") ? 1 : 0;
}

static std::string LegacyGridViewToString(int value) {
	return value == 1 ? "True" : "False";
}

static int GameBrowserViewFromGridView(int legacyGridView, GameBrowserViewMode defaultMode) {
	if (legacyGridView < 0) {
		return (int)defaultMode;
	}
	return (int)(legacyGridView ? GameBrowserViewMode::GRID : GameBrowserViewMode::LIST);
}

// Recent, Games and Homebrew switch everyone to the large list, ignoring the old setting. Remote keeps it.
static int DefaultGameBrowserView1() { return (int)GameBrowserViewMode::LARGE_LIST; }
static int DefaultGameBrowserView2() { return (int)GameBrowserViewMode::LARGE_LIST; }
static int DefaultGameBrowserView3() { return (int)GameBrowserViewMode::LARGE_LIST; }
static int DefaultGameBrowserView4() { return GameBrowserViewFromGridView(g_Config.iLegacyGridView4, GameBrowserViewMode::LIST); }

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

#if defined(__GNUC__) || defined(__clang__)
// offsetof on classes with virtual functions (like ConfigBlock) is conditionally supported, and our compilers do support it.
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// The type of the block a setting lives in, from an expression like g_Config or g_Config.gestureControls[0].
#define CONFIG_BLOCK_TYPE(a) std::remove_reference_t<decltype(a)>
// Offsets are taken with offsetof, so the tables can be constexpr.
#define SETTING(a, x) ConfigSettingOffset<decltype(CONFIG_BLOCK_TYPE(a)::x)>{ (uint32_t)offsetof(CONFIG_BLOCK_TYPE(a), x) }
#define SETTING_IDX(a, x, i) ConfigSettingOffset<std::remove_extent_t<decltype(CONFIG_BLOCK_TYPE(a)::x)>>{ (uint32_t)(offsetof(CONFIG_BLOCK_TYPE(a), x) + (i) * sizeof(std::remove_extent_t<decltype(CONFIG_BLOCK_TYPE(a)::x)>)) }

// All relative to g_Config.
static constexpr const char *autoLoadSaveStateNames[] = { "Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5" };

static constexpr const char *backgroundAnimationNames[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background", "Bouncing icon", "Colored floating symbols" };

static constexpr const char *notificationPosNames[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

static constexpr const char *screenshotModeNames[] = { "Final processed image", "Raw game image" };

// What has to happen when the UI changes these settings.
static void OnInternalResolutionChanged() {
	if (g_Config.iAndroidHwScale == 1) {
		System_RecreateActivity();
	}
	Reporting::UpdateConfig();
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
}

static void OnAndroidHwScaleChanged() {
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
	System_RecreateActivity();
}

static void OnRenderResizingChanged() {
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
}

static void OnGPUConfigChanged() {
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

static void OnFullscreenChanged() {
	System_ApplyFullscreenState();
}

static void OnAutoFrameSkipChanged() {
	g_Config.UpdateAfterSettingAutoFrameSkip();
}

static void OnSkipBufferEffectsChanged() {
	if (g_Config.bSkipBufferEffects) {
		g_Config.bAutoFrameSkip = false;
	}
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

static void OnAudioModeChanged() {
	System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
}

static void OnMicDeviceChanged() {
	Microphone::onMicDeviceChange();
}

static void OnCameraDeviceChanged() {
	Camera::onCameraDeviceChange();
}

static void OnAppSwitchModeChanged() {
	System_Notify(SystemNotification::APP_SWITCH_MODE_CHANGED);
}

static void OnSaveStateSlotCountChanged() {
	System_Notify(SystemNotification::UI);
}

static void OnCheckForNewVersionChanged() {
	// Reset the dismissed version so it will check again.
	if (g_Config.bCheckForNewVersion) {
		g_Config.sDismissedVersion.clear();
	}
}

static void OnButtonPreferenceChanged() {
	KeyMap::UpdateNativeMenuKeys();
}

static constexpr ConfigSetting generalSettings[] = {
	ConfigSetting("FirstRun", SETTING(g_Config, bFirstRun), true, CfgFlag::DEFAULT),
	ConfigSetting("RunCount", SETTING(g_Config, iRunCount), 0, CfgFlag::DEFAULT),
	ConfigSetting("Enable Logging", SETTING(g_Config, bEnableLogging), true, CfgFlag::PER_GAME),
	ConfigSetting("FileLogging", SETTING(g_Config, bEnableFileLogging), false, CfgFlag::PER_GAME)
		.Label(I18NCat::DEVELOPER, "Log to file"),
	ConfigSetting("AutoRun", SETTING(g_Config, bAutoRun), true, CfgFlag::DEFAULT),
	ConfigSetting("IgnoreBadMemAccess", SETTING(g_Config, bIgnoreBadMemAccess), true, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Ignore bad memory accesses"),
	ConfigSetting("EnableFPUExceptionTraps", SETTING(g_Config, bEnableFPUExceptionTraps), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Enable FPU exceptions"),
	ConfigSetting("CurrentDirectory", SETTING(g_Config, currentDirectory), "", CfgFlag::DEFAULT),
	ConfigSetting("ShowDebuggerOnLoad", SETTING(g_Config, bShowDebuggerOnLoad), false, CfgFlag::DEFAULT),
	ConfigSetting("ShowImDebugger", SETTING(g_Config, bShowImDebugger), false, CfgFlag::DONT_SAVE),
	ConfigSetting("CheckForNewVersion", SETTING(g_Config, bCheckForNewVersion), true, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "VersionCheck", "Check for new versions of PPSSPP").OnChanged(&OnCheckForNewVersionChanged),
#if PPSSPP_PLATFORM(WINDOWS) && PPSSPP_ARCH(X86)
	ConfigSetting("Wow64WarningDismissed", SETTING(g_Config, bWow64WarningDismissed), false, CfgFlag::DEFAULT),
#endif
	ConfigSetting("Language", SETTING(g_Config, sLanguageIni), &DefaultLangRegion, CfgFlag::DEFAULT),
	ConfigSetting("ForceLagSync2", SETTING(g_Config, bForceLagSync), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Force real clock sync (slower, less lag)"),
	ConfigSetting("DiscordRichPresence", SETTING(g_Config, bDiscordRichPresence), false, CfgFlag::DEFAULT)
		.Label(I18NCat::NETWORKING, "Send Discord Presence information"),
	ConfigSetting("UISound", SETTING(g_Config, bUISound), false, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "UI sound"),

	ConfigSetting("DisableHTTPS", SETTING(g_Config, bDisableHTTPS), false, CfgFlag::DONT_SAVE),
	ConfigSetting("AutoLoadSaveState", SETTING(g_Config, iAutoLoadSaveState), 0, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Auto load savestate").Choices(autoLoadSaveStateNames),
	ConfigSetting("EnableCheats", SETTING(g_Config, bEnableCheats), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "Enable Cheats"),
	ConfigSetting("EnablePlugins", SETTING(g_Config, bEnablePlugins), true, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "Enable plugins"),
	ConfigSetting("EnableFileHandlerPlugins", SETTING(g_Config, bEnableFileHandlerPlugins), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Enable file handler plugins (insecure)"),
	ConfigSetting("CwCheatRefreshRate", SETTING(g_Config, iCwCheatRefreshIntervalMs), 77, CfgFlag::PER_GAME),
	ConfigSetting("CwCheatScrollPosition", SETTING(g_Config, fCwCheatScrollPosition), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("GameListScrollPosition", SETTING(g_Config, fGameListScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("HomebrewScrollPosition", SETTING(g_Config, fHomebrewScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("RemoteScrollPosition", SETTING(g_Config, fRemoteScrollPosition), 0.0f, CfgFlag::DEFAULT),
	ConfigSetting("DebugOverlay", SETTING(g_Config, iDebugOverlay), 0, CfgFlag::DONT_SAVE),
	ConfigSetting("DefaultTab", SETTING(g_Config, iDefaultTab), 0, CfgFlag::DEFAULT),
	ConfigSetting("DisableHLEFlags", SETTING(g_Config, iDisableHLE), 0, CfgFlag::PER_GAME),
	ConfigSetting("ForceEnableHLEFlags", SETTING(g_Config, iForceEnableHLE), 0, CfgFlag::PER_GAME),

	ConfigSetting("ScreenshotMode", SETTING(g_Config, iScreenshotMode), 0, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Screenshot mode").Choices(screenshotModeNames),
	ConfigSetting("ScreenshotsAsPNG", SETTING(g_Config, bScreenshotsAsPNG), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Screenshots as PNG"),
	ConfigSetting("DumpFrames", SETTING(g_Config, bDumpFrames), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Record Display"),
	ConfigSetting("DumpVideoOutput", SETTING(g_Config, bDumpVideoOutput), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Use output buffer (with overlay) for recording"),
	ConfigSetting("DumpAudio", SETTING(g_Config, bDumpAudio), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Record Audio"),
	ConfigSetting("ShowSaveLoadIndicator", SETTING(g_Config, bShowSaveLoadIndicator), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Show indicator when saving/loading"),
	ConfigSetting("SaveLoadResetsAVdumping", SETTING(g_Config, bSaveLoadResetsAVdumping), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Reset Recording on Save/Load State"),
	ConfigSetting("StateSlot", SETTING(g_Config, iCurrentStateSlot), 0, CfgFlag::PER_GAME),
	ConfigSetting("EnableStateUndo", SETTING(g_Config, bEnableStateUndo), &DefaultEnableStateUndo, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Savestate slot backups"),
	ConfigSetting("ConfirmLoadState", SETTING(g_Config, bConfirmLoadState), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Ask to confirm on load"),
	ConfigSetting("StateLoadUndoGame", SETTING(g_Config, sStateLoadUndoGame), "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveGame", SETTING(g_Config, sStateUndoLastSaveGame), "NA", CfgFlag::DEFAULT),
	ConfigSetting("StateUndoLastSaveSlot", SETTING(g_Config, iStateUndoLastSaveSlot), -5, CfgFlag::DEFAULT), // Start with an "invalid" value
	ConfigSetting("RewindSnapshotInterval", SETTING(g_Config, iRewindSnapshotInterval), 0, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Rewind Snapshot Interval").Range(0, 60).Units("seconds").ZeroLabel("Off"),
	ConfigSetting("SaveStateSlotCount", SETTING(g_Config, iSaveStateSlotCount), 5, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Savestate slot count").Range(1, 30).OnChanged(&OnSaveStateSlotCountChanged),
	ConfigSetting("ReportAccurateFreeStorageSpace", SETTING(g_Config, bReportAccurateFreeStorageSpace), false, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Report free storage space accurately"),

	ConfigSetting("ShowRegionOnGameIcon", SETTING(g_Config, bShowRegionOnGameIcon), false, CfgFlag::DEFAULT),
	ConfigSetting("ShowIDOnGameIcon", SETTING(g_Config, bShowIDOnGameIcon), false, CfgFlag::DEFAULT),
	ConfigSetting("GameGridScale", SETTING(g_Config, fGameGridScale), &DefaultGameGridScale, CfgFlag::DEFAULT),
	ConfigSetting("GridView4", SETTING(g_Config, iLegacyGridView4), -1, &LegacyGridViewToString, &LegacyGridViewFromString, CfgFlag::DONT_SAVE),
	// These must come after GridView*, which their defaults are taken from.
	ConfigSetting("GameBrowserView1", SETTING(g_Config, iGameBrowserView1), &DefaultGameBrowserView1, CfgFlag::DEFAULT),
	ConfigSetting("GameBrowserView2", SETTING(g_Config, iGameBrowserView2), &DefaultGameBrowserView2, CfgFlag::DEFAULT),
	ConfigSetting("GameBrowserView3", SETTING(g_Config, iGameBrowserView3), &DefaultGameBrowserView3, CfgFlag::DEFAULT),
	ConfigSetting("GameBrowserView4", SETTING(g_Config, iGameBrowserView4), &DefaultGameBrowserView4, CfgFlag::DEFAULT),
	ConfigSetting("RightAnalogUp", SETTING(g_Config, iRightAnalogUp), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDown", SETTING(g_Config, iRightAnalogDown), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogLeft", SETTING(g_Config, iRightAnalogLeft), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogRight", SETTING(g_Config, iRightAnalogRight), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogPress", SETTING(g_Config, iRightAnalogPress), 0, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogCustom", SETTING(g_Config, bRightAnalogCustom), false, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogDisableDiagonal", SETTING(g_Config, bRightAnalogDisableDiagonal), false, CfgFlag::PER_GAME),

	ConfigSetting("TouchGliding", SETTING(g_Config, bTouchGliding), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Keep first touched button pressed when dragging"),

	// "default" means let emulator decide, "" means disable.
	ConfigSetting("ReportingHost", SETTING(g_Config, sReportHost), "default", CfgFlag::DEFAULT),
	ConfigSetting("AutoSaveSymbolMap", SETTING(g_Config, bAutoSaveSymbolMap), false, CfgFlag::PER_GAME),
	ConfigSetting("CompressSymbols", SETTING(g_Config, bCompressSymbols), true, CfgFlag::DEFAULT),
	ConfigSetting("CacheFullIsoInRam", SETTING(g_Config, bCacheFullIsoInRam), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Cache full ISO in RAM"),
	ConfigSetting("AutoUpgradeFirmware", SETTING(g_Config, bAutoUpgradeFirmware), true, CfgFlag::DEFAULT),
	ConfigSetting("CSOPreset", SETTING(g_Config, iCSOPreset), 0, CfgFlag::DEFAULT),
	ConfigSetting("PackKeepOriginal", SETTING(g_Config, bPackKeepOriginal), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOPort", SETTING(g_Config, iRemoteISOPort), 0, CfgFlag::DEFAULT)
		.Label(I18NCat::REMOTEISO, "Local Server Port").Range(0, 65535, 100),
	ConfigSetting("LastRemoteISOServer", SETTING(g_Config, sLastRemoteISOServer), "", CfgFlag::DEFAULT),
	ConfigSetting("LastRemoteISOPort", SETTING(g_Config, iLastRemoteISOPort), 0, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOManualConfig", SETTING(g_Config, bRemoteISOManual), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteShareOnStartup", SETTING(g_Config, bRemoteShareOnStartup), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSubdir", SETTING(g_Config, sRemoteISOSubdir), "/", CfgFlag::DEFAULT),
	ConfigSetting("RemoteDebuggerOnStartup", SETTING(g_Config, bRemoteDebuggerOnStartup), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteDebuggerLocal", SETTING(g_Config, bRemoteDebuggerLocal), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Use locally hosted remote debugger"),
	ConfigSetting("RemoteTab", SETTING(g_Config, bRemoteTab), false, CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOSharedDir", SETTING(g_Config, sRemoteISOSharedDir), "", CfgFlag::DEFAULT),
	ConfigSetting("RemoteISOShareType", SETTING(g_Config, iRemoteISOShareType), (int)RemoteISOShareType::RECENT, CfgFlag::DEFAULT),
	ConfigSetting("AskForExitConfirmationAfterSeconds", SETTING(g_Config, iAskForExitConfirmationAfterSeconds), 300, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Ask for exit confirmation after seconds").Range(0, 1200).Units("s").ZeroLabel("Off"),

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	ConfigSetting("ScreenRotation", SETTING(g_Config, iScreenRotation), ROTATION_AUTO, CfgFlag::DEFAULT),
#endif

	ConfigSetting("BackgroundAnimation", SETTING(g_Config, iBackgroundAnimation), (int)BackgroundAnimation::WAVE, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "UI background animation").Choices(backgroundAnimationNames),
	ConfigSetting("TransparentBackground", SETTING(g_Config, bTransparentBackground), true, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Transparent UI background"),
	ConfigSetting("UITint", SETTING(g_Config, fUITint), 0.0, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Color tint").Range(0.0f, 1.0f, 0.01f),
	ConfigSetting("UISaturation", SETTING(g_Config, fUISaturation), &DefaultUISaturation, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Color saturation").Range(0.0f, 2.0f, 0.01f),

	// Current settings tabs
	// Decided for now that these should not be saved, so commented out from here. Still saved within the session, of course.
	// ConfigSetting("SettingsCurrentTab", SETTING(g_Config, iSettingsCurrentTab), 0, CfgFlag::DEFAULT),

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	ConfigSetting("TopMost", SETTING(g_Config, bTopMost), false, CfgFlag::DEFAULT),
#endif

#if PPSSPP_PLATFORM(WINDOWS) || (defined (SDL) && !defined(MOBILE_DEVICE))
	ConfigSetting("PauseOnLostFocus", SETTING(g_Config, bPauseOnLostFocus), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Pause when not focused"),
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

	ConfigSetting("MemStickInserted", SETTING(g_Config, bMemStickInserted), true, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "Memory Stick inserted"),
	ConfigSetting("LoadPlugins", SETTING(g_Config, bLoadPlugins), true, CfgFlag::PER_GAME),
	ConfigSetting("NotificationPos", SETTING(g_Config, iNotificationPos), (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Notification screen position").Choices(notificationPosNames, -1),

	ConfigSetting("IgnoreCompatSettings", SETTING(g_Config, sIgnoreCompatSettings), "", CfgFlag::PER_GAME | CfgFlag::REPORT),

	ConfigSetting("RunBehindPauseMenu", SETTING(g_Config, bRunBehindPauseMenu), false, CfgFlag::DEFAULT),

	ConfigSetting("ShowGPOLEDs", SETTING(g_Config, bShowGPOLEDs), false, CfgFlag::PER_GAME),

	ConfigSetting("UIScaleFactor", SETTING(g_Config, iUIScaleFactor), &DefaultUIScaleFactor, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "UI size adjustment (DPI)").Range(-8, 8).ZeroLabel("Off"),

	ConfigSetting("VulkanDisableImplicitLayers", SETTING(g_Config, bVulkanDisableImplicitLayers), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Prevent loading overlays"),
};

static bool DefaultSasThread() {
	return cpu_info.num_cores > 1;
}

static constexpr const char *achievementsLeaderboardTrackerPosNames[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

static constexpr ConfigSetting achievementSettings[] = {
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

	ConfigSetting("AchievementsLeaderboardTrackerPos", SETTING(g_Config, iAchievementsLeaderboardTrackerPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT)
		.Label(I18NCat::ACHIEVEMENTS, "Leaderboard tracker").Choices(achievementsLeaderboardTrackerPosNames, -1),
	ConfigSetting("AchievementsLeaderboardStartedOrFailedPos", SETTING(g_Config, iAchievementsLeaderboardStartedOrFailedPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsLeaderboardSubmittedPos", SETTING(g_Config, iAchievementsLeaderboardSubmittedPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsProgressPos", SETTING(g_Config, iAchievementsProgressPos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsChallengePos", SETTING(g_Config, iAchievementsChallengePos), (int)ScreenEdgePosition::TOP_LEFT, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
	ConfigSetting("AchievementsUnlockedPos", SETTING(g_Config, iAchievementsUnlockedPos), (int)ScreenEdgePosition::TOP_CENTER, CfgFlag::PER_GAME | CfgFlag::DEFAULT),
};

static constexpr const char *iOTimingMethodNames[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays", "Simulate UMD slow reading speed" };

static constexpr const char *cPUCoreNames[] = { "Interpreter", "Dynarec/JIT (recommended)", "IR Interpreter", "JIT using IR" };

static constexpr ConfigSetting cpuSettings[] = {
	ConfigSetting("CPUCore", SETTING(g_Config, iCpuCore), &DefaultCpuCore, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "CPU Core").Choices(cPUCoreNames),
	ConfigSetting("SeparateSASThread", SETTING(g_Config, bSeparateSASThread), &DefaultSasThread, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("IOTimingMethod", SETTING(g_Config, iIOTimingMethod), IOTIMING_FAST, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "I/O timing method").Choices(iOTimingMethodNames),
	ConfigSetting("FastMemoryAccess", SETTING(g_Config, bFastMemory), true, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Fast Memory"),
	ConfigSetting("EmulateMediaEngine", SETTING(g_Config, bEmulateMediaEngine), false, CfgFlag::REPORT)
		.Label(I18NCat::DEVELOPER, "Enable ME emulation for homebrew"),
	ConfigSetting("FunctionReplacements", SETTING(g_Config, bFuncReplacements), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("HideSlowWarnings", SETTING(g_Config, bHideSlowWarnings), false, CfgFlag::DEFAULT),
	ConfigSetting("HideStateWarnings", SETTING(g_Config, bHideStateWarnings), false, CfgFlag::DEFAULT),
	ConfigSetting("JitDisableFlags", SETTING(g_Config, uJitDisableFlags), (uint32_t)0, CfgFlag::PER_GAME),
	ConfigSetting("CPUSpeed", SETTING(g_Config, iLockedCPUSpeed), 0, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "Change CPU Clock", "Change CPU Clock (unstable)").Range(0, 1000).Units("MHz").ZeroLabel("Auto"),
};

static int DefaultInternalResolution() {
	// Auto on Windows and Linux, 2x on large screens and iOS, 1x elsewhere.
#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {
		return 4;
	}
	int longestDisplaySide = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES));
	int scale = longestDisplaySide >= 1000 ? 2 : 1;
	INFO_LOG(Log::Config, "Longest display side: %d pixels. Choosing scale %d", longestDisplaySide, scale);
	return scale;
#elif PPSSPP_PLATFORM(IOS)
	return 2;
#else
	return 0;
#endif
}

static int DefaultFastForwardMode() {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS)
	return (int)FastForwardMode::SKIP_FLIP;
#else
	return (int)FastForwardMode::CONTINUOUS;
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

#elif PPSSPP_PLATFORM(TVOS) && !defined(__LIBRETRO__)
	// Metal hasn't been tried on tvOS yet.
	return (int)GPUBackend::VULKAN;
#elif PPSSPP_PLATFORM(IOS) && !defined(__LIBRETRO__)
	return (int)GPUBackend::METAL;
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
		ERROR_LOG(Log::Config, "Graphics backend failed for %d, trying another", iGPUBackend);

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
		ERROR_LOG(Log::Config, "All graphics backends failed");
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

#if !PPSSPP_API(METAL)
	if (backend == GPUBackend::METAL)
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

std::unique_ptr<ConfigBlock> DisplayLayoutConfig::NewDefault(std::string_view blockName) const {
	std::unique_ptr<DisplayLayoutConfig> block = std::make_unique<DisplayLayoutConfig>();
	block->ResetToDefault(blockName);
	return block;
}

// These were previously part of Graphics.
// It's instantiated into g_Config.displayLayoutLandscape and g_Config.displayLayoutPortrait.
// Defaults are set directly on the struct declaration (and adjusted per instance in ResetToDefault above).
static constexpr ConfigSetting displayLayoutSettings[] = {
	ConfigSetting("BufferFiltering", SETTING(g_Config.displayLayoutLandscape, iDisplayFilter), CfgFlag::PER_GAME),
	ConfigSetting("DisplayStretch", SETTING(g_Config.displayLayoutLandscape, bDisplayStretch), CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetX", SETTING(g_Config.displayLayoutLandscape, fDisplayOffsetX), CfgFlag::PER_GAME),
	ConfigSetting("DisplayOffsetY", SETTING(g_Config.displayLayoutLandscape, fDisplayOffsetY), CfgFlag::PER_GAME),
	ConfigSetting("DisplayScale", SETTING(g_Config.displayLayoutLandscape, fDisplayScale), CfgFlag::PER_GAME),
	ConfigSetting("DisplayIntegerScale", SETTING(g_Config.displayLayoutLandscape, bDisplayIntegerScale), CfgFlag::PER_GAME),
	ConfigSetting("DisplayAspectRatio", SETTING(g_Config.displayLayoutLandscape, fDisplayAspectRatio), CfgFlag::PER_GAME),
	ConfigSetting("IgnoreScreenInsets", SETTING(g_Config.displayLayoutLandscape, bIgnoreScreenInsets), CfgFlag::PER_GAME),
	ConfigSetting("Overlay", SETTING(g_Config.displayLayoutLandscape, sOverlay), "", CfgFlag::PER_GAME),
	ConfigSetting("InternalScreenRotation", SETTING(g_Config.displayLayoutLandscape, iInternalScreenRotation), CfgFlag::PER_GAME),
	ConfigSetting("RotateControlsWithScreen", SETTING(g_Config.displayLayoutLandscape, bRotateControlsWithScreen), CfgFlag::PER_GAME),
	ConfigSetting("EnableCardboardVR", SETTING(g_Config.displayLayoutLandscape, bEnableCardboardVR), CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Enable Cardboard VR"),
	ConfigSetting("CardboardScreenSize", SETTING(g_Config.displayLayoutLandscape, iCardboardScreenSize), CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Cardboard Screen Size", "Screen Size (in % of the viewport)").Range(30, 150).Units("% of viewport"),
	ConfigSetting("CardboardXShift", SETTING(g_Config.displayLayoutLandscape, iCardboardXShift), CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Cardboard Screen X Shift", "X Shift (in % of the void)").Range(-150, 150).Units("% of the void"),
	ConfigSetting("CardboardYShift", SETTING(g_Config.displayLayoutLandscape, iCardboardYShift), CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Cardboard Screen Y Shift", "Y Shift (in % of the void)").Range(-100, 100).Units("% of the void"),
	ConfigSetting("ImmersiveMode", SETTING(g_Config.displayLayoutLandscape, bImmersiveMode), CfgFlag::PER_GAME)
#if PPSSPP_PLATFORM(IOS)
		.Label(I18NCat::SYSTEM, "Hide status bar"),
#else
		.Label(I18NCat::SYSTEM, "Hide navigation bar"),
#endif
};

bool GestureControlConfig::ResetToDefault(std::string_view blockName) {
	static const GestureControlConfig defaultLayout;
	*this = defaultLayout;
	// TODO: Check blockname to set defaults differently for secondary zone(s).
	return true;
}

std::unique_ptr<ConfigBlock> GestureControlConfig::NewDefault(std::string_view blockName) const {
	std::unique_ptr<GestureControlConfig> block = std::make_unique<GestureControlConfig>();
	block->ResetToDefault(blockName);
	return block;
}

// These were previously mostly part of General (which was wrong).
static constexpr ConfigSetting gestureControlSettings[] = {
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

// The alternative speeds used to be stored as frame rates, out of 60.
static int LegacyFpsLimitToPercent(int fps) {
	return fps < 0 ? -1 : (fps * 100 + 30) / 60;
}

static int DefaultAltSpeedPercent1() {
	return LegacyFpsLimitToPercent(g_Config.iLegacyFpsLimit1);
}

static int DefaultAltSpeedPercent2() {
	return LegacyFpsLimitToPercent(g_Config.iLegacyFpsLimit2);
}

static int DefaultAnalogAltSpeedPercent() {
	return LegacyFpsLimitToPercent(g_Config.iLegacyAnalogFpsLimit);
}

static constexpr const char *externalDisplayNames[] = { "Show the game", "Mirror the screen" };
static constexpr const char *gpuBackendNames[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan", "Metal" };
static constexpr const char *internalResolutionNames[] = { "Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
static constexpr const char *msaaLevelNames[] = { "Off", "2x", "4x", "8x", "16x" };
static constexpr const char *androidHwScaleNames[] = { "Native device resolution", "Same as Rendering resolution", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
static constexpr const char *frameSkipNames[] = { "Off", "1", "2", "3", "4" };
static constexpr const char *skipGPUReadbackModeNames[] = { "No", "Skip", "Copy to texture" };
static constexpr const char *depthRasterModeNames[] = { "Auto", "Low", "Off", "Always on" };
static constexpr const char *bloomHackNames[] = { "Off", "Safe", "Balanced", "Aggressive" };
static constexpr const char *inflightFramesNames[] = { "No buffer", "Up to 1" };
static constexpr const char *texScalingTypeNames[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic" };
#ifndef MOBILE_DEVICE
static constexpr const char *texScalingLevelNames[] = { "Off", "2x", "3x", "4x", "5x" };
#else
static constexpr const char *texScalingLevelNames[] = { "Off", "2x", "3x" };
#endif
static constexpr const char *anisotropyLevelNames[] = { "Off", "2x", "4x", "8x", "16x" };
static constexpr const char *texFilteringNames[] = { "Auto", "Nearest", "Linear", "Auto Max Quality" };

static constexpr const char *appSwitchModeNames[] = { "Swipe once to switch app (indicator auto-hides)", "Swipe twice to switch app (indicator stays visible)" };

static constexpr const char *replacementTextureLoadSpeedNames[] = { "Slow (smooth)", "Medium", "Fast", "Instant (may stutter)" };

static constexpr const char *androidFramerateModeNames[] = { "Default", "Request 60 Hz", "Force 60Hz" };

static constexpr ConfigSetting graphicsSettings[] = {
	ConfigSetting("iShowStatusFlags", SETTING(g_Config, iShowStatusFlags), 0, CfgFlag::PER_GAME),
	ConfigSetting("GraphicsBackend", SETTING(g_Config, iGPUBackend), &DefaultGPUBackend, &GPUBackendTranslator::To, &GPUBackendTranslator::From, CfgFlag::DEFAULT | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Backend").Choices(gpuBackendNames),
	ConfigSetting("ExternalDisplay", SETTING(g_Config, iExternalDisplay), (int)ExternalDisplayMode::SHOW_GAME, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "External display").Choices(externalDisplayNames),
#if PPSSPP_PLATFORM(ANDROID) && PPSSPP_ARCH(ARM64)
	ConfigSetting("CustomDriver", SETTING(g_Config, sCustomDriver), "", CfgFlag::DEFAULT),
#endif
	ConfigSetting("DisabledGraphicsBackends", SETTING(g_Config, sDisabledGPUBackends), "", CfgFlag::DEFAULT),
	ConfigSetting("VulkanDevice", SETTING(g_Config, sVulkanDevice), "", CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Device"),
#ifdef _WIN32
	ConfigSetting("D3D11Device", SETTING(g_Config, sD3D11Device), "", CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Device"),
#endif
	ConfigSetting("CameraDevice", SETTING(g_Config, sCameraDevice), "", CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Camera Device").OnChanged(&OnCameraDeviceChanged),
	ConfigSetting("CameraMirrorHorizontal", SETTING(g_Config, bCameraMirrorHorizontal), false, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Mirror camera image"),
	ConfigSetting("AndroidFramerateMode", SETTING(g_Config, iDisplayFramerateMode), 1, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Framerate mode").Choices(androidFramerateModeNames),
	ConfigSetting("VendorBugChecksEnabled", SETTING(g_Config, bVendorBugChecksEnabled), true, CfgFlag::DONT_SAVE)
		.Label(I18NCat::DEVELOPER, "Enable driver bug workarounds"),
	ConfigSetting("SkipBufferEffects", SETTING(g_Config, bSkipBufferEffects), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Skip Buffer Effects").OnChanged(&OnSkipBufferEffectsChanged),
	ConfigSetting("DepthRasterMode", SETTING(g_Config, iDepthRasterMode), &DefaultDepthRaster, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Lens flare occlusion").Choices(depthRasterModeNames),
	ConfigSetting("SoftwareRenderer", SETTING(g_Config, bSoftwareRendering), false, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Software Rendering", "Software Rendering (slow)"),
	ConfigSetting("SoftwareDisableDithering", SETTING(g_Config, bSoftwareDisableDithering), false, CfgFlag::PER_GAME),
	ConfigSetting("HardwareTransformDebug", SETTING(g_Config, bHardwareTransform), true, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Hardware Transform"),
	ConfigSetting("TextureFiltering", SETTING(g_Config, iTexFiltering), 1, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Texture Filter").Choices(texFilteringNames, 1),
	ConfigSetting("Smart2DTexFiltering", SETTING(g_Config, bSmart2DTexFiltering), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Smart 2D texture filtering"),
	ConfigSetting("InternalResolution", SETTING(g_Config, iInternalResolution), &DefaultInternalResolution, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Rendering Resolution").Choices(internalResolutionNames).OnChanged(&OnInternalResolutionChanged),
	ConfigSetting("AndroidHwScale", SETTING(g_Config, iAndroidHwScale), 0, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Display Resolution (HW scaler)").Choices(androidHwScaleNames).OnChanged(&OnAndroidHwScaleChanged),
	ConfigSetting("HighQualityDepth", SETTING(g_Config, bHighQualityDepth), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("FrameSkip", SETTING(g_Config, iFrameSkip), 0, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Frame Skipping").Choices(frameSkipNames),
	ConfigSetting("AutoFrameSkip", SETTING(g_Config, bAutoFrameSkip), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Auto FrameSkip").OnChanged(&OnAutoFrameSkipChanged),
	ConfigSetting("StereoRendering", SETTING(g_Config, bStereoRendering), false, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Stereo rendering"),
	ConfigSetting("StereoToMonoShader", SETTING(g_Config, sStereoToMonoShader), "RedBlue", CfgFlag::PER_GAME),
	// Read before the new settings, which use them as defaults.
	ConfigSetting("FrameRate", SETTING(g_Config, iLegacyFpsLimit1), 0, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("FrameRate2", SETTING(g_Config, iLegacyFpsLimit2), -1, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("AnalogFrameRate", SETTING(g_Config, iLegacyAnalogFpsLimit), 240, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("AltSpeedPercent", SETTING(g_Config, iAltSpeedPercent1), &DefaultAltSpeedPercent1, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Alternative Speed", "Alternative speed").Range(0, 1000, 5).Units("%").ZeroLabel("Unlimited").NegativeLabel("Disabled"),
	ConfigSetting("AltSpeedPercent2", SETTING(g_Config, iAltSpeedPercent2), &DefaultAltSpeedPercent2, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)").Range(0, 1000, 5).Units("%").ZeroLabel("Unlimited").NegativeLabel("Disabled"),
	ConfigSetting("AnalogAltSpeedPercent", SETTING(g_Config, iAnalogAltSpeedPercent), &DefaultAnalogAltSpeedPercent, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Analog alternative speed", "Analog alternative speed (in %)").Range(1, 1000, 5).Units("%"),
#if PPSSPP_PLATFORM(WINDOWS)
	ConfigSetting("RestartRequired", SETTING(g_Config, bRestartRequired), false, CfgFlag::DONT_SAVE),
#endif

	// Most low-performance (and many high performance) mobile GPUs do not support aniso anyway so defaulting to 4 is fine.
	ConfigSetting("AnisotropyLevel", SETTING(g_Config, iAnisotropyLevel), 4, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Anisotropic Filtering").Choices(anisotropyLevelNames).OnChanged(&OnGPUConfigChanged),
	ConfigSetting("MultiSampleLevel", SETTING(g_Config, iMultiSampleLevel), 0, CfgFlag::PER_GAME)  // Number of samples is 1 << iMultiSampleLevel
		.Label(I18NCat::GRAPHICS, "Antialiasing (MSAA)").Choices(msaaLevelNames).OnChanged(&OnRenderResizingChanged),

	ConfigSetting("VertexDecJit", SETTING(g_Config, bVertexDecoderJit), &DefaultCodeGen, CfgFlag::DONT_SAVE | CfgFlag::REPORT),

#ifndef MOBILE_DEVICE
	ConfigSetting("FullScreen", SETTING(g_Config, bFullScreen), false, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Full screen").OnChanged(&OnFullscreenChanged),
	ConfigSetting("FullScreenExclusive", SETTING(g_Config, bAllowFullScreenExclusive), false, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Allow exclusive fullscreen"),
	ConfigSetting("FullScreenMulti", SETTING(g_Config, bFullScreenMulti), false, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Use all displays").OnChanged(&OnFullscreenChanged),
#endif

#if PPSSPP_PLATFORM(IOS)
	ConfigSetting("AppSwitchMode", SETTING(g_Config, iAppSwitchMode), (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "App switching mode").Choices(appSwitchModeNames).OnChanged(&OnAppSwitchModeChanged),
#endif

	ConfigSetting("DisplayCropTo16x9", SETTING(g_Config, bDisplayCropTo16x9), true, CfgFlag::PER_GAME),

	ConfigSetting("SustainedPerformanceMode", SETTING(g_Config, bSustainedPerformanceMode), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Sustained performance mode"),

	ConfigSetting("ReplaceTextures", SETTING(g_Config, bReplaceTextures), true, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::DEVELOPER, "Replace textures"),
	ConfigSetting("SaveNewTextures", SETTING(g_Config, bSaveNewTextures), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::DEVELOPER, "Save new textures"),
	ConfigSetting("IgnoreTextureFilenames", SETTING(g_Config, bIgnoreTextureFilenames), false, CfgFlag::PER_GAME),
	ConfigSetting("ReplacementTextureLoadSpeed", SETTING(g_Config, iReplacementTextureLoadSpeed), 0, CfgFlag::PER_GAME)
		.Label(I18NCat::DEVELOPER, "Replacement texture load speed").Choices(replacementTextureLoadSpeedNames),

	ConfigSetting("TexScalingLevel", SETTING(g_Config, iTexScalingLevel), 1, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Upscale Level").Choices(texScalingLevelNames, 1),
	ConfigSetting("TexScalingType", SETTING(g_Config, iTexScalingType), 0, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "CPU texture upscaler (slow)").Choices(texScalingTypeNames),
	ConfigSetting("TexDeposterize", SETTING(g_Config, bTexDeposterize), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Deposterize"),
	ConfigSetting("TexHardwareScaling", SETTING(g_Config, bTexHardwareScaling), false, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VerticalSync", SETTING(g_Config, bVSync), true, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "VSync"),
	ConfigSetting("LowLatencyPresent", SETTING(g_Config, bLowLatencyPresent), false, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Low latency display"),
	ConfigSetting("BloomHack", SETTING(g_Config, iBloomHack), 0, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Lower resolution for effects").Choices(bloomHackNames),

	ConfigSetting("TextureShader", SETTING(g_Config, sTextureShaderName), "Off", CfgFlag::PER_GAME),
	ConfigSetting("ShaderChainRequires60FPS", SETTING(g_Config, bShaderChainRequires60FPS), false, CfgFlag::PER_GAME),

	ConfigSetting("SkipGPUReadbackMode", SETTING(g_Config, iSkipGPUReadbackMode), false, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::GRAPHICS, "Skip GPU Readbacks").Choices(skipGPUReadbackModeNames),

	ConfigSetting("GfxDebugOutput", SETTING(g_Config, bGfxDebugOutput), false, CfgFlag::DONT_SAVE),
	ConfigSetting("LogFrameDrops", SETTING(g_Config, bLogFrameDrops), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Log Dropped Frame Statistics"),

	ConfigSetting("InflightFrames", SETTING(g_Config, iInflightFrames), 2, CfgFlag::DEFAULT)
		.Label(I18NCat::GRAPHICS, "Buffer graphics commands").Choices(inflightFramesNames, 1),
	ConfigSetting("RenderDuplicateFrames", SETTING(g_Config, bRenderDuplicateFrames), false, CfgFlag::PER_GAME)
		.Label(I18NCat::GRAPHICS, "Render duplicate frames to 60hz"),

	ConfigSetting("MultiThreading", SETTING(g_Config, bRenderMultiThreading), true, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Multi-threaded rendering"),

	ConfigSetting("ShaderCache", SETTING(g_Config, bShaderCache), true, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Enable shader cache"),
	ConfigSetting("GpuLogProfiler", SETTING(g_Config, bGpuLogProfiler), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "GPU log profiler"),

	ConfigSetting("UberShaderVertex", SETTING(g_Config, bUberShaderVertex), true, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Vertex"),

	ConfigSetting("DisplayRefreshRate", SETTING(g_Config, iDisplayRefreshRate), 60, CfgFlag::PER_GAME)
		.Label(I18NCat::DEVELOPER, "Display refresh rate").Range(60, 1000).Units("Hz"),
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

std::string DefaultProAdhocServer() {
	return "socom.cc";
}

bool DefaultAudioMixWithOthers() {
#if PPSSPP_PLATFORM(IOS)
	// On iOS, it's generally best to mix with others by default, since users expect that and the hardware is generally good enough to handle it.
	return true;
#else
	// On Android, we historically took audio focus.
	return false;
#endif
}

static int DefaultAndroidAudioApi() {
	// AAudio isn't stable before Android 8.1 (API 27), Oboe's own cutoff.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) < 27) {
		return (int)AndroidAudioApi::OPENSLES;
	}
	// An Android 11 TV played nothing over Bluetooth when AAudio used MMAP, which should never
	// be used for a Bluetooth route. OpenSL ES worked, also when the headset was connected mid-game.
	// Latency on a TV is mostly the TV's own anyway.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
		return (int)AndroidAudioApi::OPENSLES;
	}
	return (int)AndroidAudioApi::AAUDIO;
}

static constexpr const char *androidAudioApiNames[] = { "AAudio", "OpenSL ES" };
static constexpr const char *audioPlaybackModes[] = { "Smooth (reduces artifacts)", "Classic (lowest latency)" };

static constexpr ConfigSetting soundSettings[] = {
	ConfigSetting("Enable", SETTING(g_Config, bEnableSound), true, CfgFlag::PER_GAME)
		.Label(I18NCat::AUDIO, "Enable Sound"),
	ConfigSetting("ExtraAudioBuffering", SETTING(g_Config, bExtraAudioBuffering), false, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "AudioBufferingForBluetooth", "Bluetooth-friendly buffer (slower)"),
	ConfigSetting("AndroidAudioBackend", SETTING(g_Config, iAndroidAudioApi), &DefaultAndroidAudioApi, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Audio API").Choices(androidAudioApiNames),
	ConfigSetting("AndroidAudioMMap", SETTING(g_Config, bAndroidAudioMMap), true, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Low-latency audio (MMAP)"),
	ConfigSetting("AudioBufferSize", SETTING(g_Config, iSDLAudioBufferSize), 256, CfgFlag::DEFAULT),

	ConfigSetting("FillAudioGaps", SETTING(g_Config, bFillAudioGaps), true, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Fill audio gaps"),
	ConfigSetting("AudioSyncMode", SETTING(g_Config, iAudioPlaybackMode), (int)AudioSyncMode::CLASSIC_PITCH, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Playback mode").Choices(audioPlaybackModes),

	// Legacy volume settings, these get auto upgraded through default handlers on the new settings. NOTE: Must be before the new ones in the order here.
	// The default settings here are still relevant, they will get propagated into the new ones.
	// In 1.21 or so, let's get rid of these.
	ConfigSetting("GlobalVolume", SETTING(g_Config, iLegacyGameVolume), VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("ReverbVolume", SETTING(g_Config, iLegacyReverbVolume), VOLUME_FULL, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),
	ConfigSetting("AchievementSoundVolume", SETTING(g_Config, iLegacyAchievementVolume), 6, CfgFlag::PER_GAME | CfgFlag::DONT_SAVE),

	// Current volume settings.
	ConfigSetting("GameVolume", SETTING(g_Config, iGameVolume), &DefaultGameVolume, CfgFlag::PER_GAME)
		.Label(I18NCat::AUDIO, "Game volume").Range(VOLUME_OFF, VOLUMEHI_FULL).Units("%").ZeroLabel("Mute"),
	ConfigSetting("ReverbRelativeVolume", SETTING(g_Config, iReverbVolume), &DefaultReverbVolume, CfgFlag::PER_GAME)
		.Label(I18NCat::AUDIO, "Reverb volume").Range(VOLUME_OFF, 2 * VOLUMEHI_FULL).Units("%").ZeroLabel("Disabled"),
	ConfigSetting("AltSpeedRelativeVolume", SETTING(g_Config, iAltSpeedVolume), VOLUMEHI_FULL, CfgFlag::PER_GAME)
		.Label(I18NCat::AUDIO, "Alternate speed volume").Range(VOLUME_OFF, VOLUMEHI_FULL).Units("%").ZeroLabel("Mute"),
	ConfigSetting("AchievementVolume", SETTING(g_Config, iAchievementVolume), &DefaultAchievementVolume, CfgFlag::PER_GAME)
		.Label(I18NCat::AUDIO, "Achievement sound volume").Range(VOLUME_OFF, VOLUMEHI_FULL).Units("%").ZeroLabel("Mute"),
	ConfigSetting("UIVolume", SETTING(g_Config, iUIVolume), 75, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "UI volume").Range(VOLUME_OFF, VOLUMEHI_FULL).Units("%").ZeroLabel("Mute"),
	ConfigSetting("GamePreviewVolume", SETTING(g_Config, iGamePreviewVolume), &DefaultGamePreviewVolume, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Game preview volume").Range(VOLUME_OFF, VOLUMEHI_FULL).Units("%").ZeroLabel("Mute"),

	ConfigSetting("AudioDevice", SETTING(g_Config, sAudioDevice), "", CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Device"),
	ConfigSetting("MicDevice", SETTING(g_Config, sMicDevice), "", CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Microphone Device").OnChanged(&OnMicDeviceChanged),
	ConfigSetting("AutoAudioDevice", SETTING(g_Config, bAutoSwitchAudioDevice), true, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Use new audio devices automatically"),
	ConfigSetting("AudioMixWithOthers", SETTING(g_Config, bAudioMixWithOthers), &DefaultAudioMixWithOthers, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Mix audio with other apps").OnChanged(&OnAudioModeChanged),
	ConfigSetting("AudioRespectSilentMode", SETTING(g_Config, bAudioRespectSilentMode), false, CfgFlag::DEFAULT)
		.Label(I18NCat::AUDIO, "Respect silent mode").OnChanged(&OnAudioModeChanged),
	ConfigSetting("UseOldAtrac", SETTING(g_Config, bUseOldAtrac), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Use the old sceAtrac implementation"),
};

static bool DefaultShowTouchControls() {
	switch (System_GetPropertyInt(SYSPROP_DEVICE_TYPE)) {
	case DEVICE_TYPE_MOBILE:
		return !KeyMap::HasBuiltinController(System_GetProperty(SYSPROP_NAME));
	default:
		return false;
	}
}

static constexpr float defaultControlScale = 1.15f;
static constexpr ConfigTouchPos defaultTouchPosShow = { -1.0f, -1.0f, defaultControlScale, true };
static constexpr ConfigTouchPos defaultTouchPosHide = { -1.0f, -1.0f, defaultControlScale, false };

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
	reset(&touchHomeKey);
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

std::unique_ptr<ConfigBlock> TouchControlConfig::NewDefault(std::string_view blockName) const {
	std::unique_ptr<TouchControlConfig> block = std::make_unique<TouchControlConfig>();
	block->ResetToDefault(blockName);
	return block;
}

static constexpr ConfigSetting touchControlSettings[] = {
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
	ConfigSetting("HomeKeyX", "HomeKeyY", "HomeKeyScale", "ShowTouchHome", SETTING(g_Config.touchControlsLandscape, touchHomeKey), defaultTouchPosHide, CfgFlag::PER_GAME),
	ConfigSetting("UnthrottleKeyX", "UnthrottleKeyY", "UnthrottleKeyScale", "ShowTouchUnthrottle", SETTING(g_Config.touchControlsLandscape, touchFastForwardKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("LKeyX", "LKeyY", "LKeyScale", "ShowTouchLTrigger", SETTING(g_Config.touchControlsLandscape, touchLKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RKeyX", "RKeyY", "RKeyScale", "ShowTouchRTrigger", SETTING(g_Config.touchControlsLandscape, touchRKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("PauseKeyX", "PauseKeyY", "PauseKeyScale", "ShowTouchPause", SETTING(g_Config.touchControlsLandscape, touchPauseKey), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("AnalogStickX", "AnalogStickY", "AnalogStickScale", "ShowAnalogStick", SETTING(g_Config.touchControlsLandscape, touchAnalogStick), defaultTouchPosShow, CfgFlag::PER_GAME),
	ConfigSetting("RightAnalogStickX", "RightAnalogStickY", "RightAnalogStickScale", "ShowRightAnalogStick", SETTING(g_Config.touchControlsLandscape, touchRightAnalogStick), defaultTouchPosHide, CfgFlag::PER_GAME),

	ConfigSetting("LeftStickHeadScale", SETTING(g_Config.touchControlsLandscape, fLeftStickHeadScale), CfgFlag::PER_GAME),
	ConfigSetting("RightStickHeadScale", SETTING(g_Config.touchControlsLandscape, fRightStickHeadScale), CfgFlag::PER_GAME),
	ConfigSetting("HideStickBackground", SETTING(g_Config.touchControlsLandscape, bHideStickBackground), CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Hide touch analog stick background circle"),
};

static constexpr const char *touchButtonStyleNames[] = { "Classic", "Thin borders", "Glowing borders" };

static constexpr ConfigSetting controlSettings[] = {
	ConfigSetting("HapticFeedback", SETTING(g_Config, bHapticFeedback), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "HapticFeedback", "Haptic Feedback (vibration)"),
	
#if PPSSPP_PLATFORM(WINDOWS)
	ConfigSetting("IgnoreWindowsKey", SETTING(g_Config, bIgnoreWindowsKey), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Ignore Windows Key"),
#endif

	ConfigSetting("ShowTouchControls", SETTING(g_Config, bShowTouchControls), &DefaultShowTouchControls, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "On-screen touch controls"),

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
	ConfigSetting("TiltUseGyro", SETTING(g_Config, bTiltUseGyro), true, CfgFlag::PER_GAME),

	ConfigSetting("DisableDpadDiagonals", SETTING(g_Config, bDisableDpadDiagonals), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Disable D-Pad diagonals (4-way touch)"),
	ConfigSetting("GamepadOnlyFocused", SETTING(g_Config, bGamepadOnlyFocused), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Ignore gamepads when not focused"),
	ConfigSetting("TouchButtonStyle", SETTING(g_Config, iTouchButtonStyle), 1, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Button style").Choices(touchButtonStyleNames),
	ConfigSetting("TouchButtonOpacity", SETTING(g_Config, iTouchButtonOpacity), 65, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Button Opacity").Range(0, 100).Units("%"),
	ConfigSetting("TouchButtonHideSeconds", SETTING(g_Config, iTouchButtonHideSeconds), 8, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Auto-hide buttons after delay").Range(0, 300).Units("seconds").ZeroLabel("Off"),
	ConfigSetting("AutoCenterTouchAnalog", SETTING(g_Config, bAutoCenterTouchAnalog), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Auto-centering analog stick"),
	ConfigSetting("StickyTouchDPad", SETTING(g_Config, bStickyTouchDPad), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Sticky D-Pad"),

	// Snap touch control position
	ConfigSetting("TouchSnapToGrid", SETTING(g_Config, bTouchSnapToGrid), false, CfgFlag::PER_GAME),
	ConfigSetting("TouchSnapGridSize", SETTING(g_Config, iTouchSnapGridSize), 64, CfgFlag::PER_GAME),

	ConfigSetting("AnalogDeadzone", SETTING(g_Config, fAnalogDeadzone), 0.15f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogInverseDeadzone", SETTING(g_Config, fAnalogInverseDeadzone), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogSensitivity", SETTING(g_Config, fAnalogSensitivity), 1.1f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogIsCircular", SETTING(g_Config, bAnalogIsCircular), false, CfgFlag::PER_GAME),
	ConfigSetting("AnalogAutoRotSpeed", SETTING(g_Config, fAnalogAutoRotSpeed), 8.0f, CfgFlag::PER_GAME),

	// Advanced analog deadzone settings.
	ConfigSetting("AnalogDeadzoneShape", SETTING(g_Config, iAnalogDeadzoneShape), 1, CfgFlag::PER_GAME),  // Default 1 (Square) matches legacy max-norm behavior
	ConfigSetting("AnalogAxialDeadzone", SETTING(g_Config, fAnalogAxialDeadzone), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("AnalogResponseCurve", SETTING(g_Config, iAnalogResponseCurve), 0, CfgFlag::PER_GAME),

	ConfigSetting("AnalogLimiterDeadzone", SETTING(g_Config, fAnalogLimiterDeadzone), 0.6f, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "Analog Limiter").Range(0.0f, 1.0f, 0.10f).Units("/ 1.0"),
	ConfigSetting("AnalogTriggerThreshold", SETTING(g_Config, fAnalogTriggerThreshold), 0.75f, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "Analog trigger threshold").Range(0.02f, 0.98f, 1.0f),
	ConfigSetting("AnalogStickThreshold", SETTING(g_Config, fAnalogStickThreshold), 0.75f, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "Analog stick threshold").Range(0.2f, 0.98f, 1.0f),

	ConfigSetting("AllowMappingCombos", SETTING(g_Config, bAllowMappingCombos), false, CfgFlag::DEFAULT),
	ConfigSetting("StrictComboOrder", SETTING(g_Config, bStrictComboOrder), false, CfgFlag::DEFAULT),

	ConfigSetting("UseMouse", SETTING(g_Config, bMouseControl), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Use Mouse Control"),
	ConfigSetting("ConfineMap", SETTING(g_Config, bMouseConfine), false, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Confine Mouse", "Trap mouse within window/display area"),
	ConfigSetting("MouseSensitivity", SETTING(g_Config, fMouseSensitivity), 0.1f, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Mouse sensitivity").Range(0.01f, 1.0f, 0.01f).Units("x"),
	ConfigSetting("MouseSmoothing", SETTING(g_Config, fMouseSmoothing), 0.9f, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Mouse smoothing").Range(0.0f, 0.95f, 0.05f).Units("x"),
	ConfigSetting("MouseWheelUpDelayMs", SETTING(g_Config, iMouseWheelUpDelayMs), 80, CfgFlag::PER_GAME)
		.Label(I18NCat::CONTROLS, "Mouse wheel button-release delay").Range(10, 300).Units("ms"),

	ConfigSetting("SystemControls", SETTING(g_Config, bSystemControls), true, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "Enable standard shortcut keys"),
	ConfigSetting("RapidFileInterval", SETTING(g_Config, iRapidFireInterval), 5, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "Rapid fire interval").Range(1, 10).Units("frames"),

#if PPSSPP_PLATFORM(WINDOWS)
	ConfigSetting("AllowHIDInput", SETTING(g_Config, bAllowHIDInput), true, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "HID input"),
	ConfigSetting("AllowXInput", SETTING(g_Config, bAllowXInput), true, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "XInput"),
	ConfigSetting("AllowDInput", SETTING(g_Config, bAllowDInput), true, CfgFlag::DEFAULT)
		.Label(I18NCat::CONTROLS, "DirectInput"),
#endif
};

static const std::vector<std::string_view> emptyList;

static constexpr const char *adhocServerRelayModeNames[] = { "Auto", "Yes", "No" };

static constexpr const char *chatButtonPositionNames[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right", "None" };

static constexpr const char *chatScreenPositionNames[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right" };

static constexpr ConfigSetting networkSettings[] = {
	ConfigSetting("EnableWlan", SETTING(g_Config, bEnableWlan), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Enable networking", "Enable networking/wlan (beta)"),
	ConfigSetting("EnableAdhocServer", SETTING(g_Config, bEnableAdhocServer), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Enable built-in ad hoc server"),
	ConfigSetting("proAdhocServer", SETTING(g_Config, sProAdhocServer), &DefaultProAdhocServer, CfgFlag::PER_GAME),
	ConfigSetting("AdhocServerRelayMode", SETTING(g_Config, iAdhocServerRelayMode), (int)AdhocServerRelayMode::Auto, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Try to use server-provided packet relay").Choices(adhocServerRelayModeNames),
	ConfigSetting("AdhocUseUdp", SETTING(g_Config, bAdhocUseUdp), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Connect to adhoc server using UDP (ENet) instead of TCP"),
	ConfigSetting("AdhocRelayModeEnetPdpReliable", SETTING(g_Config, bAdhocRelayModeEnetPdpReliable), true, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Reliable datagram packet delivery on packet relay in UDP mode"),
	ConfigSetting("AdhocServerShowPlayerPorts", SETTING(g_Config, bAdhocServerShowPlayerPorts), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Show player port numbers"),
	ConfigSetting("PortOffset", SETTING(g_Config, iPortOffset), 10000, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Port offset").Range(0, 60000, 100),
	ConfigSetting("PrimaryDNSServer", SETTING(g_Config, sInfrastructureDNSServer), "67.222.156.250", CfgFlag::PER_GAME),
	ConfigSetting("MinTimeout", SETTING(g_Config, iMinTimeout), 0, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Minimum Timeout", "Minimum Timeout (override in ms, 0 = default)").Range(0, 15000, 50).Units("ms"),
	ConfigSetting("ForcedFirstConnect", SETTING(g_Config, bForcedFirstConnect), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Forced First Connect", "Forced First Connect (faster Connect)"),
	ConfigSetting("EnableUPnP", SETTING(g_Config, bEnableUPnP), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Enable UPnP", "Enable UPnP (need a few seconds to detect)"),
	ConfigSetting("UPnPUseOriginalPort", SETTING(g_Config, bUPnPUseOriginalPort), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "UPnP use original port", "UPnP use original port (Enabled = PSP compatibility)"),
	ConfigSetting("InfrastructureUsername", SETTING(g_Config, sInfrastructureUsername), &DefaultInfrastructureUsername, CfgFlag::PER_GAME),
	ConfigSetting("InfrastructureAutoDNS", SETTING(g_Config, bInfrastructureAutoDNS), true, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Autoconfigure"),
	ConfigSetting("AllowSavestateWhileConnected", SETTING(g_Config, bAllowSavestateWhileConnected), false, CfgFlag::DONT_SAVE),
	ConfigSetting("AllowSpeedControlWhileConnected", SETTING(g_Config, bAllowSpeedControlWhileConnected), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Allow speed control while connected (not recommended)"),
	ConfigSetting("DontDownloadInfraJson", SETTING(g_Config, bDontDownloadInfraJson), false, CfgFlag::DONT_SAVE)
		.Label(I18NCat::DEVELOPER, "Don't download infra-dns.json"),
	ConfigSetting("proAdhocServerList", SETTING(g_Config, vCustomAdhocServerList), &emptyList, CfgFlag::DEFAULT),  // Customizable server list.
	ConfigSetting("RelayAdhocServerList", SETTING(g_Config, vCustomAdhocServerListWithRelay), &emptyList, CfgFlag::DEFAULT),  // Customizable server list.
	ConfigSetting("AdhocServerListUrl", SETTING(g_Config, sAdhocServerListUrl), "http://metadata.ppsspp.org/adhoc-servers.json", CfgFlag::DEFAULT),  // URL for the server list. Can be set to a local path too.
	ConfigSetting("EnableNetworkChat", SETTING(g_Config, bEnableNetworkChat), false, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Enable network chat"),
	ConfigSetting("ChatButtonPosition", SETTING(g_Config, iChatButtonPosition), (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Chat Button Position").Choices(chatButtonPositionNames),
	ConfigSetting("ChatScreenPosition", SETTING(g_Config, iChatScreenPosition), (int)ScreenEdgePosition::BOTTOM_LEFT, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Chat Screen Position").Choices(chatScreenPositionNames),
	ConfigSetting("ChatTimestamps", SETTING(g_Config, bChatTimestamps), true, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Show timestamps in chat"),
	ConfigSetting("EnableQuickChat", SETTING(g_Config, bEnableQuickChat), true, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "Enable quick chat"),
	ConfigSetting("QuickChat1", SETTING_IDX(g_Config, sQuickChat, 0), "Quick Chat 1", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat2", SETTING_IDX(g_Config, sQuickChat, 1), "Quick Chat 2", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat3", SETTING_IDX(g_Config, sQuickChat, 2), "Quick Chat 3", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat4", SETTING_IDX(g_Config, sQuickChat, 3), "Quick Chat 4", CfgFlag::PER_GAME),
	ConfigSetting("QuickChat5", SETTING_IDX(g_Config, sQuickChat, 4), "Quick Chat 5", CfgFlag::PER_GAME),
};

static constexpr const char *wlanAdhocChannelNames[] = { "Auto", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };

static constexpr const char *buttonPreferenceNames[] = { "Use O to confirm", "Use X to confirm" };

static constexpr const char *gameLanguageNames[] = { "Auto", "Japanese", "English", "French", "Spanish", "German", "Italian", "Dutch", "Portuguese", "Russian", "Korean", "Chinese (traditional)", "Chinese (simplified)" };

static constexpr const char *pSPModelNames[] = { "PSP-1000", "PSP-2000/3000" };

static constexpr const char *paramDateFormatNames[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY" };

static constexpr const char *paramTimeFormatNames[] = { "24HR", "12HR" };

static constexpr ConfigSetting systemParamSettings[] = {
	ConfigSetting("PSPModel", SETTING(g_Config, iPSPModel), PSP_MODEL_SLIM, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "PSP Model").Choices(pSPModelNames),
	ConfigSetting("PSPFirmwareVersion", SETTING(g_Config, iFirmwareVersion), PSP_DEFAULT_FIRMWARE, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("NickName", SETTING(g_Config, sNickName), "PPSSPP", CfgFlag::PER_GAME),
	ConfigSetting("MacAddress", SETTING(g_Config, sMACAddress), "", CfgFlag::PER_GAME),
	ConfigSetting("GameLanguage", SETTING(g_Config, iLanguage), -1, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::PSPSETTINGS, "Game language").Choices(gameLanguageNames, -1),
	ConfigSetting("ParamTimeFormat", SETTING(g_Config, iTimeFormat), PSP_SYSTEMPARAM_TIME_FORMAT_24HR, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Time Format").Choices(paramTimeFormatNames),
	ConfigSetting("ParamDateFormat", SETTING(g_Config, iDateFormat), PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Date Format").Choices(paramDateFormatNames),
	ConfigSetting("TimeZone", SETTING(g_Config, iTimeZone), 0, CfgFlag::PER_GAME),
	ConfigSetting("DayLightSavings", SETTING(g_Config, bDayLightSavings), (bool) PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Daylight savings"),
	ConfigSetting("ButtonPreference", SETTING(g_Config, iButtonPreference), PSP_SYSTEMPARAM_BUTTON_CROSS, CfgFlag::PER_GAME | CfgFlag::REPORT)
		.Label(I18NCat::SYSTEM, "Confirmation Button").Choices(buttonPreferenceNames).OnChanged(&OnButtonPreferenceChanged),
	ConfigSetting("LockParentalLevel", SETTING(g_Config, iLockParentalLevel), 0, CfgFlag::PER_GAME),
	ConfigSetting("WlanAdhocChannel", SETTING(g_Config, iWlanAdhocChannel), PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC, CfgFlag::PER_GAME)
		.Label(I18NCat::NETWORKING, "WLAN Channel").Choices(wlanAdhocChannelNames),
	ConfigSetting("BypassOSKWithKeyboard", SETTING(g_Config, bBypassOSKWithKeyboard), false, CfgFlag::PER_GAME)
		.Label(I18NCat::SYSTEM, "Use system native keyboard"),
	ConfigSetting("WlanPowerSave", SETTING(g_Config, bWlanPowerSave), (bool) PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF, CfgFlag::PER_GAME),
	ConfigSetting("EncryptSave", SETTING(g_Config, bEncryptSave), true, CfgFlag::PER_GAME | CfgFlag::REPORT),
	ConfigSetting("VshUmdAutoboot", SETTING(g_Config, bVshUmdAutoboot), false, CfgFlag::DEFAULT),
	ConfigSetting("MemStickSize", SETTING(g_Config, iMemStickSizeGB), 16, CfgFlag::DEFAULT)
		.Label(I18NCat::SYSTEM, "Memory Stick size").Range(1, 32).Units("GB"),
	ConfigSetting("FuseID", SETTING(g_Config, sFuseID), "", CfgFlag::DEFAULT),
};

static constexpr ConfigSetting debuggerSettings[] = {
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
	ConfigSetting("ShowBottomTabTitles",SETTING(g_Config, bShowBottomTabTitles), true, CfgFlag::DEFAULT),
	ConfigSetting("ShowDeveloperMenu", SETTING(g_Config, bShowDeveloperMenu), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Show in-game developer menu"),
	ConfigSetting("SkipDeadbeefFilling", SETTING(g_Config, bSkipDeadbeefFilling), false, CfgFlag::DEFAULT),
	ConfigSetting("FuncHashMap", SETTING(g_Config, bFuncHashMap), false, CfgFlag::DEFAULT),
	ConfigSetting("SkipFuncHashMap", SETTING(g_Config, sSkipFuncHashMap), "", CfgFlag::DEFAULT),
	ConfigSetting("MemInfoDetailed", SETTING(g_Config, bDebugMemInfoDetailed), false, CfgFlag::DEFAULT),
	ConfigSetting("AutoSaveLoadSymbols", SETTING(g_Config, bAutoSaveLoadSymbols), false, CfgFlag::DEFAULT)
		.Label(I18NCat::DEVELOPER, "Auto save/load symbols"),
};

static constexpr ConfigSetting jitSettings[] = {
	ConfigSetting("DiscardRegsOnJRRA", SETTING(g_Config, bDiscardRegsOnJRRA), false, CfgFlag::DONT_SAVE | CfgFlag::REPORT),
};

static constexpr ConfigSetting upgradeSettings[] = {
	ConfigSetting("UpgradeMessage", SETTING(g_Config, sUpgradeMessage), "", CfgFlag::DEFAULT),
	ConfigSetting("UpgradeVersion", SETTING(g_Config, sUpgradeVersion), "", CfgFlag::DEFAULT),
	ConfigSetting("DismissedVersion", SETTING(g_Config, sDismissedVersion), "", CfgFlag::DEFAULT),
};

static constexpr ConfigSetting themeSettings[] = {
	ConfigSetting("ThemeName", SETTING(g_Config, sThemeName), "Default", CfgFlag::DEFAULT),
};

static constexpr ConfigSetting vrSettings[] = {
	ConfigSetting("VREnable", SETTING(g_Config, bEnableVR), true, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Virtual reality"),
	ConfigSetting("VREnable6DoF", SETTING(g_Config, bEnable6DoF), false, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "6DoF movement"),
	ConfigSetting("VREnableStereo", SETTING(g_Config, bEnableStereo), false, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Stereoscopic vision (Experimental)"),
	ConfigSetting("VRForce72Hz", SETTING(g_Config, bForce72Hz), true, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Force 72Hz update"),
	ConfigSetting("VRForce", SETTING(g_Config, bForceVR), false, CfgFlag::DEFAULT)
		.Label(I18NCat::VR, "VR camera"),
	ConfigSetting("VRImmersiveMode", SETTING(g_Config, bEnableImmersiveVR), true, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Enable immersive mode"),
	ConfigSetting("VRManualForceVR", SETTING(g_Config, bManualForceVR), false, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Manual switching between flat screen and VR using SCREEN key"),
	ConfigSetting("VRPassthrough", SETTING(g_Config, bPassthrough), false, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Enable passthrough"),
	ConfigSetting("VRRescaleHUD", SETTING(g_Config, bRescaleHUD), true, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Heads-up display detection"),
	ConfigSetting("VRCameraDistance", SETTING(g_Config, fCameraDistance), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraHeight", SETTING(g_Config, fCameraHeight), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraSide", SETTING(g_Config, fCameraSide), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCameraPitch", SETTING(g_Config, fCameraPitch), 0.0f, CfgFlag::PER_GAME),
	ConfigSetting("VRCanvasDistance", SETTING(g_Config, fCanvasDistance), 12.0f, CfgFlag::DEFAULT)
		.Label(I18NCat::VR, "Distance to 2D menus and scenes").Range(1.0f, 15.0f, 1.0f),
	ConfigSetting("VRCanvas3DDistance", SETTING(g_Config, fCanvas3DDistance), 3.0f, CfgFlag::DEFAULT)
		.Label(I18NCat::VR, "Distance to 3D scenes when VR disabled").Range(1.0f, 15.0f, 1.0f),
	ConfigSetting("VRFieldOfView", SETTING(g_Config, fFieldOfViewPercentage), 100.0f, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Field of view scale").Range(100.0f, 200.0f, 10.0f).Units("% of native FoV"),
	ConfigSetting("VRHeadUpDisplayScale", SETTING(g_Config, fHeadUpDisplayScale), 0.3f, CfgFlag::PER_GAME)
		.Label(I18NCat::VR, "Heads-up display scale").Range(0.0f, 1.5f, 0.1f),
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
	{ &g_Config, upgradeSettings, ARRAY_SIZE(upgradeSettings), "Upgrade" },
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

std::map<const void *, ConfigSettingRef> &Config::getPtrLUT() {
	static std::map<const void *, ConfigSettingRef> lut;
	return lut;
}

const ConfigSettingRef *Config::FindSetting(const void *ptr) {
	const std::map<const void *, ConfigSettingRef> &lut = getPtrLUT();
	auto it = lut.find(ptr);
	return it != lut.end() ? &it->second : nullptr;
}

int Config::GetDefaultValueInt(const int *configSetting) {
	const ConfigSettingRef *ref = FindSetting(configSetting);
	if (!ref) {
		return NO_DEFAULT_INT;
	}
	if (ref->block->CanResetToDefault()) {
		// The block's defaults aren't in the setting, so read them from a reset copy.
		std::unique_ptr<ConfigBlock> defaults = ref->block->NewDefault(ref->section);
		_dbg_assert_(ref->setting->type_ == ConfigSetting::Type::TYPE_INT);
		return *(const int *)ref->setting->GetVoidPtr(defaults.get());
	}
	return ref->setting->GetDefaultInt();
}

float Config::GetDefaultValueFloat(const float *configSetting) {
	const ConfigSettingRef *ref = FindSetting(configSetting);
	if (!ref) {
		return NO_DEFAULT_FLOAT;
	}
	if (ref->block->CanResetToDefault()) {
		std::unique_ptr<ConfigBlock> defaults = ref->block->NewDefault(ref->section);
		_dbg_assert_(ref->setting->type_ == ConfigSetting::Type::TYPE_FLOAT);
		return *(const float *)ref->setting->GetVoidPtr(defaults.get());
	}
	return ref->setting->GetDefaultFloat();
}

void Config::Init() {
	// Initialize the pointer->setting lookup map.
	auto &ref = getPtrLUT();
	for (size_t i = 0; i < numSections; ++i) {
		ConfigBlock *configBlock = g_sectionMeta[i].configBlock;
		for (size_t j = 0; j < g_sectionMeta[i].settingsCount; j++) {
			const void *ptr = g_sectionMeta[i].settings[j].GetVoidPtr(configBlock);
			ref[ptr] = ConfigSettingRef{ configBlock, &g_sectionMeta[i].settings[j], g_sectionMeta[i].section };
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
	const bool useControllerIniFilename = controllerIniFilename != nullptr && strlen(controllerIniFilename) > 0;

	const char *ppssppIniFilename = IsVREnabled() ? "ppssppvr.ini" : "ppsspp.ini";
	const char *controlsIniFilename = IsVREnabled() ? "controlsvr.ini" : "controls.ini";

	bool exists;
	iniFilename_ = FindConfigFile(searchPath_, useIniFilename ? iniFileName : ppssppIniFilename, &exists);
	controllerIniFilename_ = FindConfigFile(searchPath_, useControllerIniFilename ? controllerIniFilename : controlsIniFilename, &exists);
}

bool Config::LoadAppendedConfig(bool save) {
	IniFile iniFile;
	if (!iniFile.Load(appendedConfigFileName_)) {
		ERROR_LOG(Log::Config, "Failed to read appended config '%s'.", appendedConfigFileName_.c_str());
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

	INFO_LOG(Log::Config, "Loaded appended config '%s'.", appendedConfigFileName_.c_str());

	if (save) {
		Save("Loaded appended config"); // Let's prevent reset
	}
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

std::string Config::GetConfigAsString() {
	Config::Save("beforecopy");
	std::string temp;
	if (File::ReadTextFileToString(iniFilename_, &temp)) {
		return temp;
	}
	return "";
}

void Config::Load(const char *iniFileName, const char *controllerIniFilename) {
	double startTime = time_now_d();

	if (!bUpdatedInstanceCounter) {
		InitInstanceCounter();
		bUpdatedInstanceCounter = true;
	}

	g_DownloadManager.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

	UpdateIniLocation(iniFileName, controllerIniFilename);

	INFO_LOG(Log::Config, "Loading config: %s", iniFilename_.c_str());
	bSaveSettings = true;

	IniFile iniFile;
	loadedFromFile_ = iniFile.Load(iniFilename_);
	if (!loadedFromFile_) {
		WARN_LOG(Log::Config, "Failed to read '%s'. Setting main config to default.", iniFilename_.c_str());
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
			WARN_LOG(Log::Config, "Forcing JIT off due to unavailablility");
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

	vShortcutUris.clear();
	if (const Section *shortcuts = iniFile.GetSection("Shortcuts")) {
		for (int i = 0; i < MAX_SHORTCUT_URIS; i++) {
			char keyName[64];
			snprintf(keyName, sizeof(keyName), "Uri%d", i);
			std::string uri;
			if (shortcuts->Get(keyName, &uri) && !uri.empty()) {
				vShortcutUris.push_back(uri);
			}
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
		// The ini is user-editable, and std::stof throws - which would take the process down
		// during startup config load. LoadGameConfig already parses this section this way.
		float f = 0.0f;
		if (sscanf(value.c_str(), "%f", &f) == 1) {
			mPostShaderSetting[key] = f;
		} else {
			WARN_LOG(Log::Config, "Invalid float value string for param %s: '%s'", key.c_str(), value.c_str());
		}
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

	CheckForUpdate();

	INFO_LOG(Log::Config, "Loading controller config: %s", controllerIniFilename_.c_str());
	bSaveSettings = true;

	LoadStandardControllerIni();

	// So this is all the way down here to overwrite the controller settings
	// sadly it won't benefit from all the "version conversion" going on up-above
	// but these configs shouldn't contain older versions anyhow
	_dbg_assert_(!IsGameSpecific());

	PostLoadCleanup();

	INFO_LOG(Log::Config, "Config loaded: '%s' (%0.1f ms)", iniFilename_.c_str(), (time_now_d() - startTime) * 1000.0);
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
		WARN_LOG(Log::Config, "Not saving config - secondary instances don't.");
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
			WARN_LOG(Log::Config, "Likely saving config for first time - couldn't read ini '%s'", iniFilename_.c_str());
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
					DEBUG_LOG(Log::Config, "Not saving setting '%.*s' as marked as don't save.", STR_VIEW(meta.settings[j].IniKey()));
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

		if (vShortcutUris.empty()) {
			iniFile.DeleteSection("Shortcuts");
		} else {
			Section *shortcuts = iniFile.GetOrCreateSection("Shortcuts");
			shortcuts->Clear();
			for (size_t i = 0; i < vShortcutUris.size(); ++i) {
				char keyName[64];
				snprintf(keyName, sizeof(keyName), "Uri%d", (int)i);
				shortcuts->Set(keyName, vShortcutUris[i]);
			}
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
			ERROR_LOG(Log::Config, "Error saving config (%s) - can't write ini '%s'", saveReason, iniFilename_.c_str());
			return false;
		}
		INFO_LOG(Log::Config, "Config saved (%s): '%s' (%0.1f ms)", saveReason, iniFilename_.c_str(), (time_now_d() - startTime) * 1000.0);

		if (!IsGameSpecific()) {
			// These settings can be game specific, and so are handled in SaveGameConfig().
			IniFile controllerIniFile;
			if (!controllerIniFile.Load(controllerIniFilename_)) {
				ERROR_LOG(Log::Config, "Error saving controller config - can't read ini first '%s'", controllerIniFilename_.c_str());
			}
			KeyMap::SaveToIni(controllerIniFile);
			if (!controllerIniFile.Save(controllerIniFilename_)) {
				ERROR_LOG(Log::Config, "Error saving config - can't write ini '%s'", controllerIniFilename_.c_str());
				return false;
			}
			INFO_LOG(Log::Config, "Controller config saved: %s", controllerIniFilename_.c_str());
		}

		PostSaveCleanup();
	} else {
		INFO_LOG(Log::Config, "Not saving config");
	}

	return true;
}

// A lot more cleanup tasks should be moved into here, and some of these are severely outdated.
void Config::PostLoadCleanup() {
	// JIT_IR is the same core as JIT now that every native JIT is IR-based, and the settings no
	// longer offer it.
	if (iCpuCore == (int)CPUCore::JIT_IR) {
		iCpuCore = (int)CPUCore::JIT;
	}

	// Override ppsspp.ini JIT value to prevent crashing
	jitForcedOff = DefaultCpuCore() != (int)CPUCore::JIT && (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR);
	if (jitForcedOff) {
		iCpuCore = (int)CPUCore::IR_INTERPRETER;
	}

	// This caps the aniso level exponent to 4 (so 16x.). No hardware supports more anyway.
	iAnisotropyLevel = std::clamp(iAnisotropyLevel, 0, 4);

	if (iGPUBackend == 1) {  // d3d9, no longer supported. Fall back to D3D11.
		iGPUBackend = (int)GPUBackend::DIRECT3D11;
	} else if (iGPUBackend < 0 || iGPUBackend > (int)GPUBackend::METAL) {
		iGPUBackend = (int)DefaultGPUBackend();
	}

#if PPSSPP_PLATFORM(UWP)
	// Enforce D3D11.
	iGPUBackend = (int)GPUBackend::DIRECT3D11;
#endif

	if (!IsBackendEnabled((GPUBackend)iGPUBackend)) {
		ERROR_LOG(Log::G3D, "Backend %d not enabled - switching to platform default", iGPUBackend);
		iGPUBackend = (int)DefaultGPUBackend();
	}

	// Set a default MAC, and correct if it's an old format.
	if (sMACAddress.length() != 17)
		sMACAddress = CreateRandMAC();

	if (bAutoFrameSkip && bSkipBufferEffects) {
		bSkipBufferEffects = false;
	}

	// Automatically silence secondary instances. Could be an option I guess, but meh.
	if (PPSSPP_ID > 1) {
		NOTICE_LOG(Log::Audio, "Secondary instance %d - silencing audio", (int)PPSSPP_ID);
		iGameVolume = 0;
	}

	// Automatically switch away from deprecated setting value.
	if (iTexScalingLevel <= 0) {
		iTexScalingLevel = 1;
	}

	// Remove a legacy value.
	if (sCustomDriver == "Default") {
		sCustomDriver.clear();
	}

	// Squash unsupported screen rotations.
	if (iScreenRotation == ROTATION_LOCKED_VERTICAL180) {
		iScreenRotation = ROTATION_LOCKED_VERTICAL;
	}

	// Clamp save state slot count to somewhat sane limits.
	iSaveStateSlotCount = std::clamp(iSaveStateSlotCount, 1, 100);

	// Three frames in flight showed no benefit over two, so the option is gone. Squash old values.
	iInflightFrames = std::clamp(iInflightFrames, 1, 2);

	// The touch layout editor divides by this, and loops forever drawing the grid if it's 0.
	iTouchSnapGridSize = std::clamp(iTouchSnapGridSize, 2, 256);
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

bool Config::SupportsUpgradeCheck() const {
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MACOS) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS_APP_STORE)
	return true;
#else
	return false;
#endif
}

#if 0
// Use for debugging the version check without messing with the server
#define NEW_VERSION_OVERRIDE "v1.100.3-gaaaaaaaaa"
constexpr int UPDATE_CHECK_FREQ = 1;
#else
constexpr int UPDATE_CHECK_FREQ = 5;
#endif

void Config::CheckForUpdate() {
	if (!bCheckForNewVersion || !SupportsUpgradeCheck()) {
		return;
	}

	const char *gitVer = PPSSPP_GIT_VERSION;
	Version installed(gitVer);
	Version upgrade(sUpgradeVersion);
	const bool versionsValid = installed.IsValid() && upgrade.IsValid();

	// Do this regardless of iRunCount to prevent a silly bug where one might use an older
	// build of PPSSPP, receive an upgrade notice, then start a newer version, and still receive the upgrade notice,
	// even if said newer version is >= the upgrade found online.
	if ((sDismissedVersion == sUpgradeVersion) || (versionsValid && (installed >= upgrade))) {
		sUpgradeMessage.clear();
	}

	// Check for new version on every 10 runs.
	// Sometimes the download may not be finished when the main screen shows (if the user dismisses the
	// splash screen quickly), but then we'll just show the notification next time instead, we store the
	// upgrade number in the ini.

	const bool checkThisTime = iRunCount % UPDATE_CHECK_FREQ == 0;
	if (checkThisTime) {
		const char *versionUrl = "http://www.ppsspp.org/version.json";
		const char *acceptMime = "application/json, text/*; q=0.9, */*; q=0.8";
		g_DownloadManager.StartDownload(versionUrl, Path(), http::RequestFlags::Default, acceptMime, "version", [this](http::Request &download) { VersionJsonDownloadCompleted(download); });
	}
}

void Config::VersionJsonDownloadCompleted(http::Request &download) {
	if (download.ResultCode() != 200) {
		ERROR_LOG(Log::Config, "Failed to download %s: %d", download.url().c_str(), download.ResultCode());
		return;
	}
	std::string data;
	download.buffer().TakeAll(&data);
	if (data.empty()) {
		ERROR_LOG(Log::Config, "Version check: Empty data from server!");
		return;
	}

	json::JsonReader reader(data.c_str(), data.size());
	const json::JsonGet root = reader.root();
	if (!root) {
		ERROR_LOG(Log::Config, "Failed to parse json");
		return;
	}

	std::string version;
	root.getString("version", &version);

	#ifdef NEW_VERSION_OVERRIDE
	version = NEW_VERSION_OVERRIDE;
	#endif

	const char *gitVer = PPSSPP_GIT_VERSION;
	Version installed(gitVer);
	Version upgrade(version);
	Version dismissed(g_Config.sDismissedVersion);

	if (!installed.IsValid()) {
		ERROR_LOG(Log::Config, "Version check: Local version string invalid. Build problems? %s", PPSSPP_GIT_VERSION);
		return;
	}
	if (!upgrade.IsValid()) {
		ERROR_LOG(Log::Config, "Version check: Invalid server version: %s", version.c_str());
		return;
	}

	if (installed >= upgrade) {
		INFO_LOG(Log::Config, "Version check: Already up to date, erasing any upgrade message");
		g_Config.sUpgradeMessage.clear();
		g_Config.sUpgradeVersion = upgrade.ToString();
		g_Config.sDismissedVersion.clear();
		return;
	}

	if (installed < upgrade && dismissed != upgrade) {
		g_Config.sUpgradeMessage = "New version of PPSSPP available!";
		g_Config.sUpgradeVersion = upgrade.ToString();
		g_Config.sDismissedVersion.clear();
	}
}

bool Config::ShowUpgradeReminder() {
	return !sUpgradeMessage.empty() && !sUpgradeVersion.empty() && sUpgradeVersion != sDismissedVersion;
}

void Config::DismissUpgrade() {
	INFO_LOG(Log::Config, "Upgrade dismissed for version %s", sUpgradeVersion.c_str());
	sDismissedVersion = sUpgradeVersion;
	sUpgradeMessage.clear();
}

bool Config::AddShortcutUri(std::string_view uri) {
	auto iter = std::find(vShortcutUris.begin(), vShortcutUris.end(), uri);
	const bool added = iter == vShortcutUris.end();
	if (!added) {
		vShortcutUris.erase(iter);
	}
	vShortcutUris.insert(vShortcutUris.begin(), std::string(uri));
	if ((int)vShortcutUris.size() > MAX_SHORTCUT_URIS) {
		vShortcutUris.resize(MAX_SHORTCUT_URIS);
	}
	return added;
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
		INFO_LOG(Log::Config, "Game config already exists");
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

	INFO_LOG(Log::Config, "Game-specific config saved: '%s'", fullIniFilePath.c_str());

	PostSaveCleanup();
	return true;
}

bool Config::LoadGameConfig(const std::string &gameId) {
	bool exists;
	Path iniFileNameFull = GetGameConfigFilePath(searchPath_, gameId, &exists);
	if (!exists) {
		// Bail if there's no game-specific config.
		DEBUG_LOG(Log::Config, "No game-specific settings found in %s. Using global defaults.", iniFileNameFull.c_str());
		return false;
	}

	// Switch to game specific mode, if we're not in it.
	if (gameId_.empty()) {
		INFO_LOG(Log::Config, "Switching to game specific mode before load: %s", gameId.c_str());
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
			WARN_LOG(Log::Config, "Invalid float value string for param %s: '%s'", k.c_str(), v.c_str());
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

	DEBUG_LOG(Log::Config, "Game-specific config loaded: %s", gameId_.c_str());
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
		float f = 0.0f;
		if (sscanf(v.c_str(), "%f", &f) == 1) {
			mPostShaderSetting[k] = f;
		} else {
			WARN_LOG(Log::Config, "Invalid float value string for param %s: '%s'", k.c_str(), v.c_str());
		}
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
		WARN_LOG(Log::Config, "Failed to read '%s'. Setting controller config to default.", controllerIniFilename_.c_str());
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
	VERBOSE_LOG(Log::Config, "GameTimeTracker::Start(%.*s)", STR_VIEW(gameId));

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

	VERBOSE_LOG(Log::Config, "GameTimeTracker::Stop(%.*s)", STR_VIEW(gameId));

	auto iter = tracker_.find(gameId);
	if (iter != tracker_.end()) {
		if (iter->second.startTime != 0.0) {
			iter->second.totalTimePlayed += time_now_d() - iter->second.startTime;
			iter->second.startTime = 0.0;
		}
		iter->second.lastTimePlayed = time_now_unix_utc();
		return;
	}

	// Can happen if boot gets cancelled. Not worth warn-logging.
	DEBUG_LOG(Log::Config, "GameTimeTracker::Stop called without corresponding GameTimeTracker::Start");
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
