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

#include "base/functional.h"
#include "ui/ui_screen.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"

// Game screen: Allows you to start a game, delete saves, delete the game,
// set game specific settings, etc.
// Uses GameInfoCache heavily to implement the functionality.

class MainScreen : public UIScreenWithBackground {
public:
	MainScreen() {}

protected:
	virtual void CreateViews();
	virtual void update(InputState &input);
	virtual void sendMessage(const char *message, const char *value);

private:
	UI::EventReturn OnGameSelected(UI::EventParams &e);
	// Event handlers
	UI::EventReturn OnLoadFile(UI::EventParams &e);
	UI::EventReturn OnSettings(UI::EventParams &e);
	UI::EventReturn OnCredits(UI::EventParams &e);
	UI::EventReturn OnSupport(UI::EventParams &e);
	UI::EventReturn OnExit(UI::EventParams &e);
};

class GamePauseScreen : public UIScreen {
public:
	GamePauseScreen(const std::string &filename) : UIScreen(), gamePath_(filename) {}
	~GamePauseScreen();
protected:
	virtual void DrawBackground(UIContext &dc);
	virtual void CreateViews();
	virtual void update(InputState &input);

private:
	UI::EventReturn OnMainSettings(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnContinue(UI::EventParams &e);
	UI::EventReturn OnExitToMenu(UI::EventParams &e);

	UI::EventReturn OnSaveState(UI::EventParams &e);
	UI::EventReturn OnLoadState(UI::EventParams &e);

	std::string gamePath_;

	UI::ChoiceStrip *saveSlots_;
};