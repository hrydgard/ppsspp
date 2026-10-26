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
#include <string>

#include "ppsspp_config.h"

#include "Common/BitSet.h"
#include "Common/Data/Text/I18n.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Instance.h"
#include "Core/KeyMap.h"
#include "Core/System.h"
#include "GPU/Common/PostShader.h"
#include "UI/DevScreens.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/MiscScreens.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"

static bool UsingHardwareTextureScaling() {
	return g_Config.bTexHardwareScaling && (GetGPUBackend() == GPUBackend::VULKAN || GetGPUBackend() == GPUBackend::METAL) && !g_Config.bSoftwareRendering;
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

// TODO: Make this generic
extern int DefaultDepthRaster();

static void OnRenderingBackend(SettingsPageHost *host) {
	// It only makes sense to show the restart prompt if the backend was actually changed.
	if (g_Config.iGPUBackend != (int)GetGPUBackend()) {
		host->RestartOrDo([]() {
			g_Config.iGPUBackend = (int)GetGPUBackend();
		});
	}
}

static void OnRenderingDevice(SettingsPageHost *host) {
	// It only makes sense to show the restart prompt if the device was actually changed.
	std::string *deviceNameSetting = GPUDeviceNameSetting();
	if (deviceNameSetting && *deviceNameSetting != GetGPUBackendDevice()) {
		host->RestartOrDo([host]() {
			std::string *deviceNameSetting = GPUDeviceNameSetting();
			if (deviceNameSetting)
				*deviceNameSetting = GetGPUBackendDevice();
			// Needed to redraw the setting.
			host->RecreatePageViews();
		});
	}
}

static void BuildSpeedHacksPage(SettingsPage *page) {
	using namespace UI;

	page->Header();

	page->Setting(&g_Config.iFrameSkip).EnabledFunc([] {
		return !g_Config.bAutoFrameSkip;
	}).Setup([](View *view) {
		PopupMultiChoice *choice = static_cast<PopupMultiChoice *>(view);
		choice->SetChoicesUntranslated(1, choice->NumChoices() - 1);
	});

	page->Setting(&g_Config.bAutoFrameSkip);

	page->Header();

	page->Setting(&g_Config.bSkipBufferEffects).Disabled(&g_Config.bSoftwareRendering);
	page->Hint({I18NCat::GRAPHICS, "RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games"});

	page->Setting(&g_Config.iSkipGPUReadbackMode).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		static_cast<PopupMultiChoice *>(view)->SetDefault(0);
	});

	page->Setting(&g_Config.iDepthRasterMode).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		PopupMultiChoice *depthRasterMode = static_cast<PopupMultiChoice *>(view);
		depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.
		depthRasterMode->SetDefault(DefaultDepthRaster());
		if (g_Config.iDepthRasterMode != 3)
			depthRasterMode->HideChoice(3);
	});
}

static void BuildCardboardVRPage(SettingsPage *page) {
	page->Header();

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());

	page->Setting(&config.bEnableCardboardVR);
	page->Setting(&config.iCardboardScreenSize).Enabled(&config.bEnableCardboardVR);
	page->Setting(&config.iCardboardXShift).Enabled(&config.bEnableCardboardVR);
	page->Setting(&config.iCardboardYShift).Enabled(&config.bEnableCardboardVR);
}

void BuildGraphicsSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	Draw::DrawContext *draw = host->PageScreenManager()->getDrawContext();
	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	page->Header("Rendering Mode");

#if !PPSSPP_PLATFORM(UWP)
	page->Setting(&g_Config.iGPUBackend).Setup([host](View *view) {
		PopupMultiChoice *renderingBackendChoice = static_cast<PopupMultiChoice *>(view);
		renderingBackendChoice->SetPreOpenCallback([host](UI::PopupMultiChoice *choice) {
			// Don't filter until the last possible moment, since it involves trying to initialize Vulkan, if we were
			// started in OpenGL mode on Android.
			choice->HideChoice(1);
			choice->OnChoice.Add([host](UI::EventParams &) {
				OnRenderingBackend(host);
			});

			if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
				choice->HideChoice((int)GPUBackend::OPENGL);
			if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
				choice->HideChoice((int)GPUBackend::DIRECT3D11);
			if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
				choice->HideChoice((int)GPUBackend::VULKAN);
			if (!g_Config.IsBackendEnabled(GPUBackend::METAL))
				choice->HideChoice((int)GPUBackend::METAL);
		});

		if (!IsFirstInstance()) {
			// If we're not the first instance, can't save the setting, and it requires a restart, so...
			renderingBackendChoice->SetEnabled(false);
		}
	});
#endif

#if (PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(TVOS)) || PPSSPP_PLATFORM(ANDROID)
	// Showing the game there needs a second swapchain. Otherwise the system mirrors the screen.
	// Looked up each time: the draw context is replaced when Android loses the surface, and this page isn't rebuilt.
	page->Setting(&g_Config.iExternalDisplay).EnabledFunc([host] {
		Draw::DrawContext *draw = host->PageScreenManager()->getDrawContext();
		return draw && draw->GetDeviceCaps().multipleSwapchains;
	}).OnChange([previous = g_Config.iExternalDisplay](UI::EventParams &e) mutable {
		// Picking the current value again would rebuild the TV's output for nothing (iOS starts over on a change).
		if (g_Config.iExternalDisplay == previous) {
			return;
		}
		previous = g_Config.iExternalDisplay;
		System_Notify(SystemNotification::EXTERNAL_DISPLAY_CHANGED);
	});
#endif

	// Backends that don't allow a device choice will only expose one device.
	if (draw->GetDeviceList().size() > 1) {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting) {
			page->Setting(deviceNameSetting, draw->GetDeviceList()).OnChange([host](UI::EventParams &e) {
				OnRenderingDevice(host);
			});
		}
	}

	page->Setting(&g_Config.iInternalResolution).EnabledFunc([] {
		return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
	});

	if (deviceType != DEVICE_TYPE_VR) {
		page->Setting(&g_Config.bSoftwareRendering).EnabledFunc([] {
			return !PSP_IsInited();
		});
	}

	if (draw->GetDeviceCaps().multiSampleLevelsMask != 1) {
		page->Setting(&g_Config.iMultiSampleLevel).EnabledFunc([] {
			return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
		}).Setup([draw](View *view) {
			PopupMultiChoice *msaaChoice = static_cast<PopupMultiChoice *>(view);
			if (g_Config.iMultiSampleLevel > 1 && draw->GetDeviceCaps().isTilingGPU) {
				msaaChoice->SetIconRight(ImageID("I_WARNING"), 0.7f);
			}
			// Hide unsupported levels.
			for (int i = 1; i < msaaChoice->NumChoices(); i++) {
				if ((draw->GetDeviceCaps().multiSampleLevelsMask & (1 << i)) == 0) {
					msaaChoice->HideChoice(i);
				} else if (i > 0 && draw->GetDeviceCaps().isTilingGPU) {
					msaaChoice->SetChoiceIcon(i, ImageID("I_WARNING"));
				}
			}
		});
	}

#if PPSSPP_PLATFORM(ANDROID)
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		int max_res = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 2;
		if (max_res == 3)
			max_res = 4;  // At least allow 2x
		page->Setting(&g_Config.iAndroidHwScale).Setup([max_res](View *view) {
			// Hide the scales the display is too small for.
			PopupMultiChoice *choice = static_cast<PopupMultiChoice *>(view);
			for (int i = max_res; i < choice->NumChoices(); i++) {
				choice->HideChoice(i);
			}
		});
	}
#endif

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());

	page->Setting(&g_Config.bReplaceTextures);

	page->Header("Display");

	if (deviceType != DEVICE_TYPE_VR) {
#if !defined(MOBILE_DEVICE)
		page->Setting(&g_Config.bFullScreen);
		if (draw->GetDeviceCaps().fullScreenExclusiveSupported) {
			page->Setting(&g_Config.bAllowFullScreenExclusive).EnabledFunc([] {
				return g_Config.bFullScreen && !g_Config.bFullScreenMulti;
			}).OnChange([host](UI::EventParams &e) {
				host->RestartOrDo([host]() {
					g_Config.bAllowFullScreenExclusive = !g_Config.bAllowFullScreenExclusive;
					host->RecreatePageViews();
				});
			});
		}
		if (System_GetPropertyInt(SYSPROP_DISPLAY_COUNT) > 1) {
			page->Setting(&g_Config.bFullScreenMulti).EnabledFunc([] {
				return g_Config.bFullScreen && !g_Config.bAllowFullScreenExclusive;
			});
		}
#endif
		// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
		page->Choice("Display layout & effects").OnClick([host](UI::EventParams &) {
			host->PageScreenManager()->push(new DisplayLayoutScreen(host->PageGamePath()));
		});
	}

	// If only one mode is supported (like FIFO on iOS), no need to show the options.
	if (CountSetBits((u32)draw->GetDeviceCaps().presentModesSupported) > 1) {
		// Immediate means non-synchronized, tearing.
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
			page->Setting(&g_Config.bVSync).OnChange([](UI::EventParams &e) {
				NativeResized();  // TODO: Remove
			});
		}
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) {
			SettingItem &lowLatency = page->Setting(&g_Config.bLowLatencyPresent).OnChange([](UI::EventParams &e) {
				NativeResized();  // TODO: Remove
			});

			// If the immediate mode is supported, we can tie low latency present to VSync.
			if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
				lowLatency.Enabled(&g_Config.bVSync);
			}
		}
	}

	page->Header("Frame Rate Control");

	page->Setting(&g_Config.iAltSpeedPercent1);
	page->Setting(&g_Config.iAltSpeedPercent2);
	if (KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true)) {
		page->Setting(&g_Config.iAnalogAltSpeedPercent);
	}

	page->Header();
	page->Link({I18NCat::GRAPHICS, "Speed Hacks"}, I18NCat::GRAPHICS, &BuildSpeedHacksPage);

	page->Header();
	page->Setting(&g_Config.iBloomHack).EnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.iInternalResolution != 1;
	});
	page->Hint("Reduces artifacts");

	page->Header("Performance");
	page->Setting(&g_Config.bRenderDuplicateFrames).EnabledFunc([] {
		return !g_Config.bSkipBufferEffects && g_Config.iFrameSkip == 0;
	});
	page->Hint({ I18NCat::GRAPHICS, "RenderDuplicateFrames Tip", "Can make framerate smoother in games that run at lower framerates" });

	if (draw->GetDeviceCaps().setMaxFrameLatencySupported) {
		// Changing it needs a restart, so the value in use is the one the page was built with.
		page->Setting(&g_Config.iInflightFrames).OnChange([host, previous = g_Config.iInflightFrames](UI::EventParams &e) {
			if (g_Config.iInflightFrames != previous) {
				host->RestartOrDo([previous]() {
					g_Config.iInflightFrames = previous;
				});
			}
		});
		page->Hint("Faster, input lag");  // TODO: This hint could use improvement.
	}

	page->Header("Texture upscaling");

	if (GetGPUBackend() == GPUBackend::VULKAN || GetGPUBackend() == GPUBackend::METAL) {
		page->Custom("GPU texture upscaler (fast)", [host](UI::ViewGroup *parent) -> UI::View * {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			ChoiceWithValueDisplay *textureShaderChoice = parent->Add(new ChoiceWithValueDisplay(&g_Config.sTextureShaderName, gr->T("GPU texture upscaler (fast)"), &TextureTranslateName));
			textureShaderChoice->OnClick.Add([host](UI::EventParams &e) {
				auto gr = GetI18NCategory(I18NCat::GRAPHICS);
				auto shaderScreen = new TextureShaderScreen(gr->T("GPU texture upscaler (fast)"));
				shaderScreen->OnChoice.Add([host](UI::EventParams &e) {
					System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
					host->RecreatePageViews(); // Update setting name
					g_Config.bTexHardwareScaling = g_Config.sTextureShaderName != "Off";
				});
				if (e.v)
					shaderScreen->SetPopupOrigin(e.v);
				host->PageScreenManager()->push(shaderScreen);
			});
			return textureShaderChoice;
		}).Disabled(&g_Config.bSoftwareRendering);
	}

	page->Setting(&g_Config.iTexScalingType).EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page->Setting(&g_Config.iTexScalingLevel).EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page->Hint({ I18NCat::GRAPHICS, "UpscaleLevel Tip", "CPU heavy - some scaling may be delayed to avoid stutter" });

	page->Setting(&g_Config.bTexDeposterize).EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page->Hint({ I18NCat::GRAPHICS, "Deposterize Tip", "Fixes visual banding glitches in upscaled textures" });

	page->Header("Texture Filtering");

	page->Setting(&g_Config.iAnisotropyLevel).Disabled(&g_Config.bSoftwareRendering);

	page->Setting(&g_Config.iTexFiltering).Disabled(&g_Config.bSoftwareRendering);

	page->Setting(&g_Config.bSmart2DTexFiltering).Disabled(&g_Config.bSoftwareRendering);

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	bool showCardboardSettings = deviceType != DEVICE_TYPE_VR;
#else
	// If you enabled it through the ini, you can see this. Useful for testing.
	bool showCardboardSettings = config.bEnableCardboardVR;
#endif
	// showCardboardSettings = true;  // for testing ONLY
	if (showCardboardSettings) {
		page->Header();
		page->Link({I18NCat::GRAPHICS, "Cardboard VR Settings"}, I18NCat::GRAPHICS, &BuildCardboardVRPage);
	}

	page->Header("Overlay Information");
	auto addStatusFlag = [page](ShowStatusFlags flag, const char *text) {
		page->Custom(text, [flag, text](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(&g_Config.iShowStatusFlags, (int)flag, T(I18NCat::GRAPHICS, text)));
		});
	};
	addStatusFlag(ShowStatusFlags::FPS_COUNTER, "Show FPS Counter");
	addStatusFlag(ShowStatusFlags::SPEED_COUNTER, "Show Speed");
	if (System_GetPropertyBool(SYSPROP_CAN_READ_BATTERY_PERCENTAGE)) {
		addStatusFlag(ShowStatusFlags::BATTERY_PERCENT, "Show Battery %");
	}
	page->Custom({ I18NCat::DEVELOPER, "Debug overlay" }, [host](UI::ViewGroup *parent) -> UI::View * {
		AddOverlayList(parent, host->PageScreenManager());
		return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
	});
}
