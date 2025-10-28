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

#include "UI/BaseScreens.h"
#include "Common/UI/UIScreen.h"
#include "Common/File/Path.h"

#include "UI/GameInfoCache.h"

class NoticeView;

// Game screen: Allows you to start a game, delete saves, delete the game,
// set game specific settings, etc.

// Uses GameInfoCache heavily to implement the functionality.
// Should possibly merge this with the PauseScreen.

class GameScreen : public UIBaseDialogScreen {
public:
	GameScreen(const Path &gamePath, bool inGame);
	~GameScreen();

	void update() override;

	ScreenRenderFlags render(ScreenRenderMode mode) override;

	const char *tag() const override { return "Game"; }

protected:
	void CreateViews() override;

private:
	// Event handlers
	void OnPlay(UI::EventParams &e);
	void OnGameSettings(UI::EventParams &e);
	void OnDeleteSaveData(UI::EventParams &e);
	void OnDeleteGame(UI::EventParams &e);
	void OnSwitchBack(UI::EventParams &e);
	void OnRemoveFromRecent(UI::EventParams &e);
	void OnCreateConfig(UI::EventParams &e);
	void OnDeleteConfig(UI::EventParams &e);
	void OnCwCheat(UI::EventParams &e);
	void OnSetBackground(UI::EventParams &e);

	std::string CRC32string;

	bool isHomebrew_ = false;
	bool inGame_ = false;

	// Keep track of progressive loading of metadata.
	GameInfoFlags knownFlags_ = GameInfoFlags::EMPTY;

	bool knownHasCRC_ = false;
};
