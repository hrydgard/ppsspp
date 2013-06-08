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

#include "gfx_es2/draw_buffer.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/EmuScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"

void GameScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	if (info) {
		tvTitle_ = leftColumn->Add(new TextView(0, info->title, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 10, NONE, NONE)));
	}

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);
	
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);
	rightColumnItems->Add(new Choice("Play"))->OnClick.Handle(this, &GameScreen::OnPlay);
	rightColumnItems->Add(new Choice("Game Settings"))->OnClick.Handle(this, &GameScreen::OnGameSettings);
	rightColumnItems->Add(new Choice("Delete Save Data"))->OnClick.Handle(this, &GameScreen::OnDeleteSaveData);
	rightColumnItems->Add(new Choice("Delete Game"))->OnClick.Handle(this, &GameScreen::OnDeleteGame);
}

void DrawBackground(float alpha);

void GameScreen::DrawBackground() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	::DrawBackground(1.0f);
}

void GameScreen::update(InputState &input) {
	UIScreen::update(input);

	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	if (tvTitle_)
		tvTitle_->SetText(info->title);
}

UI::EventReturn GameScreen::OnPlay(UI::EventParams &e) {
	screenManager()->switchScreen(new EmuScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnGameSettings(UI::EventParams &e) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	if (info && info->paramSFOLoaded) {
		std::string discID = info->paramSFO.GetValueString("DISC_ID");
		screenManager()->push(new GameSettingsScreen(gamePath_, discID));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnDeleteSaveData(UI::EventParams &e) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	if (info) {
		// VERY DANGEROUS, must add confirmation dialog before enabling.
		// info->DeleteAllSaveData();
	}

	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnDeleteGame(UI::EventParams &e) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	if (info) {
		// VERY DANGEROUS
		// info->DeleteGame();
	}

	RecreateViews();
	return UI::EVENT_DONE;
}
