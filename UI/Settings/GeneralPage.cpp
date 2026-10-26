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

#include "ppsspp_config.h"

#include <cstdio>
#include <string>

#include "Common/Common.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/GPU/thin3d.h"
#include "Common/Log.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"
#include "UI/Background.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MiscScreens.h"
#include "UI/MiscViews.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"
#include "UI/Theme.h"

static void OnImmersiveModeChange() {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		System_RecreateActivity();
	}
}

static void OnSustainedPerformanceModeChange() {
	System_Notify(SystemNotification::SUSTAINED_PERF_CHANGE);
}

static void OnChangeBackground(SettingsPageHost *host) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";

	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		INFO_LOG(Log::UI, "Clearing background image.");
		// The button is in clear mode.
		File::Delete(bgPng);
		File::Delete(bgJpg);
		UIBackgroundShutdown();
		host->RecreatePageViews();
		return;
	}

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	System_BrowseForImage(host->PageRequesterToken(), sy->T("Set UI background..."), bgJpg, [host](std::string_view value, int converted) {
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
		host->RecreatePageViews();
	});

	// Change to a browse or clear button.
}

static void CallbackRestoreDefaults(SystemPageState *state, bool yes) {
	if (yes) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS);
		ReadSystemPageState(state);
	}
	System_Notify(SystemNotification::UI);
}

static void OnRestoreDefaultSettings(SettingsPageHost *host, SystemPageState *state) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (g_Config.IsGameSpecific()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		host->PageScreenManager()->push(
			new UI::MessagePopupScreen(sy->T("Restore Default Settings"), dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"),
				di->T("OK"), di->T("Cancel"), [state](bool yes) { CallbackRestoreDefaults(state, yes); }));
	} else {
		std::string_view title = sy->T("Restore Default Settings");
		host->PageScreenManager()->push(new RestoreSettingsScreen(title));
	}
}

static void BuildUISettingsPage(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	// The UI context is looked up when used, since it's recreated along with the graphics.
	page->Setting(&g_Config.iUIScaleFactor).OnChange([host](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		host->PageScreenManager()->getUIContext()->InvalidateAtlas();
		NativeResized();
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		page->Choice("Clear UI background").OnClick([host](UI::EventParams &e) {
			OnChangeBackground(host);
		});
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		page->Choice("Set UI background...").OnClick([host](UI::EventParams &e) {
			OnChangeBackground(host);
		});
	}

	page->Setting(&g_Config.bTransparentBackground);

	// Shared with achievements.

	page->Setting(&g_Config.iNotificationPos);

	page->Setting(&g_Config.iBackgroundAnimation);

	page->Custom("Theme", [host](UI::ViewGroup *parent) -> UI::View * {
		PopupMultiChoiceDynamic *theme = parent->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, T(I18NCat::SYSTEM, "Theme"), GetThemeInfoNames(), I18NCat::THEMES, host->PageScreenManager()));
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

	Draw::DrawContext *draw = host->PageScreenManager()->getDrawContext();

	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		auto noDropShadow = [](View *view) {
			static_cast<PopupSliderChoiceFloat *>(view)->SetHasDropShadow(false);
		};
		page->Setting(&g_Config.fUITint).LiveUpdate().Setup(noDropShadow);
		page->Setting(&g_Config.fUISaturation).LiveUpdate().Setup(noDropShadow);
	}
}

// Using SYSTEM translations
void BuildGeneralSettings(SettingsPage *page, SystemPageState *state) {
	using namespace UI;

	SettingsPageHost *host = page->Host();

	page->Header();

	page->Custom("Language", [host](UI::ViewGroup *parent) -> UI::View * {
		auto langCodeToName = [](std::string_view value) -> std::string {
			auto &mapping = GetLangValuesMapping();
			auto iter = mapping.find(value);
			if (iter != mapping.end()) {
				return iter->second.first;
			}
			return std::string(value);
		};

		ChoiceWithValueDisplay *language = parent->Add(new ChoiceWithValueDisplay(&g_Config.sLanguageIni, T(I18NCat::SYSTEM, "Language"), langCodeToName));
		language->SetIconLeft(ImageID("I_LANGUAGE"));
		language->OnClick.Add([host](UI::EventParams &e) {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			auto langScreen = new NewLanguageScreen(sy->T("Language"));
			// The actual switching is handled in OnCompleted in NewLanguageScreen.
			if (e.v)
				langScreen->SetPopupOrigin(e.v);
			host->PageScreenManager()->push(langScreen);
		});
		return language;
	});

	page->Header();
	page->Link({I18NCat::SYSTEM, "UI"}, I18NCat::SYSTEM, &BuildUISettingsPage);

	page->Header();
	if (System_GetPropertyBool(SYSPROP_CAN_RESTRICT_ORIENTATION)) {
		// Display rotation
		page->Custom({I18NCat::CONTROLS, "Screen Rotation"}, [host](UI::ViewGroup *parent) -> UI::View * {
			AddRotationPicker(host->PageScreenManager(), parent, true);
			return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
		});
	}

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};
	page->Setting(&g_Config.iAppSwitchMode);

	{
		// Note: On iPhone, iOS hides the status bar in landscape no matter what this is set to.
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([](EventParams &e) {
			System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
		});
	}
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([](EventParams &e) {
			OnImmersiveModeChange();
		});
	}
#endif
	page->Setting(&g_Config.iAskForExitConfirmationAfterSeconds);
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS) || (defined (SDL) && !defined(MOBILE_DEVICE))
	page->Setting(&g_Config.bPauseOnLostFocus);
#endif
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		page->Setting(&g_Config.bBypassOSKWithKeyboard);
	}

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
		page->Setting(&g_Config.bSustainedPerformanceMode).OnChange([](UI::EventParams &e) {
			OnSustainedPerformanceModeChange();
		});
	}

	page->Setting(&g_Config.bCheckForNewVersion);

	page->Header();
	page->Choice("Restore Default Settings").OnClick([host, state](UI::EventParams &e) {
		OnRestoreDefaultSettings(host, state);
	});

	page->Header({I18NCat::SAVEDATA, "Save states"});  // Borrow this string from the savedata manager

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

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		page->Header({I18NCat::GRAPHICS, "Camera"});
		page->Setting(&g_Config.sCameraDevice, std::move(cameraList));
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page->Setting(&g_Config.bCameraMirrorHorizontal);
#endif
	}

	page->Header("Cheats");
	page->Setting(&g_Config.bEnableCheats);
	page->Setting(&g_Config.bEnablePlugins);

	page->Header();
	page->Setting(&g_Config.bScreenshotsAsPNG);
	page->Setting(&g_Config.iScreenshotMode);

	page->Header("Help the PPSSPP team");
	page->Bool(&state->enableReports, "Enable Compatibility Server Reports").EnabledFunc([]() {
		return Reporting::IsSupported();
	});
}
