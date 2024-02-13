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
#include <memory>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/UIScreen.h"
#include "Common/GPU/thin3d.h"

#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/UI/AsyncImageFileView.h"

#include "Core/KeyMap.h"
#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/RetroAchievements.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUmd.h"

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

#include "UI/PauseScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ReportScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MainScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GameInfoCache.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RetroAchievementScreens.h"

static void AfterSaveStateAction(SaveState::Status status, std::string_view message, void *) {
	if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
		g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR,
			message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

class ScreenshotViewScreen : public PopupScreen {
public:
	ScreenshotViewScreen(const Path &filename, std::string title, int slot, Path gamePath)
		: PopupScreen(title), filename_(filename), slot_(slot), gamePath_(gamePath) {}   // PopupScreen will translate Back on its own

	int GetSlot() const {
		return slot_;
	}

	const char *tag() const override { return "ScreenshotView"; }

protected:
	bool FillVertical() const override { return false; }
	UI::Size PopupWidth() const override { return 500; }
	bool ShowButtons() const override { return true; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
		LinearLayout *content = new LinearLayout(ORIENT_VERTICAL);
		Margins contentMargins(10, 0);
		content->Add(new AsyncImageFileView(filename_, IS_KEEP_ASPECT, new LinearLayoutParams(480, 272, contentMargins)))->SetCanBeFocused(false);

		GridLayoutSettings gridsettings(240, 64, 5);
		gridsettings.fillCells = true;
		GridLayout *grid = content->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

		Choice *back = new Choice(di->T("Back"));
		Choice *undoButton = new Choice(pa->T("Undo last save"));
		undoButton->SetEnabled(SaveState::HasUndoSaveInSlot(gamePath_, slot_));

		grid->Add(new Choice(pa->T("Save State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnSaveState);
		grid->Add(new Choice(pa->T("Load State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnLoadState);
		grid->Add(back)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		grid->Add(undoButton)->OnClick.Handle(this, &ScreenshotViewScreen::OnUndoState);

		scroll->Add(content);
		parent->Add(scroll);
	}

private:
	UI::EventReturn OnSaveState(UI::EventParams &e);
	UI::EventReturn OnLoadState(UI::EventParams &e);
	UI::EventReturn OnUndoState(UI::EventParams &e);

	Path filename_;
	Path gamePath_;
	int slot_;
};

UI::EventReturn ScreenshotViewScreen::OnSaveState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
	TriggerFinish(DR_OK); //OK will close the pause screen as well
	return UI::EVENT_DONE;
}

UI::EventReturn ScreenshotViewScreen::OnLoadState(UI::EventParams &e) {
	g_Config.iCurrentStateSlot = slot_;
	SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn ScreenshotViewScreen::OnUndoState(UI::EventParams &e) {
	SaveState::UndoSaveSlot(gamePath_, slot_);
	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

class SaveSlotView : public UI::LinearLayout {
public:
	SaveSlotView(const Path &gamePath, int slot, bool vertical, UI::LayoutParams *layoutParams = nullptr);

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500; h = 90;
	}

	void Draw(UIContext &dc) override;

	int GetSlot() const {
		return slot_;
	}

	Path GetScreenshotFilename() const {
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
	Path gamePath_;
	Path screenshotFilename_;
};

SaveSlotView::SaveSlotView(const Path &gameFilename, int slot, bool vertical, UI::LayoutParams *layoutParams) : UI::LinearLayout(UI::ORIENT_HORIZONTAL, layoutParams), slot_(slot), gamePath_(gameFilename) {
	using namespace UI;

	screenshotFilename_ = SaveState::GenerateSaveSlotFilename(gamePath_, slot, SaveState::SCREENSHOT_EXTENSION);
	Add(new Spacer(5));

	AsyncImageFileView *fv = Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, new UI::LayoutParams(82 * 2, 47 * 2)));
	fv->SetOverlayText(StringFromFormat("%d", slot_ + 1));

	auto pa = GetI18NCategory(I18NCat::PAUSE);

	LinearLayout *lines = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	lines->SetSpacing(2.0f);

	Add(lines);

	LinearLayout *buttons = new LinearLayout(vertical ? ORIENT_VERTICAL : ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	buttons->SetSpacing(10.0f);

	lines->Add(buttons);

	saveStateButton_ = buttons->Add(new Button(pa->T("Save State"), new LinearLayoutParams(0.0, G_VCENTER)));
	saveStateButton_->OnClick.Handle(this, &SaveSlotView::OnSaveState);

	fv->OnClick.Handle(this, &SaveSlotView::OnScreenshotClick);

	if (SaveState::HasSaveInSlot(gamePath_, slot)) {
		if (!Achievements::HardcoreModeActive()) {
			loadStateButton_ = buttons->Add(new Button(pa->T("Load State"), new LinearLayoutParams(0.0, G_VCENTER)));
			loadStateButton_->OnClick.Handle(this, &SaveSlotView::OnLoadState);
		}

		std::string dateStr = SaveState::GetSlotDateAsString(gamePath_, slot_);
		if (!dateStr.empty()) {
			TextView *dateView = new TextView(dateStr, new LinearLayoutParams(0.0, G_VCENTER));
			if (vertical) {
				dateView->SetSmall(true);
			}
			lines->Add(dateView)->SetShadow(true);
		}
	} else {
		fv->SetFilename(Path());
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

	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

GamePauseScreen::GamePauseScreen(const Path &filename)
	: UIDialogScreenWithGameBackground(filename) {
	// So we can tell if something blew up while on the pause screen.
	std::string assertStr = "PauseScreen: " + filename.GetFilename();
	SetExtraAssertInfo(assertStr.c_str());
}

GamePauseScreen::~GamePauseScreen() {
	__DisplaySetWasPaused();
}

bool GamePauseScreen::key(const KeyInput &key) {
	if (!UIScreen::key(key) && (key.flags & KEY_DOWN)) {
		// Special case to be able to unpause with a bound pause key.
		// Normally we can't bind keys used in the UI.
		InputMapping mapping(key.deviceId, key.keyCode);
		std::vector<int> pspButtons;
		KeyMap::InputMappingToPspButton(mapping, &pspButtons);
		for (auto button : pspButtons) {
			if (button == VIRTKEY_PAUSE) {
				TriggerFinish(DR_CANCEL);
				return true;
			}
		}
		return false;
	}
	return false;
}

void GamePauseScreen::CreateSavestateControls(UI::LinearLayout *leftColumnItems, bool vertical) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);

	static const int NUM_SAVESLOTS = 5;

	using namespace UI;

	leftColumnItems->SetSpacing(10.0);
	for (int i = 0; i < NUM_SAVESLOTS; i++) {
		SaveSlotView *slot = leftColumnItems->Add(new SaveSlotView(gamePath_, i, vertical, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		slot->OnStateLoaded.Handle(this, &GamePauseScreen::OnState);
		slot->OnStateSaved.Handle(this, &GamePauseScreen::OnState);
		slot->OnScreenshotClicked.Handle(this, &GamePauseScreen::OnScreenshotClicked);
	}
	leftColumnItems->Add(new Spacer(0.0));

	LinearLayout *buttonRow = leftColumnItems->Add(new LinearLayout(ORIENT_HORIZONTAL));
	if (g_Config.bEnableStateUndo && !Achievements::HardcoreModeActive()) {
		UI::Choice *loadUndoButton = buttonRow->Add(new Choice(pa->T("Undo last load")));
		loadUndoButton->SetEnabled(SaveState::HasUndoLoad(gamePath_));
		loadUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLoadUndo);

		UI::Choice *saveUndoButton = buttonRow->Add(new Choice(pa->T("Undo last save")));
		saveUndoButton->SetEnabled(SaveState::HasUndoLastSave(gamePath_));
		saveUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLastSaveUndo);
	}

	if (g_Config.iRewindSnapshotInterval > 0 && !Achievements::HardcoreModeActive()) {
		UI::Choice *rewindButton = buttonRow->Add(new Choice(pa->T("Rewind")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}
}

void GamePauseScreen::CreateViews() {
	using namespace UI;

	bool vertical = UseVerticalLayout();

	Margins scrollMargins(0, 10, 0, 0);
	Margins actionMenuMargins(0, 10, 15, 0);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0, scrollMargins));
	root_->Add(leftColumn);

	LinearLayout *leftColumnItems = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	leftColumn->Add(leftColumnItems);

	leftColumnItems->SetSpacing(5.0f);
	if (Achievements::IsActive()) {
		leftColumnItems->Add(new GameAchievementSummaryView());

		char buf[512];
		size_t sz = Achievements::GetRichPresenceMessage(buf, sizeof(buf));
		if (sz != (size_t)-1) {
			leftColumnItems->Add(new TextView(std::string_view(buf, sz), new UI::LinearLayoutParams(Margins(5, 5))))->SetSmall(true);
		}
	}

	if (!Achievements::HardcoreModeActive() || g_Config.bAchievementsSaveStateInHardcoreMode) {
		CreateSavestateControls(leftColumnItems, vertical);
	} else {
		// Let's show the active challenges.
		std::set<uint32_t> ids = Achievements::GetActiveChallengeIDs();
		if (!ids.empty()) {
			leftColumnItems->Add(new ItemHeader(ac->T("Active Challenges")));
			for (auto id : ids) {
				const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), id);
				if (!achievement)
					continue;
				leftColumnItems->Add(new AchievementView(achievement));
			}
		}

		// And tack on an explanation for why savestate options are not available.
		std::string_view notAvailable = ac->T("Save states not available in Hardcore Mode");
		leftColumnItems->Add(new NoticeView(NoticeLevel::INFO, notAvailable, ""));
	}

	ViewGroup *middleColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(64, FILL_PARENT, Margins(0, 10, 0, 15)));
	root_->Add(middleColumn);

	ViewGroup *rightColumnHolder = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(vertical ? 200 : 300, FILL_PARENT, actionMenuMargins));

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightColumnHolder->Add(rightColumn);

	root_->Add(rightColumnHolder);

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(pa->T("Switch UMD")))->OnClick.Add([=](UI::EventParams &) {
			screenManager()->push(new UmdReplaceScreen());
			return UI::EVENT_DONE;
		});
	}
	Choice *continueChoice = rightColumnItems->Add(new Choice(pa->T("Continue")));
	root_->SetDefaultFocusView(continueChoice);
	continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	rightColumnItems->Add(new Spacer(25.0));

	std::string gameId = g_paramSFO.GetDiscID();
	if (g_Config.hasGameConfig(gameId)) {
		rightColumnItems->Add(new Choice(pa->T("Game Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(pa->T("Delete Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
	} else {
		rightColumnItems->Add(new Choice(pa->T("Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		rightColumnItems->Add(new Choice(pa->T("Create Game Config")))->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
	}
	UI::Choice *displayEditor_ = rightColumnItems->Add(new Choice(gr->T("Display Layout && Effects")));
	displayEditor_->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		screenManager()->push(new DisplayLayoutScreen(gamePath_));
		return UI::EVENT_DONE;
	});
	if (g_Config.bEnableCheats) {
		rightColumnItems->Add(new Choice(pa->T("Cheats")))->OnClick.Add([&](UI::EventParams &e) {
			screenManager()->push(new CwCheatScreen(gamePath_));
			return UI::EVENT_DONE;
		});
	}
	if (g_Config.bAchievementsEnable && Achievements::HasAchievementsOrLeaderboards()) {
		rightColumnItems->Add(new Choice(ac->T("Achievements")))->OnClick.Add([&](UI::EventParams &e) {
			screenManager()->push(new RetroAchievementsListScreen(gamePath_));
			return UI::EVENT_DONE;
		});
	}

	// TODO, also might be nice to show overall compat rating here?
	// Based on their platform or even cpu/gpu/config.  Would add an API for it.
	if (Reporting::IsSupported() && g_paramSFO.GetValueString("DISC_ID").size()) {
		auto rp = GetI18NCategory(I18NCat::REPORTING);
		rightColumnItems->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
	}
	rightColumnItems->Add(new Spacer(25.0));
	if (g_Config.bPauseMenuExitsEmulator) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		rightColumnItems->Add(new Choice(mm->T("Exit")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);
	} else {
		rightColumnItems->Add(new Choice(pa->T("Exit to menu")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);
	}

	if (!Core_MustRunBehind()) {
		if (middleColumn) {
			playButton_ = middleColumn->Add(new Button("", g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"), new LinearLayoutParams(64, 64)));
			playButton_->OnClick.Add([=](UI::EventParams &e) {
				g_Config.bRunBehindPauseMenu = !g_Config.bRunBehindPauseMenu;
				playButton_->SetImageID(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"));
				return UI::EVENT_DONE;
			});
		}
	} else {
		auto nw = GetI18NCategory(I18NCat::NETWORKING);
		rightColumnHolder->Add(new TextView(nw->T("Network connected")));
	}
	rightColumnHolder->Add(new Spacer(10.0f));
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
	if (tag == "ScreenshotView" && dr == DR_OK) {
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
		Path fn = v->GetScreenshotFilename();
		std::string title = v->GetScreenshotTitle();
		Screen *screen = new ScreenshotViewScreen(fn, title, v->GetSlot(), gamePath_);
		screenManager()->push(screen);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnExitToMenu(UI::EventParams &e) {
	if (g_Config.bPauseMenuExitsEmulator) {
		System_ExitApp();
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

UI::EventReturn GamePauseScreen::OnLoadUndo(UI::EventParams &e) {
	SaveState::UndoLoad(gamePath_, &AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnLastSaveUndo(UI::EventParams &e) {
	SaveState::UndoLastSave(gamePath_);

	RecreateViews();
	return UI::EVENT_DONE;
}

void GamePauseScreen::CallbackDeleteConfig(bool yes)
{
	if (yes) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
		if (info->Ready(GameInfoFlags::PARAM_SFO)) {
			g_Config.unloadGameConfig();
			g_Config.deleteGameConfig(info->id);
			info->hasConfig = false;
			screenManager()->RecreateAllViews();
		}
	}
}

UI::EventReturn GamePauseScreen::OnCreateConfig(UI::EventParams &e)
{
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info->Ready(GameInfoFlags::PARAM_SFO)) {
		std::string gameId = g_paramSFO.GetDiscID();
		g_Config.createGameConfig(gameId);
		g_Config.changeGameSpecific(gameId, info->GetTitle());
		g_Config.saveGameConfig(gameId, info->GetTitle());
		if (info) {
			info->hasConfig = true;
		}
		screenManager()->topScreen()->RecreateViews();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnDeleteConfig(UI::EventParams &e)
{
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ga = GetI18NCategory(I18NCat::GAME);
	screenManager()->push(
		new PromptScreen(gamePath_, di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), ga->T("ConfirmDelete"), di->T("Cancel"),
		std::bind(&GamePauseScreen::CallbackDeleteConfig, this, std::placeholders::_1)));

	return UI::EVENT_DONE;
}
