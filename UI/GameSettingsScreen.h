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
class GameSettingsScreen : public UIScreenWithBackground {
public:
	GameSettingsScreen(std::string gamePath, std::string gameID = "") : gamePath_(gamePath), gameID_(gameID) {}

	virtual void update(InputState &input);

protected:
	virtual void CreateViews();

private:
	std::string gamePath_, gameID_;

	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextView *tvTitle_;
	UI::TextView *tvGameSize_;

	// Event handlers
	UI::EventReturn OnDownloadPlugin(UI::EventParams &e);

	// Temporaries to convert bools to int settings
	bool cap60FPS_;
};

// TODO: Move to its own file.
class GlobalSettingsScreen : public UIScreenWithBackground {
public:
	GlobalSettingsScreen() {}

protected:
	virtual void CreateViews();

private:
	// Event handlers
	UI::EventReturn OnLanguage(UI::EventParams &e);
	UI::EventReturn OnFactoryReset(UI::EventParams &e);
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);

	// Temporaries to convert bools to other kinds of settings
	bool enableReports_;
};

class DeveloperToolsScreen : public UIScreenWithBackground {
public:
	DeveloperToolsScreen() {}

protected:
	virtual void CreateViews();

private:
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
};