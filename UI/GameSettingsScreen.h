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

#pragma once

#include "ppsspp_config.h"
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Common/Data/Text/I18n.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "Core/ConfigValues.h"
#include "UI/BaseScreens.h"
#include "UI/TabbedDialogScreen.h"

class Path;
class SettingsPage;

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public UITabbedBaseDialogScreen {
public:
	GameSettingsScreen(const Path &gamePath, std::string gameID = "", bool editThenRestore = false);
	~GameSettingsScreen();

	const char *tag() const override { return "GameSettings"; }

protected:
	void CallbackRestoreDefaults(bool yes);
	void CallbackMemstickFolder(bool yes);
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	void CreateTabs() override;

private:
	void PreCreateViews() override;

	// One per tab.
	struct PageDef {
		const char *tag;
		const char *title;  // In MAINSETTINGS.
		const char *icon;
		const char *helpTag;  // If null, there's no title bar.
		I18NCat cat;
		void (GameSettingsScreen::*build)(SettingsPage &page);
	};
	static const std::vector<PageDef> &PageDefs();

	void BuildGraphicsSettings(SettingsPage &page);
	void BuildControlsSettings(SettingsPage &page);
	void BuildAudioSettings(SettingsPage &page);
	void BuildNetworkingSettings(SettingsPage &page);
	void BuildToolsSettings(SettingsPage &page);
	void BuildSystemSettings(SettingsPage &page);
	void BuildVRSettings(SettingsPage &page);
	UI::View *AddNicknameChoice(UI::ViewGroup *parent);


	std::string gameID_;
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	void CheckMemstickInstallState();

	bool installed_ = false;
	bool otherinstalled_ = false;
	bool memstickMyDocEnabled_ = false;
	bool memstickOtherEnabled_ = false;
#endif

	// Global settings handlers
	void OnChangeBackground(UI::EventParams &e);
	void OnRestoreDefaultSettings(UI::EventParams &e);
	void OnRenderingBackend(UI::EventParams &e);
	void OnRenderingDevice(UI::EventParams &e);
	void OnInflightFramesChoice(UI::EventParams &e);
	void OnJitAffectingSetting(UI::EventParams &e);
	void OnShowMemstickScreen(UI::EventParams &e);
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	void OnMemoryStickMyDoc(UI::EventParams &e);
	void OnMemoryStickOther(UI::EventParams &e);
#endif
	void OnImmersiveModeChange(UI::EventParams &e);
	void OnSustainedPerformanceModeChange(UI::EventParams &e);

	void TriggerRestartOrDo(std::function<void()> callback);
	void ReadConfigTemporaries();

	// Temporaries to convert setting types, cache enabled, etc.
	int iAlternateSpeedPercent1_ = 0;
	int iAlternateSpeedPercent2_ = 0;
	int iAlternateSpeedPercentAnalog_ = 0;
	int prevInflightFrames_ = -1;
	bool enableReports_ = false;
	bool analogSpeedMapped_ = false;

	// edit the game-specific settings and restore the global settings after exiting
	bool editGameSpecificThenRestore_ = false;

	// Android-only
	std::string pendingMemstickFolder_;
};

class GestureMappingScreen : public UITabbedBaseDialogScreen {
public:
	GestureMappingScreen(const Path &gamePath) : UITabbedBaseDialogScreen(gamePath) {}

	void CreateTabs() override;
	const char *tag() const override { return "GestureMapping"; }
	bool ShowSearchControls() const override { return false; }
protected:
	void CreateGestureTab(UI::LinearLayout *parent, int zoneIndex, bool portrait);
};

class RestoreSettingsScreen : public UI::PopupScreen {
public:
	RestoreSettingsScreen(std::string_view title);
	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "RestoreSettingsScreen"; }
private:
	void OnCompleted(DialogResult result) override;
	int restoreFlags_ = (int)(RestoreSettingsBits::SETTINGS);  // RestoreSettingsBits enum
};

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath);

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
void SetMemStickDirDarwin(int requesterToken);
#endif
