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
#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/UI/AsyncImageFileView.h"

#include "Core/KeyMap.h"
#include "Core/Reporting.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/RetroAchievements.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUmd.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetInet.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/NetAdhocCommon.h"

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

#include "UI/EmuScreen.h"
#include "UI/PauseScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ReportScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GameInfoCache.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/BackgroundAudio.h"

static void AfterSaveStateAction(SaveState::Status status, std::string_view message) {
	if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
		g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR,
			message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

class ScreenshotViewScreen : public PopupScreen {
public:
	ScreenshotViewScreen(const Path &filename, std::string title, int slot, Path gamePath)
		: PopupScreen(title), filename_(filename), slot_(slot), gamePath_(gamePath), title_(title) {}   // PopupScreen will translate Back on its own

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

		const bool hasUndo = SaveState::HasUndoSaveInSlot(gamePath_, slot_);
		const bool undoEnabled = g_Config.bEnableStateUndo;

		Choice *undoButton = nullptr;
		if (undoEnabled || hasUndo) {
			// Show the undo button if state undo is enabled in settings, OR one is available. We can load it
			// even if making new undo states is not enabled.
			Choice *undoButton = new Choice(pa->T("Undo last save"));
			undoButton->SetEnabled(hasUndo);
		}

		grid->Add(new Choice(pa->T("Save State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnSaveState);
		// We can unconditionally show the load state button, because you can only pop this dialog up if a state exists.
		grid->Add(new Choice(pa->T("Load State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnLoadState);
		grid->Add(new Choice(pa->T("Delete State")))->OnClick.Handle(this, &ScreenshotViewScreen::OnDeleteState);
		if (undoButton) {
			grid->Add(undoButton)->OnClick.Handle(this, &ScreenshotViewScreen::OnUndoState);
		}
		grid->Add(back)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

		scroll->Add(content);
		parent->Add(scroll);
	}

private:
	UI::EventReturn OnSaveState(UI::EventParams &e);
	UI::EventReturn OnLoadState(UI::EventParams &e);
	UI::EventReturn OnUndoState(UI::EventParams &e);
	UI::EventReturn OnDeleteState(UI::EventParams &e);

	Path filename_;
	Path gamePath_;
	std::string title_;
	int slot_;
};

UI::EventReturn ScreenshotViewScreen::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK); //OK will close the pause screen as well
	}
	return UI::EVENT_DONE;
}

UI::EventReturn ScreenshotViewScreen::OnLoadState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn ScreenshotViewScreen::OnUndoState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		SaveState::UndoSaveSlot(gamePath_, slot_);
		TriggerFinish(DR_CANCEL);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn ScreenshotViewScreen::OnDeleteState(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);

	std::string message(di->T("DeleteConfirmSaveState"));
	message += "\n\n" + info->GetTitle() + " (" + info->id + ")";
	message += "\n\n" + title_;

	// TODO: Also show the screenshot on the confirmation screen?

	screenManager()->push(new PromptScreen(gamePath_, message, di->T("Delete"), di->T("Cancel"), [=](bool result) {
		if (result) {
			SaveState::DeleteSlot(gamePath_, slot_);
			TriggerFinish(DR_CANCEL);
		}
	}));

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
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(gamePath_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateLoaded.Trigger(e2);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn SaveSlotView::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(gamePath_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateSaved.Trigger(e2);
	}
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
		TriggerFinish(finishNextFrameResult_);
		finishNextFrame_ = false;
	}

	const bool networkConnected = IsNetworkConnected();
	const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
	if (g_netInited != lastNetInited_ || netInetInited != lastNetInetInited_ || lastAdhocServerConnected_ != g_adhocServerConnected || lastOnline_ != networkConnected || lastDNSConfigLoaded_ != dnsConfig.loaded) {
		INFO_LOG(Log::sceNet, "Network status changed (or pause dialog just popped up), recreating views");
		RecreateViews();
		lastNetInetInited_ = netInetInited;
		lastNetInited_ = g_netInited;
		lastAdhocServerConnected_ = g_adhocServerConnected;
		lastOnline_ = networkConnected;
		lastDNSConfigLoaded_ = dnsConfig.loaded;
	}

	const bool mustRunBehind = MustRunBehind();
	playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);

	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

GamePauseScreen::GamePauseScreen(const Path &filename, bool bootPending)
	: UIDialogScreenWithGameBackground(filename), bootPending_(bootPending) {
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

	LinearLayout *buttonRow = leftColumnItems->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(Margins(10, 0, 0, 0))));
	if (g_Config.bEnableStateUndo && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
		UI::Choice *loadUndoButton = buttonRow->Add(new Choice(pa->T("Undo last load")));
		loadUndoButton->SetEnabled(SaveState::HasUndoLoad(gamePath_));
		loadUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLoadUndo);

		UI::Choice *saveUndoButton = buttonRow->Add(new Choice(pa->T("Undo last save")));
		saveUndoButton->SetEnabled(SaveState::HasUndoLastSave(gamePath_));
		saveUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLastSaveUndo);
	}

	if (g_Config.iRewindSnapshotInterval > 0 && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
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
	auto nw = GetI18NCategory(I18NCat::NETWORKING);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0, scrollMargins));
	root_->Add(leftColumn);

	LinearLayout *leftColumnItems = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	leftColumn->Add(leftColumnItems);

	// If no other banner added, we want to add a spacer to move the Save/Load state buttons down a bit.
	bool bannerAdded = false;

	leftColumnItems->SetSpacing(5.0f);
	if (Achievements::IsActive()) {
		bannerAdded = true;
		leftColumnItems->Add(new GameAchievementSummaryView());

		char buf[512];
		size_t sz = Achievements::GetRichPresenceMessage(buf, sizeof(buf));
		if (sz != (size_t)-1) {
			leftColumnItems->Add(new TextView(std::string_view(buf, sz), FLAG_WRAP_TEXT, true, new UI::LinearLayoutParams(Margins(5, 5))));
		}
	}

	if (IsNetworkConnected()) {
		bannerAdded = true;
		leftColumnItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Network connected"), ""));

		const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
		if (dnsConfig.loaded && __NetApctlConnected()) {
			leftColumnItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Infrastructure"), ""));

			if (dnsConfig.state == InfraGameState::NotWorking) {
				leftColumnItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Some network functionality in this game is not working"), ""));
				if (!dnsConfig.workingIDs.empty()) {
					std::string str(nw->T("Other versions of this game that should work:"));
					for (auto &id : dnsConfig.workingIDs) {
						str.append("\n - ");
						str += id;
					}
					leftColumnItems->Add(new TextView(str));
				}
			} else if (dnsConfig.state == InfraGameState::Unknown) {
				leftColumnItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Network functionality in this game is not guaranteed"), ""));
			}
			if (!dnsConfig.revivalTeam.empty()) {
				leftColumnItems->Add(new TextView(std::string(nw->T("Infrastructure server provided by:"))));
				leftColumnItems->Add(new TextView(dnsConfig.revivalTeam));
				if (!dnsConfig.revivalTeamURL.empty()) {
					leftColumnItems->Add(new Button(dnsConfig.revivalTeamURL))->OnClick.Add([&dnsConfig](UI::EventParams &e) {
						if (!dnsConfig.revivalTeamURL.empty()) {
							System_LaunchUrl(LaunchUrlType::BROWSER_URL, dnsConfig.revivalTeamURL.c_str());
						}
						return UI::EVENT_DONE;
					});
				}
			}
		}

		if (NetAdhocctl_GetState() >= ADHOCCTL_STATE_CONNECTED) {
			// Awkwardly re-using a string here
			leftColumnItems->Add(new TextView(std::string(nw->T("AdHoc server")) + ": " + std::string(nw->T("Connected"))));
		}
	}

	bool achievementsAllowSavestates = !Achievements::HardcoreModeActive() || g_Config.bAchievementsSaveStateInHardcoreMode;
	bool showSavestateControls = achievementsAllowSavestates;
	if (IsNetworkConnected() && !g_Config.bAllowSavestateWhileConnected) {
		showSavestateControls = false;
	}

	if (showSavestateControls) {
		if (PSP_CoreParameter().compat.flags().SaveStatesNotRecommended) {
			bannerAdded = true;
			LinearLayout *horiz = new LinearLayout(UI::ORIENT_HORIZONTAL);
			leftColumnItems->Add(horiz);
			horiz->Add(new NoticeView(NoticeLevel::WARN, pa->T("Using save states is not recommended in this game"), "", new LinearLayoutParams(1.0f)));
			horiz->Add(new Button(di->T("More info")))->OnClick.Add([](UI::EventParams &e) {
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/troubleshooting/save-state-time-warps");
				return UI::EVENT_DONE;
			});
		}

		if (!bannerAdded && System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			// Enough so that it's possible to click the save/load buttons of Save 1 without activating
			// a pulldown on Android for example.
			leftColumnItems->Add(new Spacer(30.0f));
		}

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
		if (!achievementsAllowSavestates) {
			leftColumnItems->Add(new NoticeView(NoticeLevel::INFO, ac->T("Save states not available in Hardcore Mode"), ""));
		}
	}

	LinearLayout *middleColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(64, FILL_PARENT, Margins(0, 10, 0, 15)));
	root_->Add(middleColumn);
	middleColumn->SetSpacing(0.0f);
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

	rightColumnItems->Add(new Spacer(20.0));

	if (g_paramSFO.IsValid() && g_Config.hasGameConfig(g_paramSFO.GetDiscID())) {
		rightColumnItems->Add(new Choice(pa->T("Game Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		Choice *delGameConfig = rightColumnItems->Add(new Choice(pa->T("Delete Game Config")));
		delGameConfig->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
		delGameConfig->SetEnabled(!bootPending_);
	} else {
		rightColumnItems->Add(new Choice(pa->T("Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		Choice *createGameConfig = rightColumnItems->Add(new Choice(pa->T("Create Game Config")));
		createGameConfig->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
		createGameConfig->SetEnabled(!bootPending_);
	}

	rightColumnItems->Add(new Choice(gr->T("Display layout & effects")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
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
	rightColumnItems->Add(new Spacer(20.0));
	Choice *exit;
	if (g_Config.bPauseMenuExitsEmulator) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		exit = rightColumnItems->Add(new Choice(mm->T("Exit")));
	} else {
		exit = rightColumnItems->Add(new Choice(pa->T("Exit to menu")));
	}
	exit->OnClick.Handle(this, &GamePauseScreen::OnExit);
	exit->SetEnabled(!bootPending_);

	middleColumn->SetSpacing(20.0f);
	playButton_ = middleColumn->Add(new Button("", g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"), new LinearLayoutParams(64, 64)));
	playButton_->OnClick.Add([=](UI::EventParams &e) {
		g_Config.bRunBehindPauseMenu = !g_Config.bRunBehindPauseMenu;
		playButton_->SetImageID(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"));
		return UI::EVENT_DONE;
	});

	bool mustRunBehind = MustRunBehind();
	playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);

	Button *infoButton = middleColumn->Add(new Button("", ImageID("I_INFO"), new LinearLayoutParams(64, 64)));
	infoButton->OnClick.Add([=](UI::EventParams &e) {
		screenManager()->push(new GameScreen(gamePath_, true));
		return UI::EVENT_DONE;
	});

	Button *menuButton = middleColumn->Add(new Button("", ImageID("I_THREE_DOTS"), new LinearLayoutParams(64, 64)));

	menuButton->OnClick.Add([this, menuButton](UI::EventParams &e) {
		static const ContextMenuItem ingameContextMenu[] = {
			{ "Reset" },
		};
		PopupContextMenuScreen *contextMenu = new UI::PopupContextMenuScreen(ingameContextMenu, ARRAY_SIZE(ingameContextMenu), I18NCat::DIALOG, menuButton);
		screenManager()->push(contextMenu);
		contextMenu->OnChoice.Add([=](EventParams &e) -> UI::EventReturn {
			switch (e.a) {
			case 0:  // Reset
			{
				std::string confirmMessage = GetConfirmExitMessage();
				if (!confirmMessage.empty()) {
					auto di = GetI18NCategory(I18NCat::DIALOG);
					screenManager()->push(new PromptScreen(gamePath_, confirmMessage, di->T("Reset"), di->T("Cancel"), [=](bool result) {
						if (result) {
							System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
						}
					}));
				} else {
					System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
					break;
				}
			}
			default:
				break;
			}
			return UI::EVENT_DONE;
		});

		return UI::EVENT_DONE;
	});

	// What's this for?
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
		if (tag == "Game") {
			g_BackgroundAudio.SetGame(Path());
		} else if (tag != "Prompt" && tag != "ContextMenuPopup") {
			// There may have been changes to our savestates, so let's recreate.
			RecreateViews();
		}
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

int GetUnsavedProgressSeconds() {
	const double timeSinceSaveState = SaveState::SecondsSinceLastSavestate();
	const double timeSinceGameSave = SecondsSinceLastGameSave();

	return (int)std::min(timeSinceSaveState, timeSinceGameSave);
}

// If empty, no confirmation dialog should be shown.
std::string GetConfirmExitMessage() {
	std::string confirmMessage;

	int unsavedSeconds = GetUnsavedProgressSeconds();

	// If RAIntegration has dirty info, ask for confirmation.
	if (Achievements::RAIntegrationDirty()) {
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		confirmMessage = ac->T("You have unsaved RAIntegration changes.");
		confirmMessage += '\n';
	}

	if (coreState == CORE_RUNTIME_ERROR) {
		// The game crashed, or similar. Don't bother checking for timeout or network.
		return confirmMessage;
	}

	if (IsNetworkConnected()) {
		auto nw = GetI18NCategory(I18NCat::NETWORKING);
		confirmMessage += nw->T("Network connected");
		confirmMessage += '\n';
	} else if (g_Config.iAskForExitConfirmationAfterSeconds > 0 && unsavedSeconds > g_Config.iAskForExitConfirmationAfterSeconds) {
		if (PSP_CoreParameter().fileType == IdentifiedFileType::PPSSPP_GE_DUMP) {
			// No need to ask for this type of confirmation for dumps.
			return confirmMessage;
		}
		auto di = GetI18NCategory(I18NCat::DIALOG);
		confirmMessage = ApplySafeSubstitutions(di->T("You haven't saved your progress for %1."), NiceTimeFormat((int)unsavedSeconds));
		confirmMessage += '\n';
	}

	return confirmMessage;
}

UI::EventReturn GamePauseScreen::OnExit(UI::EventParams &e) {
	std::string confirmExitMessage = GetConfirmExitMessage();

	if (!confirmExitMessage.empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		confirmExitMessage += '\n';
		confirmExitMessage += di->T("Are you sure you want to exit?");
		screenManager()->push(new PromptScreen(gamePath_, confirmExitMessage, di->T("Yes"), di->T("No"), [=](bool result) {
			if (result) {
				if (g_Config.bPauseMenuExitsEmulator) {
					System_ExitApp();
				} else {
					finishNextFrameResult_ = DR_OK;  // exit game
					finishNextFrame_ = true;
				}
			}
		}));
	} else {
		if (g_Config.bPauseMenuExitsEmulator) {
			System_ExitApp();
		} else {
			TriggerFinish(DR_OK);
		}
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

void GamePauseScreen::CallbackDeleteConfig(bool yes) {
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

UI::EventReturn GamePauseScreen::OnCreateConfig(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info->Ready(GameInfoFlags::PARAM_SFO)) {
		std::string gameId = info->id;
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

UI::EventReturn GamePauseScreen::OnDeleteConfig(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	screenManager()->push(
		new PromptScreen(gamePath_, di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), di->T("Delete"), di->T("Cancel"),
		std::bind(&GamePauseScreen::CallbackDeleteConfig, this, std::placeholders::_1)));
	return UI::EVENT_DONE;
}
