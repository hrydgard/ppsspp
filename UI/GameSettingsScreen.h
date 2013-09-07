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

#include "ui/ui_screen.h"
#include "UI/MiscScreens.h"

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public UIDialogScreenWithBackground {
public:
	GameSettingsScreen(std::string gamePath, std::string gameID = "")
		: gamePath_(gamePath), gameID_(gameID), iAlternateSpeedPercent_(3), enableReports_(false) {}

	virtual void update(InputState &input);

	UI::Event OnLanguageChanged;
	UI::Event OnRecentChanged;

protected:
	virtual void CreateViews();
	virtual void DrawBackground(UIContext &dc);
	virtual void sendMessage(const char *message, const char *value);

private:
	std::string gamePath_, gameID_;

	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextView *tvTitle_;
	UI::TextView *tvGameSize_;
	UI::CheckBox *enableReportsCheckbox_;

	// Event handlers
	UI::EventReturn OnDownloadPlugin(UI::EventParams &e);
	UI::EventReturn OnControlMapping(UI::EventParams &e);
	UI::EventReturn OnDumpNextFrameToLog(UI::EventParams &e);
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnReloadCheats(UI::EventParams &e);

	// Global settings handlers
	UI::EventReturn OnLanguage(UI::EventParams &e);
	UI::EventReturn OnLanguageChange(UI::EventParams &e);
	UI::EventReturn OnFactoryReset(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
	UI::EventReturn OnChangeNickname(UI::EventParams &e);
	UI::EventReturn OnClearRecents(UI::EventParams &e);
	UI::EventReturn OnRenderingMode(UI::EventParams &e);

	// Temporaries to convert bools to int settings
	bool cap60FPS_;
	int iAlternateSpeedPercent_;
	bool enableReports_;
};

/*
class GlobalSettingsScreen : public UIDialogScreenWithBackground {
public:
	GlobalSettingsScreen() {}

protected:
	virtual void CreateViews();

private:
	// Temporaries to convert bools to other kinds of settings
};*/


class DeveloperToolsScreen : public UIDialogScreenWithBackground {
public:
	DeveloperToolsScreen() {}

protected:
	virtual void CreateViews();
	void CallbackRestoreDefaults(bool yes);

private:
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
	UI::EventReturn OnSysInfo(UI::EventParams &e);
	UI::EventReturn OnLoggingChanged(UI::EventParams &e);
	UI::EventReturn OnLoadLanguageIni(UI::EventParams &e);
	UI::EventReturn OnSaveLanguageIni(UI::EventParams &e);
	UI::EventReturn OnRestoreDefaultSettings(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);

	// Temporary variable.
	bool enableLogging_;
};
