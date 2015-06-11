// Copyright (c) 2014- PPSSPP Project.

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

#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_screen.h"
#include "thin3d/thin3d.h"

#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUCommon.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUmd.h"

#include "UI/PauseScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ReportScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameInfoCache.h"

#include "gfx_es2/draw_buffer.h"
#include "ui/ui_context.h"

void AsyncImageFileView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO: involve sizemode
	if (texture_) {
		float texw = (float)texture_->Width();
		float texh = (float)texture_->Height();
		switch (sizeMode_) {
		case UI::IS_FIXED:
			w = fixedSizeW_;
			h = fixedSizeH_;
			break;
		case UI::IS_DEFAULT:
		default:
			w = texw;
			h = texh;
			break;
		}
	} else {
		w = 16;
		h = 16;
	}
}

void AsyncImageFileView::SetFilename(std::string filename) {
	if (filename_ != filename) {
		textureFailed_ = false;
		filename_ = filename;
		if (texture_) {
			texture_->Release();
			texture_ = nullptr;
		}
	}
}

void AsyncImageFileView::Draw(UIContext &dc) {
	if (!texture_ && !textureFailed_ && !filename_.empty()) {
		texture_ = dc.GetThin3DContext()->CreateTextureFromFile(filename_.c_str(), DETECT);
		if (!texture_)
			textureFailed_ = true;
	}

	if (HasFocus()) {
		dc.FillRect(dc.theme->itemFocusedStyle.background, bounds_.Expand(3));
	}

	// TODO: involve sizemode
	if (texture_) {
		dc.Flush();
		dc.GetThin3DContext()->SetTexture(0, texture_);
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, color_);
		dc.Flush();
		dc.RebindTexture();
		if (!text_.empty()) {
			dc.DrawText(text_.c_str(), bounds_.centerX()+1, bounds_.centerY()+1, 0x80000000, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
			dc.DrawText(text_.c_str(), bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
		}
	} else {
		if (!filename_.empty()) {
			// draw a black rectangle to represent the missing screenshot.
			dc.FillRect(UI::Drawable(0xFF000000), GetBounds());
		} else {
			// draw a dark gray rectangle to represent no save state.
			dc.FillRect(UI::Drawable(0x50202020), GetBounds());
		}
		if (!text_.empty()) {
			dc.DrawText(text_.c_str(), bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
		}
	}
}

class ScreenshotViewScreen : public PopupScreen {
public:
	ScreenshotViewScreen(std::string filename, std::string title, int slot, I18NCategory *i18n)
		: PopupScreen(title, i18n->T("Load State"), "Back"), filename_(filename), slot_(slot) {}   // PopupScreen will translate Back on its own

	int GetSlot() const {
		return slot_;
	}

	std::string tag() const override {
		return "screenshot";
	}

protected:
	virtual bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }

	virtual void CreatePopupContents(UI::ViewGroup *parent) {
		// TODO: Find an appropriate size for the image view
		parent->Add(new AsyncImageFileView(filename_, UI::IS_DEFAULT, NULL, new UI::LayoutParams(480, 272)))->SetCanBeFocused(false);
	}

private:
	std::string filename_;
	int slot_;
};

class SaveSlotView : public UI::LinearLayout {
public:
	SaveSlotView(int slot, UI::LayoutParams *layoutParams = nullptr);

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500; h = 90;
	}

	void Draw(UIContext &dc) override;

	int GetSlot() const {
		return slot_;
	}

	std::string GetScreenshotFilename() const {
		return screenshotFilename_;
	}

	std::string GetScreenshotTitle() const {
		return SaveState::GetSlotDateAsString(slot_);
	}

	UI::Event OnStateLoaded;
	UI::Event OnStateSaved;
	UI::Event OnScreenshotClicked;

private:
	UI::EventReturn OnScreenshotClick(UI::EventParams &e);
	UI::EventReturn OnSaveState(UI::EventParams &e);
	UI::EventReturn OnLoadState(UI::EventParams &e);

	UI::Button *saveStateButton_;
	UI::Button *loadStateButton_;

	int slot_;
	std::string screenshotFilename_;
};

SaveSlotView::SaveSlotView(int slot, UI::LayoutParams *layoutParams) : UI::LinearLayout(UI::ORIENT_HORIZONTAL, layoutParams), slot_(slot) {
	using namespace UI;

	screenshotFilename_ = SaveState::GenerateSaveSlotFilename(slot, "jpg");
	PrioritizedWorkQueue *wq = g_gameInfoCache.WorkQueue();
	Add(new Spacer(5));

	AsyncImageFileView *fv = Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, wq, new UI::LayoutParams(82 * 2, 47 * 2)));
	fv->SetOverlayText(StringFromFormat("%i", slot_ + 1));

	I18NCategory *i = GetI18NCategory("Pause");

	LinearLayout *buttons = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	buttons->SetSpacing(2.0);
	Add(buttons);

	saveStateButton_ = buttons->Add(new Button(i->T("Save State"), new LinearLayoutParams(0.0, G_VCENTER)));
	saveStateButton_->OnClick.Handle(this, &SaveSlotView::OnSaveState);

	fv->OnClick.Handle(this, &SaveSlotView::OnScreenshotClick);

	if (SaveState::HasSaveInSlot(slot)) {
		loadStateButton_ = buttons->Add(new Button(i->T("Load State"), new LinearLayoutParams(0.0, G_VCENTER)));
		loadStateButton_->OnClick.Handle(this, &SaveSlotView::OnLoadState);

		std::string dateStr = SaveState::GetSlotDateAsString(slot_);
		std::vector<std::string> dateStrs;
		SplitString(dateStr, ' ', dateStrs);
		if (!dateStrs.empty() && !dateStrs[0].empty()) {
			LinearLayout *strs = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
			Add(strs);
			for (size_t i = 0; i < dateStrs.size(); i++) {
				strs->Add(new TextView(dateStrs[i], new LinearLayoutParams(0.0, G_VCENTER)))->SetShadow(true);
			}
		}
	} else {
		fv->SetFilename("");
	}
}

void SaveSlotView::Draw(UIContext &dc) {
	if (g_Config.iCurrentStateSlot == slot_) {
		dc.FillRect(UI::Drawable(0x70000000), GetBounds().Expand(3));
		dc.FillRect(UI::Drawable(0x70FFFFFF), GetBounds().Expand(3));
	}
	UI::LinearLayout::Draw(dc);
}

UI::EventReturn SaveSlotView::OnLoadState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::LoadSlot(slot_, SaveState::Callback(), 0);
	UI::EventParams e2;
	e2.v = this;
	OnStateLoaded.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn SaveSlotView::OnSaveState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::SaveSlot(slot_, SaveState::Callback(), 0);
	UI::EventParams e2;
	e2.v = this;
	OnStateSaved.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn SaveSlotView::OnScreenshotClick(UI::EventParams &e) {
	UI::EventParams e2;
	e2.v = this;
	OnScreenshotClicked.Trigger(e2);
	return UI::EVENT_DONE;
}

void GamePauseScreen::update(InputState &input) {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update(input);

	if (finishNextFrame_) {
		screenManager()->finishDialog(this, DR_CANCEL);
		finishNextFrame_ = false;
	}
}

GamePauseScreen::~GamePauseScreen() {
	__DisplaySetWasPaused();
}

void GamePauseScreen::CreateViews() {
	static const int NUM_SAVESLOTS = 5;

	using namespace UI;
	Margins scrollMargins(0, 20, 0, 0);
	Margins actionMenuMargins(0, 20, 15, 0);
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *i = GetI18NCategory("Pause");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0, scrollMargins));
	root_->Add(leftColumn);

	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	leftColumn->Add(leftColumnItems);

	leftColumnItems->Add(new Spacer(0.0));
	leftColumnItems->SetSpacing(10.0);
	for (int i = 0; i < NUM_SAVESLOTS; i++) {
		SaveSlotView *slot = leftColumnItems->Add(new SaveSlotView(i, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		slot->OnStateLoaded.Handle(this, &GamePauseScreen::OnState);
		slot->OnStateSaved.Handle(this, &GamePauseScreen::OnState);
		slot->OnScreenshotClicked.Handle(this, &GamePauseScreen::OnScreenshotClicked);
	}
	leftColumnItems->Add(new Spacer(0.0));

	if (g_Config.iRewindFlipFrequency > 0) {
		UI::Choice *rewindButton = leftColumnItems->Add(new Choice(i->T("Rewind")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(i->T("Switch UMD")))->OnClick.Handle(this, &GamePauseScreen::OnSwitchUMD);
	}
	Choice *continueChoice = rightColumnItems->Add(new Choice(i->T("Continue")));
	root_->SetDefaultFocusView(continueChoice);
	continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	std::string gameId = g_paramSFO.GetValueString("DISC_ID");
	if (g_Config.hasGameConfig(gameId)) {
		rightColumnItems->Add(new Choice(i->T("Game Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(i->T("Delete Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
	} else {
		rightColumnItems->Add(new Choice(i->T("Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(i->T("Create Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
	}
	if (g_Config.bEnableCheats) {
		rightColumnItems->Add(new Choice(i->T("Cheats")))->OnClick.Handle(this, &GamePauseScreen::OnCwCheat);
	}

	// TODO, also might be nice to show overall compat rating here?
	// Based on their platform or even cpu/gpu/config.  Would add an API for it.
	if (Reporting::IsEnabled()) {
		I18NCategory *rp = GetI18NCategory("Reporting");
		rightColumnItems->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
	}
	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(i->T("Exit to menu")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);
}

UI::EventReturn GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
}

void GamePauseScreen::onFinish(DialogResult result) {
	// Do we really always need to "gpu->Resized" here?
	if (gpu)
		gpu->Resized();
	Reporting::UpdateConfig();
}

UI::EventReturn GamePauseScreen::OnState(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

void GamePauseScreen::dialogFinished(const Screen *dialog, DialogResult dr) {
	std::string tag = dialog->tag();
	if (tag == "screenshot" && dr == DR_OK) {
		ScreenshotViewScreen *s = (ScreenshotViewScreen *)dialog;
		int slot = s->GetSlot();
		g_Config.iCurrentStateSlot = slot;
		SaveState::LoadSlot(slot, SaveState::Callback(), 0);

		finishNextFrame_ = true;
	}
}

UI::EventReturn GamePauseScreen::OnScreenshotClicked(UI::EventParams &e) {
	SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
	int slot = v->GetSlot();
	g_Config.iCurrentStateSlot = v->GetSlot();
	if (SaveState::HasSaveInSlot(slot)) {
		std::string fn = v->GetScreenshotFilename();
		std::string title = v->GetScreenshotTitle();
		I18NCategory *p = GetI18NCategory("Pause");
		Screen *screen = new ScreenshotViewScreen(fn, title, v->GetSlot(), p);
		screenManager()->push(screen);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnExitToMenu(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnReportFeedback(UI::EventParams &e) {
	screenManager()->push(new ReportScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnRewind(UI::EventParams &e) {
	SaveState::Rewind(SaveState::Callback(), 0);

	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnSwitchUMD(UI::EventParams &e) {
	screenManager()->push(new UmdReplaceScreen());
	return UI::EVENT_DONE;
}

void GamePauseScreen::CallbackDeleteConfig(bool yes)
{
	if (yes) {
		GameInfo *info = g_gameInfoCache.GetInfo(NULL, gamePath_, 0);
		g_Config.unloadGameConfig();
		g_Config.deleteGameConfig(info->id);
		screenManager()->RecreateAllViews();
	}
}

UI::EventReturn GamePauseScreen::OnCreateConfig(UI::EventParams &e)
{
	std::string gameId = g_paramSFO.GetValueString("DISC_ID");
	g_Config.createGameConfig(gameId);
	g_Config.changeGameSpecific(gameId);
	g_Config.saveGameConfig(gameId);

	screenManager()->topScreen()->RecreateViews();
	return UI::EVENT_DONE;
}
UI::EventReturn GamePauseScreen::OnDeleteConfig(UI::EventParams &e)
{
	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	screenManager()->push(
		new PromptScreen(d->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), ga->T("ConfirmDelete"), d->T("Cancel"),
		std::bind(&GamePauseScreen::CallbackDeleteConfig, this, placeholder::_1)));

	return UI::EVENT_DONE;
}


void GamePauseScreen::sendMessage(const char *message, const char *value) {
	// Since the language message isn't allowed to be in native, we have to have add this
	// to every screen which directly inherits from UIScreen(which are few right now, luckily).
	if (!strcmp(message, "language")) {
		screenManager()->RecreateAllViews();
	}
}
