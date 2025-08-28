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

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/System/OSD.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/Common/PostShader.h"
#include "Core/MIPS/MIPSTracer.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/WebServer.h"
#include "UI/GPUDriverTestScreen.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/DevScreens.h"
#include "UI/DriverManagerScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/OnScreenDisplay.h"

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

void DeveloperToolsScreen::CreateTextureReplacementTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	list->Add(new ItemHeader(dev->T("Texture Replacement")));
	list->Add(new CheckBox(&g_Config.bSaveNewTextures, dev->T("Save new textures")));
	list->Add(new CheckBox(&g_Config.bReplaceTextures, dev->T("Replace textures")));

	Choice *createTextureIni = list->Add(new Choice(dev->T("Create/Open textures.ini file for current game")));
	createTextureIni->OnClick.Handle(this, &DeveloperToolsScreen::OnOpenTexturesIniFile);
	createTextureIni->SetEnabledFunc([&] {
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
		list->Add(new Choice(di->T("Show in folder")))->OnClick.Add([=](UI::EventParams &) {
			Path path;
			if (PSP_IsInited()) {
				std::string gameID = g_paramSFO.GetDiscID();
				path = GetSysDirectory(DIRECTORY_TEXTURES) / gameID;
			} else {
				// Just show the root textures directory.
				path = GetSysDirectory(DIRECTORY_TEXTURES);
			}
			System_ShowFileInFolder(path);
			return UI::EVENT_DONE;
		});
	}

	static const char *texLoadSpeeds[] = { "Slow (smooth)", "Medium", "Fast", "Instant (may stutter)" };
	PopupMultiChoice *texLoadSpeed = list->Add(new PopupMultiChoice(&g_Config.iReplacementTextureLoadSpeed, dev->T("Replacement texture load speed"), texLoadSpeeds, 0, ARRAY_SIZE(texLoadSpeeds), I18NCat::DEVELOPER, screenManager()));
	texLoadSpeed->SetChoiceIcon(3, ImageID("I_WARNING"));
}

void DeveloperToolsScreen::CreateGeneralTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	list->Add(new ItemHeader(sy->T("CPU Core")));

	bool canUseJit = System_GetPropertyBool(SYSPROP_CAN_JIT);
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)
	// In iOS App Store builds, we disable the JIT.

	static const char *cpuCores[] = { "Interpreter", "Dynarec/JIT (recommended)", "IR Interpreter", "JIT using IR" };
	PopupMultiChoice *core = list->Add(new PopupMultiChoice(&g_Config.iCpuCore, sy->T("CPU Core"), cpuCores, 0, ARRAY_SIZE(cpuCores), I18NCat::SYSTEM, screenManager()));
	core->OnChoice.Handle(this, &DeveloperToolsScreen::OnJitAffectingSetting);
	core->OnChoice.Add([](UI::EventParams &) {
		g_Config.NotifyUpdatedCpuCore();
		return UI::EVENT_DONE;
	});
	if (!canUseJit) {
		core->HideChoice(1);
		core->HideChoice(3);
	}
	// TODO: Enable "JIT using IR" on more architectures.
#if !PPSSPP_ARCH(X86) && !PPSSPP_ARCH(AMD64) && !PPSSPP_ARCH(ARM64)
	core->HideChoice(3);
#endif

	list->Add(new Choice(dev->T("JIT debug tools")))->OnClick.Handle(this, &DeveloperToolsScreen::OnJitDebugTools);
	list->Add(new CheckBox(&g_Config.bShowDeveloperMenu, dev->T("Show Developer Menu")));

	AddOverlayList(list, screenManager());

	list->Add(new ItemHeader(sy->T("General")));

	list->Add(new CheckBox(&g_Config.bEnableLogging, dev->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	list->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new CheckBox(&g_Config.bEnableFileLogging, dev->T("Log to file")))->SetEnabledPtr(&g_Config.bEnableLogging);
	list->Add(new CheckBox(&g_Config.bLogFrameDrops, dev->T("Log Dropped Frame Statistics")));
	if (GetGPUBackend() == GPUBackend::VULKAN) {
		list->Add(new CheckBox(&g_Config.bGpuLogProfiler, dev->T("GPU log profiler")));
	}

	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
	CheckBox *allowDebugger = new CheckBox(&allowDebugger_, dev->T("Allow remote debugger"));
	list->Add(allowDebugger)->OnClick.Handle(this, &DeveloperToolsScreen::OnRemoteDebugger);
	allowDebugger->SetEnabledPtr(&canAllowDebugger_);

	CheckBox *localDebugger = list->Add(new CheckBox(&g_Config.bRemoteDebuggerLocal, dev->T("Use locally hosted remote debugger")));
	localDebugger->SetEnabledPtr(&allowDebugger_);

	list->Add(new Choice(dev->T("GPI/GPO switches/LEDs")))->OnClick.Add([=](UI::EventParams &e) {
		screenManager()->push(new GPIGPOScreen(dev->T("GPI/GPO switches/LEDs")));
		return UI::EVENT_DONE;
	});

#if PPSSPP_PLATFORM(ANDROID)
	static const char *framerateModes[] = { "Default", "Request 60Hz", "Force 60Hz" };
	PopupMultiChoice *framerateMode = list->Add(new PopupMultiChoice(&g_Config.iDisplayFramerateMode, gr->T("Framerate mode"), framerateModes, 0, ARRAY_SIZE(framerateModes), I18NCat::GRAPHICS, screenManager()));
	framerateMode->SetEnabledFunc([]() { return System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30; });
	framerateMode->OnChoice.Add([](UI::EventParams &e) {
		System_Notify(SystemNotification::FORCE_RECREATE_ACTIVITY);
		return UI::EVENT_DONE;
	});
#endif

#if PPSSPP_PLATFORM(IOS)
	list->Add(new NoticeView(NoticeLevel::WARN, ms->T("Moving the memstick directory is NOT recommended on iOS"), ""));
	list->Add(new Choice(sy->T("Set Memory Stick folder")))->OnClick.Add(
		[=](UI::EventParams &) {
		SetMemStickDirDarwin(GetRequesterToken());
		return UI::EVENT_DONE;
	});
#endif

	// Makes it easy to get savestates out of an iOS device. The file listing shown in MacOS doesn't allow
	// you to descend into directories.
#if PPSSPP_PLATFORM(IOS)
	list->Add(new Choice(dev->T("Copy savestates to memstick root")))->OnClick.Handle(this, &DeveloperToolsScreen::OnCopyStatesToRoot);
#endif
}

void DeveloperToolsScreen::CreateTestsTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	list->Add(new Choice(dev->T("Touchscreen Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnTouchscreenTest);
	// list->Add(new Choice(dev->T("Memstick Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnMemstickTest);
	Choice *frameDumpTests = list->Add(new Choice(dev->T("Framedump tests")));
	frameDumpTests->OnClick.Add([this](UI::EventParams &e) {
		screenManager()->push(new FrameDumpTestScreen());
		return UI::EVENT_DONE;
	});
	frameDumpTests->SetEnabled(!PSP_IsInited());
	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		list->Add(new Choice(dev->T("GPU Driver Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnGPUDriverTest);
	}

	// Not useful enough to be made visible.
	/*
	auto memmapTest = list->Add(new Choice(dev->T("Memory map test")));
	memmapTest->OnClick.Add([this](UI::EventParams &e) {
		MemoryMapTest();
		return UI::EVENT_DONE;
	});
	memmapTest->SetEnabled(PSP_IsInited());
	*/
}

void DeveloperToolsScreen::CreateDumpFileTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	list->Add(new ItemHeader(dev->T("Dump files")));
	list->Add(new BitCheckBox(&g_Config.iDumpFileTypes, (int)DumpFileType::EBOOT, dev->T("Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game")));
	list->Add(new BitCheckBox(&g_Config.iDumpFileTypes, (int)DumpFileType::PRX, dev->T("PRX")));
	list->Add(new BitCheckBox(&g_Config.iDumpFileTypes, (int)DumpFileType::Atrac3, dev->T("Atrac3/3+")));
}

void DeveloperToolsScreen::CreateHLETab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	list->Add(new CheckBox(&g_Config.bUseOldAtrac, dev->T("Use the old sceAtrac implementation")));

	list->Add(new ItemHeader(dev->T("Disable HLE")));

	for (int i = 0; i < (int)DisableHLEFlags::Count; i++) {
		DisableHLEFlags flag = (DisableHLEFlags)(1 << i);

		// Show a checkbox, unless the setting has graduated to always disabled.
		if (!(flag & AlwaysDisableHLEFlags())) {
			const HLEModuleMeta *meta = GetHLEModuleMetaByFlag(flag);
			if (meta) {
				BitCheckBox *checkBox = list->Add(new BitCheckBox(&g_Config.iDisableHLE, (int)flag, meta->modname));
				checkBox->SetEnabled(!PSP_IsInited());
			}
		}
	}

	list->Add(new ItemHeader(dev->T("Force-enable HLE")));

	for (int i = 0; i < (int)DisableHLEFlags::Count; i++) {
		DisableHLEFlags flag = (DisableHLEFlags)(1 << i);

		// Show a checkbox, only if the setting has graduated to always disabled (and thus it makes sense to force-enable it).
		if (flag & AlwaysDisableHLEFlags()) {
			const HLEModuleMeta *meta = GetHLEModuleMetaByFlag(flag);
			if (meta) {
				BitCheckBox *checkBox = list->Add(new BitCheckBox(&g_Config.iForceEnableHLE, (int)flag, meta->modname));
				checkBox->SetEnabled(!PSP_IsInited());
			}
		}
	}
}

void DeveloperToolsScreen::CreateMIPSTracerTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	list->Add(new ItemHeader(dev->T("MIPSTracer")));

	MIPSTracerEnabled_ = mipsTracer.tracing_enabled;
	CheckBox *MIPSTracerEnabled = new CheckBox(&MIPSTracerEnabled_, dev->T("MIPSTracer enabled"));
	list->Add(MIPSTracerEnabled)->OnClick.Handle(this, &DeveloperToolsScreen::OnMIPSTracerEnabled);
	MIPSTracerEnabled->SetEnabledFunc([]() {
		bool temp = g_Config.iCpuCore == static_cast<int>(CPUCore::IR_INTERPRETER) && PSP_IsInited();
		return temp && Core_IsStepping() && coreState != CORE_POWERDOWN;
	});

	Choice *TraceDumpPath = list->Add(new Choice(dev->T("Select the file path for the trace")));
	TraceDumpPath->OnClick.Handle(this, &DeveloperToolsScreen::OnMIPSTracerPathChanged);
	TraceDumpPath->SetEnabledFunc([]() {
		if (!PSP_IsInited())
			return false;
		return true;
	});

	MIPSTracerPath_ = mipsTracer.get_logging_path();
	MIPSTracerPath = list->Add(new InfoItem(dev->T("Current log file"), MIPSTracerPath_));

	PopupSliderChoice* storage_capacity = list->Add(
		new PopupSliderChoice(
			&mipsTracer.in_storage_capacity, 0x4'0000, 0x40'0000, 0x10'0000, dev->T("Storage capacity"), 0x10000, screenManager()
		)
	);
	storage_capacity->SetFormat("0x%x asm opcodes");
	storage_capacity->OnChange.Add([&](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's storage capacity to 0x%x", mipsTracer.in_storage_capacity);
		return UI::EVENT_CONTINUE;
	});

	PopupSliderChoice* trace_max_size = list->Add(
		new PopupSliderChoice(
			&mipsTracer.in_max_trace_size, 0x1'0000, 0x40'0000, 0x10'0000, dev->T("Max allowed trace size"), 0x10000, screenManager()
		)
	);
	trace_max_size->SetFormat("%d basic blocks");
	trace_max_size->OnChange.Add([&](UI::EventParams &) {
		INFO_LOG(Log::JIT, "User changed the tracer's max trace size to %d", mipsTracer.in_max_trace_size);
		return UI::EVENT_CONTINUE;
	});

	Button *FlushTrace = list->Add(new Button(dev->T("Flush the trace")));
	FlushTrace->OnClick.Handle(this, &DeveloperToolsScreen::OnMIPSTracerFlushTrace);

	Button *InvalidateJitCache = list->Add(new Button(dev->T("Clear the JIT cache")));
	InvalidateJitCache->OnClick.Handle(this, &DeveloperToolsScreen::OnMIPSTracerClearJitCache);

	Button *ClearMIPSTracer = list->Add(new Button(dev->T("Clear the MIPSTracer")));
	ClearMIPSTracer->OnClick.Handle(this, &DeveloperToolsScreen::OnMIPSTracerClearTracer);
}

void DeveloperToolsScreen::CreateAudioTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	list->Add(new CheckBox(&g_Config.bForceFfmpegForAudioDec, dev->T("Use FFMPEG for all compressed audio")));
}

void DeveloperToolsScreen::CreateNetworkTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);
	list->Add(new ItemHeader(ms->T("Networking")));
	list->Add(new CheckBox(&g_Config.bDontDownloadInfraJson, dev->T("Don't download infra-dns.json")));

	// This is shared between RemoteISO and the remote debugger.
	PopupSliderChoice *portChoice = new PopupSliderChoice(&g_Config.iRemoteISOPort, 0, 65535, 0, ri->T("Local Server Port", "Local Server Port"), 100, screenManager());
	list->Add(portChoice);
}

void DeveloperToolsScreen::CreateGraphicsTab(UI::LinearLayout *list) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto si = GetI18NCategory(I18NCat::SYSINFO);

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	list->Add(new ItemHeader(sy->T("General")));
	list->Add(new CheckBox(&g_Config.bVendorBugChecksEnabled, dev->T("Enable driver bug workarounds")));
	list->Add(new CheckBox(&g_Config.bShaderCache, dev->T("Enable shader cache")));

	static const char *ffModes[] = { "Render all frames", "", "Frame Skipping" };
	PopupMultiChoice *ffMode = list->Add(new PopupMultiChoice(&g_Config.iFastForwardMode, dev->T("Fast-forward mode"), ffModes, 0, ARRAY_SIZE(ffModes), I18NCat::GRAPHICS, screenManager()));
	ffMode->SetEnabledFunc([]() { return !g_Config.bVSync; });
	ffMode->HideChoice(1);  // not used

	auto displayRefreshRate = list->Add(new PopupSliderChoice(&g_Config.iDisplayRefreshRate, 60, 1000, 60, dev->T("Display refresh rate"), 1, screenManager()));
	displayRefreshRate->SetFormat(si->T("%d Hz"));

	list->Add(new ItemHeader(dev->T("Vulkan")));
	list->Add(new CheckBox(&g_Config.bVulkanDisableImplicitLayers, dev->T("Prevent loading overlays")));

	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		list->Add(new CheckBox(&g_Config.bRenderMultiThreading, dev->T("Multi-threaded rendering"), ""))->OnClick.Add([](UI::EventParams &e) {
			// TODO: Not translating yet. Will combine with other translations of settings that need restart.
			g_OSD.Show(OSDType::MESSAGE_WARNING, "Restart required");
			return UI::EVENT_DONE;
		});
	}

	if (GetGPUBackend() == GPUBackend::VULKAN && SupportsCustomDriver()) {
		auto driverChoice = list->Add(new Choice(gr->T("AdrenoTools driver manager")));
		driverChoice->OnClick.Add([=](UI::EventParams &e) {
			screenManager()->push(new DriverManagerScreen(gamePath_));
			return UI::EVENT_DONE;
		});
	}

	static const char *depthRasterModes[] = { "Auto (default)", "Low", "Off", "Always on" };

	PopupMultiChoice *depthRasterMode = list->Add(new PopupMultiChoice(&g_Config.iDepthRasterMode, gr->T("Lens flare occlusion"), depthRasterModes, 0, ARRAY_SIZE(depthRasterModes), I18NCat::GRAPHICS, screenManager()));
	depthRasterMode->SetDisabledPtr(&g_Config.bSoftwareRendering);
	depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.

	list->Add(new ItemHeader(dev->T("Ubershaders")));
	if (draw->GetShaderLanguageDesc().bitwiseOps && !draw->GetBugs().Has(Draw::Bugs::UNIFORM_INDEXING_BROKEN)) {
		// If the above if fails, the checkbox is redundant since it'll be force disabled anyway.
		list->Add(new CheckBox(&g_Config.bUberShaderVertex, dev->T("Vertex")));
	}
#if !PPSSPP_PLATFORM(UWP)
	if (g_Config.iGPUBackend != (int)GPUBackend::OPENGL || gl_extensions.GLES3) {
#else
	{
#endif
		list->Add(new CheckBox(&g_Config.bUberShaderFragment, dev->T("Fragment")));
	}

	// Experimental, allow some VR features without OpenXR
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		auto vr = GetI18NCategory(I18NCat::VR);
		list->Add(new ItemHeader(vr->T("Virtual reality")));
		list->Add(new CheckBox(&g_Config.bForceVR, vr->T("VR camera")));
	}

	// Experimental, will move to main graphics settings later.
	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	if (multiViewSupported) {
		list->Add(new ItemHeader(gr->T("Stereo rendering")));
		list->Add(new CheckBox(&g_Config.bStereoRendering, gr->T("Stereo rendering")));
		std::vector<std::string> stereoShaderNames;

		ChoiceWithValueDisplay *stereoShaderChoice = list->Add(new ChoiceWithValueDisplay(&g_Config.sStereoToMonoShader, gr->T("Stereo display shader"), &PostShaderTranslateName));
		stereoShaderChoice->SetEnabledFunc(enableStereo);
		stereoShaderChoice->OnClick.Add([=](EventParams &e) {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			auto procScreen = new PostProcScreen(gr->T("Stereo display shader"), 0, true);
			if (e.v)
				procScreen->SetPopupOrigin(e.v);
			screenManager()->push(procScreen);
			return UI::EVENT_DONE;
		});
		const ShaderInfo *shaderInfo = GetPostShaderInfo(g_Config.sStereoToMonoShader);
		if (shaderInfo) {
			for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
				auto &setting = shaderInfo->settings[i];
				if (!setting.name.empty()) {
					std::string key = StringFromFormat("%sSettingCurrentValue%d", shaderInfo->section.c_str(), i + 1);
					bool keyExisted = g_Config.mPostShaderSetting.find(key) != g_Config.mPostShaderSetting.end();
					auto &value = g_Config.mPostShaderSetting[key];
					if (!keyExisted)
						value = setting.value;

					PopupSliderChoiceFloat *settingValue = list->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, setting.value, ps->T(setting.name), setting.step, screenManager()));
					settingValue->SetEnabledFunc([=] {
						return !g_Config.bSkipBufferEffects && enableStereo();
					});
				}
			}
		}
	}
}

void DeveloperToolsScreen::CreateTabs() {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	AddTab("General", sy->T("General"), [this](UI::LinearLayout *parent) {
		CreateGeneralTab(parent);
	});
	AddTab("TextureReplacement", dev->T("Texture Replacement"), [this](UI::LinearLayout *parent) {
		CreateTextureReplacementTab(parent);
	});
	AddTab("Graphics", ms->T("Graphics"), [this](UI::LinearLayout *parent) {
		CreateGraphicsTab(parent);
	});
	AddTab("Networking", ms->T("Networking"), [this](UI::LinearLayout *parent) {
		CreateNetworkTab(parent);
	});
	AddTab("Audio", ms->T("Audio"), [this](UI::LinearLayout *parent) {
		CreateAudioTab(parent);
	});
	AddTab("Tests", dev->T("Tests"), [this](UI::LinearLayout *parent) {
		CreateTestsTab(parent);
	});
	AddTab("DumpFiles", dev->T("Dump files"), [this](UI::LinearLayout *parent) {
		CreateDumpFileTab(parent);
	});
	// Need a better title string.
	AddTab("HLE", dev->T("Disable HLE"), [this](UI::LinearLayout *parent) {
		CreateHLETab(parent);
	});
#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
	AddTab("MIPSTracer", dev->T("MIPSTracer"), [this](UI::LinearLayout *parent) {
		CreateMIPSTracerTab(parent);
	});
#endif
	// Reconsider whenever recreating views.
	hasTexturesIni_ = HasIni::MAYBE;
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	UIScreen::onFinish(result);
	g_Config.Save("DeveloperToolsScreen::onFinish");
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	System_Notify(SystemNotification::TOGGLE_DEBUG_CONSOLE);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnOpenTexturesIniFile(UI::EventParams &e) {
	std::string gameID = g_paramSFO.GetDiscID();
	Path generatedFilename;

	if (TextureReplacer::GenerateIni(gameID, generatedFilename)) {
		if (System_GetPropertyBool(SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR)) {
			File::OpenFileInEditor(generatedFilename);
		} else {
			// Can't do much here, let's send a "toast" so the user sees that something happened.
			auto dev = GetI18NCategory(I18NCat::DEVELOPER);
			System_Toast((generatedFilename.ToVisualString() + ": " + dev->T_cstr("Texture ini file created")).c_str());
		}

		hasTexturesIni_ = HasIni::YES;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnJitDebugTools(UI::EventParams &e) {
	screenManager()->push(new JitDebugScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnGPUDriverTest(UI::EventParams &e) {
	screenManager()->push(new GPUDriverTestScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnTouchscreenTest(UI::EventParams &e) {
	screenManager()->push(new TouchTestScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnCopyStatesToRoot(UI::EventParams &e) {
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

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRemoteDebugger(UI::EventParams &e) {
	if (allowDebugger_) {
		StartWebServer(WebServerFlags::DEBUGGER);
	} else {
		StopWebServer(WebServerFlags::DEBUGGER);
	}
	// Persist the setting.  Maybe should separate?
	g_Config.bRemoteDebuggerOnStartup = allowDebugger_;
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerEnabled(UI::EventParams &e) {
	if (MIPSTracerEnabled_) {
		u32 capacity = mipsTracer.in_storage_capacity;
		u32 trace_size = mipsTracer.in_max_trace_size;

		mipsTracer.initialize(capacity, trace_size);
		mipsTracer.start_tracing();
	} else {
		mipsTracer.stop_tracing();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerPathChanged(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	System_BrowseForFileSave(
		GetRequesterToken(),
		dev->T("Select the log file"),
		"trace.txt",
		BrowseFileType::ANY,
		[this](const std::string &value, int) {
			mipsTracer.set_logging_path(value);
			MIPSTracerPath_ = value;
			MIPSTracerPath->SetRightText(MIPSTracerPath_);
		}
	);

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerFlushTrace(UI::EventParams &e) {
	mipsTracer.flush_to_file();
	// The error logs are emitted inside the tracer

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerClearJitCache(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the jit cache...");
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerClearTracer(UI::EventParams &e) {
	INFO_LOG(Log::JIT, "Clearing the MIPSTracer...");
	mipsTracer.clear();
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::update() {
	UIDialogScreenWithBackground::update();
	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
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

UI::EventReturn DeveloperToolsScreen::OnMemstickTest(UI::EventParams &e) {
	std::string error;
	if (RunMemstickTest(&error)) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, "Memstick test passed", error, 6.0f);
	} else {
		g_OSD.Show(OSDType::MESSAGE_ERROR, "Memstick test failed", error, 6.0f);
	}

	return UI::EVENT_DONE;
}
