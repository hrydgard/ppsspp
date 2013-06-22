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

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/EmuScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MenuScreens.h"

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

	leftColumn->Add(new Choice("Back", "", new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle(this, &GameScreen::OnSwitchBack);
	if (info) {
		texvGameIcon_ = leftColumn->Add(new TextureView(0, IS_DEFAULT, new AnchorLayoutParams(144 * 2, 80 * 2, 10, 10, NONE, NONE)));
		tvTitle_ = leftColumn->Add(new TextView(0, info->title, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 200, NONE, NONE)));
		tvGameSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 250, NONE, NONE)));
		tvSaveDataSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 290, NONE, NONE)));
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

void GameScreen::DrawBackground(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, true);
	dc.Flush();

	dc.RebindTexture();
	::DrawBackground(1.0f);
	dc.Flush();

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		dc.Flush();
		dc.RebindTexture();
	}
	/*
	if (ginfo && ginfo->pic0Texture) {
		ginfo->pic0Texture->Bind(0);
		// Pic0 is drawn in the bottom right corner, overlaying pic1.
		float sizeX = dp_xres / 480 * ginfo->pic0Texture->Width();
		float sizeY = dp_yres / 272 * ginfo->pic0Texture->Height();
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 2)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(dp_xres - sizeX, dp_yres - sizeY, dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		dc.RebindTexture();
	}*/
}

void GameScreen::update(InputState &input) {
	UIScreen::update(input);

	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	if (tvTitle_)
		tvTitle_->SetText(info->title);
	if (info->iconTexture && texvGameIcon_)	{
		texvGameIcon_->SetTexture(info->iconTexture);
		uint32_t color = whiteAlpha(ease((time_now_d() - info->timeIconWasLoaded) * 3));
		texvGameIcon_->SetColor(color);
	}

	if (info->gameSize) {
		char temp[256];
		sprintf(temp, "Game: %1.1f MB", (float)(info->gameSize) / 1024.f / 1024.f);
		tvGameSize_->SetText(temp);
		sprintf(temp, "SaveData: %1.2f MB", (float)(info->saveDataSize) / 1024.f / 1024.f);
		tvSaveDataSize_->SetText(temp);
	}
}

UI::EventReturn GameScreen::OnSwitchBack(UI::EventParams &e) {
	screenManager()->switchScreen(new MenuScreen());
	return UI::EVENT_DONE;
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
		screenManager()->push(
			new PromptScreen("Do you really want to delete all\nyour save data for this game?", "Delete Savedata", "Cancel",
			std::bind(&GameScreen::CallbackDeleteSaveData, this, placeholder::_1)));
	}

	RecreateViews();
	return UI::EVENT_DONE;
}

void GameScreen::CallbackDeleteSaveData(bool yes) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, false);
	if (yes) {
		info->DeleteAllSaveData();
	}
}

UI::EventReturn GameScreen::OnDeleteGame(UI::EventParams &e) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	if (info) {
		screenManager()->push(
			new PromptScreen("Do you really want to delete all\nthis game entirely? You can't undo this.", "Delete Game", "Cancel",
			std::bind(&GameScreen::CallbackDeleteGame, this, placeholder::_1)));
	}

	return UI::EVENT_DONE;
}

void GameScreen::CallbackDeleteGame(bool yes) {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, false);
	if (yes) {
		info->DeleteGame();
		g_gameInfoCache.Clear();
		screenManager()->switchScreen(new MenuScreen());
	}
}


void DrawBackground(float alpha);

void PromptScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
}
void PromptScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new TextView(0, message_, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 10, NONE, NONE)));

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumnItems);
	rightColumnItems->Add(new Choice(yesButtonText_))->OnClick.Handle(this, &PromptScreen::OnYes);
	if (noButtonText_ != "")
		rightColumnItems->Add(new Choice(noButtonText_))->OnClick.Handle(this, &PromptScreen::OnNo);
}

UI::EventReturn PromptScreen::OnYes(UI::EventParams &e) {
	callback_(true);
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn PromptScreen::OnNo(UI::EventParams &e) {
	callback_(false);
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}
