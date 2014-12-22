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

void GamePauseScreen::update(InputState &input) {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update(input);
}

GamePauseScreen::~GamePauseScreen() {
	if (saveSlots_ != NULL) {
		g_Config.iCurrentStateSlot = saveSlots_->GetSelection();
		g_Config.Save();
	}
	__DisplaySetWasPaused();
}

void GamePauseScreen::CreateViews() {
	static const int NUM_SAVESLOTS = 5;

	using namespace UI;
	Margins actionMenuMargins(0, 100, 15, 0);
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *i = GetI18NCategory("Pause");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(leftColumn);

	root_->Add(new Spacer(new LinearLayoutParams(1.0)));

	ViewGroup *leftColumnItems = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(leftColumnItems);

	saveSlots_ = leftColumnItems->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(300, WRAP_CONTENT)));
	for (int i = 0; i < NUM_SAVESLOTS; i++) {
		std::stringstream saveSlotText;
		saveSlotText << " " << i + 1 << " ";
		saveSlots_->AddChoice(saveSlotText.str());
		if (SaveState::HasSaveInSlot(i)) {
			saveSlots_->HighlightChoice(i);
		}
	}

	saveSlots_->SetSelection(g_Config.iCurrentStateSlot);
	saveSlots_->OnChoice.Handle(this, &GamePauseScreen::OnStateSelected);

	saveStateButton_ = leftColumnItems->Add(new Choice(i->T("Save State")));
	saveStateButton_->OnClick.Handle(this, &GamePauseScreen::OnSaveState);

	loadStateButton_ = leftColumnItems->Add(new Choice(i->T("Load State")));
	loadStateButton_->OnClick.Handle(this, &GamePauseScreen::OnLoadState);

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

	UI::EventParams e;
	e.a = g_Config.iCurrentStateSlot;
	saveSlots_->OnChoice.Trigger(e);
}

UI::EventReturn GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnStateSelected(UI::EventParams &e) {
	int st = e.a;
	loadStateButton_->SetEnabled(SaveState::HasSaveInSlot(st));
	return UI::EVENT_DONE;
}

void GamePauseScreen::onFinish(DialogResult result) {
	// Do we really always need to "gpu->Resized" here?
	if (gpu)
		gpu->Resized();
	Reporting::UpdateConfig();
}

UI::EventReturn GamePauseScreen::OnExitToMenu(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnReportFeedback(UI::EventParams &e) {
	screenManager()->push(new ReportScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnLoadState(UI::EventParams &e) {
	SaveState::LoadSlot(saveSlots_->GetSelection(), SaveState::Callback(), 0);

	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnSaveState(UI::EventParams &e) {
	SaveState::SaveSlot(saveSlots_->GetSelection(), SaveState::Callback(), 0);

	screenManager()->finishDialog(this, DR_CANCEL);
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
		g_Config.deleteGameConfig(info->id);
		g_Config.unloadGameConfig();
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
