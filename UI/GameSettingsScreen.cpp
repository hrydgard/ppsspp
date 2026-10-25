
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
#include "Common/UI/ScreenManager.h"
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
#include "UI/SettingsPage.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/FirmwareScreen.h"
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
#include "UI/AdhocServerScreen.h"

#include "Common/File/FileUtil.h"
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
#include "Core/Util/PortManager.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GPU.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "Core/Util/DarwinFileSystemServices.h"
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
	System_BrowseForFolder(requesterToken, "", initialPath, [](std::string_view value, int) {
		INFO_LOG(Log::System, "Selected path: %.*s", STR_VIEW(value));
		DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path(value));
	});
}
#endif

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: SettingsPagesDialogScreen(gamePath, &g_Config.iSettingsCurrentTab, TabDialogFlags::HorizontalOnlyIcons | TabDialogFlags::VerticalShowIcons), gameID_(gameID), editGameSpecificThenRestore_(editThenRestore) {
	if (editGameSpecificThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.LoadGameConfig(gameID_);
	}

	ReadConfigTemporaries();
}

// Also called after restoring defaults, so the old values aren't written back over them.
void GameSettingsScreen::ReadConfigTemporaries() {
	// Written back on exit, so it must be read even if the System tab is never opened.
	enableReports_ = Reporting::IsEnabled();
}

GameSettingsScreen::~GameSettingsScreen() {
	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		std::string message = "Failed to save settings!\n";
#ifdef MOBILE_DEVICE
		message += "Check permissions, or try to restart the device.";
#else
		message += "Make sure this folder isn't read-only:\n";
		message += g_Config.memStickDirectory.ToVisualString();
#endif
		System_Toast(message);
	}

	if (editGameSpecificThenRestore_) {
		// We already saved above. Just unload the game config.
		// Note that we are leaving the screen here in practice, we don't need to reset the bool.
		g_Config.UnloadGameConfig();
	}

	System_Notify(SystemNotification::UI);

	// Wipe some caches after potentially changing settings.
	// Let's not send resize messages here, handled elsewhere.
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::OpenDeveloperTools() {
	OpenPage("GameSettingsTools", "Developer Tools");
}

void GameSettingsScreen::PreCreateViews() {
	if (RecreatingForPageChange()) {
		return;
	}
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	CheckMemstickInstallState();
#endif
}

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

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
// Works out where the memstick is (installed.txt next to the exe means My Documents or a custom
// folder), and which of the two checkboxes can be changed.
void GameSettingsScreen::CheckMemstickInstallState() {
	const Path &PPSSPPpath = File::GetExeDirectory();
	const Path installedFile = PPSSPPpath / "installed.txt";
	const bool myDocsExists = W32Util::UserDocumentsPath().size() != 0;

	installed_ = File::Exists(installedFile);
	otherinstalled_ = false;
	memstickMyDocEnabled_ = !PSP_IsInited();
	memstickOtherEnabled_ = false;
	if (!installed_ && myDocsExists) {
		// Disable the setting if we can't create & delete a file next to the exe.
		if (File::CreateEmptyFile(PPSSPPpath / "installedTEMP.txt")) {
			if (!(File::Delete(PPSSPPpath / "installedTEMP.txt")))
				memstickMyDocEnabled_ = false;
			else
				memstickOtherEnabled_ = !PSP_IsInited();
		} else
			memstickMyDocEnabled_ = false;
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
				memstickOtherEnabled_ = !PSP_IsInited();
				if (tempStr && strlen(tempStr) != 0 && strcmp(tempStr, "\n") != 0) {
					installed_ = false;
					otherinstalled_ = true;
				}
				fclose(testInstalled);
			}
		} else if (!myDocsExists) {
			memstickMyDocEnabled_ = false;
		}
	}
}
#endif

// The tabs other than System, which needs more of the screen.
static void BuildGraphicsSettings(SettingsPage *page);
static void BuildControlsSettings(SettingsPage *page);
static void BuildAudioSettings(SettingsPage *page);
static void BuildNetworkingSettings(SettingsPage *page);
static void BuildToolsSettings(SettingsPage *page);
static void BuildVRSettings(SettingsPage *page);

const std::vector<GameSettingsScreen::PageDef> &GameSettingsScreen::PageDefs() {
	static const std::vector<PageDef> pages = {
		{ "GameSettingsGraphics", "Graphics", "I_DISPLAY", "graphics", I18NCat::GRAPHICS, &BuildGraphicsSettings },
		{ "GameSettingsControls", "Controls", "I_CONTROLLER", "controls", I18NCat::CONTROLS, &BuildControlsSettings },
		{ "GameSettingsAudio", "Audio", "I_SPEAKER_MAX", "audio", I18NCat::AUDIO, &BuildAudioSettings },
		{ "GameSettingsNetworking", "Networking", "I_WIFI", "network", I18NCat::NETWORKING, &BuildNetworkingSettings },
		{ "GameSettingsTools", "Tools", "I_TOOLS", "tools", I18NCat::MAINSETTINGS, &BuildToolsSettings },
		{ "GameSettingsSystem", "System", "I_PSP", "system", I18NCat::SYSTEM, nullptr },
		{ "GameSettingsVR", "VR", nullptr, nullptr, I18NCat::VR, &BuildVRSettings },
	};
	return pages;
}

void GameSettingsScreen::CreateTabs() {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	bool showVR = deviceType == DEVICE_TYPE_VR || g_Config.bForceVR;

	for (const PageDef &def : PageDefs()) {
		if (def.build == &BuildVRSettings && !showVR) {
			continue;
		}
		std::function<void(SettingsPage *)> build = def.build;
		if (!build) {
			build = [this](SettingsPage *page) {
				BuildSystemSettings(page);
			};
		}
		AddSettingsTab(def.tag, ms->T(def.title), def.icon ? ImageID(def.icon) : ImageID::invalid(), def.cat, build, def.helpTag);
	}
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

static void BuildGraphicsSettings(SettingsPage *page) {
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
	page->Setting(&g_Config.iExternalDisplay).EnabledFunc([draw] {
		return draw->GetDeviceCaps().multipleSwapchains;
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

	page->Header({ I18NCat::GRAPHICS, "Speed Hacks", "Speed Hacks (can cause rendering errors!)" });

	page->Setting(&g_Config.iFrameSkip).EnabledFunc([] {
		return !g_Config.bAutoFrameSkip;
	}).Setup([](View *view) {
		PopupMultiChoice *choice = static_cast<PopupMultiChoice *>(view);
		choice->SetChoicesUntranslated(1, choice->NumChoices() - 1);
	});

	page->Setting(&g_Config.bAutoFrameSkip);

	page->Setting(&g_Config.bSkipBufferEffects).Disabled(&g_Config.bSoftwareRendering);
	page->Hint({ I18NCat::GRAPHICS, "RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games" });

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
	if (showCardboardSettings) {
		page->Header("Cardboard VR Settings");
		page->Setting(&config.bEnableCardboardVR);
		page->Setting(&config.iCardboardScreenSize).Enabled(&config.bEnableCardboardVR);
		page->Setting(&config.iCardboardXShift).Enabled(&config.bEnableCardboardVR);
		page->Setting(&config.iCardboardYShift).Enabled(&config.bEnableCardboardVR);
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

static void BuildAudioSettings(SettingsPage *page) {
	using namespace UI;

#if PPSSPP_PLATFORM(IOS)
	page->Setting(&g_Config.bAudioRespectSilentMode).Enabled(&g_Config.bEnableSound);
#endif
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	page->Setting(&g_Config.bAudioMixWithOthers).Enabled(&g_Config.bEnableSound);
#endif
	page->Header("Audio playback");

	page->Setting(&g_Config.iAudioPlaybackMode);
	page->Setting(&g_Config.bFillAudioGaps).EnabledFunc([]() {
		return g_Config.iAudioPlaybackMode == (int)AudioSyncMode::GRANULAR;
	});

	page->Header("Game volume");

	// This is here because it now only applies to in-game. Muting the menu sounds is separate.
	page->Setting(&g_Config.bEnableSound);

	page->Setting(&g_Config.iGameVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iReverbVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iAltSpeedVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iAchievementVolume).Enabled(&g_Config.bEnableSound).OnChange([](EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
	});

	page->Header();

	page->Setting(&g_Config.bUISound);
	page->Setting(&g_Config.iUIVolume).LiveUpdate().Enabled(&g_Config.bUISound).OnChange([](EventParams &e) {
		static double lastTimePlayed = 0.0;
		double now = time_now_d();
		if (now - lastTimePlayed < 0.1) {
			return; // Don't play if we just played one, to avoid spamming when dragging.
		}
		lastTimePlayed = now;
		// Audio preview
		PlayUISound(UI::UISound::CONFIRM);
	});

	page->Setting(&g_Config.iGamePreviewVolume);

	bool sdlAudio = false;

#if defined(SDL)
	page->Header("Audio backend");
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
	page->Setting(&g_Config.sAudioDevice, std::move(audioDeviceList)).OnChange([](EventParams &e) {
		if (g_Config.sAudioDevice == T(I18NCat::AUDIO, "Auto")) {
			g_Config.sAudioDevice.clear();
		}
		System_Notify(SystemNotification::AUDIO_RESET_DEVICE);
	});
	sdlAudio = true;

	static const int bufferSizes[] = {128, 256, 512, 1024, 2048};
	page->IntSlider(&g_Config.iSDLAudioBufferSize, 0, 2048, "Buffer size").Default(256).RestrictTo(bufferSizes, ARRAY_SIZE(bufferSizes));
	page->Hint({ I18NCat::DIALOG, "This change will not take effect until PPSSPP is restarted." });
#endif

#if PPSSPP_PLATFORM(WINDOWS)
	extern AudioBackend *g_audioBackend;

	std::vector<std::string> audioDeviceNames;
	std::vector<std::string> audioDeviceIds;

	std::vector<AudioDeviceDesc> deviceDescs;
	g_audioBackend->EnumerateDevices(&deviceDescs);
	if (!deviceDescs.empty()) {
		page->Header("Audio backend");
		for (auto &desc : deviceDescs) {
			audioDeviceNames.push_back(desc.name);
			audioDeviceIds.push_back(desc.uniqueId);
		}

		audioDeviceNames.insert(audioDeviceNames.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
		audioDeviceIds.insert(audioDeviceIds.begin(), "");

		page->Setting(&g_Config.sAudioDevice, std::move(audioDeviceNames), std::move(audioDeviceIds)).OnChange([](EventParams &) {
			bool reverted;
			if (g_audioBackend->InitOutputDevice(g_Config.sAudioDevice, LatencyMode::Aggressive, &reverted)) {
				if (reverted) {
					WARN_LOG(Log::Audio, "Unexpected: After a direct choice, audio device reverted to default. '%s'", g_Config.sAudioDevice.c_str());
				}
			} else {
				WARN_LOG(Log::Audio, "InitOutputDevice failed");
			}
		});
		page->Setting(&g_Config.bAutoSwitchAudioDevice).EnabledFunc([]()->bool {
			return g_Config.sAudioDevice.empty();
		});
	}

#else
	if (sdlAudio) {
		page->Setting(&g_Config.bAutoSwitchAudioDevice);
	} else {
		page->Header("Audio backend");
	}

#if PPSSPP_PLATFORM(ANDROID)
	page->Setting(&g_Config.bExtraAudioBuffering);

	auto restartAudio = [](UI::EventParams &) {
		AndroidAudio_Restart(g_audioState);
	};
	// AAudio isn't stable before Android 8.1, so there it's always OpenSL ES.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 27) {
		page->Setting(&g_Config.iAndroidAudioApi).OnChange(restartAudio);
		page->Hint({ I18NCat::AUDIO, "AudioApiHint", "Try the other one if you have no sound or other audio problems." });

		page->Setting(&g_Config.bAndroidAudioMMap).OnChange(restartAudio).EnabledFunc([]() {
			return g_Config.iAndroidAudioApi == (int)AndroidAudioApi::AAUDIO;
		});
		page->Hint({ I18NCat::AUDIO, "AudioMMapHint", "Lowest audio delay. On some devices, it causes silence over Bluetooth or other connected audio devices. Turn it off if you get no sound." });
	}
	// Show Oboe debug info
	const std::string audioErrorStr = AndroidAudio_GetErrorString(g_audioState);
	if (!audioErrorStr.empty()) {
		page->Info("Audio Error", audioErrorStr);
	}
#endif
#endif

	std::vector<std::string> micList = Microphone::getDeviceList();
	if (!micList.empty()) {
		page->Header("Microphone");
		std::vector<std::string> micValues = micList;
		micList.emplace(micList.begin(), T(I18NCat::AUDIO, "Auto"));
		micValues.insert(micValues.begin(), "");
		page->Setting(&g_Config.sMicDevice, std::move(micList), std::move(micValues));
	}
}

static void BuildControlsSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	page->Header({ I18NCat::MAINSETTINGS, "Controls" });
	page->Choice("Control mapping").OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new ControlMappingScreen(host->PageGamePath()));
	});
	page->Choice("Calibrate analog stick").OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new AnalogCalibrationScreen(host->PageGamePath()));
	});

#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
	page->Setting(&g_Config.bSystemControls);
#endif
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	page->Setting(&g_Config.bGamepadOnlyFocused);
#endif

	if (System_GetPropertyBool(SYSPROP_HAS_ACCELEROMETER)) {
		page->Custom("Tilt control setup", [host](UI::ViewGroup *parent) -> UI::View * {
			// Show the tilt type on the item.
			Choice *customizeTilt = parent->Add(new ChoiceWithCallbackValueDisplay(T(I18NCat::CONTROLS, "Tilt control setup"), []() -> std::string {
				if (g_Config.bTiltInputEnabled && (u32)g_Config.iTiltInputType < (u32)g_numTiltTypes) {
					auto co = GetI18NCategory(I18NCat::CONTROLS);
					return std::string(co->T(g_tiltTypes[g_Config.iTiltInputType]));
				} else {
					auto di = GetI18NCategory(I18NCat::DIALOG);
					return std::string(di->T("Disabled"));
				}
			}));
			customizeTilt->OnClick.Add([host](UI::EventParams &e) {
				host->PageScreenManager()->push(new TiltAnalogSettingsScreen(host->PageGamePath()));
			});
			return customizeTilt;
		});
	} else if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {  // TODO: This seems like a regression
		page->Setting(&g_Config.bHapticFeedback);
	}

	page->Header("Analog to digital mapping");
	page->Setting(&g_Config.fAnalogTriggerThreshold);
	page->Setting(&g_Config.fAnalogStickThreshold);

	// TVs don't have touch control, at least not yet.
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		page->Header("On-screen touch controls");
		page->Setting(&g_Config.bShowTouchControls);
		page->Choice("Edit touch control layout").OnClick([host](UI::EventParams &e) {
			host->PageScreenManager()->push(new TouchControlLayoutScreen(host->PageGamePath()));
		}).Enabled(&g_Config.bShowTouchControls);
		page->Choice("Gesture mapping").OnClick([host](UI::EventParams &e) {
			host->PageScreenManager()->push(new GestureMappingScreen(host->PageGamePath()));
		}).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.iTouchButtonStyle).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.iTouchButtonOpacity).Enabled(&g_Config.bShowTouchControls);
		page->Setting(&g_Config.iTouchButtonHideSeconds).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.bTouchGliding).Enabled(&g_Config.bShowTouchControls);

		TouchControlConfig &touch = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());

		// Hide stick background, useful when increasing the size
		page->Setting(&touch.bHideStickBackground).Enabled(&g_Config.bShowTouchControls);

		// Sticky D-pad.
		page->Setting(&g_Config.bStickyTouchDPad).Enabled(&g_Config.bShowTouchControls);
		page->Hint("Easier sweeping movements");

		// Re-centers itself to the touch location on touch-down.
		page->Setting(&g_Config.bAutoCenterTouchAnalog).Enabled(&g_Config.bShowTouchControls);

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			page->Setting(&g_Config.bHapticFeedback);
		}

		// The pause button is now a regular on-screen button.

		page->Setting(&g_Config.bDisableDpadDiagonals).Enabled(&g_Config.bShowTouchControls);
	}

#if PPSSPP_PLATFORM(WINDOWS)
	page->Header("Control input sources");
	page->Setting(&g_Config.bAllowHIDInput);
	page->Setting(&g_Config.bAllowXInput);
	page->Setting(&g_Config.bAllowDInput);
#endif

	if (deviceType != DEVICE_TYPE_VR) {
		page->Header({ I18NCat::CONTROLS, "Keyboard", "Keyboard Control Settings" });
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page->Setting(&g_Config.bIgnoreWindowsKey);
#endif
		page->Setting(&g_Config.fAnalogLimiterDeadzone);
		page->Hint({ I18NCat::CONTROLS, "AnalogLimiter Tip", "When the analog limiter button is pressed" });
		page->Setting(&g_Config.iRapidFireInterval);
#if PPSSPP_PLATFORM(WINDOWS) || defined(SDL) || PPSSPP_PLATFORM(ANDROID)
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
			page->Header({ I18NCat::CONTROLS, "Mouse", "Mouse settings" });
			page->Setting(&g_Config.iMouseWheelUpDelayMs);

			page->Setting(&g_Config.bMouseControl);
			page->Hint({ I18NCat::CONTROLS, "MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon." });

#if !PPSSPP_PLATFORM(ANDROID)
			page->Setting(&g_Config.bMouseConfine).Enabled(&g_Config.bMouseControl);
#endif
			page->Setting(&g_Config.fMouseSensitivity).Enabled(&g_Config.bMouseControl).LiveUpdate();
			page->Setting(&g_Config.fMouseSmoothing).Enabled(&g_Config.bMouseControl).LiveUpdate();
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

		auto confirmScreen = new MessagePopupScreen(
			title,
			combined, di->T("Yes"), di->T("No"),
			[](bool success) {
				if (success) {
					g_Config.sMACAddress = CreateRandMAC();
				}
			});
		screenManager->push(confirmScreen);
	});
}

// The nickname is shown on both the Networking and System tabs.
static UI::View *AddNicknameChoice(SettingsPageHost *host, UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	PopupTextInputChoice *nickname = parent->Add(new PopupTextInputChoice(host->PageRequesterToken(), &g_Config.sNickName, sy->T("Nickname"), "", 32, host->PageScreenManager()));
	if (g_Config.sNickName == "PPSSPP") {
		nickname->SetIconRight(ImageID("I_WARNING"));
	}
	nickname->OnChange.Add([host](UI::EventParams &e) {
		// Copy to infrastructure name if valid and not already set.
		if (g_Config.sInfrastructureUsername.empty()) {
			if (g_Config.sNickName == SanitizeString(g_Config.sNickName, StringRestriction::AlphaNumDashUnderscore, 3, 16)) {
				g_Config.sInfrastructureUsername = g_Config.sNickName;
			}
		}
		// Updates the warning icon, and the username notice on the Networking tab.
		host->RecreatePageViews();
	});
	return nickname;
}

static void BuildNetworkingSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Header({ I18NCat::MAINSETTINGS, "Networking" });

	page->Choice("Quick start guide for multiplayer", ImageID("I_LINK_OUT")).OnClick([](EventParams &e) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		std::string url(n->T("MultiplayerQuickStartURL", "https://www.ppsspp.org/docs/multiplayer/quickstart/"));
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url);
	});

	// Ad hoc play, the built-in server and UPnP all talk to the local network.
	auto requestLocalNetwork = [](UI::EventParams &e) {
		CheckBox *checkBox = static_cast<CheckBox *>(e.v);
		if (checkBox->Toggled()) {
			System_RequestLocalNetworkPermission();
		}
	};

	page->Setting(&g_Config.bEnableWlan).OnChange(requestLocalNetwork);
	page->Custom("MAC address", [host](UI::ViewGroup *parent) -> UI::View * {
		return parent->Add(new MacAddressChooser(host->PageRequesterToken(), host->PageGamePath(), &g_Config.sMACAddress, T(I18NCat::NETWORKING, "MAC address"), host->PageScreenManager()));
	});

	page->Header("Ad Hoc multiplayer");

	page->Custom("Ad hoc server address", [host](UI::ViewGroup *parent) -> UI::View * {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		auto launchAdhocServerScreen = [host](UI::EventParams &) {
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			host->PageScreenManager()->push(new AdhocServerScreen(&g_Config.sProAdhocServer, n->T("Ad hoc server address")));
		};

		if (AdhocServerNameIsCustom()) {
			LinearLayout *serverRow = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			serverRow->SetSpacing(0.0f);
			serverRow->Add(new ChoiceWithValueDisplay(&g_Config.sProAdhocServer, n->T("Ad hoc server address"), I18NCat::NONE, new LinearLayoutParams(1.0f)))->OnClick.Add(launchAdhocServerScreen);
			serverRow->Add(new Choice(ImageID("I_EDIT_TEXT"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([host](UI::EventParams &) {
				AskToEditCurrentServer(host->PageRequesterToken(), host->PageScreenManager());
			});
			return serverRow;
		}

		AdhocServerListEntry entry;
		if (!g_Config.sProAdhocServer.empty() && AdhocGetServerByHost(g_Config.sProAdhocServer, &entry)) {
			Choice *choice = parent->Add(new ChoiceWithFixedValueDisplay(ApplySafeSubstitutions("%1 (%2)", entry.name, entry.host), n->T("Ad hoc server address")));
			choice->OnClick.Add(launchAdhocServerScreen);
			return choice;
		}
		// This case happens when updating from a previous version, as well as clicking on one of the local addresses
		Choice *choice = parent->Add(new ChoiceWithFixedValueDisplay(g_Config.sProAdhocServer, n->T("Ad hoc server address")));
		choice->OnClick.Add(launchAdhocServerScreen);
		return choice;
	});

	AdhocServerListEntry serverEntry;
	if (!AdhocServerNameIsCustom() && !g_Config.sProAdhocServer.empty() && AdhocGetServerByHost(g_Config.sProAdhocServer, &serverEntry)) {
		page->Custom("Server status: %1", [host, serverEntry](UI::ViewGroup *parent) -> UI::View * {
			Choice *status = parent->Add(new Choice(ApplySafeSubstitutions(T(I18NCat::NETWORKING, "Server status: %1"), serverEntry.name)));
			status->SetIconLeft(ImageID("I_INFO"));
			status->OnClick.Add([host, serverEntry](UI::EventParams &) {
				host->PageScreenManager()->push(new AdhocServerInfoScreen(serverEntry));
			});
			return status;
		});
	}

	page->Setting(&g_Config.iAdhocServerRelayMode).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page->Hint({ I18NCat::NETWORKING, "PacketRelayHint", "Available on servers that provide 'aemu_postoffice' packet relay, like socom.cc. Disable this for LAN or VPN play. Can be more reliable, but sometimes slower." });

	page->Custom({ I18NCat::SYSTEM, "Nickname" }, [host](UI::ViewGroup *parent) {
		return AddNicknameChoice(host, parent);
	});
	page->Hint("Try to pick a unique nickname for ad hoc play");

	page->Setting(&g_Config.bEnableAdhocServer).OnChange(requestLocalNetwork);

	page->Setting(&g_Config.bAdhocUseUdp).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page->Hint("Try switching between TCP/UDP if you suffer frequent adhoc server disconnects. Some network environments favor TCP and some UDP.");

	page->Setting(&g_Config.bAdhocRelayModeEnetPdpReliable).EnabledFunc([] {
		return !PSP_IsInited() && g_Config.bAdhocUseUdp;
	});
	page->Hint("When disabled, packet loss on datagram adhoc sockets will be exposed to game's netcode. Keep this enabled if you don't know what that means.");

	page->Header("Infrastructure");
	if (g_Config.sInfrastructureUsername.empty()) {
		page->Custom("To play in Infrastructure Mode, you must enter a username", [](UI::ViewGroup *parent) -> UI::View * {
			NoticeView *notice = parent->Add(new NoticeView(NoticeLevel::WARN, T(I18NCat::NETWORKING, "To play in Infrastructure Mode, you must enter a username"), ""));
			notice->SetWrapText(true);
			return notice;
		});
	}
	page->Custom({ I18NCat::DIALOG, "Username" }, [host](UI::ViewGroup *parent) -> UI::View * {
		PopupTextInputChoice *usernameChoice = parent->Add(new PopupTextInputChoice(host->PageRequesterToken(), &g_Config.sInfrastructureUsername, T(I18NCat::DIALOG, "Username"), "", 16, host->PageScreenManager()));
		usernameChoice->SetRestriction(StringRestriction::AlphaNumDashUnderscore, 3);
		usernameChoice->OnChange.Add([host](UI::EventParams &e) {
			host->RecreatePageViews();
		});
		return usernameChoice;
	});

	page->Setting(&g_Config.bInfrastructureAutoDNS);
	page->Custom("DNS server", [host](UI::ViewGroup *parent) -> UI::View * {
		return parent->Add(new PopupTextInputChoice(host->PageRequesterToken(), &g_Config.sInfrastructureDNSServer, T(I18NCat::NETWORKING, "DNS server"), "", 32, host->PageScreenManager()));
	}).Disabled(&g_Config.bInfrastructureAutoDNS);

	page->Header("UPnP (port-forwarding)");
	// Only togglable outside a game - sceNet latches settings like UPnPUseOriginalPort at boot,
	// and a game that's already mapped its ports wouldn't cope with them disappearing.
	page->Setting(&g_Config.bEnableUPnP).EnabledFunc([] {
		return !PSP_IsInited();
	}).OnChange([requestLocalNetwork](UI::EventParams &e) {
		requestLocalNetwork(e);
		// Wake the UPnP service thread so it connects (or tears its mappings back down) right
		// away, instead of waiting for a port request that may never come.
		UPnP_Notify();
	});
	page->Setting(&g_Config.bUPnPUseOriginalPort).Enabled(&g_Config.bEnableUPnP);
	page->Hint({ I18NCat::NETWORKING, "UseOriginalPort Tip", "May not work for all devices or games, see wiki." });

	page->Header("Chat");
	page->Setting(&g_Config.bEnableNetworkChat);
	page->Setting(&g_Config.iChatButtonPosition).Enabled(&g_Config.bEnableNetworkChat);
	page->Setting(&g_Config.iChatScreenPosition).Enabled(&g_Config.bEnableNetworkChat);
	page->Setting(&g_Config.bChatTimestamps).Enabled(&g_Config.bEnableNetworkChat);

	page->Header("Quick chat");
	page->Setting(&g_Config.bEnableQuickChat).Enabled(&g_Config.bEnableNetworkChat);

	for (int i = 0; i < 5; i++) {
		page->Custom("Quick chat %1", [host, i](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new PopupTextInputChoice(host->PageRequesterToken(), &g_Config.sQuickChat[i], ApplySafeSubstitutions(T(I18NCat::NETWORKING, "Quick chat %1"), i + 1), "", 32, host->PageScreenManager()));
		}).EnabledFunc([] {
			return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat;
		});
	}

	page->Header({ I18NCat::NETWORKING, "Misc", "Misc (default = compatibility)" });
	page->Choice("Open PPSSPP Multiplayer Wiki Page", ImageID("I_LINK_OUT")).OnClick([](EventParams &e) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		std::string url(n->T("MultiplayerHowToURL", "https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP"));
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url);
	});
	static const char *wlanChannels[] = {"Auto", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
	page->Setting(&g_Config.iWlanAdhocChannel).Setup([](View *view) {
		PopupMultiChoice *wlanChannelChoice = static_cast<PopupMultiChoice *>(view);
		wlanChannelChoice->SetChoicesUntranslated(1, (int)ARRAY_SIZE(wlanChannels) - 1);
		for (int i = 0; i < 4; i++) {
			wlanChannelChoice->HideChoice(i + 2);
			wlanChannelChoice->HideChoice(i + 7);
		}
	});
	page->Setting(&g_Config.iPortOffset);
	page->Hint("PortOffsetHint");
	page->Setting(&g_Config.iMinTimeout);
	page->Setting(&g_Config.bForcedFirstConnect);
	page->Setting(&g_Config.bAllowSpeedControlWhileConnected);
	if (Discord::IsAvailable()) {
		page->Setting(&g_Config.bDiscordRichPresence);
	}
}

static void BuildToolsSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Header("Tools");

	const bool showRetroAchievements = System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR;
	if (showRetroAchievements) {
		page->Choice({ I18NCat::SYSTEM, "RetroAchievements" }).OnClick([host](UI::EventParams &) {
			host->PageScreenManager()->push(new RetroAchievementsSettingsScreen(host->PageGamePath()));
		}).Setup([](View *view) {
			static_cast<Choice *>(view)->SetIconRight(ImageID("I_RETROACHIEVEMENTS_LOGO"));
		});
	}

	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	page->Choice({ I18NCat::SAVEDATA, "Savedata Manager" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new SavedataScreen(host->PageGamePath()));
	});
	page->Choice({ I18NCat::DEVELOPER, "System Information" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new SystemInfoScreen(host->PageGamePath()));
	});
	page->Link({ I18NCat::SYSTEM, "Developer Tools" }, I18NCat::DEVELOPER, &BuildDeveloperToolsPage);
	page->Choice({ I18NCat::REMOTEISO, "Remote disc streaming" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new RemoteISOScreen(host->PageGamePath()));
	});
	page->Choice({ I18NCat::SYSTEM, "PSP Firmware" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new FirmwareScreen(host->PageGamePath()));
	});
}

void GameSettingsScreen::BuildSystemSettings(SettingsPage *page) {
	using namespace UI;

	page->Header("UI");

	page->Custom("Language", [this](UI::ViewGroup *parent) -> UI::View * {
		auto langCodeToName = [](std::string_view value) -> std::string {
			auto &mapping = GetLangValuesMapping();
			auto iter = mapping.find(value);
			if (iter != mapping.end()) {
				return iter->second.first;
			}
			return std::string(value);
		};

		Choice *language = parent->Add(new ChoiceWithValueDisplay(&g_Config.sLanguageIni, T(I18NCat::SYSTEM, "Language"), langCodeToName));
		language->OnClick.Add([this](UI::EventParams &e) {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			auto langScreen = new NewLanguageScreen(sy->T("Language"));
			// The actual switching is handled in OnCompleted in NewLanguageScreen.
			if (e.v)
				langScreen->SetPopupOrigin(e.v);
			screenManager()->push(langScreen);
		});
		return language;
	});

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};
	page->Setting(&g_Config.iAppSwitchMode);

	{
		// Note: On iPhone, iOS hides the status bar in landscape no matter what this is set to.
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([](EventParams &e) {
			System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
		});
	}
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([this](EventParams &e) {
			OnImmersiveModeChange(e);
		});
	}
#endif

	UIContext *ctx = screenManager()->getUIContext();
	page->Setting(&g_Config.iUIScaleFactor).OnChange([ctx](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		ctx->InvalidateAtlas();
		NativeResized();
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		page->Choice("Clear UI background").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		page->Choice("Set UI background...").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	}

	page->Setting(&g_Config.bTransparentBackground);

	// Shared with achievements.

	page->Setting(&g_Config.iNotificationPos);

	page->Setting(&g_Config.iBackgroundAnimation);

	page->Custom("Theme", [this](UI::ViewGroup *parent) -> UI::View * {
		PopupMultiChoiceDynamic *theme = parent->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, T(I18NCat::SYSTEM, "Theme"), GetThemeInfoNames(), I18NCat::THEMES, screenManager()));
		theme->OnChoice.Add([](EventParams &e) {
			UpdateTheme();
			// Reset the tint/saturation if the theme changed.
			if (e.b) {
				g_Config.fUITint = 0.0f;
				g_Config.fUISaturation = 1.0f;
			}
		});
		return theme;
	});

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		auto noDropShadow = [](View *view) {
			static_cast<PopupSliderChoiceFloat *>(view)->SetHasDropShadow(false);
		};
		page->Setting(&g_Config.fUITint).LiveUpdate().Setup(noDropShadow);
		page->Setting(&g_Config.fUISaturation).LiveUpdate().Setup(noDropShadow);
	}

	page->Header("PSP Memory Stick");

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		page->Choice("Show Memory Stick folder").OnClick([](UI::EventParams &p) {
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
		page->Choice("Set Memory Stick folder").OnClick([this](UI::EventParams &) {
			SetMemStickDirDarwin(GetRequesterToken());
		});
	}
#endif

	auto addMemstickPath = [this, &page]() {
		page->Custom("Memory Stick folder", [this](UI::ViewGroup *parent) -> UI::View * {
			Choice *memstickPath = parent->Add(new ChoiceWithCallbackValueDisplay(T(I18NCat::SYSTEM, "Memory Stick folder"), []() {
				return g_Config.memStickDirectory.ToVisualString();
			}));
			memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnShowMemstickScreen);
			return memstickPath;
		}).EnabledFunc([] {
			return !PSP_IsInited();
		});
	};

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR) {
		addMemstickPath();
	}
#elif defined(_WIN32)
#if PPSSPP_PLATFORM(UWP)
	addMemstickPath();
#else
	page->Bool(&installed_, "Memory Stick in My Documents").Enabled(&memstickMyDocEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickMyDoc(e);
	});
	page->Bool(&otherinstalled_, "Memory Stick in installed.txt").Enabled(&memstickOtherEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickOther(e);
	});

	if (!System_GetPropertyBool(SYSPROP_CAN_GET_FREE_SPACE_FAST)) {
		// It might be slow, so use an option.
		page->Setting(&g_Config.bReportAccurateFreeStorageSpace);
	}
#endif
#endif
	page->Setting(&g_Config.bMemStickInserted);
	page->Setting(&g_Config.iMemStickSizeGB);

	page->Header("Help the PPSSPP team");
	page->Bool(&enableReports_, "Enable Compatibility Server Reports").EnabledFunc([]() {
		return Reporting::IsSupported();
	});

	page->Header("Emulation");

	page->Setting(&g_Config.bFastMemory).OnChange([this](UI::EventParams &e) {
		OnJitAffectingSetting(e);
	});
	page->Setting(&g_Config.bIgnoreBadMemAccess);

	page->Setting(&g_Config.iIOTimingMethod);
	page->Setting(&g_Config.bForceLagSync).Disabled(&g_Config.bAutoFrameSkip);
	page->Setting(&g_Config.iLockedCPUSpeed);

	page->Header({ I18NCat::SAVEDATA, "Save states" });  // Borrow this string from the savedata manager

	page->Setting(&g_Config.bEnableStateUndo);
	page->Setting(&g_Config.bConfirmLoadState);

	page->Setting(&g_Config.iSaveStateSlotCount);

	// NOTE: We will soon support more states, but we'll keep this niche feature limited to the first five.
	page->Setting(&g_Config.iAutoLoadSaveState).Setup([](View *view) {
		if (g_Config.iAutoLoadSaveState != 1) {
			static_cast<PopupMultiChoice *>(view)->HideChoice(1);  // Hide "Oldest Save" if not using that mode. It doesn't make sense.
		}
	});

	page->Setting(&g_Config.iRewindSnapshotInterval);

	page->Header("General");

	page->Setting(&g_Config.iAskForExitConfirmationAfterSeconds);

	if (System_GetPropertyBool(SYSPROP_CAN_RESTRICT_ORIENTATION)) {
		// Display rotation
		page->Custom({ I18NCat::CONTROLS, "Screen Rotation" }, [this](UI::ViewGroup *parent) -> UI::View * {
			AddRotationPicker(screenManager(), parent, true);
			return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
		});
	}

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
		page->Setting(&g_Config.bSustainedPerformanceMode).OnChange([this](UI::EventParams &e) {
			OnSustainedPerformanceModeChange(e);
		});
	}

	page->Choice("Restore Default Settings").OnClick([this](UI::EventParams &e) {
		OnRestoreDefaultSettings(e);
	});

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		page->Setting(&g_Config.bBypassOSKWithKeyboard);

	if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
		page->Setting(&g_Config.bCacheFullIsoInRam).EnabledFunc([] {
			return !PSP_IsInited();
		});
	}

	page->Setting(&g_Config.bCheckForNewVersion);
	page->Setting(&g_Config.bScreenshotsAsPNG);
	page->Setting(&g_Config.iScreenshotMode);
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS) || (defined (SDL) && !defined(MOBILE_DEVICE))
	page->Setting(&g_Config.bPauseOnLostFocus);
#endif

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		page->Header({ I18NCat::GRAPHICS, "Camera" });
		page->Setting(&g_Config.sCameraDevice, std::move(cameraList));
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page->Setting(&g_Config.bCameraMirrorHorizontal);
#endif
	}

	page->Header("Cheats");
	page->Setting(&g_Config.bEnableCheats);
	page->Setting(&g_Config.bEnablePlugins);

	page->Header("PSP Settings");

	// The ordering here is simply mapping directly to PSP_SYSTEMPARAM_LANGUAGE_*.
	page->Setting(&g_Config.iLanguage);
	page->Setting(&g_Config.iPSPModel).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page->Custom("Nickname", [this](UI::ViewGroup *parent) {
		return AddNicknameChoice(this, parent);
	});
	page->Setting(&g_Config.bDayLightSavings);
	page->Setting(&g_Config.iDateFormat);
	page->Setting(&g_Config.iTimeFormat);
	page->Setting(&g_Config.iButtonPreference);

#if defined(_WIN32)
	page->Header("Recording");
	page->Setting(&g_Config.bDumpFrames);
	page->Setting(&g_Config.bUseFFV1);
	page->Setting(&g_Config.bDumpVideoOutput);
	page->Setting(&g_Config.bDumpAudio);
	page->Setting(&g_Config.bSaveLoadResetsAVdumping);
#endif
}

static void BuildVRSettings(SettingsPage *page) {
	using namespace UI;

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	if (deviceType == DEVICE_TYPE_VR) {
		page->Header("Virtual reality");
		page->Setting(&g_Config.bEnableVR);
		page->Setting(&g_Config.bEnable6DoF);
		page->Setting(&g_Config.bEnableStereo);
		page->Setting(&g_Config.bEnableImmersiveVR);
		if (IsPassthroughSupported()) {
			page->Setting(&g_Config.bPassthrough);
		}
		page->Setting(&g_Config.bForce72Hz);
	}

	page->Header("VR camera");
	if (deviceType == DEVICE_TYPE_VR) {
		page->Setting(&g_Config.fCanvasDistance);
		page->Setting(&g_Config.fCanvas3DDistance);
	}
	page->Setting(&g_Config.fFieldOfViewPercentage);
	page->Setting(&g_Config.bRescaleHUD);
	page->Setting(&g_Config.fHeadUpDisplayScale).Enabled(&g_Config.bRescaleHUD);
	page->Setting(&g_Config.bManualForceVR);
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
		memstickMyDocEnabled_ = true;
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
	System_BrowseForImage(GetRequesterToken(), sy->T("Set UI background..."), bgJpg, [this](std::string_view value, int converted) {
		if (converted == 1) {
			// The platform code converted and saved the file to the desired path already.
			INFO_LOG(Log::UI, "Platform converted the file: %.*s", STR_VIEW(value));
		} else if (!value.empty()) {
			Path path(value);

			// Check the file format. Don't rely on the file extension here due to scoped storage URLs.
			FILE *f = File::OpenCFile(path, "rb");
			uint8_t buffer[8];
			ImageFileType type = ImageFileType::UNKNOWN;
			if (f != nullptr && 8 == fread(buffer, 1, ARRAY_SIZE(buffer), f)) {
				type = DetectImageFileType(buffer, ARRAY_SIZE(buffer));
			}
			if (f) {
				fclose(f);
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
				File::Copy(path, dest);
				if (path.FilePathContainsNoCase("temp_import.jpg")) {
					INFO_LOG(Log::UI, "Deleting temp file: %s", GetFriendlyPath(path).c_str());
					File::Delete(path);
				}
			} else {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
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
	if (equals(dialog->tag(), "NewLanguage") && result == DR_OK) {
		screenManager()->RecreateAllViews();
		return;
	}

	bool recreate = false;

	if (equals(dialog->tag(), "AdhocServer")) {
		const AdhocServerScreen *scr = dynamic_cast<const AdhocServerScreen *>(dialog);
		if (scr && scr->RecreateParent()) {
			recreate = true;
		}
	}

	if (result == DialogResult::DR_OK) {
		if (equals(dialog->tag(), "RestoreSettingsScreen")) {
			ReadConfigTemporaries();
		}
		recreate = true;
	}

	// The analog alternative speed is only shown when it's mapped.
	if (equals(dialog->tag(), "ControlMapping")) {
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
	std::string param = "--start-screen=gamesettings";
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

// From PauseScreen
std::string GetConfirmExitMessage();

void GameSettingsScreen::RestartOrDo(std::function<void()> callback) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::string confirmExitMessage = GetConfirmExitMessage();
	if (!confirmExitMessage.empty()) {
		confirmExitMessage = "\n\n" + confirmExitMessage;
	}
	confirmExitMessage = std::string(di->T("Changing this setting requires PPSSPP to restart.")) + confirmExitMessage;

	screenManager()->push(new UI::MessagePopupScreen(di->T("Restart"), confirmExitMessage, di->T("Restart"), di->T("Cancel"), [=](bool yes) {
		if (yes) {
			TriggerRestart("GameSettingsScreen::RenderingBackendYes", editGameSpecificThenRestore_, gamePath_);
		} else {
			callback();
		}
	}));
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS);
		ReadConfigTemporaries();
	}
	System_Notify(SystemNotification::UI);
}

void GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (g_Config.IsGameSpecific()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		screenManager()->push(
			new UI::MessagePopupScreen(sy->T("Restore Default Settings"), dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"),
				di->T("OK"), di->T("Cancel"), [this](bool yes) { CallbackRestoreDefaults(yes); }));
	} else {
		std::string_view title = sy->T("Restore Default Settings");
		screenManager()->push(new RestoreSettingsScreen(title));
	}
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

	ChoiceGroupList *list = vert->Add(new ChoiceGroupList());

	list->Add(new CheckBox(&zone.bGestureControlEnabled, co->T("Enable gesture control")))->SetRoundCorners();

	ChoiceGroup *swipeGroup = list->Add(new ChoiceGroup(co->T("Swipe")));

	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSensitivity, 0.01f, 2.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);

	ChoiceGroup *doubleTapGroup = list->Add(new ChoiceGroup(co->T("Double tap")));
	doubleTapGroup->Add(new PopupMultiChoice(&zone.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()));

	ChoiceGroup *analogGroup = list->Add(new ChoiceGroup(co->T("Analog Stick")));
	analogGroup->Add(new CheckBox(&zone.bAnalogGesture, co->T("Enable analog stick gesture")));
	analogGroup->Add(new PopupSliderChoiceFloat(&zone.fAnalogGestureSensitivity, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bAnalogGesture);
}

RestoreSettingsScreen::RestoreSettingsScreen(std::string_view title)
	: PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")) {}

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
