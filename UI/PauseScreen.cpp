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

#include <algorithm>
#include "i18n/i18n.h"
#include "gfx_es2/draw_buffer.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "ui/ui_screen.h"
#include "thin3d/thin3d.h"

#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUmd.h"

#include "UI/PauseScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ReportScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MainScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GameInfoCache.h"

AsyncImageFileView::AsyncImageFileView(const std::string &filename, UI::ImageSizeMode sizeMode, PrioritizedWorkQueue *wq, UI::LayoutParams *layoutParams)
	: UI::Clickable(layoutParams), canFocus_(true), filename_(filename), color_(0xFFFFFFFF), sizeMode_(sizeMode), textureFailed_(false), fixedSizeW_(0.0f), fixedSizeH_(0.0f) {}

AsyncImageFileView::~AsyncImageFileView() {}

static float DesiredSize(float sz, float contentSize, UI::MeasureSpec spec) {
	float measured;
	UI::MeasureBySpec(sz, contentSize, spec, &measured);
	return measured;
}

void AsyncImageFileView::GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const {
	if (texture_ && texture_->GetTexture()) {
		float texw = (float)texture_->Width();
		float texh = (float)texture_->Height();
		float desiredW = DesiredSize(layoutParams_->width, w, horiz);
		float desiredH = DesiredSize(layoutParams_->height, h, vert);
		switch (sizeMode_) {
		case UI::IS_FIXED:
			w = fixedSizeW_;
			h = fixedSizeH_;
			break;
		case UI::IS_KEEP_ASPECT:
			w = texw;
			h = texh;
			if (desiredW != w || desiredH != h) {
				float aspect = w / h;
				// We need the other dimension based on the desired scale to find the best aspect.
				float desiredWOther = DesiredSize(layoutParams_->height, h * (desiredW / w), vert);
				float desiredHOther = DesiredSize(layoutParams_->width, w * (desiredH / h), horiz);

				float diffW = fabsf(aspect - desiredW / desiredWOther);
				float diffH = fabsf(aspect - desiredH / desiredHOther);
				if (diffW < diffH) {
					w = desiredW;
					h = desiredWOther;
				} else {
					w = desiredHOther;
					h = desiredH;
				}
			}
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
		texture_.reset(nullptr);
	}
}

void AsyncImageFileView::DeviceLost() {
	if (texture_.get())
		texture_->DeviceLost();
}

void AsyncImageFileView::DeviceRestored(Draw::DrawContext *draw) {
	if (texture_.get())
		texture_->DeviceRestored(draw);
}

void AsyncImageFileView::Draw(UIContext &dc) {
	using namespace Draw;
	if (!texture_ && !textureFailed_ && !filename_.empty()) {
		texture_ = CreateTextureFromFile(dc.GetDrawContext(), filename_.c_str(), DETECT, true);
		if (!texture_.get())
			textureFailed_ = true;
	}

	if (HasFocus()) {
		dc.FillRect(dc.theme->itemFocusedStyle.background, bounds_.Expand(3));
	}

	// TODO: involve sizemode
	if (texture_ && texture_->GetTexture()) {
		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, texture_->GetTexture());
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
	bool FillVertical() const override { return false; }
	UI::Size PopupWidth() const override { return 500; }
	bool ShowButtons() const override { return true; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		UI::LinearLayout *content = new UI::LinearLayout(UI::ORIENT_VERTICAL);
		parent->Add(content);
		UI::Margins contentMargins(10, 0);
		content->Add(new AsyncImageFileView(filename_, UI::IS_KEEP_ASPECT, nullptr, new UI::LinearLayoutParams(480, 272, contentMargins)))->SetCanBeFocused(false);
	}

private:
	std::string filename_;
	int slot_;
};

class SaveSlotView : public UI::LinearLayout {
public:
	SaveSlotView(const std::string &gamePath, int slot, UI::LayoutParams *layoutParams = nullptr);

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
		return SaveState::GetSlotDateAsString(gamePath_, slot_);
	}

	UI::Event OnStateLoaded;
	UI::Event OnStateSaved;
	UI::Event OnScreenshotClicked;

private:
	UI::EventReturn OnScreenshotClick(UI::EventParams &e);
	UI::EventReturn OnSaveState(UI::EventParams &e);
	UI::EventReturn OnLoadState(UI::EventParams &e);

	UI::Button *saveStateButton_ = nullptr;
	UI::Button *loadStateButton_ = nullptr;

	int slot_;
	std::string gamePath_;
	std::string screenshotFilename_;
};

SaveSlotView::SaveSlotView(const std::string &gameFilename, int slot, UI::LayoutParams *layoutParams) : UI::LinearLayout(UI::ORIENT_HORIZONTAL, layoutParams), slot_(slot), gamePath_(gameFilename) {
	using namespace UI;

	screenshotFilename_ = SaveState::GenerateSaveSlotFilename(gamePath_, slot, SaveState::SCREENSHOT_EXTENSION);
	PrioritizedWorkQueue *wq = g_gameInfoCache->WorkQueue();
	Add(new Spacer(5));

	AsyncImageFileView *fv = Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, wq, new UI::LayoutParams(82 * 2, 47 * 2)));
	fv->SetOverlayText(StringFromFormat("%d", slot_ + 1));

	I18NCategory *pa = GetI18NCategory("Pause");

	LinearLayout *buttons = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	buttons->SetSpacing(2.0);
	Add(buttons);

	saveStateButton_ = buttons->Add(new Button(pa->T("Save State"), new LinearLayoutParams(0.0, G_VCENTER)));
	saveStateButton_->OnClick.Handle(this, &SaveSlotView::OnSaveState);

	fv->OnClick.Handle(this, &SaveSlotView::OnScreenshotClick);

	if (SaveState::HasSaveInSlot(gamePath_, slot)) {
		loadStateButton_ = buttons->Add(new Button(pa->T("Load State"), new LinearLayoutParams(0.0, G_VCENTER)));
		loadStateButton_->OnClick.Handle(this, &SaveSlotView::OnLoadState);

		std::string dateStr = SaveState::GetSlotDateAsString(gamePath_, slot_);
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

static void AfterSaveStateAction(SaveState::Status status, const std::string &message, void *) {
	if (!message.empty()) {
		osm.Show(message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

UI::EventReturn SaveSlotView::OnLoadState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
	UI::EventParams e2{};
	e2.v = this;
	OnStateLoaded.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn SaveSlotView::OnSaveState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
	UI::EventParams e2{};
	e2.v = this;
	OnStateSaved.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn SaveSlotView::OnScreenshotClick(UI::EventParams &e) {
	UI::EventParams e2{};
	e2.v = this;
	OnScreenshotClicked.Trigger(e2);
	return UI::EVENT_DONE;
}

void GamePauseScreen::update() {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update();

	if (finishNextFrame_) {
		TriggerFinish(DR_CANCEL);
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
	I18NCategory *gr = GetI18NCategory("Graphics");
	I18NCategory *pa = GetI18NCategory("Pause");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0, scrollMargins));
	root_->Add(leftColumn);

	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	leftColumn->Add(leftColumnItems);

	leftColumnItems->Add(new Spacer(0.0));
	leftColumnItems->SetSpacing(10.0);
	for (int i = 0; i < NUM_SAVESLOTS; i++) {
		SaveSlotView *slot = leftColumnItems->Add(new SaveSlotView(gamePath_, i, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		slot->OnStateLoaded.Handle(this, &GamePauseScreen::OnState);
		slot->OnStateSaved.Handle(this, &GamePauseScreen::OnState);
		slot->OnScreenshotClicked.Handle(this, &GamePauseScreen::OnScreenshotClicked);
	}
	leftColumnItems->Add(new Spacer(0.0));

	if (g_Config.iRewindFlipFrequency > 0) {
		UI::Choice *rewindButton = leftColumnItems->Add(new Choice(pa->T("Rewind")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(pa->T("Switch UMD")))->OnClick.Handle(this, &GamePauseScreen::OnSwitchUMD);
	}
	Choice *continueChoice = rightColumnItems->Add(new Choice(pa->T("Continue")));
	root_->SetDefaultFocusView(continueChoice);
	continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	std::string gameId = g_paramSFO.GetDiscID();
	if (g_Config.hasGameConfig(gameId)) {
		rightColumnItems->Add(new Choice(pa->T("Game Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(pa->T("Delete Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
	} else {
		rightColumnItems->Add(new Choice(pa->T("Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(pa->T("Create Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
	}
	if (g_Config.bEnableCheats) {
		rightColumnItems->Add(new Choice(pa->T("Cheats")))->OnClick.Handle(this, &GamePauseScreen::OnCwCheat);
	}

	// TODO, also might be nice to show overall compat rating here?
	// Based on their platform or even cpu/gpu/config.  Would add an API for it.
	if (Reporting::IsSupported() && g_paramSFO.GetValueString("DISC_ID").size()) {
		I18NCategory *rp = GetI18NCategory("Reporting");
		rightColumnItems->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
	}
	rightColumnItems->Add(new Spacer(25.0));
	if (g_Config.bPauseMenuExitsEmulator) {
		I18NCategory *mm = GetI18NCategory("MainMenu");
		rightColumnItems->Add(new Choice(mm->T("Exit")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);
	} else {
		rightColumnItems->Add(new Choice(pa->T("Exit to menu")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);
	}
}

UI::EventReturn GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnState(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

void GamePauseScreen::dialogFinished(const Screen *dialog, DialogResult dr) {
	std::string tag = dialog->tag();
	if (tag == "screenshot" && dr == DR_OK) {
		ScreenshotViewScreen *s = (ScreenshotViewScreen *)dialog;
		int slot = s->GetSlot();
		g_Config.iCurrentStateSlot = slot;
		SaveState::LoadSlot(gamePath_, slot, &AfterSaveStateAction);

		finishNextFrame_ = true;
	} else {
		// There may have been changes to our savestates, so let's recreate.
		RecreateViews();
	}
}

UI::EventReturn GamePauseScreen::OnScreenshotClicked(UI::EventParams &e) {
	SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
	int slot = v->GetSlot();
	g_Config.iCurrentStateSlot = v->GetSlot();
	if (SaveState::HasSaveInSlot(gamePath_, slot)) {
		std::string fn = v->GetScreenshotFilename();
		std::string title = v->GetScreenshotTitle();
		I18NCategory *pa = GetI18NCategory("Pause");
		Screen *screen = new ScreenshotViewScreen(fn, title, v->GetSlot(), pa);
		screenManager()->push(screen);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnExitToMenu(UI::EventParams &e) {
	if (g_Config.bPauseMenuExitsEmulator) {
		System_SendMessage("finish", "");
	} else {
		TriggerFinish(DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnReportFeedback(UI::EventParams &e) {
	screenManager()->push(new ReportScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnRewind(UI::EventParams &e) {
	SaveState::Rewind(&AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnSwitchUMD(UI::EventParams &e) {
	screenManager()->push(new UmdReplaceScreen());
	return UI::EVENT_DONE;
}

void GamePauseScreen::CallbackDeleteConfig(bool yes)
{
	if (yes) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
		g_Config.unloadGameConfig();
		g_Config.deleteGameConfig(info->id);
		info->hasConfig = false;
		screenManager()->RecreateAllViews();
	}
}

UI::EventReturn GamePauseScreen::OnCreateConfig(UI::EventParams &e)
{
	std::string gameId = g_paramSFO.GetDiscID();
	g_Config.createGameConfig(gameId);
	g_Config.changeGameSpecific(gameId);
	g_Config.saveGameConfig(gameId);
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	if (info) {
		info->hasConfig = true;
	}

	screenManager()->topScreen()->RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnDeleteConfig(UI::EventParams &e)
{
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	screenManager()->push(
		new PromptScreen(di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), ga->T("ConfirmDelete"), di->T("Cancel"),
		std::bind(&GamePauseScreen::CallbackDeleteConfig, this, std::placeholders::_1)));

	return UI::EVENT_DONE;
}
