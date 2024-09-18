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

#include <functional>

#include "UI/MiscScreens.h"
#include "Common/UI/UIScreen.h"
#include "Common/File/Path.h"

class NoticeView;

// Game screen: Allows you to start a game, delete saves, delete the game,
// set game specific settings, etc.

// Uses GameInfoCache heavily to implement the functionality.
// Should possibly merge this with the PauseScreen.

class GameScreen : public UIDialogScreenWithGameBackground {
public:
	GameScreen(const Path &gamePath, bool inGame);
	~GameScreen();

	void update() override;

	ScreenRenderFlags render(ScreenRenderMode mode) override;

	const char *tag() const override { return "Game"; }

protected:
	void CreateViews() override;
	void CallbackDeleteConfig(bool yes);
	void CallbackDeleteSaveData(bool yes);
	void CallbackDeleteGame(bool yes);
	bool isRecentGame(const Path &gamePath);

private:
	UI::Choice *AddOtherChoice(UI::Choice *choice);

	// Event handlers
	UI::EventReturn OnPlay(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnDeleteSaveData(UI::EventParams &e);
	UI::EventReturn OnDeleteGame(UI::EventParams &e);
	UI::EventReturn OnSwitchBack(UI::EventParams &e);
	UI::EventReturn OnRemoveFromRecent(UI::EventParams &e);
	UI::EventReturn OnShowInFolder(UI::EventParams &e);
	UI::EventReturn OnCreateConfig(UI::EventParams &e);
	UI::EventReturn OnDeleteConfig(UI::EventParams &e);
	UI::EventReturn OnCwCheat(UI::EventParams &e);
	UI::EventReturn OnSetBackground(UI::EventParams &e);
	UI::EventReturn OnDoCRC32(UI::EventParams& e);

	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextView *tvTitle_ = nullptr;
	UI::TextView *tvGameSize_ = nullptr;
	UI::TextView *tvSaveDataSize_ = nullptr;
	UI::TextView *tvInstallDataSize_ = nullptr;
	UI::TextView *tvRegion_ = nullptr;
	UI::TextView *tvPlayTime_ = nullptr;
	UI::TextView *tvCRC_ = nullptr;
	UI::TextView *tvID_ = nullptr;
	UI::Button *tvCRCCopy_ = nullptr;
	NoticeView *tvVerified_ = nullptr;

	UI::Choice *btnGameSettings_ = nullptr;
	UI::Choice *btnCreateGameConfig_ = nullptr;
	UI::Choice *btnDeleteGameConfig_ = nullptr;
	UI::Choice *btnDeleteSaveData_ = nullptr;
	UI::Choice *btnSetBackground_ = nullptr;

	UI::Choice *btnCalcCRC_ = nullptr;

	std::vector<UI::Choice *> otherChoices_;
	std::vector<Path> saveDirs;
	std::string CRC32string;

	bool isHomebrew_ = false;
	bool inGame_ = false;
};
