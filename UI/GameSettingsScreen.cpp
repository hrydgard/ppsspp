
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

#include "ppsspp_config.h"

#include <algorithm>
#include <set>

#include "Common/Net/Resolve.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/Notice.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/BitSet.h"
#include "Common/System/Display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/UI/PopupScreens.h"
#include "UI/EmuScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/SystemInfoScreen.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/MemStickScreen.h"
#include "UI/Theme.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/OnScreenDisplay.h"
#include "UI/DiscordIntegration.h"
#include "UI/Background.h"
#include "UI/BackgroundAudio.h"
#include "UI/MiscViews.h"

#include "Common/File/FileUtil.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/KeyMap.h"
#include "Core/TiltEventProcessor.h"
#include "Core/Instance.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/HLE/sceUtility.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GPU.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "UI/DarwinFileSystemServices.h"
#endif

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "Windows/MainWindow.h"
#include <shlobj.h>
#include "Windows/W32Util/ShellUtil.h"
#endif

#if PPSSPP_PLATFORM(ANDROID)

#include "android/jni/AndroidAudio.h"
#include "Common/File/AndroidStorage.h"

extern AndroidAudioState *g_audioState;

#endif

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
void SetMemStickDirDarwin(int requesterToken) {
	auto initialPath = g_Config.memStickDirectory;
	INFO_LOG(Log::System, "Current path: %s", initialPath.c_str());
	System_BrowseForFolder(requesterToken, "", initialPath, [](const std::string &value, int) {
		INFO_LOG(Log::System, "Selected path: %s", value.c_str());
		DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path(value));
	});
}
#endif

class SettingHint : public UI::TextView {
public:
	SettingHint(std::string_view text)
		: UI::TextView(text, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)) {
		SetTextSize(UI::TextSize::Tiny);
		SetPadding(UI::Margins(14, 0, 0, 8));
		SetAlign(FLAG_WRAP_TEXT);
	}

	void Draw(UIContext &dc) override {
		UI::Style style = dc.GetTheme().itemStyle;
		dc.FillRect(style.background, bounds_);
		UI::TextView::Draw(dc);
	}
};

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: UITabbedBaseDialogScreen(gamePath, TabDialogFlags::HorizontalOnlyIcons | TabDialogFlags::VerticalShowIcons), gameID_(gameID), editGameSpecificThenRestore_(editThenRestore) {
	prevInflightFrames_ = g_Config.iInflightFrames;
	analogSpeedMapped_ = KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true);

	if (editGameSpecificThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.LoadGameConfig(gameID_);
	}

	iAlternateSpeedPercent1_ = g_Config.iFpsLimit1 < 0 ? -1 : (g_Config.iFpsLimit1 * 100) / 60;
	iAlternateSpeedPercent2_ = g_Config.iFpsLimit2 < 0 ? -1 : (g_Config.iFpsLimit2 * 100) / 60;
	iAlternateSpeedPercentAnalog_ = (g_Config.iAnalogFpsLimit * 100) / 60;
}

GameSettingsScreen::~GameSettingsScreen() {
	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		System_Toast("Failed to save settings!\nCheck permissions, or try to restart the device.");
	}

	if (editGameSpecificThenRestore_) {
		// We already saved above. Just unload the game config.
		// Note that we are leaving the screen here in practice, we don't need to reset the bool.
		g_Config.UnloadGameConfig();
	}

	System_Notify(SystemNotification::UI);

	KeyMap::UpdateNativeMenuKeys();

	// Wipe some caches after potentially changing settings.
	// Let's not send resize messages here, handled elsewhere.
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::PreCreateViews() {
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();
}

// This needs before run CheckGPUFeatures()
// TODO: Remove this if fix the issue
static bool CheckSupportShaderTessellationGLES() {
#if PPSSPP_PLATFORM(UWP)
	return true;
#else
	// TODO: Make work with non-GL backends
	int maxVertexTextureImageUnits = gl_extensions.maxVertexTextureUnits;
	bool vertexTexture = maxVertexTextureImageUnits >= 3; // At least 3 for hardware tessellation

	bool textureFloat = gl_extensions.ARB_texture_float || gl_extensions.OES_texture_float;
	bool hasTexelFetch = gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.VersionGEThan(3, 3, 0)) || gl_extensions.EXT_gpu_shader4;

	return vertexTexture && textureFloat && hasTexelFetch;
#endif
}

static bool DoesBackendSupportHWTess() {
	switch (GetGPUBackend()) {
	case GPUBackend::OPENGL:
		return CheckSupportShaderTessellationGLES();
	case GPUBackend::VULKAN:
	case GPUBackend::DIRECT3D11:
		return true;
	default:
		return false;
	}
}

static bool UsingHardwareTextureScaling() {
	// For now, Vulkan only.
	return g_Config.bTexHardwareScaling && GetGPUBackend() == GPUBackend::VULKAN && !g_Config.bSoftwareRendering;
}

static std::string TextureTranslateName(std::string_view value) {
	const TextureShaderInfo *info = GetTextureShaderInfo(value);
	if (info) {
		auto ts = GetI18NCategory(I18NCat::TEXTURESHADERS);
		return std::string(ts->T(value, info->name.c_str()));
	} else {
		return std::string(value);
	}
}

static std::string *GPUDeviceNameSetting() {
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		return &g_Config.sVulkanDevice;
	}
#ifdef _WIN32
	if (g_Config.iGPUBackend == (int)GPUBackend::DIRECT3D11) {
		return &g_Config.sD3D11Device;
	}
#endif
	return nullptr;
}

static bool PathToVisualUsbPath(Path path, std::string &outPath) {
	switch (path.Type()) {
	case PathType::NATIVE:
		if (path.StartsWith(g_Config.memStickDirectory)) {
			return g_Config.memStickDirectory.ComputePathTo(path, outPath);
		}
		break;
	case PathType::CONTENT_URI:
#if PPSSPP_PLATFORM(ANDROID)
	{
		// Try to parse something sensible out of the content URI.
		AndroidContentURI uri(path.ToString());
		outPath = uri.RootPath();
		if (startsWith(outPath, "primary:")) {
			outPath = "/" + outPath.substr(8);
		}
		return true;
	}
#endif
	default:
		break;
	}
	return false;
}

void GameSettingsScreen::CreateTabs() {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	AddTab("GameSettingsGraphics", ms->T("Graphics"), ImageID("I_DISPLAY"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("Graphics"), "graphics", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		CreateGraphicsSettings(parent);
	});

	AddTab("GameSettingsControls", ms->T("Controls"), ImageID("I_CONTROLLER"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("Controls"), "controls"));
		CreateControlsSettings(parent);
	});

	AddTab("GameSettingsAudio", ms->T("Audio"), ImageID("I_SPEAKER_MAX"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("Audio"), "audio"));
		CreateAudioSettings(parent);
	});

	AddTab("GameSettingsNetworking", ms->T("Networking"), ImageID("I_WIFI"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("Networking"), "network"));
		CreateNetworkingSettings(parent);
	});

	AddTab("GameSettingsTools", ms->T("Tools"), ImageID("I_TOOLS"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("Tools"), "tools"));
		CreateToolsSettings(parent);
	});

	AddTab("GameSettingsSystem", ms->T("System"), ImageID("I_PSP"), [this](UI::LinearLayout *parent) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		parent->Add(new PaneTitleBar(gamePath_, ms->T("System"), "system"));
		CreateSystemSettings(parent);
	});

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	if ((deviceType == DEVICE_TYPE_VR) || g_Config.bForceVR) {
		AddTab("GameSettingsVR", ms->T("VR"), ImageID::invalid(), [this](UI::LinearLayout *parent) {
			CreateVRSettings(parent);
		});
	}
}

// Graphics
void GameSettingsScreen::CreateGraphicsSettings(UI::ViewGroup *graphicsSettings) {
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto vr = GetI18NCategory(I18NCat::VR);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	using namespace UI;

	graphicsSettings->Add(new ItemHeader(gr->T("Rendering Mode")));

	Draw::DrawContext *draw = screenManager()->getDrawContext();

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), I18NCat::GRAPHICS, screenManager()));
	renderingBackendChoice->SetPreOpenCallback([this](UI::PopupMultiChoice *choice) {
		// Don't filter until the last possible moment, since it involves trying to initialize Vulkan, if we were
		// started in OpenGL mode on Android.
		choice->HideChoice(1);
		choice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingBackend);

		if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
			choice->HideChoice((int)GPUBackend::OPENGL);
		if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
			choice->HideChoice((int)GPUBackend::DIRECT3D11);
		if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
			choice->HideChoice((int)GPUBackend::VULKAN);
	});

	if (!IsFirstInstance()) {
		// If we're not the first instance, can't save the setting, and it requires a restart, so...
		renderingBackendChoice->SetEnabled(false);
	}
#endif

	// Backends that don't allow a device choice will only expose one device.
	if (draw->GetDeviceList().size() > 1) {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting) {
			PopupMultiChoiceDynamic *deviceChoice = graphicsSettings->Add(new PopupMultiChoiceDynamic(deviceNameSetting, gr->T("Device"), draw->GetDeviceList(), I18NCat::NONE, screenManager()));
			deviceChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingDevice);
		}
	}

	static const char *internalResolutions[] = { "Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
	PopupMultiChoice *resolutionChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInternalResolution, gr->T("Rendering Resolution"), internalResolutions, 0, ARRAY_SIZE(internalResolutions), I18NCat::GRAPHICS, screenManager()));
	resolutionChoice->OnChoice.Add([](UI::EventParams &e) {
		if (g_Config.iAndroidHwScale == 1) {
			System_RecreateActivity();
		}
		Reporting::UpdateConfig();
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	});
	resolutionChoice->SetEnabledFunc([] {
		return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
	});

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	if (deviceType != DEVICE_TYPE_VR) {
		CheckBox *softwareGPU = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareRendering, gr->T("Software Rendering", "Software Rendering (slow)")));
		softwareGPU->SetEnabled(!PSP_IsInited());
	}

	if (draw->GetDeviceCaps().multiSampleLevelsMask != 1) {
		static const char *msaaModes[] = { "Off", "2x", "4x", "8x", "16x" };
		auto msaaChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iMultiSampleLevel, gr->T("Antialiasing (MSAA)"), msaaModes, 0, ARRAY_SIZE(msaaModes), I18NCat::GRAPHICS, screenManager()));
		msaaChoice->OnChoice.Add([&](UI::EventParams &) -> void {
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		});
		msaaChoice->SetEnabledFunc([] {
			return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
		});
		if (g_Config.iMultiSampleLevel > 1 && draw->GetDeviceCaps().isTilingGPU) {
			msaaChoice->SetIconRight(ImageID("I_WARNING"), 0.7f);
		}
		msaaChoice->SetEnabledFunc([] {
			return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
		});

		// Hide unsupported levels.
		for (int i = 1; i < 5; i++) {
			if ((draw->GetDeviceCaps().multiSampleLevelsMask & (1 << i)) == 0) {
				msaaChoice->HideChoice(i);
			} else if (i > 0 && draw->GetDeviceCaps().isTilingGPU) {
				msaaChoice->SetChoiceIcon(i, ImageID("I_WARNING"));
			}
		}
	} else {
		g_Config.iMultiSampleLevel = 0;
	}

#if PPSSPP_PLATFORM(ANDROID)
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		static const char *deviceResolutions[] = { "Native device resolution", "Same as Rendering resolution", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
		int max_res_temp = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 2;
		if (max_res_temp == 3)
			max_res_temp = 4;  // At least allow 2x
		int max_res = std::min(max_res_temp, (int)ARRAY_SIZE(deviceResolutions));
		UI::PopupMultiChoice *hwscale = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAndroidHwScale, gr->T("Display Resolution (HW scaler)"), deviceResolutions, 0, max_res, I18NCat::GRAPHICS, screenManager()));
		hwscale->OnChoice.Add([](UI::EventParams &) {
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
			System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
			System_RecreateActivity();
		});
	}
#endif

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());

	graphicsSettings->Add(new CheckBox(&g_Config.bReplaceTextures, dev->T("Replace textures")));

	graphicsSettings->Add(new ItemHeader(gr->T("Display")));

	if (deviceType != DEVICE_TYPE_VR) {
#if !defined(MOBILE_DEVICE)
		CheckBox *fullscreenCheckbox = graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gr->T("FullScreen", "Full Screen")));
		fullscreenCheckbox->OnClick.Add([](UI::EventParams &e) {
			System_ApplyFullscreenState();
		});
		if (System_GetPropertyInt(SYSPROP_DISPLAY_COUNT) > 1) {
			CheckBox *fullscreenMulti = graphicsSettings->Add(new CheckBox(&g_Config.bFullScreenMulti, gr->T("Use all displays")));
			fullscreenMulti->SetEnabledFunc([] {
				return g_Config.bFullScreen;
			});
			fullscreenMulti->OnClick.Add([](UI::EventParams &e) {
				System_ApplyFullscreenState();
			});
		}
#endif

#if PPSSPP_PLATFORM(ANDROID)
		// Hide Immersive Mode on pre-kitkat Android
		if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
			// Let's reuse the Fullscreen translation string from desktop.
			graphicsSettings->Add(new CheckBox(&config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnImmersiveModeChange);
		}
#endif
		// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
		Choice *displayEditor = graphicsSettings->Add(new Choice(gr->T("Display layout & effects")));
		displayEditor->OnClick.Add([&](UI::EventParams &) -> void {
			screenManager()->push(new DisplayLayoutScreen(gamePath_));
		});
	}

	// If only one mode is supported (like FIFO on iOS), no need to show the options.
	if (CountSetBits((u32)draw->GetDeviceCaps().presentModesSupported) > 1) {
		// Immediate means non-synchronized, tearing.
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
			CheckBox *vSync = graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gr->T("VSync")));
			vSync->OnClick.Add([=](EventParams &e) {
				NativeResized();  // TODO: Remove
			});
		}
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) {
			CheckBox *lowLatency = graphicsSettings->Add(new CheckBox(&g_Config.bLowLatencyPresent, gr->T("Low latency display")));
			lowLatency->OnClick.Add([=](EventParams &e) {
				NativeResized();  // TODO: Remove
			});

			// If the immediate mode is supported, we can tie low latency present to VSync.
			if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
				lowLatency->SetEnabledPtr(&g_Config.bVSync);
			}
		}
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	PopupMultiChoice *frameSkipping = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gr->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), I18NCat::GRAPHICS, screenManager()));
	frameSkipping->SetEnabledFunc([] {
		return !g_Config.bAutoFrameSkip;
	});

	CheckBox *frameSkipAuto = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gr->T("Auto FrameSkip")));
	frameSkipAuto->OnClick.Add([](UI::EventParams &e) {
		g_Config.UpdateAfterSettingAutoFrameSkip();
	});

	PopupSliderChoice *altSpeed1 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent1_, 0, 1000, UI::NO_DEFAULT_INT, gr->T("Alternative Speed", "Alternative speed"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed1->SetFormat("%i%%");
	altSpeed1->SetZeroLabel(gr->T("Unlimited"));
	altSpeed1->SetNegativeDisable(gr->T("Disabled"));

	PopupSliderChoice *altSpeed2 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent2_, 0, 1000, UI::NO_DEFAULT_INT, gr->T("Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed2->SetFormat("%i%%");
	altSpeed2->SetZeroLabel(gr->T("Unlimited"));
	altSpeed2->SetNegativeDisable(gr->T("Disabled"));

	if (analogSpeedMapped_) {
		PopupSliderChoice *analogSpeed = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercentAnalog_, 1, 1000, UI::NO_DEFAULT_INT, gr->T("Analog alternative speed", "Analog alternative speed (in %)"), 5, screenManager(), "%"));
		altSpeed2->SetFormat("%i%%");
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Speed Hacks", "Speed Hacks (can cause rendering errors!)")));

	CheckBox *skipBufferEffects = graphicsSettings->Add(new CheckBox(&g_Config.bSkipBufferEffects, gr->T("Skip Buffer Effects")));
	graphicsSettings->Add(new SettingHint(gr->T("RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games")));
	skipBufferEffects->OnClick.Add([=](EventParams &e) {
		if (g_Config.bSkipBufferEffects) {
			g_Config.bAutoFrameSkip = false;
		}

		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	});
	skipBufferEffects->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *disableCulling = graphicsSettings->Add(new CheckBox(&g_Config.bDisableRangeCulling, gr->T("Disable culling")));
	disableCulling->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *skipGpuReadbackModes[] = { "No (default)", "Skip", "Copy to texture" };

	PopupMultiChoice *skipGPUReadbacks = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSkipGPUReadbackMode, gr->T("Skip GPU Readbacks"), skipGpuReadbackModes, 0, ARRAY_SIZE(skipGpuReadbackModes), I18NCat::GRAPHICS, screenManager()));
	skipGPUReadbacks->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *depthRasterModes[] = { "Auto (default)", "Low", "Off", "Always on" };

	PopupMultiChoice *depthRasterMode = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iDepthRasterMode, gr->T("Lens flare occlusion"), depthRasterModes, 0, ARRAY_SIZE(depthRasterModes), I18NCat::GRAPHICS, screenManager()));
	depthRasterMode->SetDisabledPtr(&g_Config.bSoftwareRendering);
	depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.
	if (g_Config.iDepthRasterMode != 3)
		depthRasterMode->HideChoice(3);

	CheckBox *texBackoff = graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gr->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	texBackoff->SetDisabledPtr(&g_Config.bSoftwareRendering);
	graphicsSettings->Add(new SettingHint(gr->T("Lazy texture caching Tip", "Faster, but can cause text problems in a few games")));

	static const char *quality[] = { "Low", "Medium", "High" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSplineBezierQuality, gr->T("LowCurves", "Spline/Bezier curves quality"), quality, 0, ARRAY_SIZE(quality), I18NCat::GRAPHICS, screenManager()));
	graphicsSettings->Add(new SettingHint(gr->T("LowCurves Tip", "Only used by some games, controls smoothness of curves")));

	graphicsSettings->Add(new ItemHeader(gr->T("Performance")));
	CheckBox *frameDuplication = graphicsSettings->Add(new CheckBox(&g_Config.bRenderDuplicateFrames, gr->T("Render duplicate frames to 60hz")));
	frameDuplication->SetEnabledFunc([] {
		return !g_Config.bSkipBufferEffects && g_Config.iFrameSkip == 0;
	});
	graphicsSettings->Add(new SettingHint(gr->T("RenderDuplicateFrames Tip", "Can make framerate smoother in games that run at lower framerates")));

	if (draw->GetDeviceCaps().setMaxFrameLatencySupported) {
		static const char *bufferOptions[] = { "No buffer", "Up to 1", "Up to 2" };
		PopupMultiChoice *inflightChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInflightFrames, gr->T("Buffer graphics commands (faster, input lag)"), bufferOptions, 1, ARRAY_SIZE(bufferOptions), I18NCat::GRAPHICS, screenManager()));
		inflightChoice->OnChoice.Handle(this, &GameSettingsScreen::OnInflightFramesChoice);
	}

	if (GetGPUBackend() == GPUBackend::VULKAN) {
		const bool usable = draw->GetDeviceCaps().geometryShaderSupported && !draw->GetBugs().Has(Draw::Bugs::GEOMETRY_SHADERS_SLOW_OR_BROKEN);
		const bool vertexSupported = draw->GetDeviceCaps().clipDistanceSupported && draw->GetDeviceCaps().cullDistanceSupported;
		if (usable && !vertexSupported) {
			CheckBox *geometryCulling = graphicsSettings->Add(new CheckBox(&g_Config.bUseGeometryShader, gr->T("Geometry shader culling")));
			geometryCulling->SetDisabledPtr(&g_Config.bSoftwareRendering);
		}
	}

	if (deviceType != DEVICE_TYPE_VR) {
		CheckBox *hwTransform = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gr->T("Hardware Transform")));
		hwTransform->SetDisabledPtr(&g_Config.bSoftwareRendering);
	}

	CheckBox *swSkin = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareSkinning, gr->T("Software Skinning")));
	swSkin->SetDisabledPtr(&g_Config.bSoftwareRendering);
	graphicsSettings->Add(new SettingHint(gr->T("SoftwareSkinning Tip", "Combine skinned model draws on the CPU, faster in most games")));

	if (DoesBackendSupportHWTess()) {
		CheckBox *tessellationHW = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTessellation, gr->T("Hardware Tessellation")));
		tessellationHW->SetEnabledFunc([]() {
			return !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
		});
		graphicsSettings->Add(new SettingHint(gr->T("HardwareTessellation Tip", "Uses hardware to make curves")));
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Texture upscaling")));

	if (GetGPUBackend() == GPUBackend::VULKAN) {
		ChoiceWithValueDisplay *textureShaderChoice = graphicsSettings->Add(new ChoiceWithValueDisplay(&g_Config.sTextureShaderName, gr->T("GPU texture upscaler (fast)"), &TextureTranslateName));
		textureShaderChoice->OnClick.Add([this](UI::EventParams &e) {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			auto shaderScreen = new TextureShaderScreen(gr->T("GPU texture upscaler (fast)"));
			shaderScreen->OnChoice.Add([this](UI::EventParams &e) {
				System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
				RecreateViews(); // Update setting name
				g_Config.bTexHardwareScaling = g_Config.sTextureShaderName != "Off";
			});
			if (e.v)
				shaderScreen->SetPopupOrigin(e.v);
			screenManager()->push(shaderScreen);
		});
		textureShaderChoice->SetDisabledPtr(&g_Config.bSoftwareRendering);
	}

#ifndef MOBILE_DEVICE
	static const char *texScaleLevels[] = {"Off", "2x", "3x", "4x", "5x"};
#else
	static const char *texScaleLevels[] = {"Off", "2x", "3x"};
#endif

	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	PopupMultiChoice *texScalingType = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gr->T("CPU texture upscaler (slow)"), texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos), I18NCat::GRAPHICS, screenManager()));
	texScalingType->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	PopupMultiChoice *texScalingChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gr->T("Upscale Level"), texScaleLevels, 1, ARRAY_SIZE(texScaleLevels), I18NCat::GRAPHICS, screenManager()));
	// TODO: Better check?  When it won't work, it scales down anyway.
	if (!gl_extensions.OES_texture_npot && GetGPUBackend() == GPUBackend::OPENGL) {
		texScalingChoice->HideChoice(3); // 3x
		texScalingChoice->HideChoice(5); // 5x
	}
	graphicsSettings->Add(new SettingHint(gr->T("UpscaleLevel Tip", "CPU heavy - some scaling may be delayed to avoid stutter")));

	texScalingChoice->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});

	CheckBox *deposterize = graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gr->T("Deposterize")));
	deposterize->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	graphicsSettings->Add(new SettingHint(gr->T("Deposterize Tip", "Fixes visual banding glitches in upscaled textures")));

	graphicsSettings->Add(new ItemHeader(gr->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	PopupMultiChoice *anisoFiltering = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gr->T("Anisotropic Filtering"), anisoLevels, 0, ARRAY_SIZE(anisoLevels), I18NCat::GRAPHICS, screenManager()));
	anisoFiltering->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Auto Max Quality"};
	PopupMultiChoice *filters = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gr->T("Texture Filter"), texFilters, 1, ARRAY_SIZE(texFilters), I18NCat::GRAPHICS, screenManager()));
	filters->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *smartFiltering = graphicsSettings->Add(new CheckBox(&g_Config.bSmart2DTexFiltering, gr->T("Smart 2D texture filtering")));
	smartFiltering->SetDisabledPtr(&g_Config.bSoftwareRendering);

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	bool showCardboardSettings = deviceType != DEVICE_TYPE_VR;
#else
	// If you enabled it through the ini, you can see this. Useful for testing.
	bool showCardboardSettings = config.bEnableCardboardVR;
#endif
	if (showCardboardSettings) {
		graphicsSettings->Add(new ItemHeader(gr->T("Cardboard VR Settings", "Cardboard VR Settings")));
		graphicsSettings->Add(new CheckBox(&config.bEnableCardboardVR, gr->T("Enable Cardboard VR", "Enable Cardboard VR")));
		PopupSliderChoice *cardboardScreenSize = graphicsSettings->Add(new PopupSliderChoice(&config.iCardboardScreenSize, 30, 150, 50, gr->T("Cardboard Screen Size", "Screen Size (in % of the viewport)"), 1, screenManager(), gr->T("% of viewport")));
		cardboardScreenSize->SetEnabledPtr(&config.bEnableCardboardVR);
		PopupSliderChoice *cardboardXShift = graphicsSettings->Add(new PopupSliderChoice(&config.iCardboardXShift, -150, 150, 0, gr->T("Cardboard Screen X Shift", "X Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardXShift->SetEnabledPtr(&config.bEnableCardboardVR);
		PopupSliderChoice *cardboardYShift = graphicsSettings->Add(new PopupSliderChoice(&config.iCardboardYShift, -100, 100, 0, gr->T("Cardboard Screen Y Shift", "Y Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardYShift->SetEnabledPtr(&config.bEnableCardboardVR);
	}

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		graphicsSettings->Add(new ItemHeader(gr->T("Camera")));
		PopupMultiChoiceDynamic *cameraChoice = graphicsSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sCameraDevice, gr->T("Camera Device"), cameraList, I18NCat::NONE, screenManager()));
		cameraChoice->OnChoice.Handle(this, &GameSettingsScreen::OnCameraDeviceChange);
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		graphicsSettings->Add(new CheckBox(&g_Config.bCameraMirrorHorizontal, gr->T("Mirror camera image")));
#endif
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Hack Settings", "Hack Settings (these WILL cause glitches)")));

	static const char *bloomHackOptions[] = { "Off", "Safe", "Balanced", "Aggressive" };
	PopupMultiChoice *bloomHack = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iBloomHack, gr->T("Lower resolution for effects (reduces artifacts)"), bloomHackOptions, 0, ARRAY_SIZE(bloomHackOptions), I18NCat::GRAPHICS, screenManager()));
	bloomHack->SetEnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.iInternalResolution != 1;
	});

	graphicsSettings->Add(new ItemHeader(gr->T("Overlay Information")));
	graphicsSettings->Add(new BitCheckBox(&g_Config.iShowStatusFlags, (int)ShowStatusFlags::FPS_COUNTER, gr->T("Show FPS Counter")));
	graphicsSettings->Add(new BitCheckBox(&g_Config.iShowStatusFlags, (int)ShowStatusFlags::SPEED_COUNTER, gr->T("Show Speed")));
	if (System_GetPropertyBool(SYSPROP_CAN_READ_BATTERY_PERCENTAGE)) {
		graphicsSettings->Add(new BitCheckBox(&g_Config.iShowStatusFlags, (int)ShowStatusFlags::BATTERY_PERCENT, gr->T("Show Battery %")));
	}
	AddOverlayList(graphicsSettings, screenManager());
}

void GameSettingsScreen::CreateAudioSettings(UI::ViewGroup *audioSettings) {
	using namespace UI;

	auto a = GetI18NCategory(I18NCat::AUDIO);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

#if PPSSPP_PLATFORM(IOS)
	CheckBox *respectSilentMode = audioSettings->Add(new CheckBox(&g_Config.bAudioRespectSilentMode, a->T("Respect silent mode")));
	respectSilentMode->OnClick.Add([=](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
	});
	respectSilentMode->SetEnabledPtr(&g_Config.bEnableSound);
	CheckBox *mixWithOthers = audioSettings->Add(new CheckBox(&g_Config.bAudioMixWithOthers, a->T("Mix audio with other apps")));
	mixWithOthers->OnClick.Add([=](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
	});
	mixWithOthers->SetEnabledPtr(&g_Config.bEnableSound);
#endif
	audioSettings->Add(new ItemHeader(a->T("Audio playback")));

	static const char *syncModes[] = { "Smooth (reduces artifacts)", "Classic (lowest latency)" };

	audioSettings->Add(new PopupMultiChoice(&g_Config.iAudioPlaybackMode, a->T("Playback mode"), syncModes, 0, ARRAY_SIZE(syncModes), I18NCat::AUDIO, screenManager()));
	audioSettings->Add(new CheckBox(&g_Config.bFillAudioGaps, a->T("Fill audio gaps")))->SetEnabledFunc([]() {
		return g_Config.iAudioPlaybackMode == (int)AudioSyncMode::GRANULAR;
	});

	audioSettings->Add(new ItemHeader(a->T("Game volume")));

	// This is here because it now only applies to in-game. Muting the menu sounds is separate.
	CheckBox *enableSound = audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));

	PopupSliderChoice *volume = audioSettings->Add(new PopupSliderChoice(&g_Config.iGameVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iGameVolume), a->T("Game volume"), screenManager()));
	volume->SetFormat("%d%%");
	volume->SetEnabledPtr(&g_Config.bEnableSound);
	volume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *reverbVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iReverbVolume, VOLUME_OFF, 2 * VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iReverbVolume), a->T("Reverb volume"), screenManager()));
	reverbVolume->SetFormat("%d%%");
	reverbVolume->SetEnabledPtr(&g_Config.bEnableSound);
	reverbVolume->SetZeroLabel(a->T("Disabled"));

	PopupSliderChoice *altVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAltSpeedVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iAltSpeedVolume), a->T("Alternate speed volume"), screenManager()));
	altVolume->SetFormat("%d%%");
	altVolume->SetEnabledPtr(&g_Config.bEnableSound);
	altVolume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *achievementVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAchievementVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iAchievementVolume), ac->T("Achievement sound volume"), screenManager()));
	achievementVolume->SetFormat("%d%%");
	achievementVolume->SetEnabledPtr(&g_Config.bEnableSound);
	achievementVolume->SetZeroLabel(a->T("Mute"));
	achievementVolume->OnChange.Add([](UI::EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
	});

	audioSettings->Add(new ItemHeader(a->T("UI sound")));

	audioSettings->Add(new CheckBox(&g_Config.bUISound, a->T("UI sound")));
	PopupSliderChoice *uiVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iUIVolume, 0, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iUIVolume), a->T("UI volume"), screenManager()));
	uiVolume->SetFormat("%d%%");
	uiVolume->SetZeroLabel(a->T("Mute"));
	uiVolume->SetLiveUpdate(true);
	uiVolume->OnChange.Add([](UI::EventParams &e) {
		static double lastTimePlayed = 0.0;
		double now = time_now_d();
		if (now - lastTimePlayed < 0.1) {
			return; // Don't play if we just played one, to avoid spamming when dragging.
		}
		lastTimePlayed = now;
		// Audio preview
		PlayUISound(UI::UISound::CONFIRM);
	});
	uiVolume->SetEnabledPtr(&g_Config.bUISound);

	PopupSliderChoice *gamePreviewVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iGamePreviewVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iGamePreviewVolume), a->T("Game preview volume"), screenManager()));
	gamePreviewVolume->SetFormat("%d%%");
	gamePreviewVolume->SetZeroLabel(a->T("Mute"));

	bool sdlAudio = false;

#if defined(SDL)
	audioSettings->Add(new ItemHeader(a->T("Audio backend")));
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), a->T_cstr("Auto"));
	PopupMultiChoiceDynamic *audioDevice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sAudioDevice, a->T("Device"), audioDeviceList, I18NCat::NONE, screenManager()));
	audioDevice->OnChoice.Handle(this, &GameSettingsScreen::OnAudioDevice);
	sdlAudio = true;

	static const int bufferSizes[] = {128, 256, 512, 1024, 2048};
	PopupSliderChoice *bufferSize = audioSettings->Add(new PopupSliderChoice(&g_Config.iSDLAudioBufferSize, 0, 2048, 256, a->T("Buffer size"), screenManager()));
	bufferSize->RestrictChoices(bufferSizes, ARRAY_SIZE(bufferSizes));
	audioSettings->Add(new SettingHint(di->T("This change will not take effect until PPSSPP is restarted.")));
#endif

#if PPSSPP_PLATFORM(WINDOWS)
	extern AudioBackend *g_audioBackend;

	std::vector<std::string> audioDeviceNames;
	std::vector<std::string> audioDeviceIds;

	std::vector<AudioDeviceDesc> deviceDescs;
	g_audioBackend->EnumerateDevices(&deviceDescs);
	if (!deviceDescs.empty()) {
		audioSettings->Add(new ItemHeader(a->T("Audio backend")));
		for (auto &desc : deviceDescs) {
			audioDeviceNames.push_back(desc.name);
			audioDeviceIds.push_back(desc.uniqueId);
		}

		audioDeviceNames.insert(audioDeviceNames.begin(), std::string(a->T("Auto")));
		audioDeviceIds.insert(audioDeviceIds.begin(), "");

		PopupMultiChoiceDynamic *audioDevice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sAudioDevice, a->T("Device"), audioDeviceNames, I18NCat::NONE, screenManager(), &audioDeviceIds));
		audioDevice->OnChoice.Add([](UI::EventParams &) {
			bool reverted;
			if (g_audioBackend->InitOutputDevice(g_Config.sAudioDevice, LatencyMode::Aggressive, &reverted)) {
				if (reverted) {
					WARN_LOG(Log::Audio, "Unexpected: After a direct choice, audio device reverted to default. '%s'", g_Config.sAudioDevice.c_str());
				}
			} else {
				WARN_LOG(Log::Audio, "InitOutputDevice failed");
			}
		});
		CheckBox *autoAudio = audioSettings->Add(new CheckBox(&g_Config.bAutoAudioDevice, a->T("Use new audio devices automatically")));
		autoAudio->SetEnabledFunc([]()->bool {
			return g_Config.sAudioDevice.empty();
		});
	}

	const bool isWindows = true;
#else
	const bool isWindows = false;
	audioSettings->Add(new ItemHeader(a->T("Audio backend")));

	if (sdlAudio) {
		audioSettings->Add(new CheckBox(&g_Config.bAutoAudioDevice, a->T("Use new audio devices automatically")));
	}

#if PPSSPP_PLATFORM(ANDROID)
	CheckBox *extraAudio = audioSettings->Add(new CheckBox(&g_Config.bExtraAudioBuffering, a->T("AudioBufferingForBluetooth", "Bluetooth-friendly buffer (slower)")));

	// Show OpenSL debug info
	const std::string audioErrorStr = AndroidAudio_GetErrorString(g_audioState);
	if (!audioErrorStr.empty()) {
		audioSettings->Add(new InfoItem(a->T("Audio Error"), audioErrorStr));
	}
#endif
#endif

	std::vector<std::string> micList = Microphone::getDeviceList();
	if (!micList.empty()) {
		audioSettings->Add(new ItemHeader(a->T("Microphone")));
		PopupMultiChoiceDynamic *MicChoice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sMicDevice, a->T("Microphone Device"), micList, I18NCat::NONE, screenManager()));
		MicChoice->OnChoice.Handle(this, &GameSettingsScreen::OnMicDeviceChange);
	}
}

void GameSettingsScreen::CreateControlsSettings(UI::ViewGroup *controlsSettings) {
	using namespace UI;

	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	controlsSettings->Add(new ItemHeader(ms->T("Controls")));
	controlsSettings->Add(new Choice(co->T("Control mapping")))->OnClick.Add([this](UI::EventParams &e) {
		screenManager()->push(new ControlMappingScreen(gamePath_));
	});
	controlsSettings->Add(new Choice(co->T("Calibrate analog stick")))->OnClick.Add([this](UI::EventParams &e) {
		screenManager()->push(new AnalogCalibrationScreen(gamePath_));
	});
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogTriggerThreshold, 0.02f, 0.98f, 0.75f, co->T("Analog trigger threshold"), screenManager()));

#if defined(USING_WIN_UI) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
	controlsSettings->Add(new CheckBox(&g_Config.bSystemControls, co->T("Enable standard shortcut keys")));
#endif
#if defined(USING_WIN_UI)
	controlsSettings->Add(new CheckBox(&g_Config.bGamepadOnlyFocused, co->T("Ignore gamepads when not focused")));
#endif

	if (System_GetPropertyBool(SYSPROP_HAS_ACCELEROMETER)) {
		// Show the tilt type on the item.
		Choice *customizeTilt = controlsSettings->Add(new ChoiceWithCallbackValueDisplay(co->T("Tilt control setup"), []() -> std::string {
			if (g_Config.bTiltInputEnabled && (u32)g_Config.iTiltInputType < (u32)g_numTiltTypes) {
				auto co = GetI18NCategory(I18NCat::CONTROLS);
				return std::string(co->T(g_tiltTypes[g_Config.iTiltInputType]));
			} else {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				return std::string(di->T("Disabled"));
			}
		}));
		customizeTilt->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new TiltAnalogSettingsScreen(gamePath_));
		});
	} else if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {  // TODO: This seems like a regression
		controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, co->T("HapticFeedback", "Haptic Feedback (vibration)")));
	}

	// TVs don't have touch control, at least not yet.
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		controlsSettings->Add(new ItemHeader(co->T("On-screen touch controls")));
		controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, co->T("On-screen touch controls")));
		Choice *layoutEditorChoice = controlsSettings->Add(new Choice(co->T("Edit touch control layout")));
		layoutEditorChoice->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new TouchControlLayoutScreen(gamePath_));
		});
		layoutEditorChoice->SetEnabledPtr(&g_Config.bShowTouchControls);

		Choice *gesture = controlsSettings->Add(new Choice(co->T("Gesture mapping")));
		gesture->OnClick.Add([=](EventParams &e) {
			screenManager()->push(new GestureMappingScreen(gamePath_));
		});
		gesture->SetEnabledPtr(&g_Config.bShowTouchControls);

		static const char *touchControlStyles[] = { "Classic", "Thin borders", "Glowing borders" };
		View *style = controlsSettings->Add(new PopupMultiChoice(&g_Config.iTouchButtonStyle, co->T("Button style"), touchControlStyles, 0, ARRAY_SIZE(touchControlStyles), I18NCat::CONTROLS, screenManager()));
		style->SetEnabledPtr(&g_Config.bShowTouchControls);

		PopupSliderChoice *opacity = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 100, 65, co->T("Button Opacity"), screenManager(), "%"));
		opacity->SetEnabledPtr(&g_Config.bShowTouchControls);
		opacity->SetFormat("%i%%");
		PopupSliderChoice *autoHide = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonHideSeconds, 0, 300, 20, co->T("Auto-hide buttons after delay"), screenManager(), di->T("seconds, 0:off")));
		autoHide->SetEnabledPtr(&g_Config.bShowTouchControls);
		autoHide->SetFormat(di->T("%d seconds"));
		autoHide->SetZeroLabel(co->T("Off"));

		CheckBox *touchGliding = controlsSettings->Add(new CheckBox(&g_Config.bTouchGliding, co->T("Keep first touched button pressed when dragging")));
		touchGliding->SetEnabledPtr(&g_Config.bShowTouchControls);

		TouchControlConfig &touch = g_Config.GetTouchControlsConfig(GetDeviceOrientation());

		// Hide stick background, useful when increasing the size
		CheckBox *hideStickBackground = controlsSettings->Add(new CheckBox(&touch.bHideStickBackground, co->T("Hide touch analog stick background circle")));
		hideStickBackground->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Sticky D-pad.
		CheckBox *stickyDpad = controlsSettings->Add(new CheckBox(&g_Config.bStickyTouchDPad, co->T("Sticky D-Pad (easier sweeping movements)")));
		stickyDpad->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Re-centers itself to the touch location on touch-down.
		CheckBox *floatingAnalog = controlsSettings->Add(new CheckBox(&g_Config.bAutoCenterTouchAnalog, co->T("Auto-centering analog stick")));
		floatingAnalog->SetEnabledPtr(&g_Config.bShowTouchControls);

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, co->T("HapticFeedback", "Haptic Feedback (vibration)")));
		}

		// The pause button is now a regular on-screen button.

		CheckBox *disableDiags = controlsSettings->Add(new CheckBox(&g_Config.bDisableDpadDiagonals, co->T("Disable D-Pad diagonals (4-way touch)")));
		disableDiags->SetEnabledPtr(&g_Config.bShowTouchControls);
	}

	if (deviceType != DEVICE_TYPE_VR) {
		controlsSettings->Add(new ItemHeader(co->T("Keyboard", "Keyboard Control Settings")));
#if defined(USING_WIN_UI)
		controlsSettings->Add(new CheckBox(&g_Config.bIgnoreWindowsKey, co->T("Ignore Windows Key")));
#endif // #if defined(USING_WIN_UI)
		auto analogLimiter = new PopupSliderChoiceFloat(&g_Config.fAnalogLimiterDeadzone, 0.0f, 1.0f, 0.6f, co->T("Analog Limiter"), 0.10f, screenManager(), "/ 1.0");
		controlsSettings->Add(analogLimiter);
		controlsSettings->Add(new SettingHint(co->T("AnalogLimiter Tip", "When the analog limiter button is pressed")));
		controlsSettings->Add(new PopupSliderChoice(&g_Config.iRapidFireInterval, 1, 10, 5, co->T("Rapid fire interval"), screenManager(), "frames"));
#if defined(USING_WIN_UI) || defined(SDL) || PPSSPP_PLATFORM(ANDROID)
		bool enableMouseSettings = true;
#if PPSSPP_PLATFORM(ANDROID)
		if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) < 12) {
			enableMouseSettings = false;
		}
#endif
#else
		bool enableMouseSettings = false;
#endif
		if (enableMouseSettings) {
			// The mousewheel button-release setting is independent of actual mouse delta control.
			controlsSettings->Add(new ItemHeader(co->T("Mouse", "Mouse settings")));
			auto wheelUpDelaySlider = controlsSettings->Add(new PopupSliderChoice(&g_Config.iMouseWheelUpDelayMs, 10, 300, 1, co->T("Mouse wheel button-release delay"), screenManager()));
			wheelUpDelaySlider->SetFormat(di->T("%d ms"));

			CheckBox *mouseControl = controlsSettings->Add(new CheckBox(&g_Config.bMouseControl, co->T("Use Mouse Control")));
			controlsSettings->Add(new SettingHint(co->T("MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon.")));

#if !PPSSPP_PLATFORM(ANDROID)
			controlsSettings->Add(new CheckBox(&g_Config.bMouseConfine, co->T("Confine Mouse", "Trap mouse within window/display area")))->SetEnabledPtr(&g_Config.bMouseControl);
#endif
			auto sensitivitySlider = controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSensitivity, 0.01f, 1.0f, 0.1f, co->T("Mouse sensitivity"), 0.01f, screenManager(), "x"));
			sensitivitySlider->SetEnabledPtr(&g_Config.bMouseControl);
			sensitivitySlider->SetLiveUpdate(true);
			auto smoothingSlider = controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSmoothing, 0.0f, 0.95f, 0.9f, co->T("Mouse smoothing"), 0.05f, screenManager(), "x"));
			smoothingSlider->SetEnabledPtr(&g_Config.bMouseControl);
			smoothingSlider->SetLiveUpdate(true);
		}
	}
}

// Compound view just like the audio file choosers
class MacAddressChooser : public UI::LinearLayout {
public:
	MacAddressChooser(RequesterToken token, Path gamePath, std::string *value, std::string_view title, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr);
};

MacAddressChooser::MacAddressChooser(RequesterToken token, Path gamePath, std::string *value, std::string_view title, ScreenManager *screenManager, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(5.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	std::string initialValue = *value;
	Add(new PopupTextInputChoice(token, value, title, g_Config.sMACAddress, 17, screenManager, new LinearLayoutParams(1.0f)))->OnChange.Add([=](UI::EventParams &e) {
		// Validate the chosen address, and restore to initialValue if bad.
		if (g_Config.sMACAddress.size() != 17) {
			// TODO: Alert the user
			*value = initialValue;
		}
	});
	Add(new Choice(n->T("Randomize"), new LinearLayoutParams(WRAP_CONTENT, ITEM_HEIGHT)))->OnClick.Add([=](UI::EventParams &) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		std::string_view confirmMessage = n->T("ChangeMacSaveConfirm", "Generate a new MAC address?");
		std::string_view warningMessage = n->T("ChangeMacSaveWarning", "Some games verify the MAC address when loading savedata, so this may break old saves.");
		std::string combined = g_Config.sMACAddress + "\n\n" + std::string(confirmMessage) + "\n\n" + std::string(warningMessage);

		auto confirmScreen = new PromptScreen(
			gamePath,
			combined, di->T("Yes"), di->T("No"),
			[&](bool success) {
				if (success) {
					g_Config.sMACAddress = CreateRandMAC();
				}}
		);
		screenManager->push(confirmScreen);
	});
}

void GameSettingsScreen::CreateNetworkingSettings(UI::ViewGroup *networkingSettings) {
	using namespace UI;

	auto n = GetI18NCategory(I18NCat::NETWORKING);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	networkingSettings->Add(new ItemHeader(ms->T("Networking")));

	Choice *wiki = networkingSettings->Add(new Choice(n->T("Open PPSSPP Multiplayer Wiki Page"), ImageID("I_LINK_OUT")));
	wiki->OnClick.Handle(this, &GameSettingsScreen::OnAdhocGuides);

	networkingSettings->Add(new CheckBox(&g_Config.bEnableWlan, n->T("Enable networking", "Enable networking/wlan (beta)")));
	networkingSettings->Add(new MacAddressChooser(GetRequesterToken(), gamePath_, &g_Config.sMACAddress, n->T("Change Mac Address"), screenManager()));
	static const char* wlanChannels[] = { "Auto", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };
	auto wlanChannelChoice = networkingSettings->Add(new PopupMultiChoice(&g_Config.iWlanAdhocChannel, n->T("WLAN Channel"), wlanChannels, 0, ARRAY_SIZE(wlanChannels), I18NCat::NETWORKING, screenManager()));
	for (int i = 0; i < 4; i++) {
		wlanChannelChoice->HideChoice(i + 2);
		wlanChannelChoice->HideChoice(i + 7);
	}

	if (Discord::IsAvailable()) {
		networkingSettings->Add(new CheckBox(&g_Config.bDiscordRichPresence, n->T("Send Discord Presence information")));
	}

	networkingSettings->Add(new ItemHeader(n->T("Ad Hoc multiplayer")));
	networkingSettings->Add(new CheckBox(&g_Config.bUseServerRelay, n->T("Try to use server-provided packet relay")))->SetEnabled(!PSP_IsInited());
	networkingSettings->Add(new SettingHint(n->T("PacketRelayHint", "Available on servers that provide 'aemu_postoffice' packet relay, like socom.cc. Disable this for LAN or VPN play. Can be more reliable, but sometimes slower.")));

	networkingSettings->Add(new ItemHeader(n->T("Ad Hoc server")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableAdhocServer, n->T("Enable built-in PRO Adhoc Server", "Enable built-in PRO Adhoc Server")));
	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sProAdhocServer, n->T("Change proAdhocServer Address"), I18NCat::NONE))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new HostnameSelectScreen(&g_Config.sProAdhocServer, &g_Config.proAdhocServerList, n->T("proAdhocServer Address:")));
	});
	networkingSettings->Add(new SettingHint(n->T("Change proAdhocServer address hint")));

	networkingSettings->Add(new ItemHeader(n->T("Infrastructure")));
	if (g_Config.sInfrastructureUsername.empty()) {
		networkingSettings->Add(new NoticeView(NoticeLevel::WARN, n->T("To play in Infrastructure Mode, you must enter a username"), ""));
	}
	PopupTextInputChoice *usernameChoice = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureUsername, di->T("Username"), "", 16, screenManager()));
	usernameChoice->SetRestriction(StringRestriction::AlphaNumDashUnderscore, 3);
	usernameChoice->OnChange.Add([this](UI::EventParams &e) {
		RecreateViews();
	});

	networkingSettings->Add(new CheckBox(&g_Config.bInfrastructureAutoDNS, n->T("Autoconfigure")));
	auto *dnsServer = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureDNSServer, n->T("DNS server"), "", 32, screenManager()));
	dnsServer->SetDisabledPtr(&g_Config.bInfrastructureAutoDNS);

	networkingSettings->Add(new ItemHeader(n->T("UPnP (port-forwarding)")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableUPnP, n->T("Enable UPnP", "Enable UPnP (need a few seconds to detect)")));
	auto useOriPort = networkingSettings->Add(new CheckBox(&g_Config.bUPnPUseOriginalPort, n->T("UPnP use original port", "UPnP use original port (Enabled = PSP compatibility)")));
	networkingSettings->Add(new SettingHint(n->T("UseOriginalPort Tip", "May not work for all devices or games, see wiki.")));

	useOriPort->SetEnabledPtr(&g_Config.bEnableUPnP);

	networkingSettings->Add(new ItemHeader(n->T("Chat")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableNetworkChat, n->T("Enable network chat", "Enable network chat")));
	static const char *chatButtonPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right", "None" };
	networkingSettings->Add(new PopupMultiChoice(&g_Config.iChatButtonPosition, n->T("Chat Button Position"), chatButtonPositions, 0, ARRAY_SIZE(chatButtonPositions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bEnableNetworkChat);
	static const char *chatScreenPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right" };
	networkingSettings->Add(new PopupMultiChoice(&g_Config.iChatScreenPosition, n->T("Chat Screen Position"), chatScreenPositions, 0, ARRAY_SIZE(chatScreenPositions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bEnableNetworkChat);

#if (!defined(MOBILE_DEVICE) && !defined(USING_QT_UI)) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID) // Missing only iOS?
	networkingSettings->Add(new ItemHeader(n->T("QuickChat", "Quick Chat")));
	CheckBox *qc = networkingSettings->Add(new CheckBox(&g_Config.bEnableQuickChat, n->T("EnableQuickChat", "Enable Quick Chat")));
	qc->SetEnabledPtr(&g_Config.bEnableNetworkChat);
#endif

#if !defined(MOBILE_DEVICE) && !defined(USING_QT_UI)  // TODO: Add all platforms where KeyInputFlags::CHAR support is added
	PopupTextInputChoice *qc1 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[0], n->T("Quick Chat 1"), "", 32, screenManager()));
	PopupTextInputChoice *qc2 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[1], n->T("Quick Chat 2"), "", 32, screenManager()));
	PopupTextInputChoice *qc3 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[2], n->T("Quick Chat 3"), "", 32, screenManager()));
	PopupTextInputChoice *qc4 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[3], n->T("Quick Chat 4"), "", 32, screenManager()));
	PopupTextInputChoice *qc5 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[4], n->T("Quick Chat 5"), "", 32, screenManager()));
#elif defined(USING_QT_UI)
	Choice *qc1 = networkingSettings->Add(new Choice(n->T("Quick Chat 1")));
	Choice *qc2 = networkingSettings->Add(new Choice(n->T("Quick Chat 2")));
	Choice *qc3 = networkingSettings->Add(new Choice(n->T("Quick Chat 3")));
	Choice *qc4 = networkingSettings->Add(new Choice(n->T("Quick Chat 4")));
	Choice *qc5 = networkingSettings->Add(new Choice(n->T("Quick Chat 5")));
#elif PPSSPP_PLATFORM(ANDROID)
	ChoiceWithValueDisplay *qc1 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat[0], n->T("Quick Chat 1"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc2 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat[1], n->T("Quick Chat 2"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc3 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat[2], n->T("Quick Chat 3"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc4 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat[3], n->T("Quick Chat 4"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc5 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat[4], n->T("Quick Chat 5"), I18NCat::NONE));
#endif

#if (!defined(MOBILE_DEVICE) && !defined(USING_QT_UI)) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	qc1->SetEnabledFunc([] { return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat; });
	qc2->SetEnabledFunc([] { return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat; });
	qc3->SetEnabledFunc([] { return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat; });
	qc4->SetEnabledFunc([] { return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat; });
	qc5->SetEnabledFunc([] { return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat; });
#endif

#if defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		qc1->OnClick.Handle(this, &GameSettingsScreen::OnChangeQuickChat0);
		qc2->OnClick.Handle(this, &GameSettingsScreen::OnChangeQuickChat1);
		qc3->OnClick.Handle(this, &GameSettingsScreen::OnChangeQuickChat2);
		qc4->OnClick.Handle(this, &GameSettingsScreen::OnChangeQuickChat3);
		qc5->OnClick.Handle(this, &GameSettingsScreen::OnChangeQuickChat4);
	}
#endif

	networkingSettings->Add(new ItemHeader(n->T("Misc", "Misc (default = compatibility)")));
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iPortOffset, 0, 60000, 10000, n->T("Port offset", "Port offset (0 = PSP compatibility)"), 100, screenManager()));
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iMinTimeout, 0, 15000, 0, n->T("Minimum Timeout", "Minimum Timeout (override in ms, 0 = default)"), 50, screenManager()))->SetFormat(di->T("%d ms"));
	networkingSettings->Add(new CheckBox(&g_Config.bForcedFirstConnect, n->T("Forced First Connect", "Forced First Connect (faster Connect)")));
	networkingSettings->Add(new CheckBox(&g_Config.bAllowSpeedControlWhileConnected, n->T("Allow speed control while connected (not recommended)")));
}

void GameSettingsScreen::CreateToolsSettings(UI::ViewGroup *tools) {
	using namespace UI;

	auto sa = GetI18NCategory(I18NCat::SAVEDATA);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	tools->Add(new ItemHeader(ms->T("Tools")));

	const bool showRetroAchievements = System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR;
	if (showRetroAchievements) {
		auto retro = tools->Add(new Choice(sy->T("RetroAchievements")));
		retro->OnClick.Add([=](UI::EventParams &) -> void {
			screenManager()->push(new RetroAchievementsSettingsScreen(gamePath_));
		});
		retro->SetIconRight(ImageID("I_RETROACHIEVEMENTS_LOGO"));
	}

	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	tools->Add(new Choice(sa->T("Savedata Manager")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new SavedataScreen(gamePath_));
	});
	tools->Add(new Choice(dev->T("System Information")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new SystemInfoScreen(gamePath_));
	});
	tools->Add(new Choice(sy->T("Developer Tools")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new DeveloperToolsScreen(gamePath_));
	});
	tools->Add(new Choice(ri->T("Remote disc streaming")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new RemoteISOScreen(gamePath_));
	});
}

void GameSettingsScreen::CreateSystemSettings(UI::ViewGroup *systemSettings) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto vr = GetI18NCategory(I18NCat::VR);
	auto th = GetI18NCategory(I18NCat::THEMES);
	auto psps = GetI18NCategory(I18NCat::PSPSETTINGS);  // TODO: Should move more into this section.

	systemSettings->Add(new ItemHeader(sy->T("UI")));

	auto langCodeToName = [](std::string_view value) -> std::string {
		auto &mapping = GetLangValuesMapping();
		auto iter = mapping.find(value);
		if (iter != mapping.end()) {
			return iter->second.first;
		}
		return std::string(value);
	};

	systemSettings->Add(new ChoiceWithValueDisplay(&g_Config.sLanguageIni, sy->T("Language"), langCodeToName))->OnClick.Add([&](UI::EventParams &e) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto langScreen = new NewLanguageScreen(sy->T("Language"));
		// The actual switching is handled in OnCompleted in NewLanguageScreen.
		if (e.v)
			langScreen->SetPopupOrigin(e.v);
		screenManager()->push(langScreen);
	});

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};
	PopupMultiChoice *switchMode = systemSettings->Add(new PopupMultiChoice(&g_Config.iAppSwitchMode, sy->T("App switching mode"), indicator, 0, ARRAY_SIZE(indicator), I18NCat::SYSTEM, screenManager()));
	switchMode->OnChoice.Add([](EventParams &e) {
		System_Notify(SystemNotification::APP_SWITCH_MODE_CHANGED);
	});
#endif

	PopupSliderChoice *uiScale = systemSettings->Add(new PopupSliderChoice(&g_Config.iUIScaleFactor, -8, 8, 0, sy->T("UI size adjustment (DPI)"), screenManager()));
	uiScale->SetZeroLabel(sy->T("Off"));
	UIContext *ctx = screenManager()->getUIContext();
	uiScale->OnChange.Add([ctx](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		ctx->InvalidateAtlas();
		NativeResized();
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	Choice *backgroundChoice = nullptr;
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		backgroundChoice = systemSettings->Add(new Choice(sy->T("Clear UI background")));
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		backgroundChoice = systemSettings->Add(new Choice(sy->T("Set UI background...")));
	}
	if (backgroundChoice) {
		backgroundChoice->OnClick.Handle(this, &GameSettingsScreen::OnChangeBackground);
	}

	systemSettings->Add(new CheckBox(&g_Config.bTransparentBackground, sy->T("Transparent UI background")));

	// Shared with achievements.
	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

	systemSettings->Add(new PopupMultiChoice(&g_Config.iNotificationPos, sy->T("Notification screen position"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()));

	static const char *backgroundAnimations[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background", "Bouncing icon", "Colored floating symbols" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iBackgroundAnimation, sy->T("UI background animation"), backgroundAnimations, 0, ARRAY_SIZE(backgroundAnimations), I18NCat::SYSTEM, screenManager()));

	PopupMultiChoiceDynamic *theme = systemSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, sy->T("Theme"), GetThemeInfoNames(), I18NCat::THEMES, screenManager()));
	theme->OnChoice.Add([](EventParams &e) {
		UpdateTheme();
		// Reset the tint/saturation if the theme changed.
		if (e.b) {
			g_Config.fUITint = 0.0f;
			g_Config.fUISaturation = 1.0f;
		}
	});

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		PopupSliderChoiceFloat *tint = new PopupSliderChoiceFloat(&g_Config.fUITint, 0.0f, 1.0f, 0.0f, sy->T("Color tint"), 0.01f, screenManager());
		tint->SetHasDropShadow(false);
		tint->SetLiveUpdate(true);
		systemSettings->Add(tint);
		PopupSliderChoiceFloat *saturation = new PopupSliderChoiceFloat(&g_Config.fUISaturation, 0.0f, 2.0f, 1.0f, sy->T("Color saturation"), 0.01f, screenManager());
		saturation->SetHasDropShadow(false);
		saturation->SetLiveUpdate(true);
		systemSettings->Add(saturation);
	}

	systemSettings->Add(new ItemHeader(sy->T("PSP Memory Stick")));

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		systemSettings->Add(new Choice(sy->T("Show Memory Stick folder")))->OnClick.Add([](UI::EventParams &p) {
			System_LaunchUrl(LaunchUrlType::LOCAL_FOLDER, g_Config.memStickDirectory.ToString());
		});
	}

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	bool showItHere = true;
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	if (g_Config.memStickDirectory == DarwinFileSystemServices::defaultMemoryStickPath()) {
		// We still keep a way to access it on the developer tools screen.
		showItHere = false;
	}
#endif
	if (showItHere) {
		systemSettings->Add(new Choice(sy->T("Set Memory Stick folder")))->OnClick.Add(
			[=](UI::EventParams &) {
				SetMemStickDirDarwin(GetRequesterToken());
			});	
	}
#endif

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR) {
		memstickDisplay_ = g_Config.memStickDirectory.ToVisualString();
		auto memstickPath = systemSettings->Add(new ChoiceWithValueDisplay(&memstickDisplay_, sy->T("Memory Stick folder"), I18NCat::NONE));
		memstickPath->SetEnabled(!PSP_IsInited());
		memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnShowMemstickScreen);

		// Display USB path for convenience.
		std::string usbPath;
		if (PathToVisualUsbPath(g_Config.memStickDirectory, usbPath)) {
			if (usbPath.empty()) {
				// Probably it's just the root. So let's add PSP to make it clear.
				usbPath = "/PSP";
			}
		}
	}
#elif defined(_WIN32)
#if PPSSPP_PLATFORM(UWP)
	memstickDisplay_ = g_Config.memStickDirectory.ToVisualString();
	auto memstickPath = systemSettings->Add(new ChoiceWithValueDisplay(&memstickDisplay_, sy->T("Memory Stick folder"), I18NCat::NONE));
	memstickPath->SetEnabled(!PSP_IsInited());
	memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnShowMemstickScreen);
#else
	SavePathInMyDocumentChoice = systemSettings->Add(new CheckBox(&installed_, sy->T("Memory Stick in My Documents")));
	SavePathInMyDocumentChoice->SetEnabled(!PSP_IsInited());
	SavePathInMyDocumentChoice->OnClick.Handle(this, &GameSettingsScreen::OnMemoryStickMyDoc);
	SavePathInOtherChoice = systemSettings->Add(new CheckBox(&otherinstalled_, sy->T("Memory Stick in installed.txt")));
	SavePathInOtherChoice->SetEnabled(false);
	SavePathInOtherChoice->OnClick.Handle(this, &GameSettingsScreen::OnMemoryStickOther);
	const bool myDocsExists = W32Util::UserDocumentsPath().size() != 0;

	const Path &PPSSPPpath = File::GetExeDirectory();
	const Path installedFile = PPSSPPpath / "installed.txt";
	installed_ = File::Exists(installedFile);
	otherinstalled_ = false;
	if (!installed_ && myDocsExists) {
		if (File::CreateEmptyFile(PPSSPPpath / "installedTEMP.txt")) {
			// Disable the setting whether cannot create & delete file
			if (!(File::Delete(PPSSPPpath / "installedTEMP.txt")))
				SavePathInMyDocumentChoice->SetEnabled(false);
			else
				SavePathInOtherChoice->SetEnabled(!PSP_IsInited());
		} else
			SavePathInMyDocumentChoice->SetEnabled(false);
	} else {
		if (installed_ && myDocsExists) {
			FILE *testInstalled = File::OpenCFile(installedFile, "rt");
			if (testInstalled) {
				char temp[2048];
				char *tempStr = fgets(temp, sizeof(temp), testInstalled);
				// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
				if (tempStr && strncmp(tempStr, "\xEF\xBB\xBF", 3) == 0) {
					tempStr += 3;
				}
				SavePathInOtherChoice->SetEnabled(!PSP_IsInited());
				if (tempStr && strlen(tempStr) != 0 && strcmp(tempStr, "\n") != 0) {
					installed_ = false;
					otherinstalled_ = true;
				}
				fclose(testInstalled);
			}
		} else if (!myDocsExists) {
			SavePathInMyDocumentChoice->SetEnabled(false);
		}
	}
#endif
#endif
	systemSettings->Add(new CheckBox(&g_Config.bMemStickInserted, sy->T("Memory Stick inserted")));
	UI::PopupSliderChoice *sizeChoice = systemSettings->Add(new PopupSliderChoice(&g_Config.iMemStickSizeGB, 1, 32, 16, sy->T("Memory Stick size", "Memory Stick size"), screenManager(), "GB"));
	sizeChoice->SetFormat("%d GB");

	systemSettings->Add(new ItemHeader(sy->T("Help the PPSSPP team")));
	if (!enableReportsSet_)
		enableReports_ = Reporting::IsEnabled();
	enableReportsSet_ = true;
	CheckBox *enableReportsCheckbox;
	enableReportsCheckbox = new CheckBox(&enableReports_, sy->T("Enable Compatibility Server Reports"));
	enableReportsCheckbox->SetEnabledFunc([]() { return Reporting::IsSupported(); });
	systemSettings->Add(enableReportsCheckbox);

	systemSettings->Add(new ItemHeader(sy->T("Emulation")));

	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, sy->T("Fast Memory", "Fast Memory")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);
	systemSettings->Add(new CheckBox(&g_Config.bIgnoreBadMemAccess, sy->T("Ignore bad memory accesses")));

	static const char *ioTimingMethods[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays", "Simulate UMD slow reading speed"};
	View *ioTimingMethod = systemSettings->Add(new PopupMultiChoice(&g_Config.iIOTimingMethod, sy->T("I/O timing method"), ioTimingMethods, 0, ARRAY_SIZE(ioTimingMethods), I18NCat::SYSTEM, screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bForceLagSync, sy->T("Force real clock sync (slower, less lag)")))->SetDisabledPtr(&g_Config.bAutoFrameSkip);
	PopupSliderChoice *lockedMhz = systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, 0, sy->T("Change CPU Clock", "Change CPU Clock (unstable)"), screenManager(), sy->T("MHz, 0:default")));
	lockedMhz->SetZeroLabel(sy->T("Auto"));

	auto sa = GetI18NCategory(I18NCat::SAVEDATA);

	systemSettings->Add(new ItemHeader(sa->T("Save states")));  // Borrow this string from the savedata manager

	systemSettings->Add(new CheckBox(&g_Config.bEnableStateUndo, sy->T("Savestate slot backups")));

	PopupSliderChoice* savestateSlotCount = systemSettings->Add(new PopupSliderChoice(&g_Config.iSaveStateSlotCount, 1, 30, 5, sy->T("Savestate slot count"), screenManager()));
	savestateSlotCount->OnChange.Add([](UI::EventParams &e) {
		System_Notify(SystemNotification::UI);
	});

	// NOTE: We will soon support more states, but we'll keep this niche feature limited to the first five.
	static const char *autoLoadSaveStateChoices[] = {"Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5"};
	PopupMultiChoice *autoloadSaveState = systemSettings->Add(new PopupMultiChoice(&g_Config.iAutoLoadSaveState, sy->T("Auto load savestate"), autoLoadSaveStateChoices, 0, ARRAY_SIZE(autoLoadSaveStateChoices), I18NCat::SYSTEM, screenManager()));
	if (g_Config.iAutoLoadSaveState != 1) {
		autoloadSaveState->HideChoice(1);  // Hide "Oldest Save" if not using that mode. It doesn't make sense.
	}

	PopupSliderChoice *rewindInterval = systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindSnapshotInterval, 0, 60, 0, sy->T("Rewind Snapshot Interval"), screenManager(), di->T("seconds, 0:off")));
	rewindInterval->SetFormat(di->T("%d seconds"));
	rewindInterval->SetZeroLabel(sy->T("Off"));

	systemSettings->Add(new ItemHeader(sy->T("General")));

	PopupSliderChoice *exitConfirmation = systemSettings->Add(new PopupSliderChoice(&g_Config.iAskForExitConfirmationAfterSeconds, 0, 1200, 300, sy->T("Ask for exit confirmation after seconds"), screenManager(), "s"));
	exitConfirmation->SetZeroLabel(sy->T("Off"));

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		auto co = GetI18NCategory(I18NCat::CONTROLS);

		// Display rotation 
		AddRotationPicker(screenManager(), systemSettings, true);

		if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
			systemSettings->Add(new CheckBox(&g_Config.bSustainedPerformanceMode, sy->T("Sustained performance mode")))->OnClick.Handle(this, &GameSettingsScreen::OnSustainedPerformanceModeChange);
		}
	}

	systemSettings->Add(new Choice(sy->T("Restore Default Settings")))->OnClick.Handle(this, &GameSettingsScreen::OnRestoreDefaultSettings);

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		systemSettings->Add(new CheckBox(&g_Config.bBypassOSKWithKeyboard, sy->T("Use system native keyboard")));

	if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
		systemSettings->Add(new CheckBox(&g_Config.bCacheFullIsoInRam, sy->T("Cache full ISO in RAM")))->SetEnabled(!PSP_IsInited());
	}

	systemSettings->Add(new CheckBox(&g_Config.bCheckForNewVersion, sy->T("VersionCheck", "Check for new versions of PPSSPP")));
	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, sy->T("Screenshots as PNG")));
	static const char *screenshotModeChoices[] = { "Final processed image", "Raw game image" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenshotMode, sy->T("Screenshot mode"), screenshotModeChoices, 0, ARRAY_SIZE(screenshotModeChoices), I18NCat::SYSTEM, screenManager()));
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS)
	systemSettings->Add(new CheckBox(&g_Config.bPauseOnLostFocus, sy->T("Pause when not focused")));
#endif

	systemSettings->Add(new ItemHeader(sy->T("Cheats", "Cheats")));
	systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, sy->T("Enable Cheats")));
	systemSettings->Add(new CheckBox(&g_Config.bEnablePlugins, sy->T("Enable plugins")));

	systemSettings->Add(new ItemHeader(sy->T("PSP Settings")));

	// The ordering here is simply mapping directly to PSP_SYSTEMPARAM_LANGUAGE_*.
	static const char *defaultLanguages[] = { "Auto", "Japanese", "English", "French", "Spanish", "German", "Italian", "Dutch", "Portuguese", "Russian", "Korean", "Chinese (traditional)", "Chinese (simplified)" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iLanguage, psps->T("Game language"), defaultLanguages, -1, ARRAY_SIZE(defaultLanguages), I18NCat::PSPSETTINGS, screenManager()));
	static const char *models[] = { "PSP-1000", "PSP-2000/3000" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iPSPModel, sy->T("PSP Model"), models, 0, ARRAY_SIZE(models), I18NCat::SYSTEM, screenManager()))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sNickName, sy->T("Change Nickname"), "", 32, screenManager()))->OnChange.Add([](UI::EventParams &e) {
		// Copy to infrastructure name if valid and not already set.
		if (g_Config.sInfrastructureUsername.empty()) {
			if (g_Config.sNickName == SanitizeString(g_Config.sNickName, StringRestriction::AlphaNumDashUnderscore, 3, 16)) {
				g_Config.sInfrastructureUsername = g_Config.sNickName;
			}
		}
	});
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, sy->T("Daylight savings")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, sy->T("Date Format"), dateFormat, 0, ARRAY_SIZE(dateFormat), I18NCat::SYSTEM, screenManager()));
	static const char *timeFormat[] = { "24HR", "12HR" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, sy->T("Time Format"), timeFormat, 0, ARRAY_SIZE(timeFormat), I18NCat::SYSTEM, screenManager()));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, sy->T("Confirmation Button"), buttonPref, 0, ARRAY_SIZE(buttonPref), I18NCat::SYSTEM, screenManager()));

#if defined(_WIN32) || (defined(USING_QT_UI) && !defined(MOBILE_DEVICE))
	systemSettings->Add(new ItemHeader(sy->T("Recording")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpFrames, sy->T("Record Display")));
	systemSettings->Add(new CheckBox(&g_Config.bUseFFV1, sy->T("Use Lossless Video Codec (FFV1)")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpVideoOutput, sy->T("Use output buffer (with overlay) for recording")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpAudio, sy->T("Record Audio")));
	systemSettings->Add(new CheckBox(&g_Config.bSaveLoadResetsAVdumping, sy->T("Reset Recording on Save/Load State")));
#endif
}

void GameSettingsScreen::CreateVRSettings(UI::ViewGroup *vrSettings) {
	using namespace UI;

	auto vr = GetI18NCategory(I18NCat::VR);
	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	if (deviceType == DEVICE_TYPE_VR) {
		vrSettings->Add(new ItemHeader(vr->T("Virtual reality")));
		vrSettings->Add(new CheckBox(&g_Config.bEnableVR, vr->T("Virtual reality")));
		vrSettings->Add(new CheckBox(&g_Config.bEnable6DoF, vr->T("6DoF movement")));
		vrSettings->Add(new CheckBox(&g_Config.bEnableStereo, vr->T("Stereoscopic vision (Experimental)")));
		vrSettings->Add(new CheckBox(&g_Config.bEnableImmersiveVR, vr->T("Enable immersive mode")));
		if (IsPassthroughSupported()) {
			vrSettings->Add(new CheckBox(&g_Config.bPassthrough, vr->T("Enable passthrough")));
		}
		vrSettings->Add(new CheckBox(&g_Config.bForce72Hz, vr->T("Force 72Hz update")));
	}

	vrSettings->Add(new ItemHeader(vr->T("VR camera")));
	if (deviceType == DEVICE_TYPE_VR) {
		vrSettings->Add(new PopupSliderChoiceFloat(&g_Config.fCanvasDistance, 1.0f, 15.0f, 12.0f, vr->T("Distance to 2D menus and scenes"), 1.0f, screenManager(), ""));
		vrSettings->Add(new PopupSliderChoiceFloat(&g_Config.fCanvas3DDistance, 1.0f, 15.0f, 3.0f, vr->T("Distance to 3D scenes when VR disabled"), 1.0f, screenManager(), ""));
	}
	vrSettings->Add(new PopupSliderChoiceFloat(&g_Config.fFieldOfViewPercentage, 100.0f, 200.0f, 100.0f, vr->T("Field of view scale"), 10.0f, screenManager(), vr->T("% of native FoV")));
	vrSettings->Add(new CheckBox(&g_Config.bRescaleHUD, vr->T("Heads-up display detection")));
	PopupSliderChoiceFloat* vrHudScale = vrSettings->Add(new PopupSliderChoiceFloat(&g_Config.fHeadUpDisplayScale, 0.0f, 1.5f, 0.3f, vr->T("Heads-up display scale"), 0.1f, screenManager(), ""));
	vrHudScale->SetEnabledPtr(&g_Config.bRescaleHUD);
	vrSettings->Add(new CheckBox(&g_Config.bManualForceVR, vr->T("Manual switching between flat screen and VR using SCREEN key")));
}

void GameSettingsScreen::OnAdhocGuides(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	std::string url(n->T("MultiplayerHowToURL", "https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP"));
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, url.c_str());
}

void GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		System_RecreateActivity();
	}
}

void GameSettingsScreen::OnSustainedPerformanceModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::SUSTAINED_PERF_CHANGE);
}

void GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

void GameSettingsScreen::OnShowMemstickScreen(UI::EventParams &e) {
	screenManager()->push(new MemStickScreen(false));
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

void GameSettingsScreen::OnMemoryStickMyDoc(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	const Path installedFile = PPSSPPpath / "installed.txt";
	installed_ = File::Exists(installedFile);
	if (otherinstalled_) {
		File::Delete(PPSSPPpath / "installed.txt");
		File::CreateEmptyFile(PPSSPPpath / "installed.txt");
		otherinstalled_ = false;
		const std::string myDocsPath = W32Util::UserDocumentsPath() + "/PPSSPP/";
		g_Config.memStickDirectory = Path(myDocsPath);
	} else if (installed_) {
		File::Delete(PPSSPPpath / "installed.txt");
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath / "memstick";
	} else {
		FILE *f = File::OpenCFile(PPSSPPpath / "installed.txt", "wb");
		if (f) {
			fclose(f);
		}

		const std::string myDocsPath = W32Util::UserDocumentsPath() + "/PPSSPP/";
		g_Config.memStickDirectory = Path(myDocsPath);
		installed_ = true;
	}
}

void GameSettingsScreen::OnMemoryStickOther(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	if (otherinstalled_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string initialPath = g_Config.memStickDirectory.ToCString();
		std::string folder = W32Util::BrowseForFolder2(MainWindow::GetHWND(), di->T("Choose PPSSPP save folder"), initialPath);
		if (folder.size()) {
			g_Config.memStickDirectory = Path(folder);
			FILE *f = File::OpenCFile(PPSSPPpath / "installed.txt", "wb");
			if (f) {
				std::string utfstring("\xEF\xBB\xBF");
				utfstring.append(folder);
				fwrite(utfstring.c_str(), 1, utfstring.length(), f);
				fclose(f);
			}
			installed_ = false;
		}
		else
			otherinstalled_ = false;
	}
	else {
		File::Delete(PPSSPPpath / "installed.txt");
		SavePathInMyDocumentChoice->SetEnabled(true);
		otherinstalled_ = false;
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath / "memstick";
	}
}

#endif

void GameSettingsScreen::OnChangeBackground(UI::EventParams &e) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";

	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		INFO_LOG(Log::UI, "Clearing background image.");
		// The button is in clear mode.
		File::Delete(bgPng);
		File::Delete(bgJpg);
		UIBackgroundShutdown();
		RecreateViews();
		return;
	}

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	System_BrowseForImage(GetRequesterToken(), sy->T("Set UI background..."), bgJpg, [=](const std::string &value, int converted) {
		if (converted == 1) {
			// The platform code converted and saved the file to the desired path already.
			INFO_LOG(Log::UI, "Converted file.");
		} else if (!value.empty()) {
			Path path(value);

			// Check the file format. Don't rely on the file extension here due to scoped storage URLs.
			FILE *f = File::OpenCFile(path, "rb");
			uint8_t buffer[8];
			ImageFileType type = ImageFileType::UNKNOWN;
			if (f != nullptr && 8 == fread(buffer, 1, ARRAY_SIZE(buffer), f)) {
				type = DetectImageFileType(buffer, ARRAY_SIZE(buffer));
			}

			std::string filename;
			switch (type) {
			case ImageFileType::JPEG:
				filename = "background.jpg";
				break;
			case ImageFileType::PNG:
				filename = "background.png";
				break;
			default:
				break;
			}

			if (!filename.empty()) {
				Path dest = GetSysDirectory(DIRECTORY_SYSTEM) / filename;
				File::Copy(Path(value), dest);
			} else {
				g_OSD.Show(OSDType::MESSAGE_ERROR, sy->T("Only JPG and PNG images are supported"), path.GetFilename(), 5.0);
			}
		}
		// It will init again automatically.  We can't init outside a frame on Vulkan.
		UIBackgroundShutdown();
		RecreateViews();
	});

	// Change to a browse or clear button.
}

void GameSettingsScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	bool recreate = false;
	if (result == DialogResult::DR_OK) {
		g_Config.iFpsLimit1 = iAlternateSpeedPercent1_ < 0 ? -1 : (iAlternateSpeedPercent1_ * 60) / 100;
		g_Config.iFpsLimit2 = iAlternateSpeedPercent2_ < 0 ? -1 : (iAlternateSpeedPercent2_ * 60) / 100;
		g_Config.iAnalogFpsLimit = (iAlternateSpeedPercentAnalog_ * 60) / 100;
		recreate = true;
	}

	// Show/hide the Analog Alternative Speed as appropriate - need to recreate views if this changed.
	bool mapped = KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true);
	if (mapped != analogSpeedMapped_) {
		analogSpeedMapped_ = mapped;
		recreate = true;
	}

	if (recreate) {
		RecreateViews();
	}
}

void GameSettingsScreen::CallbackMemstickFolder(bool yes) {
	if (yes) {
		Path memstickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
		std::string testWriteFile = pendingMemstickFolder_ + "/.write_verify_file";

		// Already, create away.
		if (!File::Exists(Path(pendingMemstickFolder_))) {
			File::CreateFullPath(Path(pendingMemstickFolder_));
		}
		if (!File::WriteDataToFile(true, "1", 1, Path(testWriteFile))) {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			g_OSD.Show(OSDType::MESSAGE_ERROR, sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), testWriteFile, 5.0);
			return;
		}
		File::Delete(Path(testWriteFile));

		if (!File::WriteDataToFile(true, pendingMemstickFolder_.c_str(), pendingMemstickFolder_.size(), memstickDirFile)) {
			WARN_LOG(Log::System, "Failed to write memstick folder to '%s'", memstickDirFile.c_str());
		} else {
			// Save so the settings, at least, are transferred.
			g_Config.memStickDirectory = Path(pendingMemstickFolder_);
			g_Config.Save("MemstickPathChanged");
		}
		screenManager()->RecreateAllViews();
	}
}

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath) {
	// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
	// the GPU backend code.
	g_Config.Save(why);
	std::string param = "--gamesettings";
	if (editThenRestore) {
		// We won't pass the gameID, so don't resume back into settings.
		param.clear();
	} else if (!gamePath.empty()) {
		param += " \"" + ReplaceAll(ReplaceAll(gamePath.ToString(), "\\", "\\\\"), "\"", "\\\"") + "\"";
	}
	// Make sure the new instance is considered the first.
	ShutdownInstanceCounter();
	System_RestartApp(param);
}

void GameSettingsScreen::TriggerRestartOrDo(std::function<void()> callback) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	screenManager()->push(new UI::MessagePopupScreen(di->T("Restart"), di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
		if (yes) {
			TriggerRestart("GameSettingsScreen::RenderingBackendYes", editGameSpecificThenRestore_, gamePath_);
		} else {
			callback();
		}
	}));
}

void GameSettingsScreen::OnRenderingBackend(UI::EventParams &e) {
	// It only makes sense to show the restart prompt if the backend was actually changed.
	if (g_Config.iGPUBackend != (int)GetGPUBackend()) {
		TriggerRestartOrDo([]() {
			g_Config.iGPUBackend = (int)GetGPUBackend();
		});
	}
}

void GameSettingsScreen::OnRenderingDevice(UI::EventParams &e) {
	// It only makes sense to show the restart prompt if the device was actually changed.
	std::string *deviceNameSetting = GPUDeviceNameSetting();
	if (deviceNameSetting && *deviceNameSetting != GetGPUBackendDevice()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		TriggerRestartOrDo([this]() {
			std::string *deviceNameSetting = GPUDeviceNameSetting();
			if (deviceNameSetting)
				*deviceNameSetting = GetGPUBackendDevice();
			// Needed to redraw the setting.
			RecreateViews();
		});
	}
}

void GameSettingsScreen::OnInflightFramesChoice(UI::EventParams &e) {
	if (g_Config.iInflightFrames != prevInflightFrames_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		TriggerRestartOrDo([this]() {
			g_Config.iInflightFrames = prevInflightFrames_;
		});
	}
}

void GameSettingsScreen::OnCameraDeviceChange(UI::EventParams& e) {
	Camera::onCameraDeviceChange();
}

void GameSettingsScreen::OnMicDeviceChange(UI::EventParams& e) {
	Microphone::onMicDeviceChange();
}

void GameSettingsScreen::OnAudioDevice(UI::EventParams &e) {
	auto a = GetI18NCategory(I18NCat::AUDIO);
	if (g_Config.sAudioDevice == a->T("Auto")) {
		g_Config.sAudioDevice.clear();
	}
	System_Notify(SystemNotification::AUDIO_RESET_DEVICE);
}

void GameSettingsScreen::OnChangeQuickChat0(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 1"), g_Config.sQuickChat[0], false, [](const std::string &value, int) {
		g_Config.sQuickChat[0] = value;
	});
}

void GameSettingsScreen::OnChangeQuickChat1(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 2"), g_Config.sQuickChat[1], false, [](const std::string &value, int) {
		g_Config.sQuickChat[1] = value;
	});
}

void GameSettingsScreen::OnChangeQuickChat2(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 3"), g_Config.sQuickChat[2], false, [](const std::string &value, int) {
		g_Config.sQuickChat[2] = value;
	});
}

void GameSettingsScreen::OnChangeQuickChat3(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 4"), g_Config.sQuickChat[3], false, [](const std::string &value, int) {
		g_Config.sQuickChat[3] = value;
	});
}

void GameSettingsScreen::OnChangeQuickChat4(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 5"), g_Config.sQuickChat[4], false, [](const std::string &value, int) {
		g_Config.sQuickChat[4] = value;
	});
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS);
	}
	System_Notify(SystemNotification::UI);
}

void GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (g_Config.IsGameSpecific()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		screenManager()->push(
			new PromptScreen(gamePath_, dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"),
				di->T("OK"), di->T("Cancel"), [this](bool yes) { CallbackRestoreDefaults(yes); }));
	} else {
		std::string_view title = sy->T("Restore Default Settings");
		screenManager()->push(new RestoreSettingsScreen(title));
	}
}

void HostnameSelectScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	LinearLayout *valueRow = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, Margins(0, 0, 0, 10)));

	addrView_ = new TextEdit(*value_, n->T("Hostname"), "");
	addrView_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	valueRow->Add(addrView_);
	parent->Add(valueRow);

	LinearLayout *buttonsRow1 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout *buttonsRow2 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	parent->Add(buttonsRow1);
	parent->Add(buttonsRow2);

	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_LEFT)));
	for (char c = '0'; c <= '9'; ++c) {
		char label[] = { c, '\0' };
		auto button = buttonsRow1->Add(new Button(label));
		button->OnClick.Handle(this, &HostnameSelectScreen::OnNumberClick);
		button->SetTag(label);
	}
	buttonsRow1->Add(new Button("."))->OnClick.Handle(this, &HostnameSelectScreen::OnPointClick);
	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_RIGHT)));

	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_LEFT)));
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		buttonsRow2->Add(new Button(di->T("Edit")))->OnClick.Handle(this, &HostnameSelectScreen::OnEditClick);
	}
	buttonsRow2->Add(new Button(di->T("Delete")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteClick);
	buttonsRow2->Add(new Button(di->T("Delete all")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteAllClick);
	buttonsRow2->Add(new Button(di->T("Toggle List")))->OnClick.Handle(this, &HostnameSelectScreen::OnShowIPListClick);
	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_RIGHT)));

	std::vector<std::string> listIP;
	if (listItems_) {
		listIP = *listItems_;
	}
	// Add non-editable items
	listIP.push_back("localhost");
	net::GetLocalIP4List(listIP);

	ipRows_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	ScrollView* scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout* innerView = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	if (listIP.size() > 0) {
		for (const auto& label : listIP) {
			// Filter out IP prefixed with "127." and "169.254." also "0." since they can be rendundant or unusable
			if (label.find("127.") != 0 && label.find("169.254.") != 0 && label.find("0.") != 0) {
				auto button = innerView->Add(new Button(label, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
				button->OnClick.Handle(this, &HostnameSelectScreen::OnIPClick);
				button->SetTag(label);
			}
		}
	}
	scrollView->Add(innerView);
	ipRows_->Add(scrollView);
	ipRows_->SetVisibility(V_GONE);
	parent->Add(ipRows_);
	listIP.clear(); listIP.shrink_to_fit();

	progressView_ = parent->Add(new TextView(n->T("Validating address..."), ALIGN_HCENTER, false, new LinearLayoutParams(Margins(0, 5, 0, 0))));
	progressView_->SetVisibility(UI::V_GONE);
}

void HostnameSelectScreen::SendEditKey(InputKeyCode keyCode, KeyInputFlags flags) {
	auto oldView = UI::GetFocusedView();
	UI::SetFocusedView(addrView_);
	KeyInput fakeKey{ DEVICE_ID_KEYBOARD, keyCode, KeyInputFlags::DOWN | flags };
	addrView_->Key(fakeKey);
	UI::SetFocusedView(oldView);
}

void HostnameSelectScreen::OnNumberClick(UI::EventParams &e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() == 1 && text[0] >= '0' && text[0] <= '9') {
		SendEditKey((InputKeyCode)text[0], KeyInputFlags::CHAR);  // ASCII for digits match keycodes.
	}
}

void HostnameSelectScreen::OnPointClick(UI::EventParams &e) {
	SendEditKey((InputKeyCode)'.', KeyInputFlags::CHAR);
}

void HostnameSelectScreen::OnDeleteClick(UI::EventParams &e) {
	SendEditKey(NKCODE_DEL);
}

void HostnameSelectScreen::OnDeleteAllClick(UI::EventParams &e) {
	addrView_->SetText("");
}

void HostnameSelectScreen::OnEditClick(UI::EventParams& e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("proAdhocServer Address:"), addrView_->GetText(), false, [this](const std::string& value, int) {
		addrView_->SetText(value);
	});
}

void HostnameSelectScreen::OnShowIPListClick(UI::EventParams& e) {
	if (ipRows_->GetVisibility() == UI::V_GONE) {
		ipRows_->SetVisibility(UI::V_VISIBLE);
	}
	else {
		ipRows_->SetVisibility(UI::V_GONE);
	}
}

void HostnameSelectScreen::OnIPClick(UI::EventParams& e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() > 0) {
		addrView_->SetText(text);
		// Copy the IP to clipboard for the host to easily share their IP through chatting apps.
		System_CopyStringToClipboard(text);
	}
}

void HostnameSelectScreen::ResolverThread() {
	std::unique_lock<std::mutex> guard(resolverLock_);

	while (resolverState_ != ResolverState::QUIT) {
		resolverCond_.wait(guard);

		if (resolverState_ == ResolverState::QUEUED) {
			resolverState_ = ResolverState::PROGRESS;

			addrinfo *resolved = nullptr;
			std::string err;
			toResolveResult_ = net::DNSResolve(toResolve_, "80", &resolved, err);
			if (resolved)
				net::DNSResolveFree(resolved);

			resolverState_ = ResolverState::READY;
		}
	}
}

bool HostnameSelectScreen::CanComplete(DialogResult result) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	if (result != DR_OK)
		return true;

	std::string value = addrView_->GetText();
	if (lastResolved_ == value) {
		return true;
	}

	// Currently running.
	if (resolverState_ == ResolverState::PROGRESS)
		return false;

	std::lock_guard<std::mutex> guard(resolverLock_);
	switch (resolverState_) {
	case ResolverState::PROGRESS:
	case ResolverState::QUIT:
		return false;

	case ResolverState::QUEUED:
	case ResolverState::WAITING:
		break;

	case ResolverState::READY:
		if (toResolve_ == value) {
			// Reset the state, nothing there now.
			resolverState_ = ResolverState::WAITING;
			toResolve_.clear();
			lastResolved_ = value;
			lastResolvedResult_ = toResolveResult_;

			if (lastResolvedResult_) {
				progressView_->SetVisibility(UI::V_GONE);
			} else {
				progressView_->SetText(n->T("Invalid IP or hostname"));
				progressView_->SetTextColor(0xFF3030FF);
				progressView_->SetVisibility(UI::V_VISIBLE);
			}
			return true;
		}

		// Throw away that last result, it was for a different value.
		break;
	}

	resolverState_ = ResolverState::QUEUED;
	toResolve_ = value;
	resolverCond_.notify_one();

	progressView_->SetText(n->T("Validating address..."));
	progressView_->SetTextColor(0xFFFFFFFF);
	progressView_->SetVisibility(UI::V_VISIBLE);

	return false;
}

void HostnameSelectScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK)
		*value_ = StripSpaces(addrView_->GetText());
}

void GestureMappingScreen::CreateTabs() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	AddTab("Gesture", di->T("Left side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 0, GetDeviceOrientation() == DeviceOrientation::Portrait); });
	AddTab("Gesture", di->T("Right side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 1, GetDeviceOrientation() == DeviceOrientation::Portrait); });
}

void GestureMappingScreen::CreateGestureTab(UI::LinearLayout *vert, int zoneIndex, bool portrait) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	static const char *gestureButton[ARRAY_SIZE(GestureKey::keyList) + 1];
	gestureButton[0] = "None";
	for (int i = 1; i < ARRAY_SIZE(gestureButton); ++i) {
		gestureButton[i] = KeyMap::GetPspButtonNameCharPointer(GestureKey::keyList[i - 1]);
	}

	GestureControlConfig &zone = g_Config.gestureControls[zoneIndex];

	TopBarFlags flags = TopBarFlags::NoBackButton;
	if (portrait) {
		flags |= TopBarFlags::Portrait;
	}
	vert->Add(new TopBar(*screenManager()->getUIContext(), flags, ApplySafeSubstitutions("%1: %2", co->T("Gesture"), di->T(zoneIndex == 0 ? "Left side" : "Right side"))));
	vert->Add(new CheckBox(&zone.bGestureControlEnabled, co->T("Enable gesture control")));

	vert->Add(new ItemHeader(co->T("Swipe")));
	vert->Add(new PopupMultiChoice(&zone.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&zone.fSwipeSensitivity, 0.01f, 2.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&zone.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Double tap")));
	vert->Add(new PopupMultiChoice(&zone.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Analog Stick")));
	vert->Add(new CheckBox(&zone.bAnalogGesture, co->T("Enable analog stick gesture")));
	vert->Add(new PopupSliderChoiceFloat(&zone.fAnalogGestureSensitivity, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bAnalogGesture);
}

RestoreSettingsScreen::RestoreSettingsScreen(std::string_view title)
	: PopupScreen(title, "OK", "Cancel") {}

void RestoreSettingsScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	// Carefully re-use various translations.
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	std::string_view text = dev->T(
		"RestoreDefaultSettings",
		"Restore these settings back to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings.");

	TextView *textView = parent->Add(new TextView(text, FLAG_WRAP_TEXT, false));
	textView->SetPadding(Margins(10));

	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::SETTINGS, ga->T("Game Settings")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::CONTROLS, ms->T("Controls")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::RECENT, mm->T("Recent")));
}

void RestoreSettingsScreen::OnCompleted(DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.RestoreDefaults((RestoreSettingsBits)restoreFlags_);
	}
}
