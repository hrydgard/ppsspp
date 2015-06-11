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

#include <algorithm>

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "math/curves.h"
#include "util/text/utf8.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/SavedataScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/ui_atlas.h"

#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Core/System.h"

class SavedataPopupScreen : public PopupScreen {
public:
	SavedataPopupScreen(std::string savePath, std::string title) : PopupScreen(title), path_(savePath) {
	}

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		GameInfo *info = g_gameInfoCache.GetInfo(screenManager()->getThin3DContext(), path_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);
		LinearLayout *root = new LinearLayout(ORIENT_VERTICAL);
		parent->Add(root);
		if (!info)
			return;
		root->Add(new InfoItem("Name", info->title));
		root->Add(new InfoItem("Size", StringFromFormat("%d", info->gameSize)));
	}

private:
	std::string path_;
};


class SavedataButton : public UI::Clickable {
public:
	SavedataButton(const std::string &gamePath, UI::LayoutParams *layoutParams = 0)
		: UI::Clickable(layoutParams), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500;
		h = 50;
	}

	const std::string &GamePath() const { return gamePath_; }

	void SetHoldEnabled(bool hold) {
		holdEnabled_ = hold;
	}

	void Touch(const TouchInput &input) override {
		UI::Clickable::Touch(input);
		hovering_ = bounds_.Contains(input.x, input.y);
	}

	void FocusChanged(int focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
	}

	UI::Event OnHoldClick;
	UI::Event OnHighlight;

private:
	std::string gamePath_;
	std::string title_;

	double holdStart_;
	bool holdEnabled_;
	bool hovering_;
};

void SavedataButton::Draw(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(dc.GetThin3DContext(), gamePath_, 0);
	Thin3DTexture *texture = 0;
	u32 color = 0, shadowColor = 0;
	using namespace UI;

	if (ginfo->iconTexture) {
		texture = ginfo->iconTexture;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = 144;
	int h = bounds_.h;

	UI::Style style = dc.theme->itemStyle;
	if (down_)
		style = dc.theme->itemDownStyle;

	h = 50;
	if (HasFocus())
		style = down_ ? dc.theme->itemDownStyle : dc.theme->itemFocusedStyle;

	Drawable bg = style.background;

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.FillRect(bg, bounds_);
	dc.Draw()->Flush();

	if (texture) {
		color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
		shadowColor = blackAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// TODO: Add a param to specify fit?  The below assumes it's never too wide.
		float nw = h * tw / th;
		x += (w - nw) / 2.0f;
		w = nw;
	}

	int txOffset = down_ ? 4 : 0;
	txOffset = 0;

	Bounds overlayBounds = bounds_;

	// Render button
	int dropsize = 10;
	if (texture) {
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
			overlayBounds.y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			float pulse = sinf(time_now() * 7.0f) * 0.25 + 0.8;
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize*1.5f, y - dropsize*1.5f, x + w + dropsize*1.5f, y + h + dropsize*1.5f, alphaMul(color, pulse), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize, y - dropsize*0.5f, x + w + dropsize, y + h + dropsize*1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}
	}

	if (texture) {
		dc.Draw()->Flush();
		dc.GetThin3DContext()->SetTexture(0, texture);
		if (holdStart_ != 0.0) {
			double time_held = time_now_d() - holdStart_;
			int holdFrameCount = (int)(time_held * 60.0f);
			if (holdFrameCount > 60) {
				// Blink before launching by holding
				if (((holdFrameCount >> 3) & 1) == 0)
					color = darkenColor(color);
			}
		}
		dc.Draw()->DrawTexRect(x, y, x + w, y + h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	char discNumInfo[8];
	if (ginfo->disc_total > 1)
		sprintf(discNumInfo, "-DISC%d", ginfo->disc_number);
	else
		strcpy(discNumInfo, "");

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.SetFontStyle(dc.theme->uiFont);

	float tw, th;
	dc.Draw()->Flush();
	dc.PushScissor(bounds_);
	if (title_.empty() && !ginfo->title.empty()) {
		title_ = ReplaceAll(ginfo->title + discNumInfo, "&", "&&");
		title_ = ReplaceAll(title_, "\n", " ");
		title_ = ReplaceAll(title_, "\r", " ");
	}

	dc.MeasureText(dc.GetFontStyle(), title_.c_str(), &tw, &th, 0);

	int availableWidth = bounds_.w - 150;
	float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

	float tx = 150;
	if (availableWidth < tw) {
		tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
		Bounds tb = bounds_;
		tb.x = bounds_.x + 150;
		tb.w = bounds_.w - 150;
		dc.PushScissor(tb);
	}
	dc.DrawText(title_.c_str(), bounds_.x + tx, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
	if (availableWidth < tw) {
		dc.PopScissor();
	}
	dc.Draw()->Flush();
	dc.PopScissor();

	dc.RebindTexture();
}

SavedataBrowser::SavedataBrowser(std::string path, UI::LayoutParams *layoutParams)
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams), gameList_(0), path_(path) {
	Refresh();
}

void SavedataBrowser::Refresh() {
	using namespace UI;

	// Kill all the contents
	Clear();

	Add(new Spacer(1.0f));
	I18NCategory *m = GetI18NCategory("MainMenu");

	UI::LinearLayout *gl = new UI::LinearLayout(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	gl->SetSpacing(4.0f);
	gameList_ = gl;
	Add(gameList_);

	// Find games in the current directory and create new ones.
	std::vector<SavedataButton *> savedataButtons;

	std::vector<FileInfo> fileInfo;

	getFilesInDir(path_.c_str(), &fileInfo, "ppst:");

	for (size_t i = 0; i < fileInfo.size(); i++) {
		bool isState = !fileInfo[i].isDirectory;
		bool isSaveData = false;
		
		if (!isState && File::Exists(path_ + fileInfo[i].name + "/PARAM.SFO"))
			isSaveData = true;

		if (isSaveData) {
			savedataButtons.push_back(new SavedataButton(fileInfo[i].fullName, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)));
		} else if (isState) {
			savedataButtons.push_back(new SavedataButton(fileInfo[i].fullName, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	}

	for (size_t i = 0; i < savedataButtons.size(); i++) {
		SavedataButton *b = gameList_->Add(savedataButtons[i]);
		b->OnClick.Handle(this, &SavedataBrowser::SavedataButtonClick);
	}
}

UI::EventReturn SavedataBrowser::SavedataButtonClick(UI::EventParams &e) {
	SavedataButton *button = static_cast<SavedataButton *>(e.v);
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

SavedataScreen::SavedataScreen(std::string gamePath) : UIScreenWithGameBackground(gamePath) {
}

void SavedataScreen::CreateViews() {
	using namespace UI;
	I18NCategory *m = GetI18NCategory("MainMenu");
	I18NCategory *di = GetI18NCategory("Dialog");
	std::string savedata_dir = GetSysDirectory(DIRECTORY_SAVEDATA);
	std::string savestate_dir = GetSysDirectory(DIRECTORY_SAVESTATE);

	gridStyle_ = false;
	root_ = new LinearLayout(ORIENT_VERTICAL);

	TabHolder *tabs = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	browser_ = scroll->Add(new SavedataBrowser(savedata_dir, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	browser_->OnChoice.Handle(this, &SavedataScreen::OnSavedataButtonClick);

	tabs->AddTab(m->T("Save Data"), scroll);

	ScrollView *scroll2 = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	SavedataBrowser *browser2 = scroll2->Add(new SavedataBrowser(savestate_dir));
	browser2->OnChoice.Handle(this, &SavedataScreen::OnSavedataButtonClick);
	tabs->AddTab(m->T("Save States"), scroll2);

	root_->Add(tabs);
	root_->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

UI::EventReturn SavedataScreen::OnSavedataButtonClick(UI::EventParams &e) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(screenManager()->getThin3DContext(), e.s, 0);
	screenManager()->push(new SavedataPopupScreen(e.s, ginfo->title));
	// the game path: e.s;
	return UI::EVENT_DONE;
}
