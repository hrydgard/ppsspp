// Copyright (c) 2013- PPSSPP Project.

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

#include <string>

#include "android/jni/app-android.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileUtil.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/StringUtils.h"
#include "Common/UI/ScreenManager.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/Common/PostShader.h"
#include "Core/MIPS/MIPSTracer.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/WebServer.h"
#include "Core/Util/PathUtil.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "UI/GPUDriverTestScreen.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/DevScreens.h"
#include "UI/DriverManagerScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/IconCache.h"
#include "UI/MiscViews.h"
#include "UI/SettingsPage.h"

#if PPSSPP_PLATFORM(ANDROID)

static bool CheckKgslPresent() {
	constexpr auto KgslPath{ "/dev/kgsl-3d0" };

	return access(KgslPath, F_OK) == 0;
}

static bool SupportsCustomDriver() {
	return android_get_device_api_level() >= 28 && CheckKgslPresent();
}

#else

static bool SupportsCustomDriver() {
#ifdef _DEBUG
	return false;  // change to true to debug driver installation on other platforms
#else
	return false;
#endif
}

#endif

static std::string PostShaderTranslateName(std::string_view value) {
	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
		return std::string(ps->T(value, info->name));
	} else {
		return std::string(value);
	}
}

DeveloperToolsScreen::DeveloperToolsScreen(const Path &gamePath)
	: UITabbedBaseDialogScreen(gamePath, &g_Config.iDeveloperSettingsCurrentTab, TabDialogFlags::AddAutoTitles) {}

void DeveloperToolsScreen::BuildTextureReplacementPage(SettingsPage &page) {
	using namespace UI;

	page.Header("Texture Replacement");
	page.Bool(&g_Config.bSaveNewTextures, "Save new textures");
	page.Bool(&g_Config.bReplaceTextures, "Replace textures");

	page.Choice("Create/Open textures.ini file for current game").OnClick([this](UI::EventParams &e) {
		OnOpenTexturesIniFile(e);
	}).EnabledFunc([this] {
		if (!PSP_IsInited())
			return false;

		// Disable the choice to Open/Create if the textures.ini file already exists, and we can't open it due to platform support limitations.
		if (!System_GetPropertyBool(SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR)) {
			if (hasTexturesIni_ == HasIni::MAYBE)
				hasTexturesIni_ = TextureReplacer::IniExists(g_paramSFO.GetDiscID()) ? HasIni::YES : HasIni::NO;
			return hasTexturesIni_ != HasIni::YES;
		}
		return true;
	});

	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		// Best string we have
		page.Choice({ I18NCat::DIALOG, "Show in folder" }).OnClick([](UI::EventParams &) {
			Path path;
			if (PSP_IsInited()) {
				std::string gameID = g_paramSFO.GetDiscID();
				path = GetSysDirectory(DIRECTORY_TEXTURES) / gameID;
			} else {
				// Just show the root textures directory.
				path = GetSysDirectory(DIRECTORY_TEXTURES);
			}
			System_ShowFileInFolder(path);
		});
	}

	static const char *texLoadSpeeds[] = { "Slow (smooth)", "Medium", "Fast", "Instant (may stutter)" };
	page.IntChoice(&g_Config.iReplacementTextureLoadSpeed, "Replacement texture load speed", texLoadSpeeds, 0, ARRAY_SIZE(texLoadSpeeds)).Setup([](View *view) {
		static_cast<PopupMultiChoice *>(view)->SetChoiceIcon(3, ImageID("I_WARNING"));
	});
}

void DeveloperToolsScreen::BuildGeneralPage(SettingsPage &page) {
	using namespace UI;

	page.Header({ I18NCat::SYSTEM, "CPU Core" });

	bool canUseJit = System_GetPropertyBool(SYSPROP_CAN_JIT);
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)
	// In iOS App Store builds, we disable the JIT.

	static const char *cpuCores[] = { "Interpreter", "Dynarec/JIT (recommended)", "IR Interpreter", "JIT using IR" };
	page.IntChoice(&g_Config.iCpuCore, { I18NCat::SYSTEM, "CPU Core" }, cpuCores, 0, ARRAY_SIZE(cpuCores)).OnChange([this](UI::EventParams &e) {
		OnJitAffectingSetting(e);
		g_Config.NotifyUpdatedCpuCore();
	}).Setup([canUseJit](View *view) {
		PopupMultiChoice *core = static_cast<PopupMultiChoice *>(view);
		if (!canUseJit) {
			core->HideChoice(1);
		}
		// Every native JIT is IR-based, so "JIT using IR" is the same as "JIT".
		core->HideChoice(3);
	});

	// Only the interpreter raises these, but don't hide it behind the CPU core choice - it'd just
	// look like the setting had vanished.
	page.Bool(&g_Config.bEnableFPUExceptionTraps, "Enable FPU exceptions");
	// Experimental, so not translated yet.
	page.Bool(&g_Config.bEmulateMediaEngine, "Enable ME emulation for homebrew");

	page.Choice("JIT debug tools").OnClick([this](UI::EventParams &e) {
		OnJitDebugTools(e);
	});
	page.Bool(&g_Config.bShowDeveloperMenu, "Show in-game developer menu");

	page.Custom("Debug overlay", [this](UI::ViewGroup *parent) -> UI::View * {
		AddOverlayList(parent, screenManager());
		return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
	});

	page.Header({ I18NCat::SYSTEM, "General" });

	page.Bool(&g_Config.bEnableLogging, "Enable Logging").OnChange([this](UI::EventParams &e) {
		OnLoggingChanged(e);
	});
	page.Choice("Logging Channels").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new LogConfigScreen());
	});
	page.Bool(&g_Config.bEnableFileLogging, "Log to file").Enabled(&g_Config.bEnableLogging);
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		page.Choice("Show log file in folder").OnClick([](UI::EventParams &e) {
			Path logFilePath = g_logManager.GetLogFilePath();
			if (logFilePath.empty()) {
				ERROR_LOG(Log::System, "No log file path configured.");
				return;
			}
			if (File::Exists(logFilePath)) {
				System_ShowFileInFolder(logFilePath);
			} else {
				System_LaunchUrl(LaunchUrlType::LOCAL_FILE, logFilePath.NavigateUp().ToString());
			}
		});
	}

	page.Bool(&g_Config.bLogFrameDrops, "Log Dropped Frame Statistics");
	if (GetGPUBackend() == GPUBackend::VULKAN) {
		page.Bool(&g_Config.bGpuLogProfiler, "GPU log profiler");
	}

	// allowDebugger_ and canAllowDebugger_ are kept up to date in update().
	page.Bool(&allowDebugger_, "Allow remote debugger").Enabled(&canAllowDebugger_).OnChange([this](UI::EventParams &e) {
		OnRemoteDebugger(e);
	});
	page.Bool(&g_Config.bRemoteDebuggerLocal, "Use locally hosted remote debugger").Enabled(&allowDebugger_);

	page.Bool(&g_Config.bAutoSaveLoadSymbols, "Auto save/load symbols");

	page.Choice("GPI/GPO switches/LEDs").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new GPIGPOScreen(T(I18NCat::DEVELOPER, "GPI/GPO switches/LEDs")));
	});

	page.Bool(&g_Config.bShowSaveLoadIndicator, "Show indicator when saving/loading");

#if PPSSPP_PLATFORM(ANDROID)
	static const char *framerateModes[] = { "Default", "Request 60 Hz", "Force 60Hz" };
	page.IntChoice(&g_Config.iDisplayFramerateMode, { I18NCat::GRAPHICS, "Framerate mode" }, framerateModes, 0, ARRAY_SIZE(framerateModes)).EnabledFunc([]() {
		return System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30;
	}).OnChange([](UI::EventParams &e) {
		System_Notify(SystemNotification::FORCE_RECREATE_ACTIVITY);
	});
#endif

#if PPSSPP_PLATFORM(IOS)
	page.Custom({ I18NCat::MAINSETTINGS, "Moving the memstick directory is NOT recommended on iOS" }, [](UI::ViewGroup *parent) -> UI::View * {
		NoticeView *notice = parent->Add(new NoticeView(NoticeLevel::WARN, T(I18NCat::MAINSETTINGS, "Moving the memstick directory is NOT recommended on iOS"), ""));
		notice->SetWrapText(true);
		return notice;
	});
	page.Choice({ I18NCat::SYSTEM, "Set Memory Stick folder" }).OnClick([this](UI::EventParams &) {
		SetMemStickDirDarwin(GetRequesterToken());
	});
#endif

	// Makes it easy to get savestates out of an iOS device. The file listing shown in MacOS doesn't allow
	// you to descend into directories.
#if PPSSPP_PLATFORM(IOS)
	page.Choice("Copy savestates to memstick root").OnClick([this](UI::EventParams &e) {
		OnCopyStatesToRoot(e);
	});
#endif

	// Reuse strings to the max, heh.
	page.Custom({ I18NCat::DIALOG, "Copy to clipboard" }, [](UI::ViewGroup *parent) -> UI::View * {
		Choice *copy = parent->Add(new Choice(ApplySafeSubstitutions("%1: ppsspp.ini", T(I18NCat::DIALOG, "Copy to clipboard"))));
		copy->OnClick.Add([](UI::EventParams &) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			std::string configStr = g_Config.GetConfigAsString();
			if (!configStr.empty()) {
				System_CopyStringToClipboard(configStr);
				g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions(di->T("Copied to clipboard: %1"), "ppsspp.ini"), 0.0f, "copyToClip");
			}
		});
		return copy;
	});

#if PLATFORM_SUPPORTS_FILE_HANDLER_PLUGINS
	page.Bool(&g_Config.bEnableFileHandlerPlugins, "Enable file handler plugins (insecure)");
#endif
}

void DeveloperToolsScreen::BuildTestsPage(SettingsPage &page) {
	using namespace UI;

	page.Choice("Touchscreen Test").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new TouchTestScreen(gamePath_));
	});
	// page.Choice("Memstick Test").OnClick([this](UI::EventParams &e) { OnMemstickTest(e); });
	page.Choice("Framedump tests").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new FrameDumpTestScreen());
	}).EnabledFunc([] {
		return !PSP_IsInited();
	});
	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		page.Choice("GPU Driver Test").OnClick([this](UI::EventParams &e) {
			OnGPUDriverTest(e);
		});
	}

	// Not useful enough to be made visible.
	/*
	page.Choice("Memory map test").OnClick([this](UI::EventParams &e) {
		MemoryMapTest();
	}).EnabledFunc([] { return PSP_IsInited(); });
	*/
}

void DeveloperToolsScreen::BuildDumpFilePage(SettingsPage &page) {
	using namespace UI;

	auto addDumpType = [&page](DumpFileType type, SettingText text) {
		page.Custom(text, [type, text](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(&g_Config.iDumpFileTypes, (int)type, text.Translate(I18NCat::DEVELOPER)));
		});
	};

	page.Header("Dump files");
	addDumpType(DumpFileType::EBOOT, { I18NCat::DEVELOPER, "Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game" });
	addDumpType(DumpFileType::PRX, "PRX");
	addDumpType(DumpFileType::Atrac3, "Atrac3/3+");
	addDumpType(DumpFileType::PBP_ISO, "ISO from PBP");
}

void DeveloperToolsScreen::BuildHLEPage(SettingsPage &page) {
	using namespace UI;

	page.Bool(&g_Config.bUseOldAtrac, "Use the old sceAtrac implementation");

	auto addModule = [&page](int *flags, DisableHLEFlags flag, const char *modname) {
		page.Custom(SettingText::Raw(modname), [flags, flag, modname](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(flags, (int)flag, modname));
		}).EnabledFunc([] {
			return !PSP_IsInited();
		});
	};

	page.Header("Disable HLE");

	for (int i = 0; i < (int)DisableHLEFlags::Count; i++) {
		DisableHLEFlags flag = (DisableHLEFlags)(1 << i);

		// Show a checkbox, unless the setting has graduated to always disabled.
		if (!(flag & AlwaysDisableHLEFlags())) {
			const HLEModuleMeta *meta = GetHLEModuleMetaByFlag(flag);
			if (meta) {
				addModule(&g_Config.iDisableHLE, flag, meta->modname);
			}
		}
	}

	page.Header("Force-enable HLE");

	for (int i = 0; i < (int)DisableHLEFlags::Count; i++) {
		DisableHLEFlags flag = (DisableHLEFlags)(1 << i);

		// Show a checkbox, only if the setting has graduated to always disabled (and thus it makes sense to force-enable it).
		if (flag & AlwaysDisableHLEFlags()) {
			const HLEModuleMeta *meta = GetHLEModuleMetaByFlag(flag);
			if (meta) {
				addModule(&g_Config.iForceEnableHLE, flag, meta->modname);
			}
		}
	}
}

void DeveloperToolsScreen::BuildMIPSTracerPage(SettingsPage &page) {
	using namespace UI;

	page.Header("MIPSTracer");

	// MIPSTracerEnabled_ is read from the tracer in PreCreateViews.
	page.Bool(&MIPSTracerEnabled_, "MIPSTracer enabled").OnChange([this](UI::EventParams &e) {
		OnMIPSTracerEnabled(e);
	}).EnabledFunc([]() {
		bool temp = g_Config.iCpuCore == static_cast<int>(CPUCore::IR_INTERPRETER) && PSP_IsInited();
		return temp && Core_IsStepping() && coreState != CORE_POWERDOWN;
	});

	page.Choice("Select the file path for the trace").OnClick([this](UI::EventParams &e) {
		OnMIPSTracerPathChanged(e);
	}).EnabledFunc([]() {
		if (!PSP_IsInited())
			return false;
		return true;
	});

	page.Info("Current log file", mipsTracer.get_logging_path());

	page.IntSlider(&mipsTracer.in_storage_capacity, 0x4'0000, 0x40'0000, "Storage capacity").Default(0x10'0000).Step(0x10000).Format("0x%x asm opcodes").OnChange([](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's storage capacity to 0x%x", mipsTracer.in_storage_capacity);
	});
	page.IntSlider(&mipsTracer.in_max_trace_size, 0x1'0000, 0x40'0000, "Max allowed trace size").Default(0x10'0000).Step(0x10000).Format("%d basic blocks").OnChange([](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's max trace size to %d", mipsTracer.in_max_trace_size);
	});

	page.Header("MIPSTracer actions");
	page.Choice("Flush the trace").OnClick([this](UI::EventParams &e) {
		OnMIPSTracerFlushTrace(e);
	});
	page.Choice("Clear the JIT cache").OnClick([this](UI::EventParams &e) {
		OnMIPSTracerClearJitCache(e);
	});
	page.Choice("Clear the MIPSTracer").OnClick([this](UI::EventParams &e) {
		OnMIPSTracerClearTracer(e);
	});
}

void DeveloperToolsScreen::BuildAudioPage(SettingsPage &page) {
	page.Bool(&g_Config.bForceFfmpegForAudioDec, "Use FFMPEG for all compressed audio");
}

void DeveloperToolsScreen::BuildUIPage(SettingsPage &page) {
	using namespace UI;

	UIContext *uiContext = screenManager()->getUIContext();
	page.Choice("Reload UI atlas").OnClick([uiContext](UI::EventParams &) {
		uiContext->InvalidateAtlas();
	});

	// TODO: Move most of these strings out of the SysInfo category.
	const I18NCat si = I18NCat::SYSINFO;

	page.Header({ si, "Icon cache" });
	IconCacheStats iconStats = g_iconCache.GetStats();
	page.Info({ si, "Image data count" }, StringFromFormat("%d", iconStats.cachedCount));
	page.Info({ si, "Texture count" }, StringFromFormat("%d", iconStats.textureCount));
	page.Info({ si, "Data size" }, NiceSizeFormat(iconStats.dataSize));
	page.Choice({ I18NCat::DIALOG, "Clear" }).OnClick([this](UI::EventParams &) {
		g_iconCache.ClearData();
		RecreateViews();
	});

	page.Header({ si, "Font cache" });
	const TextDrawer *text = screenManager()->getUIContext()->Text();
	if (text) {
		page.Info({ si, "Texture count" }, StringFromFormat("%d", text->GetStringCacheSize()));
		page.Info({ si, "Data size" }, NiceSizeFormat(text->GetCacheDataSize()));
	}

	page.Header({ si, "Slider test" });
	page.Custom({ si, "Slider test" }, [this](UI::ViewGroup *parent) -> UI::View * {
		return parent->Add(new Slider(&testSliderValue_, 0, 100, 1, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	});

	// Same encoding as in GameSettingsScreen: None is -1.
	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

	page.Header({ si, "Notification tests" });
	page.Choice({ si, "Error" }).OnClick([](UI::EventParams &) {
		std::string str = "Error " + CodepointToUTF8(0x1F41B) + CodepointToUTF8(0x1F41C) + CodepointToUTF8(0x1F914);
		g_OSD.Show(OSDType::MESSAGE_ERROR, str);
	});
	page.Choice({ si, "Warning" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "Warning, a pretty long warning heading", "Some\nAdditional\nDetail, some of which is very, very long and wide and will need line wrapping on most screens.");
	});
	page.Choice({ si, "Info" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_INFO, "Info, info info info info info info info info info info");
	});
	// This one is clickable
	page.Choice({ si, "Success" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, "Success", 0.0f, "clickable");
		g_OSD.SetClickCallback("clickable", []() {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.google.com/");
		});
	});
	page.Choice({ I18NCat::SYSTEM, "RetroAchievements" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "RetroAchievements warning", "", "I_RETROACHIEVEMENTS_LOGO");
	});
	page.Header({ si, "Progress tests" });
	page.Choice({ si, "30%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 30, 0.0f);
	});
	page.Choice({ si, "100%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 100, 1.0f);
	});
	page.Choice({ si, "N/A%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 0, 0, 0, 0.0f);
	});
	page.Choice({ si, "Success" }).OnClick([](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", true, 0.5f);
	});
	page.Choice({ si, "Failure" }).OnClick([](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", false, 0.5f);
	});
	page.Header({ si, "Achievement tests" });
	page.Choice({ si, "Leaderboard tracker: Show" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "My leaderboard tracker", true);
	});
	page.Choice({ si, "Leaderboard tracker: Update" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "Updated tracker", true);
	});
	page.Choice({ si, "Leaderboard tracker: Hide" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "", false);
	});

	page.Header({ I18NCat::ACHIEVEMENTS, "Notifications" });
	page.IntChoice(&g_Config.iNotificationPos, { I18NCat::SYSTEM, "Notification screen position" }, positions, -1, ARRAY_SIZE(positions)).ChoiceCat(I18NCat::DIALOG);
	page.IntChoice(&g_Config.iAchievementsLeaderboardTrackerPos, { I18NCat::ACHIEVEMENTS, "Leaderboard tracker" }, positions, -1, ARRAY_SIZE(positions)).ChoiceCat(I18NCat::DIALOG);

#ifdef _DEBUG
	page.Bool(&pretendIngame_, { I18NCat::ACHIEVEMENTS, "Pretend to be in-game (for testing)" });
	// Untranslated string because this is debug mode only, only for PPSSPP developers.
	page.Header({ I18NCat::ACHIEVEMENTS, "Assert" });
	page.Choice(SettingText::Raw("Assert")).OnClick([](UI::EventParams &) {
		_dbg_assert_msg_(false, "Test assert message");
	});
#endif
#if PPSSPP_PLATFORM(ANDROID)
	page.Choice({ si, "Exception" }).OnClick([](UI::EventParams &) {
		System_Notify(SystemNotification::TEST_JAVA_EXCEPTION);
	});
#endif
}

void DeveloperToolsScreen::BuildNetworkPage(SettingsPage &page) {
	page.Header({ I18NCat::MAINSETTINGS, "Networking" });
	page.Bool(&g_Config.bDontDownloadInfraJson, "Don't download infra-dns.json");
	// This is shared between RemoteISO and the remote debugger.
	page.IntSlider(&g_Config.iRemoteISOPort, 0, 65535, { I18NCat::REMOTEISO, "Local Server Port", "Local Server Port" }).Default(0).Step(100);
	page.Header({ I18NCat::NETWORKING, "AdHoc server" });
	page.Bool(&g_Config.bAdhocServerShowPlayerPorts, { I18NCat::NETWORKING, "Show player port numbers" });
}

// TODO: Make this generic
extern int DefaultDepthRaster();

void DeveloperToolsScreen::BuildGraphicsPage(SettingsPage &page) {
	using namespace UI;

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	page.Header({ I18NCat::SYSTEM, "General" });
	page.Bool(&g_Config.bVendorBugChecksEnabled, "Enable driver bug workarounds");
	page.Bool(&g_Config.bShaderCache, "Enable shader cache");
	page.Bool(&g_Config.bHardwareTransform, { I18NCat::GRAPHICS, "Hardware Transform" });

	page.IntSlider(&g_Config.iDisplayRefreshRate, 60, 1000, "Display refresh rate").Default(60).Format(T(I18NCat::SYSINFO, "%d Hz"));

	page.Header("Vulkan");
	page.Bool(&g_Config.bVulkanDisableImplicitLayers, "Prevent loading overlays");

	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		page.Bool(&g_Config.bRenderMultiThreading, "Multi-threaded rendering").OnChange([](UI::EventParams &e) {
			// TODO: Not translating yet. Will combine with other translations of settings that need restart.
			g_OSD.Show(OSDType::MESSAGE_WARNING, "Restart required");
		});
	}

	if (GetGPUBackend() == GPUBackend::VULKAN && SupportsCustomDriver()) {
		page.Choice({ I18NCat::GRAPHICS, "AdrenoTools driver manager" }).OnClick([this](UI::EventParams &e) {
			screenManager()->push(new DriverManagerScreen(gamePath_));
		});
	}

	static const char *depthRasterModes[] = { "Auto", "Low", "Off", "Always on" };

	page.IntChoice(&g_Config.iDepthRasterMode, { I18NCat::GRAPHICS, "Lens flare occlusion" }, depthRasterModes, 0, ARRAY_SIZE(depthRasterModes)).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		PopupMultiChoice *depthRasterMode = static_cast<PopupMultiChoice *>(view);
		depthRasterMode->SetDefault(DefaultDepthRaster());
		depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.
	});

	page.Header("Ubershaders");
	if (draw->GetShaderLanguageDesc().bitwiseOps && !draw->GetBugs().Has(Draw::Bugs::UNIFORM_INDEXING_BROKEN)) {
		// If the above if fails, the checkbox is redundant since it'll be force disabled anyway.
		page.Bool(&g_Config.bUberShaderVertex, "Vertex");
	}

	// Experimental, allow some VR features without OpenXR
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		page.Header({ I18NCat::VR, "Virtual reality" });
		page.Bool(&g_Config.bForceVR, { I18NCat::VR, "VR camera" });
	}

	// Experimental, will move to main graphics settings later.
	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	if (multiViewSupported) {
		page.Header({ I18NCat::GRAPHICS, "Stereo rendering" });
		page.Bool(&g_Config.bStereoRendering, { I18NCat::GRAPHICS, "Stereo rendering" });

		page.Custom({ I18NCat::GRAPHICS, "Stereo display shader" }, [this](UI::ViewGroup *parent) -> UI::View * {
			ChoiceWithValueDisplay *stereoShaderChoice = parent->Add(new ChoiceWithValueDisplay(&g_Config.sStereoToMonoShader, T(I18NCat::GRAPHICS, "Stereo display shader"), &PostShaderTranslateName));
			stereoShaderChoice->OnClick.Add([this](EventParams &e) {
				auto gr = GetI18NCategory(I18NCat::GRAPHICS);
				auto procScreen = new PostProcScreen(gr->T("Stereo display shader"), 0, true);
				procScreen->OnChoice.Add([this](EventParams &) {
					RecreateViews();
				});
				if (e.v)
					procScreen->SetPopupOrigin(e.v);
				screenManager()->push(procScreen);
			});
			return stereoShaderChoice;
		}).EnabledFunc(enableStereo);

		const ShaderInfo *shaderInfo = GetPostShaderInfo(g_Config.sStereoToMonoShader);
		if (shaderInfo) {
			for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
				auto &setting = shaderInfo->settings[i];
				if (setting.name.empty()) {
					continue;
				}
				// The value is looked up (and added if missing) when the slider is created, not when the page is built.
				page.Custom({ I18NCat::POSTSHADERS, setting.name.c_str() }, [this, shaderInfo, i](UI::ViewGroup *parent) -> UI::View * {
					auto &setting = shaderInfo->settings[i];
					std::string key = StringFromFormat("%sSettingCurrentValue%d", shaderInfo->section.c_str(), i + 1);
					bool keyExisted = g_Config.mPostShaderSetting.find(key) != g_Config.mPostShaderSetting.end();
					auto &value = g_Config.mPostShaderSetting[key];
					if (!keyExisted)
						value = setting.value;
					return parent->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, setting.value, T(I18NCat::POSTSHADERS, setting.name), setting.step, screenManager()));
				}).EnabledFunc([=] {
					return !g_Config.bSkipBufferEffects && enableStereo();
				});
			}
		}
	}
}

void DeveloperToolsScreen::CreateCrashHistoryTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	std::vector<std::string> reports = Android_GetNativeCrashHistory(20);
    list->Add(new ItemHeader(dev->T("Crash history")));
	if (reports.empty()) {
		list->Add(new TextView(di->T("None")));
		return;
	}
	for (size_t i = 0; i < reports.size(); i++) {
		std::string name = StringFromFormat("Crash %d", (int)i);
		CollapsibleSection *section = list->Add(new CollapsibleSection(name));
		const std::string report = reports[i];
		if (report.size() > 150) {
			section->Add(new Choice(di->T("Copy to clipboard"), ImageID("I_FILE_COPY")))->OnClick.Add([report](UI::EventParams&) {
				System_CopyStringToClipboard(report);
			});
		}
		section->Add(new TextView(report, FLAG_WRAP_TEXT | FLAG_DYNAMIC_ASCII, true));
	}
}

void DeveloperToolsScreen::PreCreateViews() {
	MIPSTracerEnabled_ = mipsTracer.tracing_enabled;
	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
	// Reconsider whenever recreating views.
	hasTexturesIni_ = HasIni::MAYBE;
}

void DeveloperToolsScreen::CreateTabs() {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	auto addPage = [this](const char *tag, std::string_view title, void (DeveloperToolsScreen::*build)(SettingsPage &)) {
		AddSettingsTab(tag, title, ImageID::invalid(), I18NCat::DEVELOPER, [this, build](SettingsPage &page) {
			(this->*build)(page);
		});
	};

	addPage("General", sy->T("General"), &DeveloperToolsScreen::BuildGeneralPage);
	addPage("TextureReplacement", dev->T("Texture Replacement"), &DeveloperToolsScreen::BuildTextureReplacementPage);
	addPage("Graphics", ms->T("Graphics"), &DeveloperToolsScreen::BuildGraphicsPage);
	addPage("Networking", ms->T("Networking"), &DeveloperToolsScreen::BuildNetworkPage);
	addPage("Audio", ms->T("Audio"), &DeveloperToolsScreen::BuildAudioPage);
	addPage("Tests", dev->T("Tests"), &DeveloperToolsScreen::BuildTestsPage);
	addPage("UI", dev->T("UI"), &DeveloperToolsScreen::BuildUIPage);
	addPage("DumpFiles", dev->T("Dump files"), &DeveloperToolsScreen::BuildDumpFilePage);
	addPage("HLE", dev->T("HLE"), &DeveloperToolsScreen::BuildHLEPage);
#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
	addPage("MIPSTracer", dev->T("MIPSTracer"), &DeveloperToolsScreen::BuildMIPSTracerPage);
#endif
//#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		// A list of reports rather than settings, so not a settings page.
		AddTab("Crash history", dev->T("Crash history"), [this](UI::LinearLayout *parent) {
			CreateCrashHistoryTab(parent);
		});
	}
//#endif
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	UIScreen::onFinish(result);
	g_Config.Save("DeveloperToolsScreen::onFinish");
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	System_Notify(SystemNotification::TOGGLE_DEBUG_CONSOLE);
}

void DeveloperToolsScreen::OnOpenTexturesIniFile(UI::EventParams &e) {
	std::string gameID = g_paramSFO.GetDiscID();
	Path generatedFilename;

	if (TextureReplacer::GenerateIni(gameID, generatedFilename)) {
		if (System_GetPropertyBool(SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR)) {
			File::OpenFileInEditor(generatedFilename);
		} else {
			// Can't do much here, let's send a "toast" so the user sees that something happened.
			auto dev = GetI18NCategory(I18NCat::DEVELOPER);
			System_Toast((GetFriendlyPath(generatedFilename) + ": " + dev->T_cstr("Texture ini file created")).c_str());
		}

		hasTexturesIni_ = HasIni::YES;
	}
}

void DeveloperToolsScreen::OnJitDebugTools(UI::EventParams &e) {
	screenManager()->push(new JitDebugScreen());
}

void DeveloperToolsScreen::OnGPUDriverTest(UI::EventParams &e) {
	screenManager()->push(new GPUDriverTestScreen());
}

void DeveloperToolsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

void DeveloperToolsScreen::OnCopyStatesToRoot(UI::EventParams &e) {
	Path savestate_dir = GetSysDirectory(DIRECTORY_SAVESTATE);
	Path root_dir = GetSysDirectory(DIRECTORY_MEMSTICK_ROOT);

	std::vector<File::FileInfo> files;
	GetFilesInDir(savestate_dir, &files, nullptr, 0);

	for (const File::FileInfo &file : files) {
		Path src = file.fullName;
		Path dst = root_dir / file.name;
		INFO_LOG(Log::System, "Copying file '%s' to '%s'", src.c_str(), dst.c_str());
		File::Copy(src, dst);
	}
}

void DeveloperToolsScreen::OnRemoteDebugger(UI::EventParams &e) {
	if (allowDebugger_) {
		// Only needed for connecting from another device, but that's the usual case.
		System_RequestLocalNetworkPermission();
		StartWebServer(WebServerFlags::DEBUGGER);
	} else {
		StopWebServer(WebServerFlags::DEBUGGER);
	}
	// Persist the setting.  Maybe should separate?
	g_Config.bRemoteDebuggerOnStartup = allowDebugger_;
}

void DeveloperToolsScreen::OnMIPSTracerEnabled(UI::EventParams &e) {
	if (MIPSTracerEnabled_) {
		u32 capacity = mipsTracer.in_storage_capacity;
		u32 trace_size = mipsTracer.in_max_trace_size;

		mipsTracer.initialize(capacity, trace_size);
		mipsTracer.start_tracing();
	} else {
		mipsTracer.stop_tracing();
	}
}

void DeveloperToolsScreen::OnMIPSTracerPathChanged(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	System_BrowseForFileSave(
		GetRequesterToken(),
		dev->T("Select the log file"),
		"trace.txt",
		BrowseFileType::ANY,
		[this](std::string_view value, int) {
			mipsTracer.set_logging_path(std::string(value));
			// The views may have been recreated while the dialog was open, so don't keep a pointer to the path view.
			RecreateViews();
		}
	);
}

void DeveloperToolsScreen::OnMIPSTracerFlushTrace(UI::EventParams &e) {
	mipsTracer.flush_to_file();
	// The error logs are emitted inside the tracer
}

void DeveloperToolsScreen::OnMIPSTracerClearJitCache(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the jit cache...");
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

void DeveloperToolsScreen::OnMIPSTracerClearTracer(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the MIPSTracer...");
	mipsTracer.clear();
}

void DeveloperToolsScreen::update() {
	UITabbedBaseDialogScreen::update();
	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);

	// For the UI tab's notification tests.
	if (pretendIngame_) {
		g_OSD.NudgeIngameNotifications();
	}
}

void DeveloperToolsScreen::MemoryMapTest() {
	int sum = 0;
	for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x1000) {
		const u32 addr32 = (u32)addr;
		if (Memory::IsValidAddress(addr32)) {
			sum += Memory::ReadUnchecked_U32(addr32);
		}
	}
	// Just to force the compiler to do things properly.
	INFO_LOG(Log::JIT, "Total sum: %08x", sum);
}

static bool RunMemstickTest(std::string *error) {
	Path testRoot = GetSysDirectory(PSPDirectories::DIRECTORY_CACHE) / "test";

	*error = "N/A";

	File::CreateDir(testRoot);
	if (!File::Exists(testRoot)) {
		return false;
	}

	Path testFilePath = testRoot / "temp.txt";
	File::CreateEmptyFile(testFilePath);

	// Attempt to delete the test root. This should fail since it still contains files.
	File::DeleteDir(testRoot);
	if (!File::Exists(testRoot)) {
		*error = "testroot was deleted with a file in it!";
		return false;
	}

	File::Delete(testFilePath);
	if (File::Exists(testFilePath)) {
		*error = "testfile wasn't deleted";
		return false;
	}

	File::DeleteDir(testRoot);
	if (File::Exists(testRoot)) {
		*error = "testroot wasn't deleted, even when empty";
		return false;
	}

	*error = "passed";
	return true;
}

void DeveloperToolsScreen::OnMemstickTest(UI::EventParams &e) {
	std::string error;
	if (RunMemstickTest(&error)) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, "Memstick test passed", error, 6.0f);
	} else {
		g_OSD.Show(OSDType::MESSAGE_ERROR, "Memstick test failed", error, 6.0f);
	}
}
