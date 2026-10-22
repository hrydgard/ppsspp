
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
	: UITabbedBaseDialogScreen(gamePath, &g_Config.iSettingsCurrentTab, TabDialogFlags::HorizontalOnlyIcons | TabDialogFlags::VerticalShowIcons), gameID_(gameID), editGameSpecificThenRestore_(editThenRestore) {
	prevInflightFrames_ = g_Config.iInflightFrames;
	analogSpeedMapped_ = KeyMap::InputMappingsFromPspButton(VIRTKEY_SPEED_ANALOG, nullptr, true);

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
	iAlternateSpeedPercent1_ = g_Config.iFpsLimit1 < 0 ? -1 : (g_Config.iFpsLimit1 * 100) / 60;
	iAlternateSpeedPercent2_ = g_Config.iFpsLimit2 < 0 ? -1 : (g_Config.iFpsLimit2 * 100) / 60;
	iAlternateSpeedPercentAnalog_ = (g_Config.iAnalogFpsLimit * 100) / 60;
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

void GameSettingsScreen::PreCreateViews() {
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

const std::vector<GameSettingsScreen::PageDef> &GameSettingsScreen::PageDefs() {
	static const std::vector<PageDef> pages = {
		{ "GameSettingsGraphics", "Graphics", "I_DISPLAY", "graphics", I18NCat::GRAPHICS, &GameSettingsScreen::BuildGraphicsSettings },
		{ "GameSettingsControls", "Controls", "I_CONTROLLER", "controls", I18NCat::CONTROLS, &GameSettingsScreen::BuildControlsSettings },
		{ "GameSettingsAudio", "Audio", "I_SPEAKER_MAX", "audio", I18NCat::AUDIO, &GameSettingsScreen::BuildAudioSettings },
		{ "GameSettingsNetworking", "Networking", "I_WIFI", "network", I18NCat::NETWORKING, &GameSettingsScreen::BuildNetworkingSettings },
		{ "GameSettingsTools", "Tools", "I_TOOLS", "tools", I18NCat::MAINSETTINGS, &GameSettingsScreen::BuildToolsSettings },
		{ "GameSettingsSystem", "System", "I_PSP", "system", I18NCat::SYSTEM, &GameSettingsScreen::BuildSystemSettings },
		{ "GameSettingsVR", "VR", nullptr, nullptr, I18NCat::VR, &GameSettingsScreen::BuildVRSettings },
	};
	return pages;
}

void GameSettingsScreen::CreateTabs() {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	bool showVR = deviceType == DEVICE_TYPE_VR || g_Config.bForceVR;

	pages_.clear();
	for (const PageDef &def : PageDefs()) {
		if (def.build == &GameSettingsScreen::BuildVRSettings && !showVR) {
			continue;
		}
		pages_.push_back(&def);
	}

	for (const PageDef *def : pages_) {
		AddTab(def->tag, ms->T(def->title), def->icon ? ImageID(def->icon) : ImageID::invalid(), [this, def](UI::LinearLayout *parent) {
			if (def->helpTag) {
				parent->Add(new PaneTitleBar(gamePath_, T(I18NCat::MAINSETTINGS, def->title), def->helpTag));
			}
			SettingsPage page(def->cat);
			(this->*def->build)(page);
			CreateSettingViews(page, parent, screenManager());
		});
	}
}

// TODO: Make this generic
extern int DefaultDepthRaster();

void GameSettingsScreen::BuildGraphicsSettings(SettingsPage &page) {
	using namespace UI;

	Draw::DrawContext *draw = screenManager()->getDrawContext();
	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	page.Header("Rendering Mode");

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan", "Metal" };
	page.IntChoice(&g_Config.iGPUBackend, "Backend", renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend)).Setup([this](View *view) {
		PopupMultiChoice *renderingBackendChoice = static_cast<PopupMultiChoice *>(view);
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
			if (!g_Config.IsBackendEnabled(GPUBackend::METAL))
				choice->HideChoice((int)GPUBackend::METAL);
		});

		if (!IsFirstInstance()) {
			// If we're not the first instance, can't save the setting, and it requires a restart, so...
			renderingBackendChoice->SetEnabled(false);
		}
	});
#endif

	// Backends that don't allow a device choice will only expose one device.
	if (draw->GetDeviceList().size() > 1) {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting) {
			page.StringChoice(deviceNameSetting, "Device", draw->GetDeviceList()).OnChange([this](UI::EventParams &e) {
				OnRenderingDevice(e);
			});
		}
	}

	static const char *internalResolutions[] = { "Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
	page.IntChoice(&g_Config.iInternalResolution, "Rendering Resolution", internalResolutions, 0, ARRAY_SIZE(internalResolutions)).OnChange([](UI::EventParams &e) {
		if (g_Config.iAndroidHwScale == 1) {
			System_RecreateActivity();
		}
		Reporting::UpdateConfig();
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	}).EnabledFunc([] {
		return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
	});

	if (deviceType != DEVICE_TYPE_VR) {
		page.Bool(&g_Config.bSoftwareRendering, { I18NCat::GRAPHICS, "Software Rendering", "Software Rendering (slow)" }).EnabledFunc([] {
			return !PSP_IsInited();
		});
	}

	if (draw->GetDeviceCaps().multiSampleLevelsMask != 1) {
		static const char *msaaModes[] = { "Off", "2x", "4x", "8x", "16x" };
		page.IntChoice(&g_Config.iMultiSampleLevel, "Antialiasing (MSAA)", msaaModes, 0, ARRAY_SIZE(msaaModes)).OnChange([](UI::EventParams &) {
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		}).EnabledFunc([] {
			return !g_Config.bSoftwareRendering && !g_Config.bSkipBufferEffects;
		}).Setup([draw](View *view) {
			PopupMultiChoice *msaaChoice = static_cast<PopupMultiChoice *>(view);
			if (g_Config.iMultiSampleLevel > 1 && draw->GetDeviceCaps().isTilingGPU) {
				msaaChoice->SetIconRight(ImageID("I_WARNING"), 0.7f);
			}
			// Hide unsupported levels.
			for (int i = 1; i < 5; i++) {
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
		static const char *deviceResolutions[] = { "Native device resolution", "Same as Rendering resolution", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
		int max_res_temp = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 2;
		if (max_res_temp == 3)
			max_res_temp = 4;  // At least allow 2x
		int max_res = std::min(max_res_temp, (int)ARRAY_SIZE(deviceResolutions));
		page.IntChoice(&g_Config.iAndroidHwScale, "Display Resolution (HW scaler)", deviceResolutions, 0, max_res).OnChange([](UI::EventParams &) {
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
			System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
			System_RecreateActivity();
		});
	}
#endif

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());

	page.Bool(&g_Config.bReplaceTextures, { I18NCat::DEVELOPER, "Replace textures" });

	page.Header("Display");

	if (deviceType != DEVICE_TYPE_VR) {
#if !defined(MOBILE_DEVICE)
		page.Bool(&g_Config.bFullScreen, "Full screen").OnChange([](UI::EventParams &e) {
			System_ApplyFullscreenState();
		});
		if (draw->GetDeviceCaps().fullScreenExclusiveSupported) {
			page.Bool(&g_Config.bAllowFullScreenExclusive, "Allow exclusive fullscreen").EnabledFunc([] {
				return g_Config.bFullScreen && !g_Config.bFullScreenMulti;
			}).OnChange([this](UI::EventParams &e) {
				TriggerRestartOrDo([this]() {
					g_Config.bAllowFullScreenExclusive = !g_Config.bAllowFullScreenExclusive;
					RecreateViews();
				});
			});
		}
		if (System_GetPropertyInt(SYSPROP_DISPLAY_COUNT) > 1) {
			page.Bool(&g_Config.bFullScreenMulti, "Use all displays").EnabledFunc([] {
				return g_Config.bFullScreen && !g_Config.bAllowFullScreenExclusive;
			}).OnChange([](UI::EventParams &e) {
				System_ApplyFullscreenState();
			});
		}
#endif
		// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
		page.Choice("Display layout & effects").OnClick([this](UI::EventParams &) {
			screenManager()->push(new DisplayLayoutScreen(gamePath_));
		});
	}

	// If only one mode is supported (like FIFO on iOS), no need to show the options.
	if (CountSetBits((u32)draw->GetDeviceCaps().presentModesSupported) > 1) {
		// Immediate means non-synchronized, tearing.
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
			page.Bool(&g_Config.bVSync, "VSync").OnChange([](UI::EventParams &e) {
				NativeResized();  // TODO: Remove
			});
		}
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) {
			SettingItem &lowLatency = page.Bool(&g_Config.bLowLatencyPresent, "Low latency display").OnChange([](UI::EventParams &e) {
				NativeResized();  // TODO: Remove
			});

			// If the immediate mode is supported, we can tie low latency present to VSync.
			if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
				lowLatency.Enabled(&g_Config.bVSync);
			}
		}
	}

	page.Header("Frame Rate Control");

	page.IntSlider(&iAlternateSpeedPercent1_, 0, 1000, { I18NCat::GRAPHICS, "Alternative Speed", "Alternative speed" }).Default(UI::NO_DEFAULT_INT).Step(5).Units(T(I18NCat::GRAPHICS, "%, 0:unlimited")).Format("%i%%").ZeroLabel("Unlimited").NegativeLabel(T(I18NCat::GRAPHICS, "Disabled")).OnChange([this](UI::EventParams &) {
		g_Config.iFpsLimit1 = iAlternateSpeedPercent1_ < 0 ? -1 : (iAlternateSpeedPercent1_ * 60) / 100;
	});
	page.IntSlider(&iAlternateSpeedPercent2_, 0, 1000, { I18NCat::GRAPHICS, "Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)" }).Default(UI::NO_DEFAULT_INT).Step(5).Units(T(I18NCat::GRAPHICS, "%, 0:unlimited")).Format("%i%%").ZeroLabel("Unlimited").NegativeLabel(T(I18NCat::GRAPHICS, "Disabled")).OnChange([this](UI::EventParams &) {
		g_Config.iFpsLimit2 = iAlternateSpeedPercent2_ < 0 ? -1 : (iAlternateSpeedPercent2_ * 60) / 100;
	});
	if (analogSpeedMapped_) {
		page.IntSlider(&iAlternateSpeedPercentAnalog_, 1, 1000, { I18NCat::GRAPHICS, "Analog alternative speed", "Analog alternative speed (in %)" }).Default(UI::NO_DEFAULT_INT).Step(5).Units("%").Format("%i%%").OnChange([this](UI::EventParams &) {
			g_Config.iAnalogFpsLimit = (iAlternateSpeedPercentAnalog_ * 60) / 100;
		});
	}

	page.Header({ I18NCat::GRAPHICS, "Speed Hacks", "Speed Hacks (can cause rendering errors!)" });

	static const char *frameSkip[] = {"Off", "1", "2", "3", "4"};
	page.IntChoice(&g_Config.iFrameSkip, "Frame Skipping", frameSkip, 0, ARRAY_SIZE(frameSkip)).EnabledFunc([] {
		return !g_Config.bAutoFrameSkip;
	}).Setup([](View *view) {
		static_cast<PopupMultiChoice *>(view)->SetChoicesUntranslated(1, (int)ARRAY_SIZE(frameSkip) - 1);
	});

	page.Bool(&g_Config.bAutoFrameSkip, "Auto FrameSkip").OnChange([](UI::EventParams &e) {
		g_Config.UpdateAfterSettingAutoFrameSkip();
	});

	page.Bool(&g_Config.bSkipBufferEffects, "Skip Buffer Effects").OnChange([](UI::EventParams &e) {
		if (g_Config.bSkipBufferEffects) {
			g_Config.bAutoFrameSkip = false;
		}

		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}).Disabled(&g_Config.bSoftwareRendering);
	page.Hint({ I18NCat::GRAPHICS, "RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games" });

	static const char *skipGpuReadbackModes[] = { "No", "Skip", "Copy to texture" };
	page.IntChoice(&g_Config.iSkipGPUReadbackMode, "Skip GPU Readbacks", skipGpuReadbackModes, 0, ARRAY_SIZE(skipGpuReadbackModes)).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		static_cast<PopupMultiChoice *>(view)->SetDefault(0);
	});

	static const char *depthRasterModes[] = { "Auto", "Low", "Off", "Always on" };
	page.IntChoice(&g_Config.iDepthRasterMode, "Lens flare occlusion", depthRasterModes, 0, ARRAY_SIZE(depthRasterModes)).Disabled(&g_Config.bSoftwareRendering).Setup([](View *view) {
		PopupMultiChoice *depthRasterMode = static_cast<PopupMultiChoice *>(view);
		depthRasterMode->SetChoiceIcon(3, ImageID("I_WARNING"));  // It's a performance trap.
		depthRasterMode->SetDefault(DefaultDepthRaster());
		if (g_Config.iDepthRasterMode != 3)
			depthRasterMode->HideChoice(3);
	});

	static const char *bloomHackOptions[] = {"Off", "Safe", "Balanced", "Aggressive"};
	page.IntChoice(&g_Config.iBloomHack, "Lower resolution for effects", bloomHackOptions, 0, ARRAY_SIZE(bloomHackOptions)).EnabledFunc([] {
		return !g_Config.bSoftwareRendering && g_Config.iInternalResolution != 1;
	});
	page.Hint("Reduces artifacts");

	page.Header("Performance");
	page.Bool(&g_Config.bRenderDuplicateFrames, "Render duplicate frames to 60hz").EnabledFunc([] {
		return !g_Config.bSkipBufferEffects && g_Config.iFrameSkip == 0;
	});
	page.Hint({ I18NCat::GRAPHICS, "RenderDuplicateFrames Tip", "Can make framerate smoother in games that run at lower framerates" });

	if (draw->GetDeviceCaps().setMaxFrameLatencySupported) {
		static const char *bufferOptions[] = { "No buffer", "Up to 1" };
		page.IntChoice(&g_Config.iInflightFrames, "Buffer graphics commands", bufferOptions, 1, ARRAY_SIZE(bufferOptions)).OnChange([this](UI::EventParams &e) {
			OnInflightFramesChoice(e);
		});
		page.Hint("Faster, input lag");  // TODO: This hint could use improvement.
	}

	page.Header("Texture upscaling");

	if (GetGPUBackend() == GPUBackend::VULKAN || GetGPUBackend() == GPUBackend::METAL) {
		page.Custom("GPU texture upscaler (fast)", [this](UI::ViewGroup *parent) -> UI::View * {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			ChoiceWithValueDisplay *textureShaderChoice = parent->Add(new ChoiceWithValueDisplay(&g_Config.sTextureShaderName, gr->T("GPU texture upscaler (fast)"), &TextureTranslateName));
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
			return textureShaderChoice;
		}).Disabled(&g_Config.bSoftwareRendering);
	}

#ifndef MOBILE_DEVICE
	static const char *texScaleLevels[] = {"Off", "2x", "3x", "4x", "5x"};
#else
	static const char *texScaleLevels[] = {"Off", "2x", "3x"};
#endif

	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	page.IntChoice(&g_Config.iTexScalingType, "CPU texture upscaler (slow)", texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos)).EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page.IntChoice(&g_Config.iTexScalingLevel, "Upscale Level", texScaleLevels, 1, ARRAY_SIZE(texScaleLevels)).EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page.Hint({ I18NCat::GRAPHICS, "UpscaleLevel Tip", "CPU heavy - some scaling may be delayed to avoid stutter" });

	page.Bool(&g_Config.bTexDeposterize, "Deposterize").EnabledFunc([]() {
		return !g_Config.bSoftwareRendering && !UsingHardwareTextureScaling();
	});
	page.Hint({ I18NCat::GRAPHICS, "Deposterize Tip", "Fixes visual banding glitches in upscaled textures" });

	page.Header("Texture Filtering");

	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	page.IntChoice(&g_Config.iAnisotropyLevel, "Anisotropic Filtering", anisoLevels, 0, ARRAY_SIZE(anisoLevels)).Disabled(&g_Config.bSoftwareRendering).OnChange([](UI::EventParams &e) {
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	});

	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Auto Max Quality"};
	page.IntChoice(&g_Config.iTexFiltering, "Texture Filter", texFilters, 1, ARRAY_SIZE(texFilters)).Disabled(&g_Config.bSoftwareRendering);

	page.Bool(&g_Config.bSmart2DTexFiltering, "Smart 2D texture filtering").Disabled(&g_Config.bSoftwareRendering);

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	bool showCardboardSettings = deviceType != DEVICE_TYPE_VR;
#else
	// If you enabled it through the ini, you can see this. Useful for testing.
	bool showCardboardSettings = config.bEnableCardboardVR;
#endif
	if (showCardboardSettings) {
		page.Header("Cardboard VR Settings");
		page.Bool(&config.bEnableCardboardVR, "Enable Cardboard VR");
		page.IntSlider(&config.iCardboardScreenSize, 30, 150, { I18NCat::GRAPHICS, "Cardboard Screen Size", "Screen Size (in % of the viewport)" }).Default(50).Units(T(I18NCat::GRAPHICS, "% of viewport")).Enabled(&config.bEnableCardboardVR);
		page.IntSlider(&config.iCardboardXShift, -150, 150, { I18NCat::GRAPHICS, "Cardboard Screen X Shift", "X Shift (in % of the void)" }).Default(0).Units(T(I18NCat::GRAPHICS, "% of the void")).Enabled(&config.bEnableCardboardVR);
		page.IntSlider(&config.iCardboardYShift, -100, 100, { I18NCat::GRAPHICS, "Cardboard Screen Y Shift", "Y Shift (in % of the void)" }).Default(0).Units(T(I18NCat::GRAPHICS, "% of the void")).Enabled(&config.bEnableCardboardVR);
	}

	page.Header("Overlay Information");
	auto addStatusFlag = [&page](ShowStatusFlags flag, const char *text) {
		page.Custom(text, [flag, text](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new BitCheckBox(&g_Config.iShowStatusFlags, (int)flag, T(I18NCat::GRAPHICS, text)));
		});
	};
	addStatusFlag(ShowStatusFlags::FPS_COUNTER, "Show FPS Counter");
	addStatusFlag(ShowStatusFlags::SPEED_COUNTER, "Show Speed");
	if (System_GetPropertyBool(SYSPROP_CAN_READ_BATTERY_PERCENTAGE)) {
		addStatusFlag(ShowStatusFlags::BATTERY_PERCENT, "Show Battery %");
	}
	page.Custom({ I18NCat::DEVELOPER, "Debug overlay" }, [this](UI::ViewGroup *parent) -> UI::View * {
		AddOverlayList(parent, screenManager());
		return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
	});
}

void GameSettingsScreen::BuildAudioSettings(SettingsPage &page) {
	using namespace UI;

#if PPSSPP_PLATFORM(IOS)
	page.Bool(&g_Config.bAudioRespectSilentMode, "Respect silent mode").Enabled(&g_Config.bEnableSound).OnChange([](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
	});
#endif
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	page.Bool(&g_Config.bAudioMixWithOthers, "Mix audio with other apps").Enabled(&g_Config.bEnableSound).OnChange([](EventParams &e) {
		System_Notify(SystemNotification::AUDIO_MODE_CHANGED);
	});
#endif
	page.Header("Audio playback");

	static const char *syncModes[] = { "Smooth (reduces artifacts)", "Classic (lowest latency)" };

	page.IntChoice(&g_Config.iAudioPlaybackMode, "Playback mode", syncModes, 0, ARRAY_SIZE(syncModes));
	page.Bool(&g_Config.bFillAudioGaps, "Fill audio gaps").EnabledFunc([]() {
		return g_Config.iAudioPlaybackMode == (int)AudioSyncMode::GRANULAR;
	});

	page.Header("Game volume");

	// This is here because it now only applies to in-game. Muting the menu sounds is separate.
	page.Bool(&g_Config.bEnableSound, "Enable Sound");

	page.IntSlider(&g_Config.iGameVolume, VOLUME_OFF, VOLUMEHI_FULL, "Game volume").Format("%d%%").ZeroLabel("Mute").Enabled(&g_Config.bEnableSound);
	page.IntSlider(&g_Config.iReverbVolume, VOLUME_OFF, 2 * VOLUMEHI_FULL, "Reverb volume").Format("%d%%").ZeroLabel("Disabled").Enabled(&g_Config.bEnableSound);
	page.IntSlider(&g_Config.iAltSpeedVolume, VOLUME_OFF, VOLUMEHI_FULL, "Alternate speed volume").Format("%d%%").ZeroLabel("Mute").Enabled(&g_Config.bEnableSound);
	page.IntSlider(&g_Config.iAchievementVolume, VOLUME_OFF, VOLUMEHI_FULL, { I18NCat::ACHIEVEMENTS, "Achievement sound volume" }).Format("%d%%").ZeroLabel("Mute").Enabled(&g_Config.bEnableSound).OnChange([](EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
	});

	page.Header("UI sound");

	page.Bool(&g_Config.bUISound, "UI sound");
	page.IntSlider(&g_Config.iUIVolume, 0, VOLUMEHI_FULL, "UI volume").Format("%d%%").ZeroLabel("Mute").LiveUpdate().Enabled(&g_Config.bUISound).OnChange([](EventParams &e) {
		static double lastTimePlayed = 0.0;
		double now = time_now_d();
		if (now - lastTimePlayed < 0.1) {
			return; // Don't play if we just played one, to avoid spamming when dragging.
		}
		lastTimePlayed = now;
		// Audio preview
		PlayUISound(UI::UISound::CONFIRM);
	});

	page.IntSlider(&g_Config.iGamePreviewVolume, VOLUME_OFF, VOLUMEHI_FULL, "Game preview volume").Format("%d%%").ZeroLabel("Mute");

	bool sdlAudio = false;

#if defined(SDL)
	page.Header("Audio backend");
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
	page.StringChoice(&g_Config.sAudioDevice, "Device", std::move(audioDeviceList)).OnChange([](EventParams &e) {
		if (g_Config.sAudioDevice == T(I18NCat::AUDIO, "Auto")) {
			g_Config.sAudioDevice.clear();
		}
		System_Notify(SystemNotification::AUDIO_RESET_DEVICE);
	});
	sdlAudio = true;

	static const int bufferSizes[] = {128, 256, 512, 1024, 2048};
	page.IntSlider(&g_Config.iSDLAudioBufferSize, 0, 2048, "Buffer size").Default(256).RestrictTo(bufferSizes, ARRAY_SIZE(bufferSizes));
	page.Hint({ I18NCat::DIALOG, "This change will not take effect until PPSSPP is restarted." });
#endif

#if PPSSPP_PLATFORM(WINDOWS)
	extern AudioBackend *g_audioBackend;

	std::vector<std::string> audioDeviceNames;
	std::vector<std::string> audioDeviceIds;

	std::vector<AudioDeviceDesc> deviceDescs;
	g_audioBackend->EnumerateDevices(&deviceDescs);
	if (!deviceDescs.empty()) {
		page.Header("Audio backend");
		for (auto &desc : deviceDescs) {
			audioDeviceNames.push_back(desc.name);
			audioDeviceIds.push_back(desc.uniqueId);
		}

		audioDeviceNames.insert(audioDeviceNames.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
		audioDeviceIds.insert(audioDeviceIds.begin(), "");

		page.StringChoice(&g_Config.sAudioDevice, "Device", std::move(audioDeviceNames), std::move(audioDeviceIds)).OnChange([](EventParams &) {
			bool reverted;
			if (g_audioBackend->InitOutputDevice(g_Config.sAudioDevice, LatencyMode::Aggressive, &reverted)) {
				if (reverted) {
					WARN_LOG(Log::Audio, "Unexpected: After a direct choice, audio device reverted to default. '%s'", g_Config.sAudioDevice.c_str());
				}
			} else {
				WARN_LOG(Log::Audio, "InitOutputDevice failed");
			}
		});
		page.Bool(&g_Config.bAutoSwitchAudioDevice, "Use new audio devices automatically").EnabledFunc([]()->bool {
			return g_Config.sAudioDevice.empty();
		});
	}

#else
	if (sdlAudio) {
		page.Bool(&g_Config.bAutoSwitchAudioDevice, "Use new audio devices automatically");
	} else {
		page.Header("Audio backend");
	}

#if PPSSPP_PLATFORM(ANDROID)
	page.Bool(&g_Config.bExtraAudioBuffering, { I18NCat::AUDIO, "AudioBufferingForBluetooth", "Bluetooth-friendly buffer (slower)" });

	// Show Oboe debug info
	const std::string audioErrorStr = AndroidAudio_GetErrorString(g_audioState);
	if (!audioErrorStr.empty()) {
		page.Info("Audio Error", audioErrorStr);
	}
#endif
#endif

	std::vector<std::string> micList = Microphone::getDeviceList();
	if (!micList.empty()) {
		page.Header("Microphone");
		std::vector<std::string> micValues = micList;
		micList.emplace(micList.begin(), T(I18NCat::AUDIO, "Auto"));
		micValues.insert(micValues.begin(), "");
		page.StringChoice(&g_Config.sMicDevice, "Microphone Device", std::move(micList), std::move(micValues)).OnChange([](EventParams &e) {
			Microphone::onMicDeviceChange();
		});
	}
}

void GameSettingsScreen::BuildControlsSettings(SettingsPage &page) {
	using namespace UI;

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	page.Header({ I18NCat::MAINSETTINGS, "Controls" });
	page.Choice("Control mapping").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new ControlMappingScreen(gamePath_));
	});
	page.Choice("Calibrate analog stick").OnClick([this](UI::EventParams &e) {
		screenManager()->push(new AnalogCalibrationScreen(gamePath_));
	});

#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
	page.Bool(&g_Config.bSystemControls, "Enable standard shortcut keys");
#endif
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	page.Bool(&g_Config.bGamepadOnlyFocused, "Ignore gamepads when not focused");
#endif

	if (System_GetPropertyBool(SYSPROP_HAS_ACCELEROMETER)) {
		page.Custom("Tilt control setup", [this](UI::ViewGroup *parent) -> UI::View * {
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
			customizeTilt->OnClick.Add([this](UI::EventParams &e) {
				screenManager()->push(new TiltAnalogSettingsScreen(gamePath_));
			});
			return customizeTilt;
		});
	} else if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {  // TODO: This seems like a regression
		page.Bool(&g_Config.bHapticFeedback, { I18NCat::CONTROLS, "HapticFeedback", "Haptic Feedback (vibration)" });
	}

	page.Header("Analog to digital mapping");
	page.FloatSlider(&g_Config.fAnalogTriggerThreshold, 0.02f, 0.98f, 0.75f, 1.0f, "Analog trigger threshold");
	page.FloatSlider(&g_Config.fAnalogStickThreshold, 0.2f, 0.98f, 0.75f, 1.0f, "Analog stick threshold");

	// TVs don't have touch control, at least not yet.
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		page.Header("On-screen touch controls");
		page.Bool(&g_Config.bShowTouchControls, "On-screen touch controls");
		page.Choice("Edit touch control layout").OnClick([this](UI::EventParams &e) {
			screenManager()->push(new TouchControlLayoutScreen(gamePath_));
		}).Enabled(&g_Config.bShowTouchControls);
		page.Choice("Gesture mapping").OnClick([this](UI::EventParams &e) {
			screenManager()->push(new GestureMappingScreen(gamePath_));
		}).Enabled(&g_Config.bShowTouchControls);

		static const char *touchControlStyles[] = { "Classic", "Thin borders", "Glowing borders" };
		page.IntChoice(&g_Config.iTouchButtonStyle, "Button style", touchControlStyles, 0, ARRAY_SIZE(touchControlStyles)).Enabled(&g_Config.bShowTouchControls);

		page.IntSlider(&g_Config.iTouchButtonOpacity, 0, 100, "Button Opacity").Default(65).Units("%").Format("%i%%").Enabled(&g_Config.bShowTouchControls);
		page.IntSlider(&g_Config.iTouchButtonHideSeconds, 0, 300, "Auto-hide buttons after delay").Default(20).Units(T(I18NCat::DIALOG, "seconds, 0:off")).Format(T(I18NCat::DIALOG, "%d seconds")).ZeroLabel("Off").Enabled(&g_Config.bShowTouchControls);

		page.Bool(&g_Config.bTouchGliding, "Keep first touched button pressed when dragging").Enabled(&g_Config.bShowTouchControls);

		TouchControlConfig &touch = g_Config.GetTouchControlsConfig(GetDeviceOrientation());

		// Hide stick background, useful when increasing the size
		page.Bool(&touch.bHideStickBackground, "Hide touch analog stick background circle").Enabled(&g_Config.bShowTouchControls);

		// Sticky D-pad.
		page.Bool(&g_Config.bStickyTouchDPad, "Sticky D-Pad").Enabled(&g_Config.bShowTouchControls);
		page.Hint("Easier sweeping movements");

		// Re-centers itself to the touch location on touch-down.
		page.Bool(&g_Config.bAutoCenterTouchAnalog, "Auto-centering analog stick").Enabled(&g_Config.bShowTouchControls);

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			page.Bool(&g_Config.bHapticFeedback, { I18NCat::CONTROLS, "HapticFeedback", "Haptic Feedback (vibration)" });
		}

		// The pause button is now a regular on-screen button.

		page.Bool(&g_Config.bDisableDpadDiagonals, "Disable D-Pad diagonals (4-way touch)").Enabled(&g_Config.bShowTouchControls);
	}

#if PPSSPP_PLATFORM(WINDOWS)
	page.Header("Control input sources");
	page.Bool(&g_Config.bAllowHIDInput, "HID input");
	page.Bool(&g_Config.bAllowXInput, "XInput");
	page.Bool(&g_Config.bAllowDInput, "DirectInput");
#endif

	if (deviceType != DEVICE_TYPE_VR) {
		page.Header({ I18NCat::CONTROLS, "Keyboard", "Keyboard Control Settings" });
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page.Bool(&g_Config.bIgnoreWindowsKey, "Ignore Windows Key");
#endif
		page.FloatSlider(&g_Config.fAnalogLimiterDeadzone, 0.0f, 1.0f, 0.6f, 0.10f, "Analog Limiter").Units("/ 1.0");
		page.Hint({ I18NCat::CONTROLS, "AnalogLimiter Tip", "When the analog limiter button is pressed" });
		page.IntSlider(&g_Config.iRapidFireInterval, 1, 10, "Rapid fire interval").Default(5).Units("frames");
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
			page.Header({ I18NCat::CONTROLS, "Mouse", "Mouse settings" });
			page.IntSlider(&g_Config.iMouseWheelUpDelayMs, 10, 300, "Mouse wheel button-release delay").Default(1).Format(T(I18NCat::DIALOG, "%d ms"));

			page.Bool(&g_Config.bMouseControl, "Use Mouse Control");
			page.Hint({ I18NCat::CONTROLS, "MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon." });

#if !PPSSPP_PLATFORM(ANDROID)
			page.Bool(&g_Config.bMouseConfine, { I18NCat::CONTROLS, "Confine Mouse", "Trap mouse within window/display area" }).Enabled(&g_Config.bMouseControl);
#endif
			page.FloatSlider(&g_Config.fMouseSensitivity, 0.01f, 1.0f, 0.1f, 0.01f, "Mouse sensitivity").Units("x").Enabled(&g_Config.bMouseControl).LiveUpdate();
			page.FloatSlider(&g_Config.fMouseSmoothing, 0.0f, 0.95f, 0.9f, 0.05f, "Mouse smoothing").Units("x").Enabled(&g_Config.bMouseControl).LiveUpdate();
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
UI::View *GameSettingsScreen::AddNicknameChoice(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	PopupTextInputChoice *nickname = parent->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sNickName, sy->T("Nickname"), "", 32, screenManager()));
	if (g_Config.sNickName == "PPSSPP") {
		nickname->SetIconRight(ImageID("I_WARNING"));
	}
	nickname->OnChange.Add([this](UI::EventParams &e) {
		// Copy to infrastructure name if valid and not already set.
		if (g_Config.sInfrastructureUsername.empty()) {
			if (g_Config.sNickName == SanitizeString(g_Config.sNickName, StringRestriction::AlphaNumDashUnderscore, 3, 16)) {
				g_Config.sInfrastructureUsername = g_Config.sNickName;
			}
		}
		// Updates the warning icon, and the username notice on the Networking tab.
		RecreateViews();
	});
	return nickname;
}

void GameSettingsScreen::BuildNetworkingSettings(SettingsPage &page) {
	using namespace UI;

	page.Header({ I18NCat::MAINSETTINGS, "Networking" });

	page.Choice("Quick start guide for multiplayer", ImageID("I_LINK_OUT")).OnClick([](EventParams &e) {
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

	page.Bool(&g_Config.bEnableWlan, { I18NCat::NETWORKING, "Enable networking", "Enable networking/wlan (beta)" }).OnChange(requestLocalNetwork);
	page.Custom("MAC address", [this](UI::ViewGroup *parent) -> UI::View * {
		return parent->Add(new MacAddressChooser(GetRequesterToken(), gamePath_, &g_Config.sMACAddress, T(I18NCat::NETWORKING, "MAC address"), screenManager()));
	});

	page.Header("Ad Hoc multiplayer");

	page.Custom("Ad hoc server address", [this](UI::ViewGroup *parent) -> UI::View * {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		auto launchAdhocServerScreen = [this](UI::EventParams &) {
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			screenManager()->push(new AdhocServerScreen(&g_Config.sProAdhocServer, n->T("Ad hoc server address")));
		};

		if (AdhocServerNameIsCustom()) {
			LinearLayout *serverRow = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			serverRow->SetSpacing(0.0f);
			serverRow->Add(new ChoiceWithValueDisplay(&g_Config.sProAdhocServer, n->T("Ad hoc server address"), I18NCat::NONE, new LinearLayoutParams(1.0f)))->OnClick.Add(launchAdhocServerScreen);
			serverRow->Add(new Choice(ImageID("I_EDIT_TEXT"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([this](UI::EventParams &) {
				AskToEditCurrentServer(GetRequesterToken(), screenManager());
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
		page.Custom("Server status: %1", [this, serverEntry](UI::ViewGroup *parent) -> UI::View * {
			Choice *status = parent->Add(new Choice(ApplySafeSubstitutions(T(I18NCat::NETWORKING, "Server status: %1"), serverEntry.name)));
			status->SetIconLeft(ImageID("I_INFO"));
			status->OnClick.Add([this, serverEntry](UI::EventParams &) {
				screenManager()->push(new AdhocServerInfoScreen(serverEntry));
			});
			return status;
		});
	}

	static const char *relayModes[] = {"Auto", "Yes", "No"};
	page.IntChoice(&g_Config.iAdhocServerRelayMode, "Try to use server-provided packet relay", relayModes, 0, ARRAY_SIZE(relayModes)).ChoiceCat(I18NCat::DIALOG).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page.Hint({ I18NCat::NETWORKING, "PacketRelayHint", "Available on servers that provide 'aemu_postoffice' packet relay, like socom.cc. Disable this for LAN or VPN play. Can be more reliable, but sometimes slower." });

	page.Custom({ I18NCat::SYSTEM, "Nickname" }, [this](UI::ViewGroup *parent) {
		return AddNicknameChoice(parent);
	});
	page.Hint("Try to pick a unique nickname for ad hoc play");

	page.Bool(&g_Config.bEnableAdhocServer, "Enable built-in ad hoc server").OnChange(requestLocalNetwork);

	page.Bool(&g_Config.bAdhocUseUdp, "Connect to adhoc server using UDP (ENet) instead of TCP").EnabledFunc([] {
		return !PSP_IsInited();
	});
	page.Hint("Try switching between TCP/UDP if you suffer frequent adhoc server disconnects. Some network environments favor TCP and some UDP.");

	page.Bool(&g_Config.bAdhocRelayModeEnetPdpReliable, "Reliable datagram packet delivery on packet relay in UDP mode").EnabledFunc([] {
		return !PSP_IsInited() && g_Config.bAdhocUseUdp;
	});
	page.Hint("When disabled, packet loss on datagram adhoc sockets will be exposed to game's netcode. Keep this enabled if you don't know what that means.");

	page.Header("Infrastructure");
	if (g_Config.sInfrastructureUsername.empty()) {
		page.Custom("To play in Infrastructure Mode, you must enter a username", [](UI::ViewGroup *parent) -> UI::View * {
			NoticeView *notice = parent->Add(new NoticeView(NoticeLevel::WARN, T(I18NCat::NETWORKING, "To play in Infrastructure Mode, you must enter a username"), ""));
			notice->SetWrapText(true);
			return notice;
		});
	}
	page.Custom({ I18NCat::DIALOG, "Username" }, [this](UI::ViewGroup *parent) -> UI::View * {
		PopupTextInputChoice *usernameChoice = parent->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureUsername, T(I18NCat::DIALOG, "Username"), "", 16, screenManager()));
		usernameChoice->SetRestriction(StringRestriction::AlphaNumDashUnderscore, 3);
		usernameChoice->OnChange.Add([this](UI::EventParams &e) {
			RecreateViews();
		});
		return usernameChoice;
	});

	page.Bool(&g_Config.bInfrastructureAutoDNS, "Autoconfigure");
	page.Custom("DNS server", [this](UI::ViewGroup *parent) -> UI::View * {
		return parent->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sInfrastructureDNSServer, T(I18NCat::NETWORKING, "DNS server"), "", 32, screenManager()));
	}).Disabled(&g_Config.bInfrastructureAutoDNS);

	page.Header("UPnP (port-forwarding)");
	// Only togglable outside a game - sceNet latches settings like UPnPUseOriginalPort at boot,
	// and a game that's already mapped its ports wouldn't cope with them disappearing.
	page.Bool(&g_Config.bEnableUPnP, { I18NCat::NETWORKING, "Enable UPnP", "Enable UPnP (need a few seconds to detect)" }).EnabledFunc([] {
		return !PSP_IsInited();
	}).OnChange([requestLocalNetwork](UI::EventParams &e) {
		requestLocalNetwork(e);
		// Wake the UPnP service thread so it connects (or tears its mappings back down) right
		// away, instead of waiting for a port request that may never come.
		UPnP_Notify();
	});
	page.Bool(&g_Config.bUPnPUseOriginalPort, { I18NCat::NETWORKING, "UPnP use original port", "UPnP use original port (Enabled = PSP compatibility)" }).Enabled(&g_Config.bEnableUPnP);
	page.Hint({ I18NCat::NETWORKING, "UseOriginalPort Tip", "May not work for all devices or games, see wiki." });

	page.Header("Chat");
	page.Bool(&g_Config.bEnableNetworkChat, "Enable network chat");
	static const char *chatButtonPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right", "None" };
	page.IntChoice(&g_Config.iChatButtonPosition, "Chat Button Position", chatButtonPositions, 0, ARRAY_SIZE(chatButtonPositions)).ChoiceCat(I18NCat::DIALOG).Enabled(&g_Config.bEnableNetworkChat);
	static const char *chatScreenPositions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right" };
	page.IntChoice(&g_Config.iChatScreenPosition, "Chat Screen Position", chatScreenPositions, 0, ARRAY_SIZE(chatScreenPositions)).ChoiceCat(I18NCat::DIALOG).Enabled(&g_Config.bEnableNetworkChat);
	page.Bool(&g_Config.bChatTimestamps, "Show timestamps in chat").Enabled(&g_Config.bEnableNetworkChat);

	page.Header("Quick chat");
	page.Bool(&g_Config.bEnableQuickChat, "Enable quick chat").Enabled(&g_Config.bEnableNetworkChat);

	for (int i = 0; i < 5; i++) {
		page.Custom("Quick chat %1", [this, i](UI::ViewGroup *parent) -> UI::View * {
			return parent->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sQuickChat[i], ApplySafeSubstitutions(T(I18NCat::NETWORKING, "Quick chat %1"), i + 1), "", 32, screenManager()));
		}).EnabledFunc([] {
			return g_Config.bEnableQuickChat && g_Config.bEnableNetworkChat;
		});
	}

	page.Header({ I18NCat::NETWORKING, "Misc", "Misc (default = compatibility)" });
	page.Choice("Open PPSSPP Multiplayer Wiki Page", ImageID("I_LINK_OUT")).OnClick([](EventParams &e) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		std::string url(n->T("MultiplayerHowToURL", "https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP"));
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url);
	});
	static const char *wlanChannels[] = {"Auto", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
	page.IntChoice(&g_Config.iWlanAdhocChannel, "WLAN Channel", wlanChannels, 0, ARRAY_SIZE(wlanChannels)).Setup([](View *view) {
		PopupMultiChoice *wlanChannelChoice = static_cast<PopupMultiChoice *>(view);
		wlanChannelChoice->SetChoicesUntranslated(1, (int)ARRAY_SIZE(wlanChannels) - 1);
		for (int i = 0; i < 4; i++) {
			wlanChannelChoice->HideChoice(i + 2);
			wlanChannelChoice->HideChoice(i + 7);
		}
	});
	page.IntSlider(&g_Config.iPortOffset, 0, 60000, "Port offset").Default(10000).Step(100);
	page.Hint("PortOffsetHint");
	page.IntSlider(&g_Config.iMinTimeout, 0, 15000, { I18NCat::NETWORKING, "Minimum Timeout", "Minimum Timeout (override in ms, 0 = default)" }).Default(0).Step(50).Format(T(I18NCat::DIALOG, "%d ms"));
	page.Bool(&g_Config.bForcedFirstConnect, { I18NCat::NETWORKING, "Forced First Connect", "Forced First Connect (faster Connect)" });
	page.Bool(&g_Config.bAllowSpeedControlWhileConnected, "Allow speed control while connected (not recommended)");
	if (Discord::IsAvailable()) {
		page.Bool(&g_Config.bDiscordRichPresence, "Send Discord Presence information");
	}
}

void GameSettingsScreen::BuildToolsSettings(SettingsPage &page) {
	using namespace UI;

	page.Header("Tools");

	const bool showRetroAchievements = System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR;
	if (showRetroAchievements) {
		page.Choice({ I18NCat::SYSTEM, "RetroAchievements" }).OnClick([this](UI::EventParams &) {
			screenManager()->push(new RetroAchievementsSettingsScreen(gamePath_));
		}).Setup([](View *view) {
			static_cast<Choice *>(view)->SetIconRight(ImageID("I_RETROACHIEVEMENTS_LOGO"));
		});
	}

	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	page.Choice({ I18NCat::SAVEDATA, "Savedata Manager" }).OnClick([this](UI::EventParams &) {
		screenManager()->push(new SavedataScreen(gamePath_));
	});
	page.Choice({ I18NCat::DEVELOPER, "System Information" }).OnClick([this](UI::EventParams &) {
		screenManager()->push(new SystemInfoScreen(gamePath_));
	});
	page.Choice({ I18NCat::SYSTEM, "Developer Tools" }).OnClick([this](UI::EventParams &) {
		screenManager()->push(new DeveloperToolsScreen(gamePath_));
	});
	page.Choice({ I18NCat::REMOTEISO, "Remote disc streaming" }).OnClick([this](UI::EventParams &) {
		screenManager()->push(new RemoteISOScreen(gamePath_));
	});
	page.Choice({ I18NCat::SYSTEM, "PSP Firmware" }).OnClick([this](UI::EventParams &) {
		screenManager()->push(new FirmwareScreen(gamePath_));
	});
}

void GameSettingsScreen::BuildSystemSettings(SettingsPage &page) {
	using namespace UI;

	page.Header("UI");

	page.Custom("Language", [this](UI::ViewGroup *parent) -> UI::View * {
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
	page.IntChoice(&g_Config.iAppSwitchMode, "App switching mode", indicator, 0, ARRAY_SIZE(indicator)).OnChange([](EventParams &e) {
		System_Notify(SystemNotification::APP_SWITCH_MODE_CHANGED);
	});

	{
		// Note: On iPhone, iOS hides the status bar in landscape no matter what this is set to.
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page.Bool(&config.bImmersiveMode, "Hide status bar").OnChange([](EventParams &e) {
			System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
		});
	}
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page.Bool(&config.bImmersiveMode, "Hide navigation bar").OnChange([this](EventParams &e) {
			OnImmersiveModeChange(e);
		});
	}
#endif

	UIContext *ctx = screenManager()->getUIContext();
	page.IntSlider(&g_Config.iUIScaleFactor, -8, 8, "UI size adjustment (DPI)").Default(0).ZeroLabel("Off").OnChange([ctx](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		ctx->InvalidateAtlas();
		NativeResized();
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		page.Choice("Clear UI background").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		page.Choice("Set UI background...").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	}

	page.Bool(&g_Config.bTransparentBackground, "Transparent UI background");

	// Shared with achievements.
	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

	page.IntChoice(&g_Config.iNotificationPos, "Notification screen position", positions, -1, ARRAY_SIZE(positions)).ChoiceCat(I18NCat::DIALOG);

	static const char *backgroundAnimations[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background", "Bouncing icon", "Colored floating symbols" };
	page.IntChoice(&g_Config.iBackgroundAnimation, "UI background animation", backgroundAnimations, 0, ARRAY_SIZE(backgroundAnimations));

	page.Custom("Theme", [this](UI::ViewGroup *parent) -> UI::View * {
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
		page.FloatSlider(&g_Config.fUITint, 0.0f, 1.0f, 0.0f, 0.01f, "Color tint").LiveUpdate().Setup(noDropShadow);
		page.FloatSlider(&g_Config.fUISaturation, 0.0f, 2.0f, 1.0f, 0.01f, "Color saturation").LiveUpdate().Setup(noDropShadow);
	}

	page.Header("PSP Memory Stick");

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		page.Choice("Show Memory Stick folder").OnClick([](UI::EventParams &p) {
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
		page.Choice("Set Memory Stick folder").OnClick([this](UI::EventParams &) {
			SetMemStickDirDarwin(GetRequesterToken());
		});
	}
#endif

	auto addMemstickPath = [this, &page]() {
		page.Custom("Memory Stick folder", [this](UI::ViewGroup *parent) -> UI::View * {
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
	page.Bool(&installed_, "Memory Stick in My Documents").Enabled(&memstickMyDocEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickMyDoc(e);
	});
	page.Bool(&otherinstalled_, "Memory Stick in installed.txt").Enabled(&memstickOtherEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickOther(e);
	});

	if (!System_GetPropertyBool(SYSPROP_CAN_GET_FREE_SPACE_FAST)) {
		// It might be slow, so use an option.
		page.Bool(&g_Config.bReportAccurateFreeStorageSpace, "Report free storage space accurately");
	}
#endif
#endif
	page.Bool(&g_Config.bMemStickInserted, "Memory Stick inserted");
	page.IntSlider(&g_Config.iMemStickSizeGB, 1, 32, "Memory Stick size").Default(16).Units("GB").Format("%d GB");

	page.Header("Help the PPSSPP team");
	page.Bool(&enableReports_, "Enable Compatibility Server Reports").EnabledFunc([]() {
		return Reporting::IsSupported();
	});

	page.Header("Emulation");

	page.Bool(&g_Config.bFastMemory, "Fast Memory").OnChange([this](UI::EventParams &e) {
		OnJitAffectingSetting(e);
	});
	page.Bool(&g_Config.bIgnoreBadMemAccess, "Ignore bad memory accesses");

	static const char *ioTimingMethods[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays", "Simulate UMD slow reading speed"};
	page.IntChoice(&g_Config.iIOTimingMethod, "I/O timing method", ioTimingMethods, 0, ARRAY_SIZE(ioTimingMethods));
	page.Bool(&g_Config.bForceLagSync, "Force real clock sync (slower, less lag)").Disabled(&g_Config.bAutoFrameSkip);
	page.IntSlider(&g_Config.iLockedCPUSpeed, 0, 1000, { I18NCat::SYSTEM, "Change CPU Clock", "Change CPU Clock (unstable)" }).Default(0).Units(T(I18NCat::SYSTEM, "MHz, 0:default")).ZeroLabel("Auto");

	page.Header({ I18NCat::SAVEDATA, "Save states" });  // Borrow this string from the savedata manager

	page.Bool(&g_Config.bEnableStateUndo, "Savestate slot backups");
	page.Bool(&g_Config.bConfirmLoadState, "Ask to confirm on load");

	page.IntSlider(&g_Config.iSaveStateSlotCount, 1, 30, "Savestate slot count").Default(5).OnChange([](UI::EventParams &e) {
		System_Notify(SystemNotification::UI);
	});

	// NOTE: We will soon support more states, but we'll keep this niche feature limited to the first five.
	static const char *autoLoadSaveStateChoices[] = {"Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5"};
	page.IntChoice(&g_Config.iAutoLoadSaveState, "Auto load savestate", autoLoadSaveStateChoices, 0, ARRAY_SIZE(autoLoadSaveStateChoices)).Setup([](View *view) {
		if (g_Config.iAutoLoadSaveState != 1) {
			static_cast<PopupMultiChoice *>(view)->HideChoice(1);  // Hide "Oldest Save" if not using that mode. It doesn't make sense.
		}
	});

	page.IntSlider(&g_Config.iRewindSnapshotInterval, 0, 60, "Rewind Snapshot Interval").Default(0).Units(T(I18NCat::DIALOG, "seconds, 0:off")).Format(T(I18NCat::DIALOG, "%d seconds")).ZeroLabel("Off");

	page.Header("General");

	page.IntSlider(&g_Config.iAskForExitConfirmationAfterSeconds, 0, 1200, "Ask for exit confirmation after seconds").Default(300).Units("s").ZeroLabel("Off");

	if (System_GetPropertyBool(SYSPROP_CAN_RESTRICT_ORIENTATION)) {
		// Display rotation
		page.Custom({ I18NCat::CONTROLS, "Screen Rotation" }, [this](UI::ViewGroup *parent) -> UI::View * {
			AddRotationPicker(screenManager(), parent, true);
			return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
		});
	}

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
		page.Bool(&g_Config.bSustainedPerformanceMode, "Sustained performance mode").OnChange([this](UI::EventParams &e) {
			OnSustainedPerformanceModeChange(e);
		});
	}

	page.Choice("Restore Default Settings").OnClick([this](UI::EventParams &e) {
		OnRestoreDefaultSettings(e);
	});

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		page.Bool(&g_Config.bBypassOSKWithKeyboard, "Use system native keyboard");

	if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
		page.Bool(&g_Config.bCacheFullIsoInRam, "Cache full ISO in RAM").EnabledFunc([] {
			return !PSP_IsInited();
		});
	}

	page.Bool(&g_Config.bCheckForNewVersion, { I18NCat::SYSTEM, "VersionCheck", "Check for new versions of PPSSPP" }).OnChange([](UI::EventParams &e) {
		// Reset the dismissed version so it will check again.
		if (g_Config.bCheckForNewVersion) {
			g_Config.sDismissedVersion.clear();
		}
	});
	page.Bool(&g_Config.bScreenshotsAsPNG, "Screenshots as PNG");
	static const char *screenshotModeChoices[] = { "Final processed image", "Raw game image" };
	page.IntChoice(&g_Config.iScreenshotMode, "Screenshot mode", screenshotModeChoices, 0, ARRAY_SIZE(screenshotModeChoices));
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS) || (defined (SDL) && !defined(MOBILE_DEVICE))
	page.Bool(&g_Config.bPauseOnLostFocus, "Pause when not focused");
#endif

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		page.Header({ I18NCat::GRAPHICS, "Camera" });
		page.StringChoice(&g_Config.sCameraDevice, { I18NCat::GRAPHICS, "Camera Device" }, std::move(cameraList)).OnChange([](UI::EventParams &e) {
			Camera::onCameraDeviceChange();
		});
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page.Bool(&g_Config.bCameraMirrorHorizontal, { I18NCat::GRAPHICS, "Mirror camera image" });
#endif
	}

	page.Header("Cheats");
	page.Bool(&g_Config.bEnableCheats, "Enable Cheats");
	page.Bool(&g_Config.bEnablePlugins, "Enable plugins");

	page.Header("PSP Settings");

	// The ordering here is simply mapping directly to PSP_SYSTEMPARAM_LANGUAGE_*.
	static const char *defaultLanguages[] = { "Auto", "Japanese", "English", "French", "Spanish", "German", "Italian", "Dutch", "Portuguese", "Russian", "Korean", "Chinese (traditional)", "Chinese (simplified)" };
	page.IntChoice(&g_Config.iLanguage, { I18NCat::PSPSETTINGS, "Game language" }, defaultLanguages, -1, ARRAY_SIZE(defaultLanguages));
	static const char *models[] = { "PSP-1000", "PSP-2000/3000" };
	page.IntChoice(&g_Config.iPSPModel, "PSP Model", models, 0, ARRAY_SIZE(models)).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page.Custom("Nickname", [this](UI::ViewGroup *parent) {
		return AddNicknameChoice(parent);
	});
	page.Bool(&g_Config.bDayLightSavings, "Daylight savings");
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY" };
	page.IntChoice(&g_Config.iDateFormat, "Date Format", dateFormat, 0, ARRAY_SIZE(dateFormat));
	static const char *timeFormat[] = { "24HR", "12HR" };
	page.IntChoice(&g_Config.iTimeFormat, "Time Format", timeFormat, 0, ARRAY_SIZE(timeFormat));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	page.IntChoice(&g_Config.iButtonPreference, "Confirmation Button", buttonPref, 0, ARRAY_SIZE(buttonPref)).OnChange([](UI::EventParams &e) {
		KeyMap::UpdateNativeMenuKeys();
	});

#if defined(_WIN32)
	page.Header("Recording");
	page.Bool(&g_Config.bDumpFrames, "Record Display");
	page.Bool(&g_Config.bUseFFV1, "Use Lossless Video Codec (FFV1)");
	page.Bool(&g_Config.bDumpVideoOutput, "Use output buffer (with overlay) for recording");
	page.Bool(&g_Config.bDumpAudio, "Record Audio");
	page.Bool(&g_Config.bSaveLoadResetsAVdumping, "Reset Recording on Save/Load State");
#endif
}

void GameSettingsScreen::BuildVRSettings(SettingsPage &page) {
	using namespace UI;

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	if (deviceType == DEVICE_TYPE_VR) {
		page.Header("Virtual reality");
		page.Bool(&g_Config.bEnableVR, "Virtual reality");
		page.Bool(&g_Config.bEnable6DoF, "6DoF movement");
		page.Bool(&g_Config.bEnableStereo, "Stereoscopic vision (Experimental)");
		page.Bool(&g_Config.bEnableImmersiveVR, "Enable immersive mode");
		if (IsPassthroughSupported()) {
			page.Bool(&g_Config.bPassthrough, "Enable passthrough");
		}
		page.Bool(&g_Config.bForce72Hz, "Force 72Hz update");
	}

	page.Header("VR camera");
	if (deviceType == DEVICE_TYPE_VR) {
		page.FloatSlider(&g_Config.fCanvasDistance, 1.0f, 15.0f, 12.0f, 1.0f, "Distance to 2D menus and scenes");
		page.FloatSlider(&g_Config.fCanvas3DDistance, 1.0f, 15.0f, 3.0f, 1.0f, "Distance to 3D scenes when VR disabled");
	}
	page.FloatSlider(&g_Config.fFieldOfViewPercentage, 100.0f, 200.0f, 100.0f, 10.0f, "Field of view scale").Units(T(I18NCat::VR, "% of native FoV"));
	page.Bool(&g_Config.bRescaleHUD, "Heads-up display detection");
	page.FloatSlider(&g_Config.fHeadUpDisplayScale, 0.0f, 1.5f, 0.3f, 0.1f, "Heads-up display scale").Enabled(&g_Config.bRescaleHUD);
	page.Bool(&g_Config.bManualForceVR, "Manual switching between flat screen and VR using SCREEN key");
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

void GameSettingsScreen::TriggerRestartOrDo(std::function<void()> callback) {
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
	vert->Add(new CheckBox(&zone.bGestureControlEnabled, co->T("Enable gesture control")));

	vert->Add(new ItemHeader(co->T("Swipe")));
	vert->Add(new PopupMultiChoice(&zone.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupMultiChoice(&zone.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&zone.fSwipeSensitivity, 0.01f, 2.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);
	vert->Add(new PopupSliderChoiceFloat(&zone.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);

	vert->Add(new ItemHeader(co->T("Double tap")));
	vert->Add(new PopupMultiChoice(&zone.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()));

	vert->Add(new ItemHeader(co->T("Analog Stick")));
	vert->Add(new CheckBox(&zone.bAnalogGesture, co->T("Enable analog stick gesture")));
	vert->Add(new PopupSliderChoiceFloat(&zone.fAnalogGestureSensitivity, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bAnalogGesture);
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
