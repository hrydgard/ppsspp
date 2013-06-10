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
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/MenuScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/ui_atlas.h"
#include "Core/Config.h"

class GameButton : public UI::Clickable {
public:
	GameButton(const std::string &gamePath, UI::LayoutParams *layoutParams = 0) 
		: UI::Clickable(layoutParams), gamePath_(gamePath) {}

	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const {
		w = 144;
		h = 80;
	}
	const std::string &GamePath() const { return gamePath_; }
	
private:
	std::string gamePath_;
};

void GameButton::Draw(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, false);
	Texture *texture = 0;
	u32 color = 0;
	if (ginfo->iconTexture) {
		texture = ginfo->iconTexture;
	} else {
		return;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = bounds_.w;
	int h = bounds_.h;

	if (texture) {
		color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));

		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// TODO: Add a param to specify fit?  The below assumes it's never too wide.
		float nw = h * tw / th;
		x += (w - nw) / 2.0f;
		w = nw;
	}

	int txOffset = down_ ? 4 : 0;

	// Render button
	int dropsize = 10;
	if (texture) {
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
		}
		dc.Draw()->DrawImage4Grid(I_DROP_SHADOW, x - dropsize, y, x+w + dropsize, y+h+dropsize*1.5, 	alphaMul(color, 0.5f), 1.0f);
		dc.Draw()->Flush();
	}

	if (texture) {
		texture->Bind(0);
		dc.Draw()->DrawTexRect(x, y, x+w, y+h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	} else {
		dc.FillRect(dc.theme->buttonStyle.background, bounds_);
		dc.Draw()->Flush();
		Texture::Unbind();
	}
	
	dc.RebindTexture();
}

class GameBrowser : public UI::GridLayout {
public:
	GameBrowser(std::string path, bool allowBrowsing, UI::LayoutParams *layoutParams = 0) 
		: UI::GridLayout(UI::GridLayoutSettings(150, 85), layoutParams), path_(path), allowBrowsing_(allowBrowsing) {
		Refresh();
	}

	UI::Event OnChoice;
	
	virtual void Update(const InputState &input_state) {
		UI::GridLayout::Update(input_state);
		OnChoice.Update();
	}

private:
	void Refresh();

	UI::EventReturn GameButtonClick(UI::EventParams &e);

	bool allowBrowsing_;
	std::string path_;
};

void GameBrowser::Refresh() {
	// Kill all the buttons
	for (size_t i = 0; i < views_.size(); i++) {
		delete views_[i];
	}
	views_.clear();

	// Find games in the current directory and create new ones.
	std::vector<std::string> games;
	
	if (path_ == "<RECENT>")
		games = g_Config.recentIsos;
	else {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(path_.c_str(), &fileInfo);
		for (size_t i = 0; i < fileInfo.size(); i++) {
			games.push_back(fileInfo[i].fullName);
		}
	}

	for (size_t i = 0; i < games.size(); i++) {
		Add(new GameButton(games[i]))->OnClick.Handle(this, &GameBrowser::GameButtonClick);
	}
}

UI::EventReturn GameBrowser::GameButtonClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(1.0));
	root_->Add(leftColumn);

	GameBrowser *tabRecentGames = new GameBrowser("<RECENT>", false);
	GameBrowser *tabAllGames = new GameBrowser(g_Config.currentDirectory, true);
	GameBrowser *tabHomebrew = new GameBrowser(g_Config.memCardDirectory + "PSP/GAME/", false);
	
	leftColumn->AddTab("Recent", tabRecentGames);
	leftColumn->AddTab("Games", tabAllGames);
	leftColumn->AddTab("Homebrew & Demos", tabHomebrew);

	tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelected);
	tabAllGames->OnChoice.Handle(this, &MainScreen::OnGameSelected);
	tabHomebrew->OnChoice.Handle(this, &MainScreen::OnGameSelected);

/*
	if (info) {
		texvGameIcon_ = leftColumn->Add(new TextureView(0, IS_DEFAULT, new AnchorLayoutParams(144 * 2, 80 * 2, 10, 10, NONE, NONE)));
		tvTitle_ = leftColumn->Add(new TextView(0, info->title, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 200, NONE, NONE)));
		tvGameSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 250, NONE, NONE)));
		tvSaveDataSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 290, NONE, NONE)));
	}
	*/

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);
	
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);
	rightColumnItems->Add(new Choice("Settings"))->OnClick.Handle(this, &MainScreen::OnSettings);
	rightColumnItems->Add(new Choice("Credits"))->OnClick.Handle(this, &MainScreen::OnCredits);
	rightColumnItems->Add(new Choice("Support PPSSPP"))->OnClick.Handle(this, &MainScreen::OnSupport);
}

void DrawBackground(float alpha);

void MainScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
	dc.Flush();
}

UI::EventReturn MainScreen::OnGameSelected(UI::EventParams &e) {
	screenManager()->switchScreen(new GameScreen(e.s));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnSettings(UI::EventParams &e) {
	screenManager()->push(new SettingsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnSupport(UI::EventParams &e) {
	return UI::EVENT_DONE;
}