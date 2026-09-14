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

#include "UI/UISettingsScreen.h"

#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Root.h"
#include "Common/UI/ScreenManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"
#include "UI/BackgroundAudio.h"
#include "UI/Background.h"
#include "UI/MiscViews.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/Theme.h"

// TODO:
// - Decide which old Ini strings should be moved to the new UI category.
// - Finalize the new strings and add them to Ini files.
// - Decide which UI settings should be moved to a new ConfigSetting instance (to be created).
// - Clean up audio odd behaviors of audio settings and events (also in GameSettingsScreen).

UISettingsScreen::UISettingsScreen(const Path &gamePath)
	: UITabbedBaseDialogScreen(gamePath, &g_Config.iUISettingsCurrentTab, TabDialogFlags::AddAutoTitles) {
}

void UISettingsScreen::CreateTabs() {
	auto ui = GetI18NCategory(I18NCat::UISETTINGS);

	AddTab("GeneralUI", ui->T("General UI"), [this](UI::LinearLayout *parent) {
		CreateGeneralUISettings(parent);
	});

	AddTab("UISounds", ui->T("UI sound"), [this](UI::LinearLayout *parent) {
		CreateUISoundsSettings(parent);
	});

	AddTab("CustomizeGUI", ui->T("Customize GUI"), [this](UI::LinearLayout *parent) {
		CreateCustomizationSettings(parent);
	});

	AddTab("Accessibility", ui->T("Accessibility"), [this](UI::LinearLayout *parent) {
		CreateAccessibilitySettings(parent);
	});
}

void UISettingsScreen::CreateGeneralUISettings(UI::ViewGroup *generalUISettings) {
	using namespace UI;

	auto ui = GetI18NCategory(I18NCat::UISETTINGS);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	generalUISettings->Add(new ItemHeader(ui->T("General UI settings")));

	PopupSliderChoice *uiScale = generalUISettings->Add(new PopupSliderChoice(&g_Config.iUIScaleFactor, -8, 8, 0, sy->T("UI size adjustment (DPI)"), screenManager()));
	uiScale->SetZeroLabel(sy->T("Off"));
	UIContext *ctx = screenManager()->getUIContext();
	uiScale->OnChange.Add([ctx](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		ctx->InvalidateAtlas();
		NativeResized();
	});

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};

	PopupMultiChoice *switchMode = generalUISettings->Add(new PopupMultiChoice(&g_Config.iAppSwitchMode, sy->T("App switching mode"), indicator, 0, ARRAY_SIZE(indicator), I18NCat::SYSTEM, screenManager()));
	switchMode->OnChoice.Add([](EventParams &e) {
		System_Notify(SystemNotification::APP_SWITCH_MODE_CHANGED);
	});

	// Note: On iPhone, iOS hides the status bar in landscape no matter what this is set to.
	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	generalUISettings->Add(new CheckBox(&config.bImmersiveMode, sy->T("Hide status bar")))->OnClick.Add([](EventParams &e) {
		System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	});
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		generalUISettings->Add(new CheckBox(&config.bImmersiveMode, sy->T("Hide navigation bar")))->OnClick.Handle(this, &UISettingsScreen::OnImmersiveModeChange);
	}
#endif

	generalUISettings->Add(new ItemHeader(ui->T("Mid-game UI settings")));

	generalUISettings->Add(new CheckBox(&g_Config.bTransparentBackground, sy->T("Transparent UI background")));
	generalUISettings->Add(new CheckBox(&g_Config.bShowSaveLoadIndicator, dev->T("Show indicator when saving/loading")));

    // Shared with achievements.
	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };
	generalUISettings->Add(new PopupMultiChoice(&g_Config.iNotificationPos, sy->T("Notification screen position"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()));

	generalUISettings->Add(new Choice(ui->T("RetroAchievements notifications")))->OnClick.Add([this](UI::EventParams &) {
		g_Config.iRetroAchievementsSettingsCurrentTab = 1;
		screenManager()->push(new RetroAchievementsSettingsScreen(gamePath_));
	});
}

void UISettingsScreen::CreateUISoundsSettings(UI::ViewGroup *uiSoundsSettings) {
	using namespace UI;

	auto ui = GetI18NCategory(I18NCat::UISETTINGS);
	auto a = GetI18NCategory(I18NCat::AUDIO);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	uiSoundsSettings->Add(new ItemHeader(ui->T("UI volume")));

	uiSoundsSettings->Add(new CheckBox(&g_Config.bUISound, a->T("UI sound")));
	PopupSliderChoice *uiVolume = uiSoundsSettings->Add(new PopupSliderChoice(&g_Config.iUIVolume, 0, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iUIVolume), a->T("UI volume"), screenManager()));
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

	PopupSliderChoice *achievementVolume = uiSoundsSettings->Add(new PopupSliderChoice(&g_Config.iAchievementVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iAchievementVolume), ac->T("Achievement sound volume"), screenManager()));
	achievementVolume->SetFormat("%d%%");
	achievementVolume->SetEnabledPtr(&g_Config.bEnableSound);
	achievementVolume->SetZeroLabel(a->T("Mute"));
	achievementVolume->OnChange.Add([](UI::EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
	});

	PopupSliderChoice *gamePreviewVolume = uiSoundsSettings->Add(new PopupSliderChoice(&g_Config.iGamePreviewVolume, VOLUME_OFF, VOLUMEHI_FULL, Config::GetDefaultValueInt(&g_Config.iGamePreviewVolume), a->T("Game preview volume"), screenManager()));
	gamePreviewVolume->SetFormat("%d%%");
	gamePreviewVolume->SetZeroLabel(a->T("Mute"));

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		uiSoundsSettings->Add(new ItemHeader(ui->T("Customize sound effects")));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sUISelectAudioFile, ui->T("Select"), UISound::SELECT));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sUIConfirmAudioFile, ui->T("Confirm"), UISound::CONFIRM));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sUIBackAudioFile, ui->T("Back"), UISound::BACK));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sUIToggleOnAudioFile, ui->T("Toggle on"), UISound::TOGGLE_ON));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sUIToggleOffAudioFile, ui->T("Toggle off"), UISound::TOGGLE_OFF));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsUnlockAudioFile, ac->T("Achievement unlocked"), UISound::ACHIEVEMENT_UNLOCKED));
		uiSoundsSettings->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsLeaderboardSubmitAudioFile, ac->T("Leaderboard score submission"), UISound::LEADERBOARD_SUBMITTED));
	}
}

void UISettingsScreen::CreateCustomizationSettings(UI::ViewGroup *customizationSettings) {
	using namespace UI;

	auto ui = GetI18NCategory(I18NCat::UISETTINGS);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	customizationSettings->Add(new ItemHeader(ui->T("Background")));

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	Choice *backgroundChoice = nullptr;
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		backgroundChoice = customizationSettings->Add(new Choice(sy->T("Clear UI background")));
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		backgroundChoice = customizationSettings->Add(new Choice(sy->T("Set UI background...")));
	}
	if (backgroundChoice) {
		backgroundChoice->OnClick.Handle(this, &UISettingsScreen::OnChangeBackground);
	}

	static const char *backgroundAnimations[] = { "No animation", "Floating symbols", "Recent games", "Waves", "Moving background", "Bouncing icon", "Colored floating symbols" };
	customizationSettings->Add(new PopupMultiChoice(&g_Config.iBackgroundAnimation, sy->T("UI background animation"), backgroundAnimations, 0, ARRAY_SIZE(backgroundAnimations), I18NCat::SYSTEM, screenManager()));

	customizationSettings->Add(new ItemHeader(ui->T("Theme")));

	PopupMultiChoiceDynamic *theme = customizationSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, sy->T("Theme"), GetThemeInfoNames(), I18NCat::THEMES, screenManager()));
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
		customizationSettings->Add(tint);
		PopupSliderChoiceFloat *saturation = new PopupSliderChoiceFloat(&g_Config.fUISaturation, 0.0f, 2.0f, 1.0f, sy->T("Color saturation"), 0.01f, screenManager());
		saturation->SetHasDropShadow(false);
		saturation->SetLiveUpdate(true);
		customizationSettings->Add(saturation);
	}
}

void UISettingsScreen::CreateAccessibilitySettings(UI::ViewGroup *accessibilitySettings) {
	(void)accessibilitySettings;
}

void UISettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		System_RecreateActivity();
	}
}

void UISettingsScreen::OnChangeBackground(UI::EventParams &e) {
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
