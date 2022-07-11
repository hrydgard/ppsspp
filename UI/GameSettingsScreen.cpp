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

#include "Common/System/Display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Math/curves.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "UI/EmuScreen.h"
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
#include "UI/TiltEventProcessor.h"
#include "UI/GPUDriverTestScreen.h"
#include "UI/MemStickScreen.h"
#include "UI/Theme.h"

#include "Common/File/FileUtil.h"
#include "Common/OSVersion.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Host.h"
#include "Core/KeyMap.h"
#include "Core/Instance.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/TextureReplacer.h"
#include "Core/WebServer.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
#include "GPU/Common/PostShader.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "Windows/MainWindow.h"
#include <shlobj.h>
#include "Windows/W32Util/ShellUtil.h"
#endif

#if PPSSPP_PLATFORM(ANDROID)

#include "android/jni/AndroidAudio.h"
#include "android/jni/AndroidContentURI.h"

extern AndroidAudioState *g_audioState;

#endif

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: UIDialogScreenWithGameBackground(gamePath), gameID_(gameID), editThenRestore_(editThenRestore) {
	lastVertical_ = UseVerticalLayout();
	prevInflightFrames_ = g_Config.iInflightFrames;
	analogSpeedMapped_ = KeyMap::AxisFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, nullptr, nullptr);
}

bool GameSettingsScreen::UseVerticalLayout() const {
	return dp_yres > dp_xres * 1.1f;
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

static std::string TextureTranslateName(const char *value) {
	auto ps = GetI18NCategory("TextureShaders");
	const TextureShaderInfo *info = GetTextureShaderInfo(value);
	if (info) {
		return ps->T(value, info ? info->name.c_str() : value);
	} else {
		return value;
	}
}

static std::string PostShaderTranslateName(const char *value) {
	auto ps = GetI18NCategory("PostShaders");
	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		return ps->T(value, info ? info->name.c_str() : value);
	} else {
		return value;
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

void GameSettingsScreen::CreateViews() {
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();

	if (editThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
		g_Config.loadGameConfig(gameID_, info->GetTitle());
	}

	iAlternateSpeedPercent1_ = g_Config.iFpsLimit1 < 0 ? -1 : (g_Config.iFpsLimit1 * 100) / 60;
	iAlternateSpeedPercent2_ = g_Config.iFpsLimit2 < 0 ? -1 : (g_Config.iFpsLimit2 * 100) / 60;
	iAlternateSpeedPercentAnalog_ = (g_Config.iAnalogFpsLimit * 100) / 60;

	bool vertical = UseVerticalLayout();

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	auto co = GetI18NCategory("Controls");
	auto a = GetI18NCategory("Audio");
	auto sa = GetI18NCategory("Savedata");
	auto se = GetI18NCategory("Search");
	auto sy = GetI18NCategory("System");
	auto n = GetI18NCategory("Networking");
	auto ms = GetI18NCategory("MainSettings");
	auto dev = GetI18NCategory("Developer");
	auto ri = GetI18NCategory("RemoteISO");
	auto ps = GetI18NCategory("PostShaders");
	auto th = GetI18NCategory("Themes");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	if (vertical) {
		LinearLayout *verticalLayout = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 200, new LinearLayoutParams(1.0f));
		verticalLayout->Add(tabHolder_);
		verticalLayout->Add(new Choice(di->T("Back"), "", false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(0))))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root_->Add(verticalLayout);
	} else {
		tabHolder_ = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));
		root_->Add(tabHolder_);
		AddStandardBack(root_);
	}
	tabHolder_->SetTag("GameSettings");
	root_->SetDefaultFocusView(tabHolder_);
	settingTabContents_.clear();
	settingTabFilterNotices_.clear();

	float leftSide = 40.0f;
	if (!vertical) {
		leftSide += 200.0f;
	}
	settingInfo_ = new SettingInfoMessage(ALIGN_CENTER | FLAG_WRAP_TEXT, new AnchorLayoutParams(dp_xres - leftSide - 40.0f, WRAP_CONTENT, leftSide, dp_yres - 80.0f - 40.0f, NONE, NONE));
	settingInfo_->SetBottomCutoff(dp_yres - 200.0f);
	root_->Add(settingInfo_);

	// Show it again if we recreated the view
	if (oldSettingInfo_ != "") {
		settingInfo_->Show(oldSettingInfo_, nullptr);
	}

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	LinearLayout *graphicsSettings = AddTab("GameSettingsGraphics", ms->T("Graphics"));

	graphicsSettings->Add(new ItemHeader(gr->T("Rendering Mode")));

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), gr->GetName(), screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D9))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D9);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);

	if (!IsFirstInstance()) {
		// If we're not the first instance, can't save the setting, and it requires a restart, so...
		renderingBackendChoice->SetEnabled(false);
	}
#endif

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	// Backends that don't allow a device choice will only expose one device.
	if (draw->GetDeviceList().size() > 1) {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting) {
			PopupMultiChoiceDynamic *deviceChoice = graphicsSettings->Add(new PopupMultiChoiceDynamic(deviceNameSetting, gr->T("Device"), draw->GetDeviceList(), nullptr, screenManager()));
			deviceChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingDevice);
		}
	}

	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering" };
	PopupMultiChoice *renderingModeChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gr->T("Mode"), renderingMode, 0, ARRAY_SIZE(renderingMode), gr->GetName(), screenManager()));
	renderingModeChoice->OnChoice.Add([=](EventParams &e) {
		switch (g_Config.iRenderingMode) {
		case FB_NON_BUFFERED_MODE:
			settingInfo_->Show(gr->T("RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games"), e.v);
			break;
		case FB_BUFFERED_MODE:
			break;
		}
		return UI::EVENT_CONTINUE;
	});
	renderingModeChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingMode);
	renderingModeChoice->SetDisabledPtr(&g_Config.bSoftwareRendering);
	CheckBox *blockTransfer = graphicsSettings->Add(new CheckBox(&g_Config.bBlockTransferGPU, gr->T("Simulate Block Transfer", "Simulate Block Transfer")));
	blockTransfer->OnClick.Add([=](EventParams &e) {
		if (!g_Config.bBlockTransferGPU)
			settingInfo_->Show(gr->T("BlockTransfer Tip", "Some games require this to be On for correct graphics"), e.v);
		return UI::EVENT_CONTINUE;
	});
	blockTransfer->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *softwareGPU = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareRendering, gr->T("Software Rendering", "Software Rendering (slow)")));
	softwareGPU->SetEnabled(!PSP_IsInited());

	graphicsSettings->Add(new ItemHeader(gr->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gr->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), gr->GetName(), screenManager()));
	static const char *frameSkipType[] = {"Number of Frames", "Percent of FPS"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkipType, gr->T("Frame Skipping Type"), frameSkipType, 0, ARRAY_SIZE(frameSkipType), gr->GetName(), screenManager()));
	frameSkipAuto_ = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gr->T("Auto FrameSkip")));
	frameSkipAuto_->OnClick.Handle(this, &GameSettingsScreen::OnAutoFrameskip);

	PopupSliderChoice *altSpeed1 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent1_, 0, 1000, gr->T("Alternative Speed", "Alternative speed"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed1->SetFormat("%i%%");
	altSpeed1->SetZeroLabel(gr->T("Unlimited"));
	altSpeed1->SetNegativeDisable(gr->T("Disabled"));

	PopupSliderChoice *altSpeed2 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent2_, 0, 1000, gr->T("Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed2->SetFormat("%i%%");
	altSpeed2->SetZeroLabel(gr->T("Unlimited"));
	altSpeed2->SetNegativeDisable(gr->T("Disabled"));

	if (analogSpeedMapped_) {
		PopupSliderChoice *analogSpeed = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercentAnalog_, 1, 1000, gr->T("Analog Alternative Speed", "Analog alternative speed (in %)"), 5, screenManager(), gr->T("%")));
		altSpeed2->SetFormat("%i%%");
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Postprocessing effect")));

	std::set<std::string> alreadyAddedShader;
	for (int i = 0; i < (int)g_Config.vPostShaderNames.size() + 1 && i < ARRAY_SIZE(shaderNames_); ++i) {
		// Vector element pointer get invalidated on resize, cache name to have always a valid reference in the rendering thread
		shaderNames_[i] = i == g_Config.vPostShaderNames.size() ? "Off" : g_Config.vPostShaderNames[i];
		postProcChoice_ = graphicsSettings->Add(new ChoiceWithValueDisplay(&shaderNames_[i], StringFromFormat("%s #%d", gr->T("Postprocessing Shader"), i + 1), &PostShaderTranslateName));
		postProcChoice_->OnClick.Add([=](EventParams &e) {
			auto gr = GetI18NCategory("Graphics");
			auto procScreen = new PostProcScreen(gr->T("Postprocessing Shader"), i);
			procScreen->OnChoice.Handle(this, &GameSettingsScreen::OnPostProcShaderChange);
			if (e.v)
				procScreen->SetPopupOrigin(e.v);
			screenManager()->push(procScreen);
			return UI::EVENT_DONE;
		});
		postProcChoice_->SetEnabledFunc([] {
			return g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		});

		// No need for settings on the last one.
		if (i == g_Config.vPostShaderNames.size())
			continue;

		auto shaderChain = GetPostShaderChain(g_Config.vPostShaderNames[i]);
		for (auto shaderInfo : shaderChain) {
			// Disable duplicated shader slider
			bool duplicated = alreadyAddedShader.find(shaderInfo->section) != alreadyAddedShader.end();
			alreadyAddedShader.insert(shaderInfo->section);
			for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
				auto &setting = shaderInfo->settings[i];
				if (!setting.name.empty()) {
					auto &value = g_Config.mPostShaderSetting[StringFromFormat("%sSettingValue%d", shaderInfo->section.c_str(), i + 1)];
					if (duplicated) {
						auto sliderName = StringFromFormat("%s %s", ps->T(setting.name), ps->T("(duplicated setting, previous slider will be used)"));
						PopupSliderChoiceFloat *settingValue = graphicsSettings->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, sliderName, setting.step, screenManager()));
						settingValue->SetEnabled(false);
					} else {
						PopupSliderChoiceFloat *settingValue = graphicsSettings->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, ps->T(setting.name), setting.step, screenManager()));
						settingValue->SetEnabledFunc([] {
							return g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
						});
					}
				}
			}
		}
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Screen layout")));
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
	// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
	displayEditor_ = graphicsSettings->Add(new Choice(gr->T("Display layout editor")));
	displayEditor_->OnClick.Handle(this, &GameSettingsScreen::OnDisplayLayoutEditor);

#if PPSSPP_PLATFORM(ANDROID)
	// Hide insets option if no insets, or OS too old.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 28 &&
		(System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT) != 0.0f ||
		 System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP) != 0.0f ||
		 System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT) != 0.0f ||
		 System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM) != 0.0f)) {
		graphicsSettings->Add(new CheckBox(&g_Config.bIgnoreScreenInsets, gr->T("Ignore camera notch when centering")));
	}

	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		// Let's reuse the Fullscreen translation string from desktop.
		graphicsSettings->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnImmersiveModeChange);
	}
#endif

	graphicsSettings->Add(new ItemHeader(gr->T("Performance")));
	static const char *internalResolutions[] = { "Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
	resolutionChoice_ = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInternalResolution, gr->T("Rendering Resolution"), internalResolutions, 0, ARRAY_SIZE(internalResolutions), gr->GetName(), screenManager()));
	resolutionChoice_->OnChoice.Handle(this, &GameSettingsScreen::OnResolutionChange);
	resolutionChoice_->SetEnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	});

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		static const char *deviceResolutions[] = { "Native device resolution", "Auto (same as Rendering)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
		int max_res_temp = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 2;
		if (max_res_temp == 3)
			max_res_temp = 4;  // At least allow 2x
		int max_res = std::min(max_res_temp, (int)ARRAY_SIZE(deviceResolutions));
		UI::PopupMultiChoice *hwscale = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAndroidHwScale, gr->T("Display Resolution (HW scaler)"), deviceResolutions, 0, max_res, gr->GetName(), screenManager()));
		hwscale->OnChoice.Handle(this, &GameSettingsScreen::OnHwScaleChange);  // To refresh the display mode
	}
#endif

#if !(PPSSPP_PLATFORM(ANDROID) || defined(USING_QT_UI) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS))
	CheckBox *vSync = graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gr->T("VSync")));
	vSync->OnClick.Add([=](EventParams &e) {
		NativeResized();
		return UI::EVENT_CONTINUE;
	});
#endif

	CheckBox *frameDuplication = graphicsSettings->Add(new CheckBox(&g_Config.bRenderDuplicateFrames, gr->T("Render duplicate frames to 60hz")));
	frameDuplication->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("RenderDuplicateFrames Tip", "Can make framerate smoother in games that run at lower framerates"), e.v);
		return UI::EVENT_CONTINUE;
	});
	frameDuplication->SetEnabledFunc([] {
		return g_Config.iRenderingMode != FB_NON_BUFFERED_MODE && g_Config.iFrameSkip == 0;
	});

	if (GetGPUBackend() == GPUBackend::VULKAN || GetGPUBackend() == GPUBackend::OPENGL) {
		static const char *bufferOptions[] = { "No buffer", "Up to 1", "Up to 2" };
		PopupMultiChoice *inflightChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInflightFrames, gr->T("Buffer graphics commands (faster, input lag)"), bufferOptions, 0, ARRAY_SIZE(bufferOptions), gr->GetName(), screenManager()));
		inflightChoice->OnChoice.Handle(this, &GameSettingsScreen::OnInflightFramesChoice);
	}

	CheckBox *hwTransform = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gr->T("Hardware Transform")));
	hwTransform->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *swSkin = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareSkinning, gr->T("Software Skinning")));
	swSkin->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("SoftwareSkinning Tip", "Combine skinned model draws on the CPU, faster in most games"), e.v);
		return UI::EVENT_CONTINUE;
	});
	swSkin->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *vtxCache = graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gr->T("Vertex Cache")));
	vtxCache->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("VertexCache Tip", "Faster, but may cause temporary flicker"), e.v);
		return UI::EVENT_CONTINUE;
	});
	vtxCache->SetEnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.bHardwareTransform && g_Config.iGPUBackend != (int)GPUBackend::OPENGL;
	});

	CheckBox *texBackoff = graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gr->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	texBackoff->SetDisabledPtr(&g_Config.bSoftwareRendering);
	texBackoff->OnClick.Add([=](EventParams& e) {
		settingInfo_->Show(gr->T("Lazy texture caching Tip", "Faster, but can cause text problems in a few games"), e.v);
		return UI::EVENT_CONTINUE;
	});

	CheckBox *texSecondary_ = graphicsSettings->Add(new CheckBox(&g_Config.bTextureSecondaryCache, gr->T("Retain changed textures", "Retain changed textures (speedup, mem hog)")));
	texSecondary_->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("RetainChangedTextures Tip", "Makes many games slower, but some games a lot faster"), e.v);
		return UI::EVENT_CONTINUE;
	});
	texSecondary_->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *framebufferSlowEffects = graphicsSettings->Add(new CheckBox(&g_Config.bDisableSlowFramebufEffects, gr->T("Disable slower effects (speedup)")));
	framebufferSlowEffects->SetDisabledPtr(&g_Config.bSoftwareRendering);

	// Seems solid, so we hide the setting.
	/*CheckBox *vtxJit = graphicsSettings->Add(new CheckBox(&g_Config.bVertexDecoderJit, gr->T("Vertex Decoder JIT")));

	if (PSP_IsInited()) {
		vtxJit->SetEnabled(false);
	}*/

	static const char *quality[] = { "Low", "Medium", "High" };
	PopupMultiChoice *beziersChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSplineBezierQuality, gr->T("LowCurves", "Spline/Bezier curves quality"), quality, 0, ARRAY_SIZE(quality), gr->GetName(), screenManager()));
	beziersChoice->OnChoice.Add([=](EventParams &e) {
		if (g_Config.iSplineBezierQuality != 0) {
			settingInfo_->Show(gr->T("LowCurves Tip", "Only used by some games, controls smoothness of curves"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});

	CheckBox *tessellationHW = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTessellation, gr->T("Hardware Tessellation")));
	tessellationHW->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("HardwareTessellation Tip", "Uses hardware to make curves"), e.v);
		return UI::EVENT_CONTINUE;
	});

	tessellationHW->SetEnabledFunc([]() {
		return DoesBackendSupportHWTess() && !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	});

	// In case we're going to add few other antialiasing option like MSAA in the future.
	// graphicsSettings->Add(new CheckBox(&g_Config.bFXAA, gr->T("FXAA")));
	graphicsSettings->Add(new ItemHeader(gr->T("Texture Scaling")));
#ifndef MOBILE_DEVICE
	static const char *texScaleLevels[] = {"Off", "2x", "3x", "4x", "5x"};
#else
	static const char *texScaleLevels[] = {"Off", "2x", "3x"};
#endif

	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	PopupMultiChoice *texScalingType = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gr->T("Upscale Type"), texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos), gr->GetName(), screenManager()));
	texScalingType->SetEnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	PopupMultiChoice *texScalingChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gr->T("Upscale Level"), texScaleLevels, 1, ARRAY_SIZE(texScaleLevels), gr->GetName(), screenManager()));
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

	ChoiceWithValueDisplay *textureShaderChoice = graphicsSettings->Add(new ChoiceWithValueDisplay(&g_Config.sTextureShaderName, gr->T("Texture Shader"), &TextureTranslateName));
	textureShaderChoice->OnClick.Handle(this, &GameSettingsScreen::OnTextureShader);
	textureShaderChoice->SetEnabledFunc([]() {
		return GetGPUBackend() == GPUBackend::VULKAN && !g_Config.bSoftwareRendering;
	});

	graphicsSettings->Add(new ItemHeader(gr->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	PopupMultiChoice *anisoFiltering = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gr->T("Anisotropic Filtering"), anisoLevels, 0, ARRAY_SIZE(anisoLevels), gr->GetName(), screenManager()));
	anisoFiltering->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Auto Max Quality"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gr->T("Texture Filter"), texFilters, 1, ARRAY_SIZE(texFilters), gr->GetName(), screenManager()));

	static const char *bufFilters[] = { "Linear", "Nearest", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iBufFilter, gr->T("Screen Scaling Filter"), bufFilters, 1, ARRAY_SIZE(bufFilters), gr->GetName(), screenManager()));

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	bool showCardboardSettings = true;
#else
	// If you enabled it through the ini, you can see this. Useful for testing.
	bool showCardboardSettings = g_Config.bEnableCardboardVR;
#endif
	if (showCardboardSettings) {
		graphicsSettings->Add(new ItemHeader(gr->T("Cardboard VR Settings", "Cardboard VR Settings")));
		graphicsSettings->Add(new CheckBox(&g_Config.bEnableCardboardVR, gr->T("Enable Cardboard VR", "Enable Cardboard VR")));
		PopupSliderChoice *cardboardScreenSize = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardScreenSize, 30, 150, gr->T("Cardboard Screen Size", "Screen Size (in % of the viewport)"), 1, screenManager(), gr->T("% of viewport")));
		cardboardScreenSize->SetEnabledPtr(&g_Config.bEnableCardboardVR);
		PopupSliderChoice *cardboardXShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardXShift, -150, 150, gr->T("Cardboard Screen X Shift", "X Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardXShift->SetEnabledPtr(&g_Config.bEnableCardboardVR);
		PopupSliderChoice *cardboardYShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardYShift, -100, 100, gr->T("Cardboard Screen Y Shift", "Y Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
		cardboardYShift->SetEnabledPtr(&g_Config.bEnableCardboardVR);
	}

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		graphicsSettings->Add(new ItemHeader(gr->T("Camera")));
		PopupMultiChoiceDynamic *cameraChoice = graphicsSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sCameraDevice, gr->T("Camera Device"), cameraList, nullptr, screenManager()));
		cameraChoice->OnChoice.Handle(this, &GameSettingsScreen::OnCameraDeviceChange);
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Hack Settings", "Hack Settings (these WILL cause glitches)")));

	static const char *bloomHackOptions[] = { "Off", "Safe", "Balanced", "Aggressive" };
	PopupMultiChoice *bloomHack = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iBloomHack, gr->T("Lower resolution for effects (reduces artifacts)"), bloomHackOptions, 0, ARRAY_SIZE(bloomHackOptions), gr->GetName(), screenManager()));
	bloomHack->SetEnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.iInternalResolution != 1;
	});

	graphicsSettings->Add(new ItemHeader(gr->T("Overlay Information")));
	static const char *fpsChoices[] = { "None", "Speed", "FPS", "Both" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gr->T("Show FPS Counter"), fpsChoices, 0, ARRAY_SIZE(fpsChoices), gr->GetName(), screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gr->T("Show Debug Statistics")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);

	// Developer tools are not accessible ingame, so it goes here.
	graphicsSettings->Add(new ItemHeader(gr->T("Debugging")));
	Choice *dump = graphicsSettings->Add(new Choice(gr->T("Dump next frame to log")));
	dump->OnClick.Handle(this, &GameSettingsScreen::OnDumpNextFrameToLog);
	if (!PSP_IsInited())
		dump->SetEnabled(false);

	// Audio
	LinearLayout *audioSettings = AddTab("GameSettingsAudio", ms->T("Audio"));

	audioSettings->Add(new ItemHeader(ms->T("Audio")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));

	PopupSliderChoice *volume = audioSettings->Add(new PopupSliderChoice(&g_Config.iGlobalVolume, VOLUME_OFF, VOLUME_FULL, a->T("Global volume"), screenManager()));
	volume->SetEnabledPtr(&g_Config.bEnableSound);
	volume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *altVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAltSpeedVolume, VOLUME_OFF, VOLUME_FULL, a->T("Alternate speed volume"), screenManager()));
	altVolume->SetEnabledPtr(&g_Config.bEnableSound);
	altVolume->SetZeroLabel(a->T("Mute"));
	altVolume->SetNegativeDisable(a->T("Use global volume"));

	PopupSliderChoice *reverbVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iReverbVolume, VOLUME_OFF, 2 * VOLUME_FULL, a->T("Reverb volume"), screenManager()));
	reverbVolume->SetEnabledPtr(&g_Config.bEnableSound);
	reverbVolume->SetZeroLabel(a->T("Disabled"));

	// Hide the backend selector in UWP builds (we only support XAudio2 there).
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (IsVistaOrHigher()) {
		static const char *backend[] = { "Auto", "DSound (compatible)", "WASAPI (fast)" };
		PopupMultiChoice *audioBackend = audioSettings->Add(new PopupMultiChoice(&g_Config.iAudioBackend, a->T("Audio backend", "Audio backend (restart req.)"), backend, 0, ARRAY_SIZE(backend), a->GetName(), screenManager()));
		audioBackend->SetEnabledPtr(&g_Config.bEnableSound);
	}
#endif

	bool sdlAudio = false;
#if defined(SDL)
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), a->T("Auto"));
	PopupMultiChoiceDynamic *audioDevice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sAudioDevice, a->T("Device"), audioDeviceList, nullptr, screenManager()));
	audioDevice->OnChoice.Handle(this, &GameSettingsScreen::OnAudioDevice);
	sdlAudio = true;
#endif

	if (sdlAudio || g_Config.iAudioBackend == AUDIO_BACKEND_WASAPI) {
		audioSettings->Add(new CheckBox(&g_Config.bAutoAudioDevice, a->T("Use new audio devices automatically")));
	}

#if PPSSPP_PLATFORM(ANDROID)
	CheckBox *extraAudio = audioSettings->Add(new CheckBox(&g_Config.bExtraAudioBuffering, a->T("AudioBufferingForBluetooth", "Bluetooth-friendly buffer (slower)")));
	extraAudio->SetEnabledPtr(&g_Config.bEnableSound);

	// Show OpenSL debug info
	const std::string audioErrorStr = AndroidAudio_GetErrorString(g_audioState);
	if (!audioErrorStr.empty()) {
		audioSettings->Add(new InfoItem(a->T("Audio Error"), audioErrorStr));
	}
#endif

	std::vector<std::string> micList = Microphone::getDeviceList();
	if (!micList.empty()) {
		audioSettings->Add(new ItemHeader(a->T("Microphone")));
		PopupMultiChoiceDynamic *MicChoice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sMicDevice, a->T("Microphone Device"), micList, nullptr, screenManager()));
		MicChoice->OnChoice.Handle(this, &GameSettingsScreen::OnMicDeviceChange);
	}

	// Control
	LinearLayout *controlsSettings = AddTab("GameSettingsControls", ms->T("Controls"));

	controlsSettings->Add(new ItemHeader(ms->T("Controls")));
	controlsSettings->Add(new Choice(co->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);
	controlsSettings->Add(new Choice(co->T("Calibrate Analog Stick")))->OnClick.Handle(this, &GameSettingsScreen::OnCalibrateAnalogs);

#if defined(USING_WIN_UI)
	controlsSettings->Add(new CheckBox(&g_Config.bSystemControls, co->T("Enable standard shortcut keys")));
	controlsSettings->Add(new CheckBox(&g_Config.bGamepadOnlyFocused, co->T("Ignore gamepads when not focused")));
#endif

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, co->T("HapticFeedback", "Haptic Feedback (vibration)")));

		static const char *tiltTypes[] = { "None (Disabled)", "Analog Stick", "D-PAD", "PSP Action Buttons", "L/R Trigger Buttons" };
		controlsSettings->Add(new PopupMultiChoice(&g_Config.iTiltInputType, co->T("Tilt Input Type"), tiltTypes, 0, ARRAY_SIZE(tiltTypes), co->GetName(), screenManager()))->OnClick.Handle(this, &GameSettingsScreen::OnTiltTypeChange);

		Choice *customizeTilt = controlsSettings->Add(new Choice(co->T("Customize tilt")));
		customizeTilt->OnClick.Handle(this, &GameSettingsScreen::OnTiltCustomize);
		customizeTilt->SetEnabledFunc([] {
			return g_Config.iTiltInputType != 0;
		});
	}

	// TVs don't have touch control, at least not yet.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		controlsSettings->Add(new ItemHeader(co->T("OnScreen", "On-Screen Touch Controls")));
		controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, co->T("OnScreen", "On-Screen Touch Controls")));
		layoutEditorChoice_ = controlsSettings->Add(new Choice(co->T("Customize Touch Controls")));
		layoutEditorChoice_->OnClick.Handle(this, &GameSettingsScreen::OnTouchControlLayout);
		layoutEditorChoice_->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Re-centers itself to the touch location on touch-down.
		CheckBox *floatingAnalog = controlsSettings->Add(new CheckBox(&g_Config.bAutoCenterTouchAnalog, co->T("Auto-centering analog stick")));
		floatingAnalog->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Hide stick background, usefull when increasing the size
		CheckBox *hideStickBackground = controlsSettings->Add(new CheckBox(&g_Config.bHideStickBackground, co->T("Hide touch analog stick background circle")));
		hideStickBackground->SetEnabledPtr(&g_Config.bShowTouchControls);

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
		PopupSliderChoice *opacity = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 100, co->T("Button Opacity"), screenManager(), "%"));
		opacity->SetEnabledPtr(&g_Config.bShowTouchControls);
		opacity->SetFormat("%i%%");
		PopupSliderChoice *autoHide = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonHideSeconds, 0, 300, co->T("Auto-hide buttons after seconds"), screenManager(), co->T("seconds, 0 : off")));
		autoHide->SetEnabledPtr(&g_Config.bShowTouchControls);
		autoHide->SetFormat("%is");
		autoHide->SetZeroLabel(co->T("Off"));
		static const char *touchControlStyles[] = {"Classic", "Thin borders", "Glowing borders"};
		View *style = controlsSettings->Add(new PopupMultiChoice(&g_Config.iTouchButtonStyle, co->T("Button style"), touchControlStyles, 0, ARRAY_SIZE(touchControlStyles), co->GetName(), screenManager()));
		style->SetEnabledPtr(&g_Config.bShowTouchControls);
		Choice *gesture = controlsSettings->Add(new Choice(co->T("Gesture mapping")));
		gesture->OnClick.Add([=](EventParams &e) {
			screenManager()->push(new GestureMappingScreen());
			return UI::EVENT_DONE;
		});
		gesture->SetEnabledPtr(&g_Config.bShowTouchControls);
	}

	controlsSettings->Add(new ItemHeader(co->T("Keyboard", "Keyboard Control Settings")));
#if defined(USING_WIN_UI)
	controlsSettings->Add(new CheckBox(&g_Config.bIgnoreWindowsKey, co->T("Ignore Windows Key")));
#endif // #if defined(USING_WIN_UI)
	auto analogLimiter = new PopupSliderChoiceFloat(&g_Config.fAnalogLimiterDeadzone, 0.0f, 1.0f, co->T("Analog Limiter"), 0.10f, screenManager(), "/ 1.0");
	controlsSettings->Add(analogLimiter);
	analogLimiter->OnChange.Add([=](EventParams &e) {
		settingInfo_->Show(co->T("AnalogLimiter Tip", "When the analog limiter button is pressed"), e.v);
		return UI::EVENT_CONTINUE;
	});
#if defined(USING_WIN_UI) || defined(SDL)
	controlsSettings->Add(new ItemHeader(co->T("Mouse", "Mouse settings")));
	CheckBox *mouseControl = controlsSettings->Add(new CheckBox(&g_Config.bMouseControl, co->T("Use Mouse Control")));
	mouseControl->OnClick.Add([=](EventParams &e) {
		if(g_Config.bMouseControl)
			settingInfo_->Show(co->T("MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon."), e.v);
		return UI::EVENT_CONTINUE;
	});
	controlsSettings->Add(new CheckBox(&g_Config.bMouseConfine, co->T("Confine Mouse", "Trap mouse within window/display area")))->SetEnabledPtr(&g_Config.bMouseControl);
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSensitivity, 0.01f, 1.0f, co->T("Mouse sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bMouseControl);
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSmoothing, 0.0f, 0.95f, co->T("Mouse smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bMouseControl);
#endif

	LinearLayout *networkingSettings = AddTab("GameSettingsNetworking", ms->T("Networking"));

	networkingSettings->Add(new ItemHeader(ms->T("Networking")));

	networkingSettings->Add(new Choice(n->T("Open PPSSPP Multiplayer Wiki Page")))->OnClick.Handle(this, &GameSettingsScreen::OnAdhocGuides);

	networkingSettings->Add(new CheckBox(&g_Config.bEnableWlan, n->T("Enable networking", "Enable networking/wlan (beta)")));
	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sMACAddress, n->T("Change Mac Address"), (const char*)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeMacAddress);
	static const char* wlanChannels[] = { "Auto", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };
	auto wlanChannelChoice = networkingSettings->Add(new PopupMultiChoice(&g_Config.iWlanAdhocChannel, n->T("WLAN Channel"), wlanChannels, 0, ARRAY_SIZE(wlanChannels), n->GetName(), screenManager()));
	for (int i = 0; i < 4; i++) {
		wlanChannelChoice->HideChoice(i + 2);
		wlanChannelChoice->HideChoice(i + 7);
	}
	networkingSettings->Add(new CheckBox(&g_Config.bDiscordPresence, n->T("Send Discord Presence information")));

	networkingSettings->Add(new ItemHeader(n->T("AdHoc Server")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableAdhocServer, n->T("Enable built-in PRO Adhoc Server", "Enable built-in PRO Adhoc Server")));
	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.proAdhocServer, n->T("Change proAdhocServer Address", "Change proAdhocServer Address (localhost = multiple instance)"), (const char*)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeproAdhocServerAddress);

	networkingSettings->Add(new ItemHeader(n->T("UPnP (port-forwarding)")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableUPnP, n->T("Enable UPnP", "Enable UPnP (need a few seconds to detect)")));
	auto useOriPort = networkingSettings->Add(new CheckBox(&g_Config.bUPnPUseOriginalPort, n->T("UPnP use original port", "UPnP use original port (Enabled = PSP compatibility)")));
	useOriPort->OnClick.Add([=](EventParams& e) {
		if (g_Config.bUPnPUseOriginalPort)
			settingInfo_->Show(n->T("UseOriginalPort Tip", "May not work for all devices or games, see wiki."), e.v);
		return UI::EVENT_CONTINUE;
	});
	useOriPort->SetEnabledPtr(&g_Config.bEnableUPnP);

	networkingSettings->Add(new ItemHeader(n->T("Chat")));
	networkingSettings->Add(new CheckBox(&g_Config.bEnableNetworkChat, n->T("Enable network chat", "Enable network chat")));
	static const char *chatButtonPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right", "None" };
	networkingSettings->Add(new PopupMultiChoice(&g_Config.iChatButtonPosition, n->T("Chat Button Position"), chatButtonPositions, 0, ARRAY_SIZE(chatButtonPositions), n->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bEnableNetworkChat);
	static const char *chatScreenPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right" };
	networkingSettings->Add(new PopupMultiChoice(&g_Config.iChatScreenPosition, n->T("Chat Screen Position"), chatScreenPositions, 0, ARRAY_SIZE(chatScreenPositions), n->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bEnableNetworkChat);

#if (!defined(MOBILE_DEVICE) && !defined(USING_QT_UI)) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID) // Missing only iOS?
	networkingSettings->Add(new ItemHeader(n->T("QuickChat", "Quick Chat")));
	CheckBox *qc = networkingSettings->Add(new CheckBox(&g_Config.bEnableQuickChat, n->T("EnableQuickChat", "Enable Quick Chat")));
	qc->SetEnabledPtr(&g_Config.bEnableNetworkChat);
#endif

#if !defined(MOBILE_DEVICE) && !defined(USING_QT_UI)  // TODO: Add all platforms where KEY_CHAR support is added
	PopupTextInputChoice *qc1 = networkingSettings->Add(new PopupTextInputChoice(&g_Config.sQuickChat0, n->T("Quick Chat 1"), "", 32, screenManager()));
	PopupTextInputChoice *qc2 = networkingSettings->Add(new PopupTextInputChoice(&g_Config.sQuickChat1, n->T("Quick Chat 2"), "", 32, screenManager()));
	PopupTextInputChoice *qc3 = networkingSettings->Add(new PopupTextInputChoice(&g_Config.sQuickChat2, n->T("Quick Chat 3"), "", 32, screenManager()));
	PopupTextInputChoice *qc4 = networkingSettings->Add(new PopupTextInputChoice(&g_Config.sQuickChat3, n->T("Quick Chat 4"), "", 32, screenManager()));
	PopupTextInputChoice *qc5 = networkingSettings->Add(new PopupTextInputChoice(&g_Config.sQuickChat4, n->T("Quick Chat 5"), "", 32, screenManager()));
#elif defined(USING_QT_UI)
	Choice *qc1 = networkingSettings->Add(new Choice(n->T("Quick Chat 1")));
	Choice *qc2 = networkingSettings->Add(new Choice(n->T("Quick Chat 2")));
	Choice *qc3 = networkingSettings->Add(new Choice(n->T("Quick Chat 3")));
	Choice *qc4 = networkingSettings->Add(new Choice(n->T("Quick Chat 4")));
	Choice *qc5 = networkingSettings->Add(new Choice(n->T("Quick Chat 5")));
#elif PPSSPP_PLATFORM(ANDROID)
	ChoiceWithValueDisplay *qc1 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat0, n->T("Quick Chat 1"), (const char *)nullptr));
	ChoiceWithValueDisplay *qc2 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat1, n->T("Quick Chat 2"), (const char *)nullptr));
	ChoiceWithValueDisplay *qc3 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat2, n->T("Quick Chat 3"), (const char *)nullptr));
	ChoiceWithValueDisplay *qc4 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat3, n->T("Quick Chat 4"), (const char *)nullptr));
	ChoiceWithValueDisplay *qc5 = networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sQuickChat4, n->T("Quick Chat 5"), (const char *)nullptr));
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
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iPortOffset, 0, 60000, n->T("Port offset", "Port offset (0 = PSP compatibility)"), 100, screenManager()));
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iMinTimeout, 0, 15000, n->T("Minimum Timeout", "Minimum Timeout (override in ms, 0 = default)"), 50, screenManager()));
	networkingSettings->Add(new CheckBox(&g_Config.bForcedFirstConnect, n->T("Forced First Connect", "Forced First Connect (faster Connect)")));

	LinearLayout *tools = AddTab("GameSettingsTools", ms->T("Tools"));

	tools->Add(new ItemHeader(ms->T("Tools")));
	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	tools->Add(new Choice(sa->T("Savedata Manager")))->OnClick.Handle(this, &GameSettingsScreen::OnSavedataManager);
	tools->Add(new Choice(dev->T("System Information")))->OnClick.Handle(this, &GameSettingsScreen::OnSysInfo);
	tools->Add(new Choice(sy->T("Developer Tools")))->OnClick.Handle(this, &GameSettingsScreen::OnDeveloperTools);
	tools->Add(new Choice(ri->T("Remote disc streaming")))->OnClick.Handle(this, &GameSettingsScreen::OnRemoteISO);

	// System
	LinearLayout *systemSettings = AddTab("GameSettingsSystem", ms->T("System"));

	systemSettings->Add(new ItemHeader(sy->T("UI")));
	systemSettings->Add(new Choice(dev->T("Language", "Language")))->OnClick.Handle(this, &GameSettingsScreen::OnLanguage);
	systemSettings->Add(new CheckBox(&g_Config.bUISound, sy->T("UI Sound")));
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

	PopupMultiChoiceDynamic *theme = systemSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, sy->T("Theme"), GetThemeInfoNames(), th->GetName(), screenManager()));
	theme->OnChoice.Add([=](EventParams &e) {
		UpdateTheme(screenManager()->getUIContext());

		return UI::EVENT_CONTINUE;
	});


	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		PopupSliderChoiceFloat *tint = new PopupSliderChoiceFloat(&g_Config.fUITint, 0.0, 1.0, sy->T("Color Tint"), 0.01f, screenManager());
		tint->SetHasDropShadow(false);
		tint->SetLiveUpdate(true);
		systemSettings->Add(tint);
		PopupSliderChoiceFloat *saturation = new PopupSliderChoiceFloat(&g_Config.fUISaturation, 0.0, 2.0, sy->T("Color Saturation"), 0.01f, screenManager());
		saturation->SetHasDropShadow(false);
		saturation->SetLiveUpdate(true);
		systemSettings->Add(saturation);
	}

	static const char *backgroundAnimations[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iBackgroundAnimation, sy->T("UI background animation"), backgroundAnimations, 0, ARRAY_SIZE(backgroundAnimations), sy->GetName(), screenManager()));

	systemSettings->Add(new ItemHeader(sy->T("PSP Memory Stick")));

#if (defined(USING_QT_UI) || PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC)) && !PPSSPP_PLATFORM(UWP)
	systemSettings->Add(new Choice(sy->T("Show Memory Stick folder")))->OnClick.Handle(this, &GameSettingsScreen::OnOpenMemStick);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	memstickDisplay_ = g_Config.memStickDirectory.ToVisualString();
	auto memstickPath = systemSettings->Add(new ChoiceWithValueDisplay(&memstickDisplay_, sy->T("Memory Stick folder", "Memory Stick folder"), (const char *)nullptr));
	memstickPath->SetEnabled(!PSP_IsInited());
	memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnChangeMemStickDir);

	// Display USB path for convenience.
	std::string usbPath;
	if (PathToVisualUsbPath(g_Config.memStickDirectory, usbPath)) {
		if (usbPath.empty()) {
			// Probably it's just the root. So let's add PSP to make it clear.
			usbPath = "/PSP";
		}
		systemSettings->Add(new InfoItem(sy->T("USB"), usbPath))->SetChoiceStyle(true);
	}
#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	SavePathInMyDocumentChoice = systemSettings->Add(new CheckBox(&installed_, sy->T("Save path in My Documents", "Save path in My Documents")));
	SavePathInMyDocumentChoice->SetEnabled(!PSP_IsInited());
	SavePathInMyDocumentChoice->OnClick.Handle(this, &GameSettingsScreen::OnSavePathMydoc);
	SavePathInOtherChoice = systemSettings->Add(new CheckBox(&otherinstalled_, sy->T("Save path in installed.txt", "Save path in installed.txt")));
	SavePathInOtherChoice->SetEnabled(false);
	SavePathInOtherChoice->OnClick.Handle(this, &GameSettingsScreen::OnSavePathOther);
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
	systemSettings->Add(new CheckBox(&g_Config.bMemStickInserted, sy->T("Memory Stick inserted")));
	UI::PopupSliderChoice *sizeChoice = systemSettings->Add(new PopupSliderChoice(&g_Config.iMemStickSizeGB, 1, 32, sy->T("Memory Stick size", "Memory Stick size"), screenManager(), "GB"));
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

	static const char *ioTimingMethods[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays" };
	View *ioTimingMethod = systemSettings->Add(new PopupMultiChoice(&g_Config.iIOTimingMethod, sy->T("IO timing method"), ioTimingMethods, 0, ARRAY_SIZE(ioTimingMethods), sy->GetName(), screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bForceLagSync, sy->T("Force real clock sync (slower, less lag)")))->SetDisabledPtr(&g_Config.bAutoFrameSkip);
	PopupSliderChoice *lockedMhz = systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, sy->T("Change CPU Clock", "Change CPU Clock (unstable)"), screenManager(), sy->T("MHz, 0:default")));
	lockedMhz->OnChange.Add([&](UI::EventParams &) {
		enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
		return UI::EVENT_CONTINUE;
	});
	lockedMhz->SetZeroLabel(sy->T("Auto"));
	PopupSliderChoice *rewindFreq = systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindFlipFrequency, 0, 1800, sy->T("Rewind Snapshot Frequency", "Rewind Snapshot Frequency (mem hog)"), screenManager(), sy->T("frames, 0:off")));
	rewindFreq->SetZeroLabel(sy->T("Off"));

	systemSettings->Add(new ItemHeader(sy->T("General")));

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		static const char *screenRotation[] = { "Auto", "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed", "Landscape Auto" };
		PopupMultiChoice *rot = systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenRotation, co->T("Screen Rotation"), screenRotation, 0, ARRAY_SIZE(screenRotation), co->GetName(), screenManager()));
		rot->OnChoice.Handle(this, &GameSettingsScreen::OnScreenRotation);

		if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
			systemSettings->Add(new CheckBox(&g_Config.bSustainedPerformanceMode, sy->T("Sustained performance mode")))->OnClick.Handle(this, &GameSettingsScreen::OnSustainedPerformanceModeChange);
		}
	}
#endif
	systemSettings->Add(new CheckBox(&g_Config.bCheckForNewVersion, sy->T("VersionCheck", "Check for new versions of PPSSPP")));

	systemSettings->Add(new Choice(sy->T("Restore Default Settings")))->OnClick.Handle(this, &GameSettingsScreen::OnRestoreDefaultSettings);
	systemSettings->Add(new CheckBox(&g_Config.bEnableStateUndo, sy->T("Savestate slot backups")));
	static const char *autoLoadSaveStateChoices[] = { "Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iAutoLoadSaveState, sy->T("Auto Load Savestate"), autoLoadSaveStateChoices, 0, ARRAY_SIZE(autoLoadSaveStateChoices), sy->GetName(), screenManager()));
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		systemSettings->Add(new CheckBox(&g_Config.bBypassOSKWithKeyboard, sy->T("Use system native keyboard")));

	systemSettings->Add(new CheckBox(&g_Config.bCacheFullIsoInRam, sy->T("Cache ISO in RAM", "Cache full ISO in RAM")))->SetEnabled(!PSP_IsInited());

	systemSettings->Add(new ItemHeader(sy->T("Cheats", "Cheats")));
	CheckBox *enableCheats = systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, sy->T("Enable Cheats")));
	enableCheats->OnClick.Add([&](UI::EventParams &) {
		enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
		return UI::EVENT_CONTINUE;
	});
	systemSettings->SetSpacing(0);

	systemSettings->Add(new ItemHeader(sy->T("PSP Settings")));
	static const char *models[] = {"PSP-1000", "PSP-2000/3000"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iPSPModel, sy->T("PSP Model"), models, 0, ARRAY_SIZE(models), sy->GetName(), screenManager()))->SetEnabled(!PSP_IsInited());
	// TODO: Come up with a way to display a keyboard for mobile users,
	// so until then, this is Windows/Desktop only.
#if !defined(MOBILE_DEVICE)  // TODO: Add all platforms where KEY_CHAR support is added
	systemSettings->Add(new PopupTextInputChoice(&g_Config.sNickName, sy->T("Change Nickname"), "", 32, screenManager()));
#elif PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		systemSettings->Add(new ChoiceWithValueDisplay(&g_Config.sNickName, sy->T("Change Nickname"), (const char *)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeNickname);
	else
		systemSettings->Add(new PopupTextInputChoice(&g_Config.sNickName, sy->T("Change Nickname"), "", 32, screenManager()));
#endif

	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, sy->T("Screenshots as PNG")));

#if defined(_WIN32) || (defined(USING_QT_UI) && !defined(MOBILE_DEVICE))
	systemSettings->Add(new CheckBox(&g_Config.bDumpFrames, sy->T("Record Display")));
	systemSettings->Add(new CheckBox(&g_Config.bUseFFV1, sy->T("Use Lossless Video Codec (FFV1)")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpVideoOutput, sy->T("Use output buffer (with overlay) for recording")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpAudio, sy->T("Record Audio")));
	systemSettings->Add(new CheckBox(&g_Config.bSaveLoadResetsAVdumping, sy->T("Reset Recording on Save/Load State")));
#endif
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, sy->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, sy->T("Date Format"), dateFormat, 0, 3, sy->GetName(), screenManager()));
	static const char *timeFormat[] = { "24HR", "12HR" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, sy->T("Time Format"), timeFormat, 0, 2, sy->GetName(), screenManager()));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, sy->T("Confirmation Button"), buttonPref, 0, 2, sy->GetName(), screenManager()));

#if !defined(MOBILE_DEVICE) || PPSSPP_PLATFORM(ANDROID)
	// Search
	LinearLayout *searchSettings = AddTab("GameSettingsSearch", ms->T("Search"), true);

	searchSettings->Add(new ItemHeader(se->T("Find settings")));
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		searchSettings->Add(new ChoiceWithValueDisplay(&searchFilter_, se->T("Filter"), (const char *)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeSearchFilter);
	} else {
		searchSettings->Add(new PopupTextInputChoice(&searchFilter_, se->T("Filter"), "", 64, screenManager()))->OnChange.Handle(this, &GameSettingsScreen::OnChangeSearchFilter);
	}
	clearSearchChoice_ = searchSettings->Add(new Choice(se->T("Clear filter")));
	clearSearchChoice_->OnClick.Handle(this, &GameSettingsScreen::OnClearSearchFilter);
	noSearchResults_ = searchSettings->Add(new TextView(se->T("No settings matched '%1'"), new LinearLayoutParams(Margins(20, 5))));

	ApplySearchFilter();
#endif
}

UI::LinearLayout *GameSettingsScreen::AddTab(const char *tag, const std::string &title, bool isSearch) {
	auto se = GetI18NCategory("Search");

	using namespace UI;
	ViewGroup *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	scroll->SetTag(tag);

	LinearLayout *contents = new LinearLayoutList(ORIENT_VERTICAL);
	contents->SetSpacing(0);
	scroll->Add(contents);
	tabHolder_->AddTab(title, scroll);

	if (!isSearch) {
		settingTabContents_.push_back(contents);

		auto notice = contents->Add(new TextView(se->T("Filtering settings by '%1'"), new LinearLayoutParams(Margins(20, 5))));
		notice->SetVisibility(V_GONE);
		settingTabFilterNotices_.push_back(notice);
	}

	return contents;
}

UI::EventReturn GameSettingsScreen::OnAutoFrameskip(UI::EventParams &e) {
	if (g_Config.bAutoFrameSkip && g_Config.iFrameSkip == 0) {
		g_Config.iFrameSkip = 1;
	}
	if (g_Config.bAutoFrameSkip && g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		g_Config.iRenderingMode = FB_BUFFERED_MODE;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnScreenRotation(UI::EventParams &e) {
	INFO_LOG(SYSTEM, "New display rotation: %d", g_Config.iScreenRotation);
	INFO_LOG(SYSTEM, "Sending rotate");
	System_SendMessage("rotate", "");
	INFO_LOG(SYSTEM, "Got back from rotate");
	return UI::EVENT_DONE;
}

void RecreateActivity() {
	const int SYSTEM_JELLYBEAN = 16;
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= SYSTEM_JELLYBEAN) {
		INFO_LOG(SYSTEM, "Sending recreate");
		System_SendMessage("recreate", "");
		INFO_LOG(SYSTEM, "Got back from recreate");
	} else {
		auto gr = GetI18NCategory("Graphics");
		System_SendMessage("toast", gr->T("Must Restart", "You must restart PPSSPP for this change to take effect"));
	}
}

UI::EventReturn GameSettingsScreen::OnAdhocGuides(UI::EventParams &e) {
	auto n = GetI18NCategory("Networking");
	LaunchBrowser(n->T("MultiplayerHowToURL", "https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP"));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_SendMessage("immersive", "");
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSustainedPerformanceModeChange(UI::EventParams &e) {
	System_SendMessage("sustainedPerfMode", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingMode(UI::EventParams &e) {
	// We do not want to report when rendering mode is Framebuffer to memory - so many issues
	// are caused by that (framebuffer copies overwriting display lists, etc).
	Reporting::UpdateConfig();
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	if (!Reporting::IsSupported())
		enableReports_ = Reporting::IsEnabled();

	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		g_Config.bAutoFrameSkip = false;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeMemStickDir(UI::EventParams &e) {
	screenManager()->push(new MemStickScreen(false));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnOpenMemStick(UI::EventParams &e) {
	OpenDirectory(File::ResolvePath(g_Config.memStickDirectory.ToString().c_str()).c_str());
	return UI::EVENT_DONE;
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

UI::EventReturn GameSettingsScreen::OnSavePathMydoc(UI::EventParams &e) {
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

UI::EventReturn GameSettingsScreen::OnSavePathOther(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	if (otherinstalled_) {
		auto di = GetI18NCategory("Dialog");
		std::string folder = W32Util::BrowseForFolder(MainWindow::GetHWND(), di->T("Choose PPSSPP save folder"));
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

		NativeMessageReceived("bgImage_updated", "");
	} else {
		if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER)) {
			System_SendMessage("bgImage_browse", "");
		}
	}

	// Change to a browse or clear button.
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFullscreenChange(UI::EventParams &e) {
	g_Config.iForceFullScreen = -1;
	System_SendMessage("toggle_fullscreen", g_Config.UseFullScreen() ? "1" : "0");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFullscreenMultiChange(UI::EventParams &e) {
	System_SendMessage("toggle_fullscreen", g_Config.UseFullScreen() ? "1" : "0");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDisplayLayoutEditor(UI::EventParams &e) {
	screenManager()->push(new DisplayLayoutScreen());
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnResolutionChange(UI::EventParams &e) {
	if (g_Config.iAndroidHwScale == 1) {
		RecreateActivity();
	}
	Reporting::UpdateConfig();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnHwScaleChange(UI::EventParams &e) {
	RecreateActivity();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDumpNextFrameToLog(UI::EventParams &e) {
	if (gpu) {
		gpu->DumpNextFrame();
	}
	return UI::EVENT_DONE;
}

void GameSettingsScreen::update() {
	UIScreen::update();

	bool vertical = UseVerticalLayout();
	if (vertical != lastVertical_) {
		RecreateViews();
		lastVertical_ = vertical;
	}
}

void GameSettingsScreen::onFinish(DialogResult result) {
	if (g_Config.bEnableSound) {
		if (PSP_IsInited() && !IsAudioInitialised())
			Audio_Init();
	}

	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		System_SendMessage("toast", "Failed to save settings!\nCheck permissions, or try to restart the device.");
	}

	if (editThenRestore_) {
		// In case we didn't have the title yet before, try again.
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
		g_Config.changeGameSpecific(gameID_, info->GetTitle());
		g_Config.unloadGameConfig();
	}

	host->UpdateUI();

	KeyMap::UpdateNativeMenuKeys();

	// Wipe some caches after potentially changing settings.
	NativeMessageReceived("gpu_resized", "");
	NativeMessageReceived("gpu_clearCache", "");
}

void GameSettingsScreen::sendMessage(const char *message, const char *value) {
	UIDialogScreenWithGameBackground::sendMessage(message, value);
	if (!strcmp(message, "postshader_updated")) {
		g_Config.bShaderChainRequires60FPS = PostShaderChainRequires60FPS(GetFullPostShadersChain(g_Config.vPostShaderNames));
		RecreateViews();
	}
	if (!strcmp(message, "gameSettings_search")) {
		std::string filter = value ? value : "";
		searchFilter_.resize(filter.size());
		std::transform(filter.begin(), filter.end(), searchFilter_.begin(), tolower);

		ApplySearchFilter();
	}
}

void GameSettingsScreen::ApplySearchFilter() {
	auto se = GetI18NCategory("Search");

	bool matches = searchFilter_.empty();
	for (int t = 0; t < (int)settingTabContents_.size(); ++t) {
		auto tabContents = settingTabContents_[t];
		bool tabMatches = searchFilter_.empty();

		// Show an indicator that a filter is applied.
		settingTabFilterNotices_[t]->SetVisibility(tabMatches ? UI::V_GONE : UI::V_VISIBLE);
		settingTabFilterNotices_[t]->SetText(ReplaceAll(se->T("Filtering settings by '%1'"), "%1", searchFilter_));

		UI::View *lastHeading = nullptr;
		for (int i = 1; i < tabContents->GetNumSubviews(); ++i) {
			UI::View *v = tabContents->GetViewByIndex(i);
			if (!v->CanBeFocused()) {
				lastHeading = v;
			}

			std::string label = v->DescribeText();
			std::transform(label.begin(), label.end(), label.begin(), tolower);
			bool match = v->CanBeFocused() && label.find(searchFilter_) != label.npos;
			tabMatches = tabMatches || match;

			if (match && lastHeading)
				lastHeading->SetVisibility(UI::V_VISIBLE);
			v->SetVisibility(searchFilter_.empty() || match ? UI::V_VISIBLE : UI::V_GONE);
		}
		tabHolder_->EnableTab(t, tabMatches);
		matches = matches || tabMatches;
	}

	noSearchResults_->SetText(ReplaceAll(se->T("No settings matched '%1'"), "%1", searchFilter_));
	noSearchResults_->SetVisibility(matches ? UI::V_GONE : UI::V_VISIBLE);
	clearSearchChoice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
}

void GameSettingsScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.iFpsLimit1 = iAlternateSpeedPercent1_ < 0 ? -1 : (iAlternateSpeedPercent1_ * 60) / 100;
		g_Config.iFpsLimit2 = iAlternateSpeedPercent2_ < 0 ? -1 : (iAlternateSpeedPercent2_ * 60) / 100;
		g_Config.iAnalogFpsLimit = (iAlternateSpeedPercentAnalog_ * 60) / 100;

		RecreateViews();
	}

	bool mapped = KeyMap::AxisFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, nullptr, nullptr);
	if (mapped != analogSpeedMapped_) {
		analogSpeedMapped_ = mapped;
		RecreateViews();
	}
}

void GameSettingsScreen::RecreateViews() {
	oldSettingInfo_ = settingInfo_->GetText();
	UIScreen::RecreateViews();
}

void GameSettingsScreen::CallbackMemstickFolder(bool yes) {
	auto sy = GetI18NCategory("System");

	if (yes) {
		Path memstickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
		std::string testWriteFile = pendingMemstickFolder_ + "/.write_verify_file";

		// Already, create away.
		if (!File::Exists(Path(pendingMemstickFolder_))) {
			File::CreateFullPath(Path(pendingMemstickFolder_));
		}
		if (!File::WriteDataToFile(true, "1", 1, Path(testWriteFile))) {
			settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
			return;
		}
		File::Delete(Path(testWriteFile));

		if (!File::WriteDataToFile(true, pendingMemstickFolder_.c_str(), (unsigned int)pendingMemstickFolder_.size(), memstickDirFile)) {
			WARN_LOG(SYSTEM, "Failed to write memstick folder to '%s'", memstickDirFile.c_str());
		} else {
			// Save so the settings, at least, are transferred.
			g_Config.memStickDirectory = Path(pendingMemstickFolder_);
			g_Config.Save("MemstickPathChanged");
		}
		screenManager()->RecreateAllViews();
	}
}

void GameSettingsScreen::TriggerRestart(const char *why) {
	// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
	// the GPU backend code.
	g_Config.Save(why);
	std::string param = "--gamesettings";
	if (editThenRestore_) {
		// We won't pass the gameID, so don't resume back into settings.
		param = "";
	} else if (!gamePath_.empty()) {
		param += " \"" + ReplaceAll(ReplaceAll(gamePath_.ToString(), "\\", "\\\\"), "\"", "\\\"") + "\"";
	}
	// Make sure the new instance is considered the first.
	ShutdownInstanceCounter();
	System_SendMessage("graphics_restart", param.c_str());
}

void GameSettingsScreen::CallbackRenderingBackend(bool yes) {
	// If the user ends up deciding not to restart, set the config back to the current backend
	// so it doesn't get switched by accident.
	if (yes) {
		TriggerRestart("GameSettingsScreen::RenderingBackendYes");
	} else {
		g_Config.iGPUBackend = (int)GetGPUBackend();
	}
}

void GameSettingsScreen::CallbackRenderingDevice(bool yes) {
	// If the user ends up deciding not to restart, set the config back to the current backend
	// so it doesn't get switched by accident.
	if (yes) {
		TriggerRestart("GameSettingsScreen::RenderingDeviceYes");
	} else {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting)
			*deviceNameSetting = GetGPUBackendDevice();
		// Needed to redraw the setting.
		RecreateViews();
	}
}

void GameSettingsScreen::CallbackInflightFrames(bool yes) {
	if (yes) {
		TriggerRestart("GameSettingsScreen::InflightFramesYes");
	} else {
		g_Config.iInflightFrames = prevInflightFrames_;
	}
}

UI::EventReturn GameSettingsScreen::OnRenderingBackend(UI::EventParams &e) {
	auto di = GetI18NCategory("Dialog");

	// It only makes sense to show the restart prompt if the backend was actually changed.
	if (g_Config.iGPUBackend != (int)GetGPUBackend()) {
		screenManager()->push(new PromptScreen(di->T("ChangingGPUBackends", "Changing GPU backends requires PPSSPP to restart. Restart now?"), di->T("Yes"), di->T("No"),
			std::bind(&GameSettingsScreen::CallbackRenderingBackend, this, std::placeholders::_1)));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingDevice(UI::EventParams &e) {
	auto di = GetI18NCategory("Dialog");

	// It only makes sense to show the restart prompt if the device was actually changed.
	std::string *deviceNameSetting = GPUDeviceNameSetting();
	if (deviceNameSetting && *deviceNameSetting != GetGPUBackendDevice()) {
		screenManager()->push(new PromptScreen(di->T("ChangingGPUBackends", "Changing GPU backends requires PPSSPP to restart. Restart now?"), di->T("Yes"), di->T("No"),
			std::bind(&GameSettingsScreen::CallbackRenderingDevice, this, std::placeholders::_1)));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnInflightFramesChoice(UI::EventParams &e) {
	auto di = GetI18NCategory("Dialog");
	if (g_Config.iInflightFrames != prevInflightFrames_) {
		screenManager()->push(new PromptScreen(di->T("ChangingInflightFrames", "Changing graphics command buffering requires PPSSPP to restart. Restart now?"), di->T("Yes"), di->T("No"),
			std::bind(&GameSettingsScreen::CallbackInflightFrames, this, std::placeholders::_1)));
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
	auto a = GetI18NCategory("Audio");
	if (g_Config.sAudioDevice == a->T("Auto")) {
		g_Config.sAudioDevice.clear();
	}
	System_SendMessage("audio_resetDevice", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat0(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter Quick Chat 1"), g_Config.sQuickChat0, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sQuickChat0 = value;
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat1(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter Quick Chat 2"), g_Config.sQuickChat1, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sQuickChat1 = value;
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat2(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter Quick Chat 3"), g_Config.sQuickChat2, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sQuickChat2 = value;
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat3(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter Quick Chat 4"), g_Config.sQuickChat3, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sQuickChat3 = value;
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeQuickChat4(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter Quick Chat 5"), g_Config.sQuickChat4, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sQuickChat4 = value;
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeNickname(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Enter a new PSP nickname"), g_Config.sNickName, [](bool result, const std::string &value) {
		if (result) {
			g_Config.sNickName = StripSpaces(value);
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeproAdhocServerAddress(UI::EventParams &e) {
	auto n = GetI18NCategory("Networking");

	screenManager()->push(new HostnameSelectScreen(&g_Config.proAdhocServer, n->T("proAdhocServer Address:")));

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeMacAddress(UI::EventParams &e) {
	g_Config.sMACAddress = CreateRandMAC();

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguage(UI::EventParams &e) {
	auto dev = GetI18NCategory("Developer");
	auto langScreen = new NewLanguageScreen(dev->T("Language"));
	langScreen->OnChoice.Handle(this, &GameSettingsScreen::OnLanguageChange);
	if (e.v)
		langScreen->SetPopupOrigin(e.v);
	screenManager()->push(langScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguageChange(UI::EventParams &e) {
	screenManager()->RecreateAllViews();

	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnPostProcShaderChange(UI::EventParams &e) {
	g_Config.vPostShaderNames.erase(std::remove(g_Config.vPostShaderNames.begin(), g_Config.vPostShaderNames.end(), "Off"), g_Config.vPostShaderNames.end());

	NativeMessageReceived("gpu_resized", "");
	NativeMessageReceived("postshader_updated", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTextureShader(UI::EventParams &e) {
	auto gr = GetI18NCategory("Graphics");
	auto shaderScreen = new TextureShaderScreen(gr->T("Texture Shader"));
	shaderScreen->OnChoice.Handle(this, &GameSettingsScreen::OnTextureShaderChange);
	if (e.v)
		shaderScreen->SetPopupOrigin(e.v);
	screenManager()->push(shaderScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTextureShaderChange(UI::EventParams &e) {
	NativeMessageReceived("gpu_resized", "");
	RecreateViews(); // Update setting name
	g_Config.bTexHardwareScaling = g_Config.sTextureShaderName != "Off";
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRemoteISO(UI::EventParams &e) {
	screenManager()->push(new RemoteISOScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnControlMapping(UI::EventParams &e) {
	screenManager()->push(new ControlMappingScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnCalibrateAnalogs(UI::EventParams &e) {
	screenManager()->push(new AnalogSetupScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTouchControlLayout(UI::EventParams &e) {
	screenManager()->push(new TouchControlLayoutScreen());
	return UI::EVENT_DONE;
}

//when the tilt event type is modified, we need to reset all tilt settings.
//refer to the ResetTiltEvents() function for a detailed explanation.
UI::EventReturn GameSettingsScreen::OnTiltTypeChange(UI::EventParams &e) {
	TiltEventProcessor::ResetTiltEvents();
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnTiltCustomize(UI::EventParams &e) {
	screenManager()->push(new TiltAnalogSettingsScreen());
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnSavedataManager(UI::EventParams &e) {
	auto saveData = new SavedataScreen(Path());
	screenManager()->push(saveData);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSysInfo(UI::EventParams &e) {
	screenManager()->push(new SystemInfoScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeSearchFilter(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(__ANDROID__)
	auto se = GetI18NCategory("Search");
	System_InputBoxGetString(se->T("Search term"), searchFilter_, [](bool result, const std::string &value) {
		if (result) {
			NativeMessageReceived("gameSettings_search", StripSpaces(value).c_str());
		}
	});
#else
	if (!System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		NativeMessageReceived("gameSettings_search", StripSpaces(searchFilter_).c_str());
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnClearSearchFilter(UI::EventParams &e) {
	NativeMessageReceived("gameSettings_search", "");
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	settingsScroll->SetTag("DevToolsSettings");
	root_->Add(settingsScroll);

	auto di = GetI18NCategory("Dialog");
	auto dev = GetI18NCategory("Developer");
	auto gr = GetI18NCategory("Graphics");
	auto a = GetI18NCategory("Audio");
	auto sy = GetI18NCategory("System");

	AddStandardBack(root_);

	LinearLayout *list = settingsScroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->SetSpacing(0);
	list->Add(new ItemHeader(sy->T("General")));

	bool canUseJit = true;
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)

	static const char *cpuCores[] = {"Interpreter", "Dynarec (JIT)", "IR Interpreter"};
	PopupMultiChoice *core = list->Add(new PopupMultiChoice(&g_Config.iCpuCore, gr->T("CPU Core"), cpuCores, 0, ARRAY_SIZE(cpuCores), sy->GetName(), screenManager()));
	core->OnChoice.Handle(this, &DeveloperToolsScreen::OnJitAffectingSetting);
	if (!canUseJit) {
		core->HideChoice(1);
	}

	list->Add(new Choice(dev->T("JIT debug tools")))->OnClick.Handle(this, &DeveloperToolsScreen::OnJitDebugTools);
	list->Add(new CheckBox(&g_Config.bShowDeveloperMenu, dev->T("Show Developer Menu")));
	list->Add(new CheckBox(&g_Config.bDumpDecryptedEboot, dev->T("Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game")));

#if !PPSSPP_PLATFORM(UWP)
	Choice *cpuTests = new Choice(dev->T("Run CPU Tests"));
	list->Add(cpuTests)->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);

	cpuTests->SetEnabled(TestsAvailable());
#endif
	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		list->Add(new Choice(dev->T("GPU Driver Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnGPUDriverTest);
	}
	list->Add(new CheckBox(&g_Config.bVendorBugChecksEnabled, dev->T("Enable driver bug workarounds")));
	list->Add(new Choice(dev->T("Framedump tests")))->OnClick.Handle(this, &DeveloperToolsScreen::OnFramedumpTest);
	list->Add(new Choice(dev->T("Touchscreen Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnTouchscreenTest);

	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
	CheckBox *allowDebugger = new CheckBox(&allowDebugger_, dev->T("Allow remote debugger"));
	list->Add(allowDebugger)->OnClick.Handle(this, &DeveloperToolsScreen::OnRemoteDebugger);
	allowDebugger->SetEnabledPtr(&canAllowDebugger_);

	list->Add(new CheckBox(&g_Config.bShowOnScreenMessages, dev->T("Show on-screen messages")));
	list->Add(new CheckBox(&g_Config.bEnableLogging, dev->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	if (GetGPUBackend() == GPUBackend::VULKAN) {
		list->Add(new CheckBox(&g_Config.bGpuLogProfiler, gr->T("GPU log profiler")));
	}
	list->Add(new CheckBox(&g_Config.bLogFrameDrops, dev->T("Log Dropped Frame Statistics")));
	list->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new ItemHeader(dev->T("Language")));
	list->Add(new Choice(dev->T("Load language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoadLanguageIni);
	list->Add(new Choice(dev->T("Save language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSaveLanguageIni);
	list->Add(new ItemHeader(dev->T("Texture Replacement")));
	list->Add(new CheckBox(&g_Config.bSaveNewTextures, dev->T("Save new textures")));
	list->Add(new CheckBox(&g_Config.bReplaceTextures, dev->T("Replace textures")));

	// Makes it easy to get savestates out of an iOS device. The file listing shown in MacOS doesn't allow
	// you to descend into directories.
#if PPSSPP_PLATFORM(IOS)
	list->Add(new Choice(dev->T("Copy savestates to memstick root")))->OnClick.Handle(this, &DeveloperToolsScreen::OnCopyStatesToRoot);
#endif

	// Reconsider whenever recreating views.
	hasTexturesIni_ = HasIni::MAYBE;

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
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	g_Config.Save("DeveloperToolsScreen::onFinish");
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes)
		g_Config.RestoreDefaults();
	host->UpdateUI();
}

UI::EventReturn GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	auto dev = GetI18NCategory("Developer");
	auto di = GetI18NCategory("Dialog");
	if (g_Config.bGameSpecific)
	{
		screenManager()->push(
			new PromptScreen(dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"), di->T("OK"), di->T("Cancel"),
			std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, std::placeholders::_1)));
	}
	else
	{
		screenManager()->push(
			new PromptScreen(dev->T("RestoreDefaultSettings", "Are you sure you want to restore all settings(except control mapping)\nback to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings."), di->T("OK"), di->T("Cancel"),
			std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, std::placeholders::_1)));
	}

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	host->ToggleDebugConsoleVisibility();
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
#if !PPSSPP_PLATFORM(UWP)
	RunTests();
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnSaveLanguageIni(UI::EventParams &e) {
	i18nrepo.SaveIni(g_Config.sLanguageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoadLanguageIni(UI::EventParams &e) {
	i18nrepo.LoadIni(g_Config.sLanguageIni);
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
			auto dev = GetI18NCategory("Developer");
			System_Toast((generatedFilename.ToVisualString() + ": " +  dev->T("Texture ini file created")).c_str());
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
	screenManager()->push(new TouchTestScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
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
		INFO_LOG(SYSTEM, "Copying file '%s' to '%s'", src.c_str(), dst.c_str());
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
	return UI::EVENT_CONTINUE;
}

void DeveloperToolsScreen::update() {
	UIDialogScreenWithBackground::update();
	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
}

void HostnameSelectScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto sy = GetI18NCategory("System");
	auto di = GetI18NCategory("Dialog");
	auto n = GetI18NCategory("Networking");

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
#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		buttonsRow2->Add(new Button(di->T("Edit")))->OnClick.Handle(this, &HostnameSelectScreen::OnEditClick);
#endif
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

void HostnameSelectScreen::SendEditKey(int keyCode, int flags) {
	auto oldView = UI::GetFocusedView();
	UI::SetFocusedView(addrView_);
	KeyInput fakeKey{ DEVICE_ID_KEYBOARD, keyCode, KEY_DOWN | flags };
	addrView_->Key(fakeKey);
	UI::SetFocusedView(oldView);
}

UI::EventReturn HostnameSelectScreen::OnNumberClick(UI::EventParams &e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() == 1 && text[0] >= '0' && text[0] <= '9') {
		SendEditKey(text[0], KEY_CHAR);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnPointClick(UI::EventParams &e) {
	SendEditKey('.', KEY_CHAR);
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
	auto n = GetI18NCategory("Networking");
#if PPSSPP_PLATFORM(ANDROID)
	System_InputBoxGetString(n->T("proAdhocServer Address:"), addrView_->GetText(), [this](bool result, const std::string& value) {
		if (result) {
		    addrView_->SetText(value);
		}
	});
#endif
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
		// TODO: Copy the IP to clipboard for the host to easily share their IP through chatting apps.
		System_SendMessage("setclipboardtext", text.c_str()); // Doesn't seems to be working on windows (yet?)
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
	auto n = GetI18NCategory("Networking");

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

	auto di = GetI18NCategory("Dialog");
	auto co = GetI18NCategory("Controls");
	auto mc = GetI18NCategory("MappableControls");

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
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), mc->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), mc->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), mc->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&g_Config.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), mc->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&g_Config.fSwipeSensitivity, 0.01f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&g_Config.fSwipeSmoothing, 0.0f, 0.95f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Double tap")));
	vert->Add(new PopupMultiChoice(&g_Config.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), mc->GetName(), screenManager()))->SetEnabledPtr(&g_Config.bGestureControlEnabled);
}
