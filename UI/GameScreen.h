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

// Game screen: Allows you to start a game, delete saves, delete the game,
// set game specific settings, etc.
// Uses GameInfoCache heavily to implement the functionality.

class GameScreen : public UIScreen {
public:
	GameScreen(std::string gamePath) : gamePath_(gamePath) {}

	virtual void update(InputState &input);

protected:
	virtual void CreateViews();
	virtual void DrawBackground(UIContext &dc);
	void CallbackDeleteSaveData(bool yes);
	void CallbackDeleteGame(bool yes);

private:
	// Event handlers
	UI::EventReturn OnPlay(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnDeleteSaveData(UI::EventParams &e);
	UI::EventReturn OnDeleteGame(UI::EventParams &e);
	UI::EventReturn OnSwitchBack(UI::EventParams &e);

	std::string gamePath_;

	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextureView *texvGameIcon_;
	UI::TextView *tvTitle_;
	UI::TextView *tvGameSize_;
	UI::TextView *tvSaveDataSize_;
};

inline void NoOpVoidBool(bool) {}

class PromptScreen : public UIScreen {
public:
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText, std::function<void(bool)> callback)
		: message_(message), yesButtonText_(yesButtonText), noButtonText_(noButtonText), callback_(callback) {}

	virtual void CreateViews();
protected:
	virtual void DrawBackground(UIContext &dc);

private:
	UI::EventReturn OnYes(UI::EventParams &e);
	UI::EventReturn OnNo(UI::EventParams &e);


	std::string message_;
	std::string yesButtonText_;
	std::string noButtonText_;
	std::function<void(bool)> callback_;
};
