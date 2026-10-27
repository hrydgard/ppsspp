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
#include "UI/DeveloperSettingsPages.h"
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
	return false;  // change to true to debug driver installation on other platforms. DO NOT commit as true.
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

enum class HasIni {
	NO,
	YES,
	MAYBE,
};

// The pages' handlers, defined after them.
static void OnLoggingChanged(UI::EventParams &e);
static void OpenTexturesIniFile(HasIni *hasIni);
static void OnJitDebugTools(SettingsPageHost *host);
static void OnGPUDriverTest(SettingsPageHost *host);
static void OnJitAffectingSetting(UI::EventParams &e);
#if PPSSPP_PLATFORM(IOS)
static void OnCopyStatesToRoot(UI::EventParams &e);
#endif
static void SetRemoteDebuggerAllowed(bool allow);
#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
static void SetMIPSTracerEnabled(bool enabled);
static void OnMIPSTracerPathChanged(SettingsPageHost *host);
static void OnMIPSTracerFlushTrace(UI::EventParams &e);
static void OnMIPSTracerClearJitCache(UI::EventParams &e);
static void OnMIPSTracerClearTracer(UI::EventParams &e);
#endif

static void BuildTextureReplacementPage(SettingsPage *page) {
	using namespace UI;

	page->Header("Texture Replacement");
	page->Setting(&g_Config.bSaveNewTextures);
	page->Setting(&g_Config.bReplaceTextures);

	// Checked once per build of the page, when first needed.
	std::shared_ptr<HasIni> hasTexturesIni = std::make_shared<HasIni>(HasIni::MAYBE);
	page->Choice("Create/Open textures.ini file for current game").OnClick([hasTexturesIni](UI::EventParams &e) {
		OpenTexturesIniFile(hasTexturesIni.get());
	}).EnabledFunc([hasTexturesIni] {
		if (!PSP_IsInited())
			return false;

		// Disable the choice to Open/Create if the textures.ini file already exists, and we can't open it due to platform support limitations.
		if (!System_GetPropertyBool(SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR)) {
			if (*hasTexturesIni == HasIni::MAYBE)
				*hasTexturesIni = TextureReplacer::IniExists(g_paramSFO.GetDiscID()) ? HasIni::YES : HasIni::NO;
			return *hasTexturesIni != HasIni::YES;
		}
		return true;
	});

	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		// Best string we have
		page->Choice({ I18NCat::DIALOG, "Show in folder" }).OnClick([](UI::EventParams &) {
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

	page->Setting(&g_Config.iReplacementTextureLoadSpeed).Setup([](View *view) {
		static_cast<PopupMultiChoice *>(view)->SetChoiceIcon(3, ImageID("I_WARNING"));
	});
}

static void BuildGeneralPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Header({ I18NCat::SYSTEM, "CPU Core" });

	bool canUseJit = System_GetPropertyBool(SYSPROP_CAN_JIT);
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)
	// In iOS App Store builds, we disable the JIT.

	page->Setting(&g_Config.iCpuCore).OnChange([](UI::EventParams &e) {
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
	page->Setting(&g_Config.bEnableFPUExceptionTraps);
	// Experimental, so not translated yet.
	page->Setting(&g_Config.bEmulateMediaEngine);

	page->Choice("JIT debug tools").OpensScreen().OnClick([host](UI::EventParams &e) {
		OnJitDebugTools(host);
	});
	page->Setting(&g_Config.bShowDeveloperMenu);

	page->Custom("Debug overlay", [host](UI::ViewGroup *parent) -> UI::View * {
		AddOverlayList(parent, host->PageScreenManager());
		return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
	});

	page->Header({ I18NCat::MAINSETTINGS, "General" });

	page->Bool(&g_Config.bEnableLogging, "Enable Logging").OnChange(&OnLoggingChanged);
	page->Choice("Logging Channels").OpensScreen().OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new LogConfigScreen());
	});
	page->Setting(&g_Config.bEnableFileLogging).Enabled(&g_Config.bEnableLogging);
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		page->Choice("Show log file in folder").OnClick([](UI::EventParams &e) {
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

	page->Setting(&g_Config.bLogFrameDrops);
	if (GetGPUBackend() == GPUBackend::VULKAN) {
		page->Setting(&g_Config.bGpuLogProfiler);
	}

	auto debuggerAllowed = [] {
		return !WebServerStopped(WebServerFlags::DEBUGGER);
	};
	page->Bool("Allow remote debugger", debuggerAllowed, &SetRemoteDebuggerAllowed).EnabledFunc([] {
		return !WebServerStopping(WebServerFlags::DEBUGGER);
	});
	page->Setting(&g_Config.bRemoteDebuggerLocal).EnabledFunc(debuggerAllowed);

	page->Setting(&g_Config.bAutoSaveLoadSymbols);

	page->Choice("GPI/GPO switches/LEDs").OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new GPIGPOScreen(T(I18NCat::DEVELOPER, "GPI/GPO switches/LEDs")));
	});

#if PPSSPP_PLATFORM(ANDROID)
	page->Setting(&g_Config.iDisplayFramerateMode).EnabledFunc([]() {
		return System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30;
	}).OnChange([](UI::EventParams &e) {
		System_Notify(SystemNotification::FORCE_RECREATE_ACTIVITY);
	});
#endif

#if PPSSPP_PLATFORM(IOS)
	page->Custom({ I18NCat::MEMSTICK, "Moving the memstick directory is NOT recommended on iOS" }, [](UI::ViewGroup *parent) -> UI::View * {
		NoticeView *notice = parent->Add(new NoticeView(NoticeLevel::WARN, T(I18NCat::MEMSTICK, "Moving the memstick directory is NOT recommended on iOS"), ""));
		notice->SetWrapText(true);
		return notice;
	});
	page->Choice({ I18NCat::SYSTEM, "Set Memory Stick folder" }).OnClick([host](UI::EventParams &) {
		SetMemStickDirDarwin(host->PageRequesterToken());
	});
#endif

	// Makes it easy to get savestates out of an iOS device. The file listing shown in MacOS doesn't allow
	// you to descend into directories.
#if PPSSPP_PLATFORM(IOS)
	page->Choice("Copy savestates to memstick root").OnClick(&OnCopyStatesToRoot);
#endif

	// Reuse strings to the max, heh.
	page->Custom({ I18NCat::DIALOG, "Copy to clipboard" }, [](UI::ViewGroup *parent) -> UI::View * {
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
	page->Setting(&g_Config.bEnableFileHandlerPlugins);
#endif
}

static void BuildTestsPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Choice("Touchscreen Test").OpensScreen().OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new TouchTestScreen(host->PageGamePath()));
	});
	page->Choice("Framedump tests").OpensScreen().OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new FrameDumpTestScreen());
	}).EnabledFunc([] {
		return !PSP_IsInited();
	});
	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		page->Choice("GPU Driver Test").OpensScreen().OnClick([host](UI::EventParams &e) {
			OnGPUDriverTest(host);
		});
	}

}

static void BuildDumpFilePage(SettingsPage *page) {
	using namespace UI;

	auto addDumpType = [page](DumpFileType type, SettingText text) {
		page->Custom(text, [type, text](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(&g_Config.iDumpFileTypes, (int)type, text.Translate(I18NCat::DEVELOPER)));
		});
	};

	page->Header("Dump files");
	addDumpType(DumpFileType::EBOOT, { I18NCat::DEVELOPER, "Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game" });
	addDumpType(DumpFileType::PRX, "PRX");
	addDumpType(DumpFileType::Atrac3, "Atrac3/3+");
	addDumpType(DumpFileType::PBP_ISO, "ISO from PBP");
}

static void BuildHLEPage(SettingsPage *page) {
	using namespace UI;

	page->Setting(&g_Config.bUseOldAtrac);

	auto addModule = [page](int *flags, DisableHLEFlags flag, const char *modname) {
		page->Custom(SettingText::Raw(modname), [flags, flag, modname](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(flags, (int)flag, modname));
		}).EnabledFunc([] {
			return !PSP_IsInited();
		});
	};

	page->Header("Disable HLE");

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

	page->Header("Force-enable HLE");

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

#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
static void BuildMIPSTracerPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Header("MIPSTracer");

	page->Bool("MIPSTracer enabled", [] { return mipsTracer.tracing_enabled; }, &SetMIPSTracerEnabled).EnabledFunc([]() {
		bool temp = g_Config.iCpuCore == static_cast<int>(CPUCore::IR_INTERPRETER) && PSP_IsInited();
		return temp && Core_IsStepping() && coreState != CORE_POWERDOWN;
	});

	page->Choice("Select the file path for the trace").OnClick([host](UI::EventParams &e) {
		OnMIPSTracerPathChanged(host);
	}).EnabledFunc([]() {
		if (!PSP_IsInited())
			return false;
		return true;
	});

	page->Info("Current trace file", mipsTracer.get_logging_path());

	page->IntSlider(&mipsTracer.in_storage_capacity, 0x4'0000, 0x40'0000, "Storage capacity").Default(0x10'0000).Step(0x10000).Format("0x%x asm opcodes").OnChange([](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's storage capacity to 0x%x", mipsTracer.in_storage_capacity);
	});
	page->IntSlider(&mipsTracer.in_max_trace_size, 0x1'0000, 0x40'0000, "Max allowed trace size").Default(0x10'0000).Step(0x10000).Format("%d basic blocks").OnChange([](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's max trace size to %d", mipsTracer.in_max_trace_size);
	});

	page->Header("MIPSTracer actions");
	page->Choice("Flush the trace").OnClick(&OnMIPSTracerFlushTrace);
	page->Choice("Clear the JIT cache").OnClick(&OnMIPSTracerClearJitCache);
	page->Choice("Clear the MIPSTracer").OnClick(&OnMIPSTracerClearTracer);
}
#endif

static void BuildAudioPage(SettingsPage *page) {
	page->Bool(&g_Config.bForceFfmpegForAudioDec, "Use FFMPEG for all compressed audio");
}

static void BuildUIPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	// Looked up when clicked, the UI context is recreated along with the graphics.
	page->Choice("Reload UI atlas").OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->getUIContext()->InvalidateAtlas();
	});

	// TODO: Move most of these strings out of the SysInfo category.
	const I18NCat si = I18NCat::SYSINFO;

	page->Header({ si, "Icon cache" });
	IconCacheStats iconStats = g_iconCache.GetStats();
	page->Info({ si, "Image data count" }, StringFromFormat("%d", iconStats.cachedCount));
	page->Info({ si, "Texture count" }, StringFromFormat("%d", iconStats.textureCount));
	page->Info({ si, "Data size" }, NiceSizeFormat(iconStats.dataSize));
	page->Choice({ I18NCat::DIALOG, "Clear" }).OnClick([host](UI::EventParams &) {
		g_iconCache.ClearData();
		host->RecreatePageViews();
	});

	page->Header({ si, "Font cache" });
	const TextDrawer *text = host->PageScreenManager()->getUIContext()->Text();
	if (text) {
		page->Info({ si, "Texture count" }, StringFromFormat("%d", text->GetStringCacheSize()));
		page->Info({ si, "Data size" }, NiceSizeFormat(text->GetCacheDataSize()));
	}

	page->Header({ si, "Slider test" });
	page->Custom({ si, "Slider test" }, [](UI::ViewGroup *parent) -> UI::View * {
		static int testSliderValue = 0;
		return parent->Add(new Slider(&testSliderValue, 0, 100, 1, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	});

	page->Header({ si, "Notification tests" });
	page->Choice({ si, "Error" }).OnClick([](UI::EventParams &) {
		std::string str = "Error " + CodepointToUTF8(0x1F41B) + CodepointToUTF8(0x1F41C) + CodepointToUTF8(0x1F914);
		g_OSD.Show(OSDType::MESSAGE_ERROR, str);
	});
	page->Choice({ si, "Warning" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "Warning, a pretty long warning heading", "Some\nAdditional\nDetail, some of which is very, very long and wide and will need line wrapping on most screens.");
	});
	page->Choice({ si, "Info" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_INFO, "Info, info info info info info info info info info info");
	});
	// This one is clickable
	page->Choice({ si, "Success" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, "Success", 0.0f, "clickable");
		g_OSD.SetClickCallback("clickable", []() {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.google.com/");
		});
	});
	page->Choice({ I18NCat::SYSTEM, "RetroAchievements" }).OnClick([](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "RetroAchievements warning", "", "I_RETROACHIEVEMENTS_LOGO");
	});
	page->Header({ si, "Progress tests" });
	page->Choice({ si, "30%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 30, 0.0f);
	});
	page->Choice({ si, "100%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 100, 1.0f);
	});
	page->Choice({ si, "N/A%" }).OnClick([](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 0, 0, 0, 0.0f);
	});
	page->Choice({ si, "Success" }).OnClick([](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", true, 0.5f);
	});
	page->Choice({ si, "Failure" }).OnClick([](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", false, 0.5f);
	});
	page->Header({ si, "Achievement tests" });
	page->Choice({ si, "Leaderboard tracker: Show" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "My leaderboard tracker", true);
	});
	page->Choice({ si, "Leaderboard tracker: Update" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "Updated tracker", true);
	});
	page->Choice({ si, "Leaderboard tracker: Hide" }).OnClick([](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "", false);
	});

	page->Header({ I18NCat::ACHIEVEMENTS, "Notifications" });
	page->Setting(&g_Config.iNotificationPos);
	page->Setting(&g_Config.iAchievementsLeaderboardTrackerPos);

#ifdef _DEBUG
	page->Bool({ I18NCat::ACHIEVEMENTS, "Pretend to be in-game (for testing)" }, [] {
		return g_OSD.PretendIngame();
	}, [](bool pretend) {
		g_OSD.SetPretendIngame(pretend);
	});
	// Untranslated string because this is debug mode only, only for PPSSPP developers.
	page->Header({ I18NCat::ACHIEVEMENTS, "Assert" });
	page->Choice(SettingText::Raw("Assert")).OnClick([](UI::EventParams &) {
		_dbg_assert_msg_(false, "Test assert message");
	});
#endif
#if PPSSPP_PLATFORM(ANDROID)
	page->Choice({ si, "Exception" }).OnClick([](UI::EventParams &) {
		System_Notify(SystemNotification::TEST_JAVA_EXCEPTION);
	});
#endif
}

static void BuildNetworkPage(SettingsPage *page) {
	page->Header({ I18NCat::MAINSETTINGS, "Networking" });
	page->Setting(&g_Config.bDontDownloadInfraJson);
	// This is shared between RemoteISO and the remote debugger.
	page->Setting(&g_Config.iRemoteISOPort);
	page->Header({ I18NCat::NETWORKING, "AdHoc server" });
	page->Setting(&g_Config.bAdhocServerShowPlayerPorts);
}

// TODO: Make this generic
extern int DefaultDepthRaster();

static void BuildGraphicsPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	Draw::DrawContext *draw = host->PageScreenManager()->getDrawContext();

	page->Header({ I18NCat::MAINSETTINGS, "General" });
	page->Setting(&g_Config.bVendorBugChecksEnabled);
	page->Setting(&g_Config.bShaderCache);
	page->Setting(&g_Config.bHardwareTransform);

	page->Setting(&g_Config.iDisplayRefreshRate);

	page->Header({ I18NCat::GRAPHICS, "Vulkan" });
	page->Setting(&g_Config.bVulkanDisableImplicitLayers);

	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		page->Setting(&g_Config.bRenderMultiThreading).OnChange([](UI::EventParams &e) {
			// TODO: Not translating yet. Will combine with other translations of settings that need restart.
			g_OSD.Show(OSDType::MESSAGE_WARNING, "Restart required");
		});
	}

	if (GetGPUBackend() == GPUBackend::VULKAN && SupportsCustomDriver()) {
		page->Choice({ I18NCat::GRAPHICS, "AdrenoTools driver manager" }).OpensScreen().OnClick([host](UI::EventParams &e) {
			host->PageScreenManager()->push(new DriverManagerScreen(host->PageGamePath()));
		});
	}

	page->Setting(&g_Config.iDepthRasterMode).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		PopupMultiChoice *depthRasterMode = static_cast<PopupMultiChoice *>(view);
		depthRasterMode->SetDefault(DefaultDepthRaster());
		depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.
	});

	page->Header("Ubershaders");
	if (draw->GetShaderLanguageDesc().bitwiseOps && !draw->GetBugs().Has(Draw::Bugs::UNIFORM_INDEXING_BROKEN)) {
		// If the above if fails, the checkbox is redundant since it'll be force disabled anyway.
		page->Setting(&g_Config.bUberShaderVertex);
	}

	// Experimental, allow some VR features without OpenXR
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		page->Header({ I18NCat::VR, "Virtual reality" });
		page->Setting(&g_Config.bForceVR);
	}

	// Experimental, will move to main graphics settings later.
	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	if (multiViewSupported) {
		page->Header({ I18NCat::GRAPHICS, "Stereo rendering" });
		page->Setting(&g_Config.bStereoRendering);

		page->Custom({ I18NCat::GRAPHICS, "Stereo display shader" }, [host](UI::ViewGroup *parent) -> UI::View * {
			ChoiceWithValueDisplay *stereoShaderChoice = parent->Add(new ChoiceWithValueDisplay(&g_Config.sStereoToMonoShader, T(I18NCat::GRAPHICS, "Stereo display shader"), &PostShaderTranslateName));
			stereoShaderChoice->OnClick.Add([host](EventParams &e) {
				auto gr = GetI18NCategory(I18NCat::GRAPHICS);
				auto procScreen = new PostProcScreen(gr->T("Stereo display shader"), 0, true);
				procScreen->OnChoice.Add([host](EventParams &) {
					host->RecreatePageViews();
				});
				if (e.v)
					procScreen->SetPopupOrigin(e.v);
				host->PageScreenManager()->push(procScreen);
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
				page->Custom({ I18NCat::POSTSHADERS, setting.name.c_str() }, [host, shaderInfo, i](UI::ViewGroup *parent) -> UI::View * {
					auto &setting = shaderInfo->settings[i];
					std::string key = StringFromFormat("%sSettingCurrentValue%d", shaderInfo->section.c_str(), i + 1);
					bool keyExisted = g_Config.mPostShaderSetting.find(key) != g_Config.mPostShaderSetting.end();
					auto &value = g_Config.mPostShaderSetting[key];
					if (!keyExisted)
						value = setting.value;
					return parent->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, setting.value, T(I18NCat::POSTSHADERS, setting.name), setting.step, host->PageScreenManager()));
				}).EnabledFunc([=] {
					return !g_Config.bSkipBufferEffects && enableStereo();
				});
			}
		}
	}
}

static void CreateCrashHistoryList(UI::ViewGroup *crashHistory) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	std::vector<std::string> reports = Android_GetNativeCrashHistory(20);
	if (reports.empty()) {
		crashHistory->Add(new TextView(di->T("None")));
		return;
	}
	for (size_t i = 0; i < reports.size(); i++) {
		std::string name = StringFromFormat("Crash %d", (int)i);
		CollapsibleSection *section = crashHistory->Add(new CollapsibleSection(name));
		const std::string report = reports[i];
		if (report.size() > 150) {
			section->Add(new Choice(di->T("Copy to clipboard"), ImageID("I_FILE_COPY")))->OnClick.Add([report](UI::EventParams&) {
				System_CopyStringToClipboard(report);
			});
		}
		section->Add(new TextView(report, FLAG_WRAP_TEXT | FLAG_DYNAMIC_ASCII, true));
	}
}

static void BuildCrashHistoryPage(SettingsPage *page) {
	// A list of reports rather than settings.
	page->Custom({ I18NCat::DEVELOPER, "Crash history" }, [](UI::ViewGroup *parent) -> UI::View * {
		CreateCrashHistoryList(parent);
		return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
	});
}

void BuildDeveloperToolsPage(SettingsPage *page) {
	auto link = [page](SettingText title, void (*build)(SettingsPage *)) {
		page->Link(title, I18NCat::DEVELOPER, build);
	};

	link({ I18NCat::MAINSETTINGS, "General" }, &BuildGeneralPage);
	link({ I18NCat::DEVELOPER, "Texture Replacement" }, &BuildTextureReplacementPage);
	link({ I18NCat::MAINSETTINGS, "Graphics" }, &BuildGraphicsPage);
	link({ I18NCat::MAINSETTINGS, "Networking" }, &BuildNetworkPage);
	link({ I18NCat::MAINSETTINGS, "Audio" }, &BuildAudioPage);
	link({ I18NCat::DEVELOPER, "Tests" }, &BuildTestsPage);
	link({ I18NCat::SYSTEM, "UI" }, &BuildUIPage);
	link({ I18NCat::DEVELOPER, "Dump files" }, &BuildDumpFilePage);
	link({ I18NCat::DEVELOPER, "HLE" }, &BuildHLEPage);
#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
	link({ I18NCat::DEVELOPER, "MIPSTracer" }, &BuildMIPSTracerPage);
#endif
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		link({ I18NCat::DEVELOPER, "Crash history" }, &BuildCrashHistoryPage);
	}
}

static void OnLoggingChanged(UI::EventParams &e) {
	System_Notify(SystemNotification::TOGGLE_DEBUG_CONSOLE);
}

static void OpenTexturesIniFile(HasIni *hasIni) {
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

		*hasIni = HasIni::YES;
	}
}

static void OnJitDebugTools(SettingsPageHost *host) {
	host->PageScreenManager()->push(new JitDebugScreen());
}

static void OnGPUDriverTest(SettingsPageHost *host) {
	host->PageScreenManager()->push(new GPUDriverTestScreen());
}

static void OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

#if PPSSPP_PLATFORM(IOS)
static void OnCopyStatesToRoot(UI::EventParams &e) {
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
#endif

static void SetRemoteDebuggerAllowed(bool allow) {
	if (allow) {
		// Only needed for connecting from another device, but that's the usual case.
		System_RequestLocalNetworkPermission();
		StartWebServer(WebServerFlags::DEBUGGER);
	} else {
		StopWebServer(WebServerFlags::DEBUGGER);
	}
	// Persist the setting.  Maybe should separate?
	g_Config.bRemoteDebuggerOnStartup = allow;
}

#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
static void SetMIPSTracerEnabled(bool enabled) {
	if (enabled) {
		u32 capacity = mipsTracer.in_storage_capacity;
		u32 trace_size = mipsTracer.in_max_trace_size;

		mipsTracer.initialize(capacity, trace_size);
		mipsTracer.start_tracing();
	} else {
		mipsTracer.stop_tracing();
	}
}

static void OnMIPSTracerPathChanged(SettingsPageHost *host) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	System_BrowseForFileSave(
		host->PageRequesterToken(),
		dev->T("Select the trace file"),
		"trace.txt",
		BrowseFileType::ANY,
		[host](std::string_view value, int) {
			mipsTracer.set_logging_path(std::string(value));
			// The views may have been recreated while the dialog was open, so don't keep a pointer to the path view.
			host->RecreatePageViews();
		}
	);
}

static void OnMIPSTracerFlushTrace(UI::EventParams &e) {
	mipsTracer.flush_to_file();
	// The error logs are emitted inside the tracer
}

static void OnMIPSTracerClearJitCache(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the jit cache...");
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

static void OnMIPSTracerClearTracer(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the MIPSTracer...");
	mipsTracer.clear();
}
#endif
