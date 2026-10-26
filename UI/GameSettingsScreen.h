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
#include "UI/Settings/MainSettingsPages.h"
#include "UI/SettingsPagesDialogScreen.h"
#include "UI/TabbedDialogScreen.h"

class Path;
class SettingsPage;

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public SettingsPagesDialogScreen {
public:
	GameSettingsScreen(const Path &gamePath, std::string gameID = "", bool editThenRestore = false);
	~GameSettingsScreen();

	const char *tag() const override { return "GameSettings"; }

	// Opens on the developer tools page.
	void OpenDeveloperTools();

protected:
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	void CreateTabs() override;

private:
	void PreCreateViews() override;

	std::string gameID_;

	void RestartOrDo(std::function<void()> callback) override;

	SystemPageState systemState_;

	// edit the game-specific settings and restore the global settings after exiting
	bool editGameSpecificThenRestore_ = false;
};

class GestureMappingScreen : public UITabbedBaseDialogScreen {
public:
	GestureMappingScreen(const Path &gamePath) : UITabbedBaseDialogScreen(gamePath) {}

	void CreateTabs() override;
	const char *tag() const override { return "GestureMapping"; }
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
