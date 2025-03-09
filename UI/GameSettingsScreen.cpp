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
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/System/Display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Math/curves.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "UI/EmuScreen.h"
#include "UI/DriverManagerScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/GPUDriverTestScreen.h"
#include "UI/MemStickScreen.h"
#include "UI/Theme.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/OnScreenDisplay.h"
#include "UI/DiscordIntegration.h"
#include "UI/BackgroundAudio.h"

#include "Common/File/FileUtil.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/OSVersion.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/KeyMap.h"
#include "Core/TiltEventProcessor.h"
#include "Core/Instance.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/WebServer.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/Common/PostShader.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"

#include "Core/Core.h" // for Core_IsStepping
#include "Core/MIPS/MIPSTracer.h"

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

static bool CheckKgslPresent() {
    constexpr auto KgslPath{"/dev/kgsl-3d0"};

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

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
static void SetMemStickDirDarwin(int requesterToken) {
	auto initialPath = g_Config.memStickDirectory;
	INFO_LOG(Log::System, "Current path: %s", initialPath.c_str());
	System_BrowseForFolder(requesterToken, "", initialPath, [](const std::string &value, int) {
		INFO_LOG(Log::System, "Selected path: %s", value.c_str());
		DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path(value));
	});
}
#endif

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: TabbedUIDialogScreenWithGameBackground(gamePath), gameID_(gameID), editThenRestore_(editThenRestore) {
	prevInflightFrames_ = g_Config.iInflightFrames;
	analogSpeedMapped_ = KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true);
}

// This needs before run CheckGPUFeatures()
// TODO: Remove this if fix the issue
bool CheckSupportShaderTessellationGLES() {
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

bool DoesBackendSupportHWTess() {
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

bool PathToVisualUsbPath(Path path, std::string &outPath) {
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

static std::string PostShaderTranslateName(std::string_view value) {
	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
		return std::string(ps->T(value, info->name));
	} else {
		return std::string(value);
	}
}

void GameSettingsScreen::PreCreateViews() {
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();

	if (editThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.loadGameConfig(gameID_, info->GetTitle());
	}

	iAlternateSpeedPercent1_ = g_Config.iFpsLimit1 < 0 ? -1 : (g_Config.iFpsLimit1 * 100) / 60;
	iAlternateSpeedPercent2_ = g_Config.iFpsLimit2 < 0 ? -1 : (g_Config.iFpsLimit2 * 100) / 60;
	iAlternateSpeedPercentAnalog_ = (g_Config.iAnalogFpsLimit * 100) / 60;
}

void GameSettingsScreen::CreateTabs() {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	LinearLayout *graphicsSettings = AddTab("GameSettingsGraphics", ms->T("Graphics"));
	CreateGraphicsSettings(graphicsSettings);

	LinearLayout *controlsSettings = AddTab("GameSettingsControls", ms->T("Controls"));
	CreateControlsSettings(controlsSettings);

	LinearLayout *audioSettings = AddTab("GameSettingsAudio", ms->T("Audio"));
	CreateAudioSettings(audioSettings);

	LinearLayout *networkingSettings = AddTab("GameSettingsNetworking", ms->T("Networking"));
	CreateNetworkingSettings(networkingSettings);

	LinearLayout *tools = AddTab("GameSettingsTools", ms->T("Tools"));
	CreateToolsSettings(tools);

	LinearLayout *systemSettings = AddTab("GameSettingsSystem", ms->T("System"));
	systemSettings->SetSpacing(0);
	CreateSystemSettings(systemSettings);

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	if ((deviceType == DEVICE_TYPE_VR) || g_Config.bForceVR) {
		LinearLayout *vrSettings = AddTab("GameSettingsVR", ms->T("VR"));
		CreateVRSettings(vrSettings);
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
	renderingBackendChoice->HideChoice(1);
	renderingBackendChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);

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
	resolutionChoice_ = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInternalResolution, gr->T("Rendering Resolution"), internalResolutions, 0, ARRAY_SIZE(internalResolutions), I18NCat::GRAPHICS, screenManager()));
	resolutionChoice_->OnChoice.Handle(this, &GameSettingsScreen::OnResolutionChange);
	resolutionChoice_->SetEnabledFunc([] {
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
		msaaChoice->OnChoice.Add([&](UI::EventParams &) -> UI::EventReturn {
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
			return UI::EVENT_DONE;
		});
		msaaChoice->SetEnabledFunc([] {
			return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
		});
		if (g_Config.iMultiSampleLevel > 1 && draw->GetDeviceCaps().isTilingGPU) {
			msaaChoice->SetIcon(ImageID("I_WARNING"), 0.7f);
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
			return UI::EVENT_DONE;
		});
	}
#endif

	if (deviceType != DEVICE_TYPE_VR) {
#if !defined(MOBILE_DEVICE)
		graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnFullscreenChange);
		if (System_GetPropertyInt(SYSPROP_DISPLAY_COUNT) > 1) {
			CheckBox *fullscreenMulti = new CheckBox(&g_Config.bFullScreenMulti, gr->T("Use all displays"));
			fullscreenMulti->SetEnabledFunc([] {
				return g_Config.UseFullScreen();
			});
			graphicsSettings->Add(fullscreenMulti)->OnClick.Handle(this, &GameSettingsScreen::OnFullscreenMultiChange);
		}
#endif

	// All backends support FIFO. Check if any immediate modes are supported, if so we can allow the user to choose.
	if (draw->GetDeviceCaps().presentModesSupported & (Draw::PresentMode::IMMEDIATE | Draw::PresentMode::MAILBOX)) {
		CheckBox *vSync = graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gr->T("VSync")));
		vSync->OnClick.Add([=](EventParams &e) {
			NativeResized();
			return UI::EVENT_CONTINUE;
		});
	}

#if PPSSPP_PLATFORM(ANDROID)
		// Hide Immersive Mode on pre-kitkat Android
		if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
			// Let's reuse the Fullscreen translation string from desktop.
			graphicsSettings->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnImmersiveModeChange);
		}
#endif
		// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
		displayEditor_ = graphicsSettings->Add(new Choice(gr->T("Display layout & effects")));
		displayEditor_->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			screenManager()->push(new DisplayLayoutScreen(gamePath_));
			return UI::EVENT_DONE;
		});
	}

	graphicsSettings->Add(new CheckBox(&g_Config.bReplaceTextures, dev->T("Replace textures")));

	graphicsSettings->Add(new ItemHeader(gr->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gr->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), I18NCat::GRAPHICS, screenManager()));
	static const char *frameSkipType[] = {"Number of Frames", "Percent of FPS"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkipType, gr->T("Frame Skipping Type"), frameSkipType, 0, ARRAY_SIZE(frameSkipType), I18NCat::GRAPHICS, screenManager()));
	frameSkipAuto_ = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gr->T("Auto FrameSkip")));
	frameSkipAuto_->OnClick.Handle(this, &GameSettingsScreen::OnAutoFrameskip);

	PopupSliderChoice *altSpeed1 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent1_, 0, 1000, NO_DEFAULT_INT, gr->T("Alternative Speed", "Alternative speed"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed1->SetFormat("%i%%");
	altSpeed1->SetZeroLabel(gr->T("Unlimited"));
	altSpeed1->SetNegativeDisable(gr->T("Disabled"));

	PopupSliderChoice *altSpeed2 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent2_, 0, 1000, NO_DEFAULT_INT, gr->T("Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed2->SetFormat("%i%%");
	altSpeed2->SetZeroLabel(gr->T("Unlimited"));
	altSpeed2->SetNegativeDisable(gr->T("Disabled"));

	if (analogSpeedMapped_) {
		PopupSliderChoice *analogSpeed = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercentAnalog_, 1, 1000, NO_DEFAULT_INT, gr->T("Analog Alternative Speed", "Analog alternative speed (in %)"), 5, screenManager(), gr->T("%")));
		altSpeed2->SetFormat("%i%%");
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Speed Hacks", "Speed Hacks (can cause rendering errors!)")));

	CheckBox *skipBufferEffects = graphicsSettings->Add(new CheckBox(&g_Config.bSkipBufferEffects, gr->T("Skip Buffer Effects")));
	skipBufferEffects->OnClick.Add([=](EventParams &e) {
		if (g_Config.bSkipBufferEffects) {
			settingInfo_->Show(gr->T("RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games"), e.v);
			g_Config.bAutoFrameSkip = false;
		}
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		return UI::EVENT_DONE;
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

	CheckBox *texBackoff = graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gr->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	texBackoff->SetDisabledPtr(&g_Config.bSoftwareRendering);
	texBackoff->OnClick.Add([=](EventParams& e) {
		settingInfo_->Show(gr->T("Lazy texture caching Tip", "Faster, but can cause text problems in a few games"), e.v);
		return UI::EVENT_CONTINUE;
	});

	static const char *quality[] = { "Low", "Medium", "High" };
	PopupMultiChoice *beziersChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSplineBezierQuality, gr->T("LowCurves", "Spline/Bezier curves quality"), quality, 0, ARRAY_SIZE(quality), I18NCat::GRAPHICS, screenManager()));
	beziersChoice->OnChoice.Add([=](EventParams &e) {
		if (g_Config.iSplineBezierQuality != 0) {
			settingInfo_->Show(gr->T("LowCurves Tip", "Only used by some games, controls smoothness of curves"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});

	graphicsSettings->Add(new ItemHeader(gr->T("Performance")));
	CheckBox *frameDuplication = graphicsSettings->Add(new CheckBox(&g_Config.bRenderDuplicateFrames, gr->T("Render duplicate frames to 60hz")));
	frameDuplication->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("RenderDuplicateFrames Tip", "Can make framerate smoother in games that run at lower framerates"), e.v);
		return UI::EVENT_CONTINUE;
	});
	frameDuplication->SetEnabledFunc([] {
		return !g_Config.bSkipBufferEffects && g_Config.iFrameSkip == 0;
	});

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
	swSkin->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("SoftwareSkinning Tip", "Combine skinned model draws on the CPU, faster in most games"), e.v);
		return UI::EVENT_CONTINUE;
	});
	swSkin->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *tessellationHW = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTessellation, gr->T("Hardware Tessellation")));
	tessellationHW->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("HardwareTessellation Tip", "Uses hardware to make curves"), e.v);
		return UI::EVENT_CONTINUE;
	});

	tessellationHW->SetEnabledFunc([]() {
		return DoesBackendSupportHWTess() && !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	});

	graphicsSettings->Add(new ItemHeader(gr->T("Texture upscaling")));

	if (GetGPUBackend() == GPUBackend::VULKAN) {
		ChoiceWithValueDisplay *textureShaderChoice = graphicsSettings->Add(new ChoiceWithValueDisplay(&g_Config.sTextureShaderName, gr->T("GPU texture upscaler (fast)"), &TextureTranslateName));
		textureShaderChoice->OnClick.Handle(this, &GameSettingsScreen::OnTextureShader);
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
	texScalingChoice->OnChoice.Add([=](EventParams &e) {
		if (g_Config.iTexScalingLevel != 1 && !UsingHardwareTextureScaling()) {
			settingInfo_->Show(gr->T("UpscaleLevel Tip", "CPU heavy - some scaling may be delayed to avoid stutter"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});
	texScalingChoice->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});

	CheckBox *deposterize = graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gr->T("Deposterize")));
	deposterize->OnClick.Add([=](EventParams &e) {
		if (g_Config.bTexDeposterize == true) {
			settingInfo_->Show(gr->T("Deposterize Tip", "Fixes visual banding glitches in upscaled textures"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});
	deposterize->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});

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
	bool showCardboardSettings = g_Config.bEnableCardboardVR;
#endif
	if (showCardboardSettings) {
		graphicsSettings->Add(new ItemHeader(gr->T("Cardboard VR Settings", "Cardboard VR Settings")));
		graphicsSettings->Add(new CheckBox(&g_Config.bEnableCardboardVR, gr->T("Enable Cardboard VR", "Enable Cardboard VR")));
		PopupSliderChoice *cardboardScreenSize = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardScreenSize, 30, 150, 50, gr->T("Cardboard Screen Size", "Screen Size (in % of the viewport)"), 1, screenManager(), gr->T("% of viewport")));
		cardboardScreenSize->SetEnabledPtr(&g_Config.bEnableCardboardVR);
		PopupSliderChoice *cardboardXShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardXShift, -150, 150, 0, gr->T("Cardboard Screen X Shift", "X Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardXShift->SetEnabledPtr(&g_Config.bEnableCardboardVR);
		PopupSliderChoice *cardboardYShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardYShift, -100, 100, 0, gr->T("Cardboard Screen Y Shift", "Y Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardYShift->SetEnabledPtr(&g_Config.bEnableCardboardVR);
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

#if PPSSPP_PLATFORM(IOS)
	CheckBox *respectSilentMode = audioSettings->Add(new CheckBox(&g_Config.bAudioRespectSilentMode, a->T("Respect silent mode")));
	respectSilentMode->OnClick.Add([=](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
		return UI::EVENT_DONE;
	});
	respectSilentMode->SetEnabledPtr(&g_Config.bEnableSound);
	CheckBox *mixWithOthers = audioSettings->Add(new CheckBox(&g_Config.bAudioMixWithOthers, a->T("Mix audio with other apps")));
	mixWithOthers->OnClick.Add([=](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
		return UI::EVENT_DONE;
	});
	mixWithOthers->SetEnabledPtr(&g_Config.bEnableSound);
#endif

	audioSettings->Add(new ItemHeader(a->T("Game volume")));

	// This is here because it now only applies to in-game. Muting the menu sounds is separate.
	CheckBox *enableSound = audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));

	PopupSliderChoice *volume = audioSettings->Add(new PopupSliderChoice(&g_Config.iGameVolume, VOLUME_OFF, VOLUMEHI_FULL, VOLUMEHI_FULL, a->T("Game volume"), screenManager()));
	volume->SetFormat("%d%%");
	volume->SetEnabledPtr(&g_Config.bEnableSound);
	volume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *reverbVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iReverbVolume, VOLUME_OFF, 2 * VOLUMEHI_FULL, VOLUMEHI_FULL, a->T("Reverb volume"), screenManager()));
	reverbVolume->SetFormat("%d%%");
	reverbVolume->SetEnabledPtr(&g_Config.bEnableSound);
	reverbVolume->SetZeroLabel(a->T("Disabled"));

	PopupSliderChoice *altVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAltSpeedVolume, VOLUME_OFF, VOLUMEHI_FULL, NO_DEFAULT_INT, a->T("Alternate speed volume"), screenManager()));
	altVolume->SetFormat("%d%%");
	altVolume->SetEnabledPtr(&g_Config.bEnableSound);
	altVolume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *achievementVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAchievementVolume, VOLUME_OFF, VOLUMEHI_FULL, MultiplierToVolume100(0.6f), ac->T("Achievement sound volume"), screenManager()));
	achievementVolume->SetFormat("%d%%");
	achievementVolume->SetEnabledPtr(&g_Config.bEnableSound);
	achievementVolume->SetZeroLabel(a->T("Mute"));
	achievementVolume->SetLiveUpdate(true);
	achievementVolume->OnChange.Add([](UI::EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
		return UI::EVENT_DONE;
	});

	audioSettings->Add(new ItemHeader(a->T("UI sound")));

	audioSettings->Add(new CheckBox(&g_Config.bUISound, a->T("UI sound")));
	PopupSliderChoice *uiVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iUIVolume, 0, VOLUMEHI_FULL, VOLUMEHI_FULL, a->T("UI volume"), screenManager()));
	uiVolume->SetFormat("%d%%");
	uiVolume->SetZeroLabel(a->T("Mute"));
	uiVolume->SetLiveUpdate(true);
	uiVolume->OnChange.Add([](UI::EventParams &e) {
		// Audio preview
		PlayUISound(UI::UISound::CONFIRM);
		return UI::EVENT_DONE;
	});
	audioSettings->Add(new ItemHeader(a->T("Audio backend")));

	// Hide the backend selector in UWP builds (we only support XAudio2 there).
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (IsVistaOrHigher()) {
		static const char *backend[] = { "Auto", "DSound (compatible)", "WASAPI (fast)" };
		audioSettings->Add(new PopupMultiChoice(&g_Config.iAudioBackend, a->T("Audio backend", "Audio backend (restart req.)"), backend, 0, ARRAY_SIZE(backend), I18NCat::AUDIO, screenManager()));
	}
#endif

	bool sdlAudio = false;
#if defined(SDL)
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), a->T_cstr("Auto"));
	PopupMultiChoiceDynamic *audioDevice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sAudioDevice, a->T("Device"), audioDeviceList, I18NCat::NONE, screenManager()));
	audioDevice->OnChoice.Handle(this, &GameSettingsScreen::OnAudioDevice);
	sdlAudio = true;
#endif

	if (sdlAudio || g_Config.iAudioBackend == AUDIO_BACKEND_WASAPI) {
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
	controlsSettings->Add(new Choice(co->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);
	controlsSettings->Add(new Choice(co->T("Calibrate Analog Stick")))->OnClick.Handle(this, &GameSettingsScreen::OnCalibrateAnalogs);
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
			auto co = GetI18NCategory(I18NCat::CONTROLS);
			if ((u32)g_Config.iTiltInputType < (u32)g_numTiltTypes) {
				return std::string(co->T(g_tiltTypes[g_Config.iTiltInputType]));
			}
			return "";
		}));
		customizeTilt->OnClick.Handle(this, &GameSettingsScreen::OnTiltCustomize);
	} else if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {  // TODO: This seems like a regression
		controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, co->T("HapticFeedback", "Haptic Feedback (vibration)")));
	}

	// TVs don't have touch control, at least not yet.
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		controlsSettings->Add(new ItemHeader(co->T("OnScreen", "On-Screen Touch Controls")));
		controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, co->T("OnScreen", "On-Screen Touch Controls")));
		layoutEditorChoice_ = controlsSettings->Add(new Choice(co->T("Customize Touch Controls")));
		layoutEditorChoice_->OnClick.Handle(this, &GameSettingsScreen::OnTouchControlLayout);
		layoutEditorChoice_->SetEnabledPtr(&g_Config.bShowTouchControls);

		Choice *gesture = controlsSettings->Add(new Choice(co->T("Gesture mapping")));
		gesture->OnClick.Add([=](EventParams &e) {
			screenManager()->push(new GestureMappingScreen(gamePath_));
			return UI::EVENT_DONE;
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

		controlsSettings->Add(new CheckBox(&g_Config.bTouchGliding, co->T("Keep first touched button pressed when dragging")));

		// Hide stick background, useful when increasing the size
		CheckBox *hideStickBackground = controlsSettings->Add(new CheckBox(&g_Config.bHideStickBackground, co->T("Hide touch analog stick background circle")));
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

		// On non iOS systems, offer to let the user see this button.
		// Some Windows touch devices don't have a back button or other button to call up the menu.
		if (System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON)) {
			CheckBox *enablePauseBtn = controlsSettings->Add(new CheckBox(&g_Config.bShowTouchPause, co->T("Show Touch Pause Menu Button")));

			// Don't allow the user to disable it once in-game, so they can't lock themselves out of the menu.
			if (!PSP_IsInited()) {
				enablePauseBtn->SetEnabledPtr(&g_Config.bShowTouchControls);
			} else {
				enablePauseBtn->SetEnabled(false);
			}
		}

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
		analogLimiter->OnChange.Add([=](EventParams &e) {
			settingInfo_->Show(co->T("AnalogLimiter Tip", "When the analog limiter button is pressed"), e.v);
			return UI::EVENT_CONTINUE;
		});
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
			mouseControl->OnClick.Add([=](EventParams &e) {
				if (g_Config.bMouseControl)
					settingInfo_->Show(co->T("MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon."), e.v);
				return UI::EVENT_CONTINUE;
			});
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

static constexpr UI::Size ITEM_HEIGHT = 64.f;

MacAddressChooser::MacAddressChooser(RequesterToken token, Path gamePath_, std::string *value, std::string_view title, ScreenManager *screenManager, UI::LayoutParams *layoutParams) : UI::LinearLayout(UI::ORIENT_HORIZONTAL, layoutParams) {
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
		return UI::EVENT_DONE;
	});
	Add(new Choice(n->T("Randomize"), new LinearLayoutParams(WRAP_CONTENT, ITEM_HEIGHT)))->OnClick.Add([=](UI::EventParams &) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		std::string_view confirmMessage = n->T("ChangeMacSaveConfirm", "Generate a new MAC address?");
		std::string_view warningMessage = n->T("ChangeMacSaveWarning", "Some games verify the MAC address when loading savedata, so this may break old saves.");
		std::string combined = g_Config.sMACAddress + "\n\n" + std::string(confirmMessage) + "\n\n" + std::string(warningMessage);

		auto confirmScreen = new PromptScreen(
			gamePath_,
			combined, di->T("Yes"), di->T("No"),
			[&](bool success) {
				if (success) {
					g_Config.sMACAddress = CreateRandMAC();
				}}
		);
		screenManager->push(confirmScreen);
		return UI::EVENT_DONE;
	});
}

void GameSettingsScreen::CreateNetworkingSettings(UI::ViewGroup *networkingSettings) {
	using namespace UI;

	auto n = GetI18NCategory(I18NCat::NETWORKING);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	networkingSettings->Add(new ItemHeader(ms->T("Networking")));

	networkingSettings->Add(new Choice(n->T("Open PPSSPP Multiplayer Wiki Page")))->OnClick.Handle(this, &GameSettingsScreen::OnAdhocGuides);

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

	networkingSettings->Add(new ItemHeader(n->T("AdHoc server")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableAdhocServer, n->T("Enable built-in PRO Adhoc Server", "Enable built-in PRO Adhoc Server")));
	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.proAdhocServer, n->T("Change proAdhocServer Address", "Change proAdhocServer Address (localhost = multiple instance)"), I18NCat::NONE))->OnClick.Handle(this, &GameSettingsScreen::OnChangeproAdhocServerAddress);

	networkingSettings->Add(new ItemHeader(n->T("UPnP (port-forwarding)")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableUPnP, n->T("Enable UPnP", "Enable UPnP (need a few seconds to detect)")));
	auto useOriPort = networkingSettings->Add(new CheckBox(&g_Config.bUPnPUseOriginalPort, n->T("UPnP use original port", "UPnP use original port (Enabled = PSP compatibility)")));
	useOriPort->OnClick.Add([=](EventParams& e) {
		if (g_Config.bUPnPUseOriginalPort)
			settingInfo_->Show(n->T("UseOriginalPort Tip", "May not work for all devices or games, see wiki."), e.v);
		return UI::EVENT_CONTINUE;
	});
	useOriPort->SetEnabledPtr(&g_Config.bEnableUPnP);

	networkingSettings->Add(new ItemHeader(n->T("Infrastructure")));
	if (g_Config.sInfrastructureUsername.empty()) {
		networkingSettings->Add(new NoticeView(NoticeLevel::WARN, n->T("To play in Infrastructure Mode, you must enter a username"), ""));
	}
	PopupTextInputChoice *usernameChoice = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureUsername, n->T("Username"), "", 16, screenManager()));
	usernameChoice->SetRestriction(StringRestriction::AlphaNumDashUnderscore, 3);
	usernameChoice->OnChange.Add([this](UI::EventParams &e) {
		RecreateViews();
		return UI::EVENT_DONE;
	});

	networkingSettings->Add(new CheckBox(&g_Config.bInfrastructureAutoDNS, n->T("Autoconfigure")));
	auto *dnsServer = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureDNSServer, n->T("DNS server"), "", 32, screenManager()));
	dnsServer->SetDisabledPtr(&g_Config.bInfrastructureAutoDNS);

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

#if !defined(MOBILE_DEVICE) && !defined(USING_QT_UI)  // TODO: Add all platforms where KEY_CHAR support is added
	PopupTextInputChoice *qc1 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat0, n->T("Quick Chat 1"), "", 32, screenManager()));
	PopupTextInputChoice *qc2 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat1, n->T("Quick Chat 2"), "", 32, screenManager()));
	PopupTextInputChoice *qc3 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat2, n->T("Quick Chat 3"), "", 32, screenManager()));
	PopupTextInputChoice *qc4 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat3, n->T("Quick Chat 4"), "", 32, screenManager()));
	PopupTextInputChoice *qc5 = networkingSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat4, n->T("Quick Chat 5"), "", 32, screenManager()));
#elif defined(USING_QT_UI)
	Choice *qc1 = networkingSettings->Add(new Choice(n->T("Quick Chat 1")));
	Choice *qc2 = networkingSettings->Add(new Choice(n->T("Quick Chat 2")));
	Choice *qc3 = networkingSettings->Add(new Choice(n->T("Quick Chat 3")));
	Choice *qc4 = networkingSettings->Add(new Choice(n->T("Quick Chat 4")));
	Choice *qc5 = networkingSettings->Add(new Choice(n->T("Quick Chat 5")));
#elif PPSSPP_PLATFORM(ANDROID)
	ChoiceWithValueDisplay *qc1 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat0, n->T("Quick Chat 1"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc2 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat1, n->T("Quick Chat 2"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc3 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat2, n->T("Quick Chat 3"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc4 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat3, n->T("Quick Chat 4"), I18NCat::NONE));
	ChoiceWithValueDisplay *qc5 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat4, n->T("Quick Chat 5"), I18NCat::NONE));
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

	auto di = GetI18NCategory(I18NCat::DIALOG);

	networkingSettings->Add(new ItemHeader(n->T("Misc", "Misc (default = compatibility)")));
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iPortOffset, 0, 60000, 10000, n->T("Port offset", "Port offset (0 = PSP compatibility)"), 100, screenManager()));
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iMinTimeout, 0, 15000, 0, n->T("Minimum Timeout", "Minimum Timeout (override in ms, 0 = default)"), 50, screenManager()))->SetFormat(di->T("%d ms"));
	networkingSettings->Add(new CheckBox(&g_Config.bForcedFirstConnect, n->T("Forced First Connect", "Forced First Connect (faster Connect)")));
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
		retro->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
			screenManager()->push(new RetroAchievementsSettingsScreen(gamePath_));
			return UI::EVENT_DONE;
		});
		retro->SetIcon(ImageID("I_RETROACHIEVEMENTS_LOGO"));
	}

	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	tools->Add(new Choice(sa->T("Savedata Manager")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new SavedataScreen(gamePath_));
		return UI::EVENT_DONE;
	});
	tools->Add(new Choice(dev->T("System Information")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new SystemInfoScreen(gamePath_));
		return UI::EVENT_DONE;
	});
	tools->Add(new Choice(sy->T("Developer Tools")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new DeveloperToolsScreen(gamePath_));
		return UI::EVENT_DONE;
	});
	tools->Add(new Choice(ri->T("Remote disc streaming")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new RemoteISOScreen(gamePath_));
		return UI::EVENT_DONE;
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
		auto &mapping = g_Config.GetLangValuesMapping();
		auto iter = mapping.find(value);
		if (iter != mapping.end()) {
			return iter->second.first;
		}
		return std::string(value);
	};

	systemSettings->Add(new ChoiceWithValueDisplay(&g_Config.sLanguageIni, sy->T("Language"), langCodeToName))->OnClick.Add([&](UI::EventParams &e) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto langScreen = new NewLanguageScreen(sy->T("Language"));
		langScreen->OnChoice.Add([&](UI::EventParams &e) {
			screenManager()->RecreateAllViews();
			System_Notify(SystemNotification::UI);
			return UI::EVENT_DONE;
		});
		if (e.v)
			langScreen->SetPopupOrigin(e.v);
		screenManager()->push(langScreen);
		return UI::EVENT_DONE;
	});

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};
	PopupMultiChoice *switchMode = systemSettings->Add(new PopupMultiChoice(&g_Config.iAppSwitchMode, sy->T("App switching mode"), indicator, 0, ARRAY_SIZE(indicator), I18NCat::SYSTEM, screenManager()));
	switchMode->OnChoice.Add([](EventParams &e) {
		System_Notify(SystemNotification::APP_SWITCH_MODE_CHANGED);
		return UI::EVENT_DONE;
	});
#endif

	PopupSliderChoice *uiScale = systemSettings->Add(new PopupSliderChoice(&g_Config.iUIScaleFactor, -8, 8, 0, sy->T("UI size adjustment (DPI)"), screenManager()));
	uiScale->SetZeroLabel(sy->T("Off"));
	uiScale->OnChange.Add([](UI::EventParams &e) {
		g_display.Recalculate(-1, -1, -1, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor));
		NativeResized();
		return UI::EVENT_DONE;
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		backgroundChoice_ = systemSettings->Add(new Choice(sy->T("Clear UI background")));
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER)) {
		backgroundChoice_ = systemSettings->Add(new Choice(sy->T("Set UI background...")));
	} else {
		backgroundChoice_ = nullptr;
	}
	if (backgroundChoice_ != nullptr) {
		backgroundChoice_->OnClick.Handle(this, &GameSettingsScreen::OnChangeBackground);
	}

	systemSettings->Add(new CheckBox(&g_Config.bTransparentBackground, sy->T("Transparent UI background")));

	// Shared with achievements.
	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

	systemSettings->Add(new PopupMultiChoice(&g_Config.iNotificationPos, sy->T("Notification screen position"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()));

	static const char *backgroundAnimations[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iBackgroundAnimation, sy->T("UI background animation"), backgroundAnimations, 0, ARRAY_SIZE(backgroundAnimations), I18NCat::SYSTEM, screenManager()));

	PopupMultiChoiceDynamic *theme = systemSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, sy->T("Theme"), GetThemeInfoNames(), I18NCat::THEMES, screenManager()));
	theme->OnChoice.Add([=](EventParams &e) {
		UpdateTheme(screenManager()->getUIContext());
		// Reset the tint/saturation if the theme changed.
		if (e.b) {
			g_Config.fUITint = 0.0f;
			g_Config.fUISaturation = 1.0f;
		}
		return UI::EVENT_CONTINUE;
	});

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		PopupSliderChoiceFloat *tint = new PopupSliderChoiceFloat(&g_Config.fUITint, 0.0f, 1.0f, 0.0f, sy->T("Color Tint"), 0.01f, screenManager());
		tint->SetHasDropShadow(false);
		tint->SetLiveUpdate(true);
		systemSettings->Add(tint);
		PopupSliderChoiceFloat *saturation = new PopupSliderChoiceFloat(&g_Config.fUISaturation, 0.0f, 2.0f, 1.0f, sy->T("Color Saturation"), 0.01f, screenManager());
		saturation->SetHasDropShadow(false);
		saturation->SetLiveUpdate(true);
		systemSettings->Add(saturation);
	}

	systemSettings->Add(new ItemHeader(sy->T("PSP Memory Stick")));

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		systemSettings->Add(new Choice(sy->T("Show Memory Stick folder")))->OnClick.Add([](UI::EventParams &p) {
			System_ShowFileInFolder(g_Config.memStickDirectory);
			return UI::EVENT_DONE;
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
				return UI::EVENT_DONE;
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
	enableReportsCheckbox_ = new CheckBox(&enableReports_, sy->T("Enable Compatibility Server Reports"));
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	systemSettings->Add(enableReportsCheckbox_);

	systemSettings->Add(new ItemHeader(sy->T("Emulation")));

	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, sy->T("Fast Memory", "Fast Memory")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);
	systemSettings->Add(new CheckBox(&g_Config.bIgnoreBadMemAccess, sy->T("Ignore bad memory accesses")));

	static const char *ioTimingMethods[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays", "Simulate UMD slow reading speed"};
	View *ioTimingMethod = systemSettings->Add(new PopupMultiChoice(&g_Config.iIOTimingMethod, sy->T("IO timing method"), ioTimingMethods, 0, ARRAY_SIZE(ioTimingMethods), I18NCat::SYSTEM, screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bForceLagSync, sy->T("Force real clock sync (slower, less lag)")))->SetDisabledPtr(&g_Config.bAutoFrameSkip);
	PopupSliderChoice *lockedMhz = systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, 0, sy->T("Change CPU Clock", "Change CPU Clock (unstable)"), screenManager(), sy->T("MHz, 0:default")));
	lockedMhz->OnChange.Add([&](UI::EventParams &) {
		enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
		return UI::EVENT_CONTINUE;
	});
	lockedMhz->SetZeroLabel(sy->T("Auto"));
	PopupSliderChoice *rewindInterval = systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindSnapshotInterval, 0, 60, 0, sy->T("Rewind Snapshot Interval"), screenManager(), di->T("seconds, 0:off")));
	rewindInterval->SetFormat(di->T("%d seconds"));
	rewindInterval->SetZeroLabel(sy->T("Off"));

	systemSettings->Add(new ItemHeader(sy->T("General")));

	PopupSliderChoice *exitConfirmation = systemSettings->Add(new PopupSliderChoice(&g_Config.iAskForExitConfirmationAfterSeconds, 0, 1200, 60, sy->T("Ask for exit confirmation after seconds"), screenManager(), "s"));
	exitConfirmation->SetZeroLabel(sy->T("Off"));

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		auto co = GetI18NCategory(I18NCat::CONTROLS);

		static const char *screenRotation[] = { "Auto", "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed", "Landscape Auto" };
		PopupMultiChoice *rot = systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenRotation, co->T("Screen Rotation"), screenRotation, 0, ARRAY_SIZE(screenRotation), I18NCat::CONTROLS, screenManager()));
		rot->OnChoice.Handle(this, &GameSettingsScreen::OnScreenRotation);

		if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
			systemSettings->Add(new CheckBox(&g_Config.bSustainedPerformanceMode, sy->T("Sustained performance mode")))->OnClick.Handle(this, &GameSettingsScreen::OnSustainedPerformanceModeChange);
		}
	}
#endif

	systemSettings->Add(new Choice(sy->T("Restore Default Settings")))->OnClick.Handle(this, &GameSettingsScreen::OnRestoreDefaultSettings);
	systemSettings->Add(new CheckBox(&g_Config.bEnableStateUndo, sy->T("Savestate slot backups")));
	static const char *autoLoadSaveStateChoices[] = { "Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iAutoLoadSaveState, sy->T("Auto Load Savestate"), autoLoadSaveStateChoices, 0, ARRAY_SIZE(autoLoadSaveStateChoices), I18NCat::SYSTEM, screenManager()));
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		systemSettings->Add(new CheckBox(&g_Config.bBypassOSKWithKeyboard, sy->T("Use system native keyboard")));

	systemSettings->Add(new CheckBox(&g_Config.bCacheFullIsoInRam, sy->T("Cache ISO in RAM", "Cache full ISO in RAM")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new CheckBox(&g_Config.bCheckForNewVersion, sy->T("VersionCheck", "Check for new versions of PPSSPP")));
	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, sy->T("Screenshots as PNG")));
	static const char *screenshotModeChoices[] = { "Final processed image", "Raw game image" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenshotMode, sy->T("Screenshot mode"), screenshotModeChoices, 0, ARRAY_SIZE(screenshotModeChoices), I18NCat::SYSTEM, screenManager()));
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS)
	systemSettings->Add(new CheckBox(&g_Config.bPauseOnLostFocus, sy->T("Pause when not focused")));
#endif

	systemSettings->Add(new ItemHeader(sy->T("Cheats", "Cheats")));
	CheckBox *enableCheats = systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, sy->T("Enable Cheats")));
	enableCheats->OnClick.Add([&](UI::EventParams &) {
		enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
		return UI::EVENT_CONTINUE;
	});
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
		return UI::EVENT_DONE;
	});
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, sy->T("Day Light Saving")));
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

UI::EventReturn GameSettingsScreen::OnAutoFrameskip(UI::EventParams &e) {
	g_Config.UpdateAfterSettingAutoFrameSkip();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnScreenRotation(UI::EventParams &e) {
	INFO_LOG(Log::System, "New display rotation: %d", g_Config.iScreenRotation);
	INFO_LOG(Log::System, "Sending rotate");
	System_Notify(SystemNotification::ROTATE_UPDATED);
	INFO_LOG(Log::System, "Got back from rotate");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnAdhocGuides(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	std::string url(n->T("MultiplayerHowToURL", "https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP"));
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, url.c_str());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		System_RecreateActivity();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSustainedPerformanceModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::SUSTAINED_PERF_CHANGE);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnShowMemstickScreen(UI::EventParams &e) {
	screenManager()->push(new MemStickScreen(false));
	return UI::EVENT_DONE;
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

UI::EventReturn GameSettingsScreen::OnMemoryStickMyDoc(UI::EventParams &e) {
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
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnMemoryStickOther(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	if (otherinstalled_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string initialPath = g_Config.memStickDirectory.ToCString();
		std::string folder = W32Util::BrowseForFolder(MainWindow::GetHWND(), di->T("Choose PPSSPP save folder"), initialPath);
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
	return UI::EVENT_DONE;
}

#endif

UI::EventReturn GameSettingsScreen::OnChangeBackground(UI::EventParams &e) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";

	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		File::Delete(bgPng);
		File::Delete(bgJpg);
		UIBackgroundShutdown();
		RecreateViews();
	} else {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		System_BrowseForImage(GetRequesterToken(), sy->T("Set UI background..."), [=](const std::string &value, int) {
			if (!value.empty()) {
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
	}

	// Change to a browse or clear button.
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFullscreenChange(UI::EventParams &e) {
	g_Config.iForceFullScreen = -1;
	System_ToggleFullscreenState(g_Config.UseFullScreen() ? "1" : "0");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFullscreenMultiChange(UI::EventParams &e) {
	System_ToggleFullscreenState(g_Config.UseFullScreen() ? "1" : "0");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnResolutionChange(UI::EventParams &e) {
	if (g_Config.iAndroidHwScale == 1) {
		System_RecreateActivity();
	}
	Reporting::UpdateConfig();
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	return UI::EVENT_DONE;
}

void GameSettingsScreen::onFinish(DialogResult result) {
	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		System_Toast("Failed to save settings!\nCheck permissions, or try to restart the device.");
	}

	if (editThenRestore_) {
		// In case we didn't have the title yet before, try again.
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.changeGameSpecific(gameID_, info->GetTitle());
		g_Config.unloadGameConfig();
	}

	System_Notify(SystemNotification::UI);

	KeyMap::UpdateNativeMenuKeys();

	// Wipe some caches after potentially changing settings.
	// Let's not send resize messages here, handled elsewhere.
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.iFpsLimit1 = iAlternateSpeedPercent1_ < 0 ? -1 : (iAlternateSpeedPercent1_ * 60) / 100;
		g_Config.iFpsLimit2 = iAlternateSpeedPercent2_ < 0 ? -1 : (iAlternateSpeedPercent2_ * 60) / 100;
		g_Config.iAnalogFpsLimit = (iAlternateSpeedPercentAnalog_ * 60) / 100;

		RecreateViews();
	}

	// Show/hide the Analog Alternative Speed as appropriate - need to recreate views if this changed.
	bool mapped = KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true);
	if (mapped != analogSpeedMapped_) {
		analogSpeedMapped_ = mapped;
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
			settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
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

UI::EventReturn GameSettingsScreen::OnRenderingBackend(UI::EventParams &e) {
	// It only makes sense to show the restart prompt if the backend was actually changed.
	if (g_Config.iGPUBackend != (int)GetGPUBackend()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		screenManager()->push(new PromptScreen(gamePath_, di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
			if (yes) {
				TriggerRestart("GameSettingsScreen::RenderingBackendYes", editThenRestore_, gamePath_);
			} else {
				g_Config.iGPUBackend = (int)GetGPUBackend();
			}
		}));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingDevice(UI::EventParams &e) {
	// It only makes sense to show the restart prompt if the device was actually changed.
	std::string *deviceNameSetting = GPUDeviceNameSetting();
	if (deviceNameSetting && *deviceNameSetting != GetGPUBackendDevice()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		screenManager()->push(new PromptScreen(gamePath_, di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
			// If the user ends up deciding not to restart, set the config back to the current backend
			// so it doesn't get switched by accident.
			if (yes) {
				TriggerRestart("GameSettingsScreen::RenderingDeviceYes", editThenRestore_, gamePath_);
			} else {
				std::string *deviceNameSetting = GPUDeviceNameSetting();
				if (deviceNameSetting)
					*deviceNameSetting = GetGPUBackendDevice();
				// Needed to redraw the setting.
				RecreateViews();
			}
		}));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnInflightFramesChoice(UI::EventParams &e) {
	if (g_Config.iInflightFrames != prevInflightFrames_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		screenManager()->push(new PromptScreen(gamePath_, di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
			if (yes) {
				TriggerRestart("GameSettingsScreen::InflightFramesYes", editThenRestore_, gamePath_);
			} else {
				g_Config.iInflightFrames = prevInflightFrames_;
			}
		}));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnCameraDeviceChange(UI::EventParams& e) {
	Camera::onCameraDeviceChange();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnMicDeviceChange(UI::EventParams& e) {
	Microphone::onMicDeviceChange();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnAudioDevice(UI::EventParams &e) {
	auto a = GetI18NCategory(I18NCat::AUDIO);
	if (g_Config.sAudioDevice == a->T("Auto")) {
		g_Config.sAudioDevice.clear();
	}
	System_Notify(SystemNotification::AUDIO_RESET_DEVICE);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat0(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 1"), g_Config.sQuickChat0, false, [](const std::string &value, int) {
		g_Config.sQuickChat0 = value;
	});
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat1(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 2"), g_Config.sQuickChat1, false, [](const std::string &value, int) {
		g_Config.sQuickChat1 = value;
	});
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat2(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 3"), g_Config.sQuickChat2, false, [](const std::string &value, int) {
		g_Config.sQuickChat2 = value;
	});
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat3(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 4"), g_Config.sQuickChat3, false, [](const std::string &value, int) {
		g_Config.sQuickChat3 = value;
	});
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat4(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("Enter Quick Chat 5"), g_Config.sQuickChat4, false, [](const std::string &value, int) {
		g_Config.sQuickChat4 = value;
	});
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeproAdhocServerAddress(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	screenManager()->push(new HostnameSelectScreen(&g_Config.proAdhocServer, n->T("proAdhocServer Address:")));

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTextureShader(UI::EventParams &e) {
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto shaderScreen = new TextureShaderScreen(gr->T("Texture Shader"));
	shaderScreen->OnChoice.Handle(this, &GameSettingsScreen::OnTextureShaderChange);
	if (e.v)
		shaderScreen->SetPopupOrigin(e.v);
	screenManager()->push(shaderScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTextureShaderChange(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	RecreateViews(); // Update setting name
	g_Config.bTexHardwareScaling = g_Config.sTextureShaderName != "Off";
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnControlMapping(UI::EventParams &e) {
	screenManager()->push(new ControlMappingScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnCalibrateAnalogs(UI::EventParams &e) {
	screenManager()->push(new AnalogSetupScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTouchControlLayout(UI::EventParams &e) {
	screenManager()->push(new TouchControlLayoutScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTiltCustomize(UI::EventParams &e) {
	screenManager()->push(new TiltAnalogSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
};

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	settingsScroll->SetTag("DevToolsSettings");
	root_->Add(settingsScroll);

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto a = GetI18NCategory(I18NCat::AUDIO);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	auto si = GetI18NCategory(I18NCat::SYSINFO);

	AddStandardBack(root_);

	LinearLayout *list = settingsScroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->SetSpacing(0);

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

	list->Add(new ItemHeader(sy->T("General")));

	bool canUseJit = System_GetPropertyBool(SYSPROP_CAN_JIT);
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)
	// In iOS App Store builds, we disable the JIT.

	static const char *cpuCores[] = {"Interpreter", "Dynarec/JIT (recommended)", "IR Interpreter", "JIT using IR"};
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
	list->Add(new CheckBox(&g_Config.bDumpDecryptedEboot, dev->T("Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game")));

#if !PPSSPP_PLATFORM(UWP)
	Choice *cpuTests = new Choice(dev->T("Run CPU Tests"));
	list->Add(cpuTests)->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);

	cpuTests->SetEnabled(TestsAvailable());
#endif

	// list->Add(new CheckBox(&g_Config.bUseExperimentalAtrac, dev->T("Use experimental sceAtrac")));

	AddOverlayList(list, screenManager());

	list->Add(new CheckBox(&g_Config.bEnableLogging, dev->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	list->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new CheckBox(&g_Config.bLogFrameDrops, dev->T("Log Dropped Frame Statistics")));
	if (GetGPUBackend() == GPUBackend::VULKAN) {
		list->Add(new CheckBox(&g_Config.bGpuLogProfiler, dev->T("GPU log profiler")));
	}

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

	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		list->Add(new Choice(dev->T("GPU Driver Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnGPUDriverTest);
	}
	list->Add(new CheckBox(&g_Config.bVendorBugChecksEnabled, dev->T("Enable driver bug workarounds")));
	list->Add(new Choice(dev->T("Framedump tests")))->OnClick.Handle(this, &DeveloperToolsScreen::OnFramedumpTest);
	list->Add(new Choice(dev->T("Touchscreen Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnTouchscreenTest);
	// list->Add(new Choice(dev->T("Memstick Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnMemstickTest);

	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
	CheckBox *allowDebugger = new CheckBox(&allowDebugger_, dev->T("Allow remote debugger"));
	list->Add(allowDebugger)->OnClick.Handle(this, &DeveloperToolsScreen::OnRemoteDebugger);
	allowDebugger->SetEnabledPtr(&canAllowDebugger_);

	list->Add(new CheckBox(&g_Config.bShowOnScreenMessages, dev->T("Show on-screen messages")));

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

#if PPSSPP_PLATFORM(IOS_APP_STORE)
	list->Add(new NoticeView(NoticeLevel::WARN, ms->T("Moving the memstick directory is NOT recommended on iOS"), ""));
	list->Add(new Choice(sy->T("Set Memory Stick folder")))->OnClick.Add(
		[=](UI::EventParams &) {
			SetMemStickDirDarwin(GetRequesterToken());
			return UI::EVENT_DONE;
		});
#endif

	static const char *ffModes[] = { "Render all frames", "", "Frame Skipping" };
	PopupMultiChoice *ffMode = list->Add(new PopupMultiChoice(&g_Config.iFastForwardMode, dev->T("Fast-forward mode"), ffModes, 0, ARRAY_SIZE(ffModes), I18NCat::GRAPHICS, screenManager()));
	ffMode->SetEnabledFunc([]() { return !g_Config.bVSync; });
	ffMode->HideChoice(1);  // not used

	auto displayRefreshRate = list->Add(new PopupSliderChoice(&g_Config.iDisplayRefreshRate, 60, 1000, 60, dev->T("Display refresh rate"), 1, screenManager()));
	displayRefreshRate->SetFormat(si->T("%d Hz"));

#if !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(SWITCH)
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
#endif

	Draw::DrawContext *draw = screenManager()->getDrawContext();

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

	// Makes it easy to get savestates out of an iOS device. The file listing shown in MacOS doesn't allow
	// you to descend into directories.
#if PPSSPP_PLATFORM(IOS)
	list->Add(new Choice(dev->T("Copy savestates to memstick root")))->OnClick.Handle(this, &DeveloperToolsScreen::OnCopyStatesToRoot);
#endif

	// Reconsider whenever recreating views.
	hasTexturesIni_ = HasIni::MAYBE;
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	g_Config.Save("DeveloperToolsScreen::onFinish");
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS);
	}
	System_Notify(SystemNotification::UI);
}

UI::EventReturn GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (g_Config.bGameSpecific) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		screenManager()->push(
			new PromptScreen(gamePath_, dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"), di->T("OK"), di->T("Cancel"),
			std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, std::placeholders::_1)));
	} else {
		std::string_view title = sy->T("Restore Default Settings");
		screenManager()->push(new RestoreSettingsScreen(title));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	System_Notify(SystemNotification::TOGGLE_DEBUG_CONSOLE);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
	// TODO: If game is loaded, don't do anything.
#if !PPSSPP_PLATFORM(UWP)
	RunTests();
#endif
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

UI::EventReturn DeveloperToolsScreen::OnFramedumpTest(UI::EventParams &e) {
	screenManager()->push(new FrameDumpTestScreen());
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
	}
	else {
		mipsTracer.stop_tracing();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnMIPSTracerPathChanged(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	System_BrowseForFile(GetRequesterToken(), dev->T("Select the log file"), BrowseFileType::ANY,
		[this](const std::string &value, int) {
		mipsTracer.set_logging_path(value);
		MIPSTracerPath_ = value;
		MIPSTracerPath->SetRightText(MIPSTracerPath_);
	});
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

	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, G_LEFT)));
	for (char c = '0'; c <= '9'; ++c) {
		char label[] = { c, '\0' };
		auto button = buttonsRow1->Add(new Button(label));
		button->OnClick.Handle(this, &HostnameSelectScreen::OnNumberClick);
		button->SetTag(label);
	}
	buttonsRow1->Add(new Button("."))->OnClick.Handle(this, &HostnameSelectScreen::OnPointClick);
	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, G_RIGHT)));

	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, G_LEFT)));
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		buttonsRow2->Add(new Button(di->T("Edit")))->OnClick.Handle(this, &HostnameSelectScreen::OnEditClick);
	}
	buttonsRow2->Add(new Button(di->T("Delete")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteClick);
	buttonsRow2->Add(new Button(di->T("Delete all")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteAllClick);
	buttonsRow2->Add(new Button(di->T("Toggle List")))->OnClick.Handle(this, &HostnameSelectScreen::OnShowIPListClick);
	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, G_RIGHT)));

	std::vector<std::string> listIP = {"socom.cc", "psp.gameplayer.club", "myneighborsushicat.com", "localhost"}; // TODO: Add some saved recent history too?
	net::GetIPList(listIP);
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

void HostnameSelectScreen::SendEditKey(InputKeyCode keyCode, int flags) {
	auto oldView = UI::GetFocusedView();
	UI::SetFocusedView(addrView_);
	KeyInput fakeKey{ DEVICE_ID_KEYBOARD, keyCode, KEY_DOWN | flags };
	addrView_->Key(fakeKey);
	UI::SetFocusedView(oldView);
}

UI::EventReturn HostnameSelectScreen::OnNumberClick(UI::EventParams &e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() == 1 && text[0] >= '0' && text[0] <= '9') {
		SendEditKey((InputKeyCode)text[0], KEY_CHAR);  // ASCII for digits match keycodes.
	}
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnPointClick(UI::EventParams &e) {
	SendEditKey((InputKeyCode)'.', KEY_CHAR);
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnDeleteClick(UI::EventParams &e) {
	SendEditKey(NKCODE_DEL);
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnDeleteAllClick(UI::EventParams &e) {
	addrView_->SetText("");
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnEditClick(UI::EventParams& e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("proAdhocServer Address:"), addrView_->GetText(), false, [this](const std::string& value, int) {
		addrView_->SetText(value);
	});
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnShowIPListClick(UI::EventParams& e) {
	if (ipRows_->GetVisibility() == UI::V_GONE) {
		ipRows_->SetVisibility(UI::V_VISIBLE);
	}
	else {
		ipRows_->SetVisibility(UI::V_GONE);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnIPClick(UI::EventParams& e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() > 0) {
		addrView_->SetText(text);
		// Copy the IP to clipboard for the host to easily share their IP through chatting apps.
		System_CopyStringToClipboard(text);
	}
	return UI::EVENT_DONE;
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

void GestureMappingScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	AddStandardBack(root_);
	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);
	ScrollView *rightPanel = new ScrollView(ORIENT_VERTICAL);
	tabHolder->AddTab(co->T("Gesture"), rightPanel);
	LinearLayout *vert = rightPanel->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	static const char *gestureButton[ARRAY_SIZE(GestureKey::keyList)+1];
	gestureButton[0] = "None";
	for (int i = 1; i < ARRAY_SIZE(gestureButton); ++i) {
		gestureButton[i] = KeyMap::GetPspButtonNameCharPointer(GestureKey::keyList[i-1]);
	}

	vert->Add(new CheckBox(&g_Config.bGestureControlEnabled, co->T("Enable gesture control")));

	vert->Add(new ItemHeader(co->T("Swipe")));
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&g_Config.fSwipeSensitivity, 0.01f, 1.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&g_Config.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Double tap")));
	vert->Add(new PopupMultiChoice(&g_Config.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Analog Stick")));
	vert->Add(new CheckBox(&g_Config.bAnalogGesture, co->T("Enable analog stick gesture")));
	vert->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogGestureSensibility, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bAnalogGesture);
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
	textView->SetPadding(10.0f);

	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::SETTINGS, ga->T("Game Settings")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::CONTROLS, ms->T("Controls")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::RECENT, mm->T("Recent")));
}

void RestoreSettingsScreen::OnCompleted(DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.RestoreDefaults((RestoreSettingsBits)restoreFlags_);
	}
}
