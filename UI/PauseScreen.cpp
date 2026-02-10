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
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Notice.h"
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
#include "Core/Util/GameDB.h"
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
#include "UI/TouchControlLayoutScreen.h"
#include "UI/BackgroundAudio.h"
#include "UI/MiscViews.h"

static void AfterSaveStateAction(SaveState::Status status, std::string_view message) {
	if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
		g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR,
			message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

class ScreenshotViewScreen : public UI::PopupScreen {
public:
	ScreenshotViewScreen(const Path &screenshotFilename, std::string_view saveStatePrefix, std::string_view title, int slot, Path gamePath)
		: PopupScreen(title), screenshotFilename_(screenshotFilename), saveStatePrefix_(saveStatePrefix), slot_(slot), gamePath_(gamePath), title_(title) {}   // PopupScreen will translate Back on its own

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
		content->Add(new AsyncImageFileView(screenshotFilename_, IS_KEEP_ASPECT, new LinearLayoutParams(480, 272, contentMargins)))->SetCanBeFocused(false);

		GridLayoutSettings gridsettings(240, 64, 5);
		gridsettings.fillCells = true;
		GridLayout *grid = content->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

		Choice *back = new Choice(di->T("Back"));

		const bool hasUndo = SaveState::HasUndoSaveInSlot(saveStatePrefix_, slot_);
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
	void OnSaveState(UI::EventParams &e);
	void OnLoadState(UI::EventParams &e);
	void OnUndoState(UI::EventParams &e);
	void OnDeleteState(UI::EventParams &e);

	Path screenshotFilename_;
	Path gamePath_;
	std::string saveStatePrefix_;
	std::string title_;
	int slot_;
};

void ScreenshotViewScreen::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(saveStatePrefix_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK); //OK will close the pause screen as well
	}
}

void ScreenshotViewScreen::OnLoadState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(saveStatePrefix_, slot_, &AfterSaveStateAction);
		TriggerFinish(DR_OK);
	}
}

void ScreenshotViewScreen::OnUndoState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		SaveState::UndoSaveSlot(saveStatePrefix_, slot_);
		TriggerFinish(DR_CANCEL);
	}
}

void ScreenshotViewScreen::OnDeleteState(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);

	std::string_view title = di->T("Delete");
	std::string message = std::string(di->T("DeleteConfirmSaveState")) + "\n\n" + info->GetTitle() + " (" + info->id + ")";
	message += "\n\n" + title_;

	// TODO: Also show the screenshot on the confirmation screen?

	screenManager()->push(new UI::MessagePopupScreen(title, message, di->T("Delete"), di->T("Cancel"), [this](bool result) {
		if (result) {
			SaveState::DeleteSlot(saveStatePrefix_, slot_);
			TriggerFinish(DR_CANCEL);
		}
	}));
}

class SaveSlotView : public UI::LinearLayout {
public:
	SaveSlotView(std::string_view saveStatePrefix, int slot, UI::LayoutParams *layoutParams = nullptr);

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
		return SaveState::GetSlotDateAsString(saveStatePrefix_, slot_);
	}

	UI::Event OnStateLoaded;
	UI::Event OnStateSaved;
	UI::Event OnScreenshotClicked;
	UI::Event OnSelected;

private:
	void OnSaveState(UI::EventParams &e);
	void OnLoadState(UI::EventParams &e);

	UI::Button *saveStateButton_ = nullptr;
	UI::Button *loadStateButton_ = nullptr;

	int slot_;
	std::string saveStatePrefix_;
	Path screenshotFilename_;
};

SaveSlotView::SaveSlotView(std::string_view saveStatePrefix, int slot, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams), slot_(slot), saveStatePrefix_(saveStatePrefix) {
	using namespace UI;

	screenshotFilename_ = SaveState::GenerateSaveSlotPath(saveStatePrefix_, slot, SaveState::SCREENSHOT_EXTENSION);

	std::string number = StringFromFormat("%d", slot + 1);
	Add(new Spacer(5));

	// TEMP HACK: use some other view, like a Choice, themed differently to enable keyboard access to selection.
	ClickableTextView *numberView = Add(new ClickableTextView(number, new LinearLayoutParams(40.0f, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
	numberView->SetBig(true);
	numberView->OnClick.Add([this](UI::EventParams &e) {
		e.v = this;
		OnSelected.Trigger(e);
	});

	AsyncImageFileView *fv = Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, new UI::LayoutParams(82 * 2, 47 * 2)));

	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	LinearLayout *lines = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	lines->SetSpacing(2.0f);

	Add(lines);

	LinearLayout *buttons = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	buttons->SetSpacing(10.0f);

	lines->Add(buttons);

	saveStateButton_ = buttons->Add(new Button(pa->T("Save State"), new LinearLayoutParams(0.0, Gravity::G_VCENTER)));
	saveStateButton_->OnClick.Handle(this, &SaveSlotView::OnSaveState);

	fv->OnClick.Add([this](UI::EventParams &e) {
		e.v = this;
		OnScreenshotClicked.Trigger(e);
	});

	if (SaveState::HasSaveInSlot(saveStatePrefix_, slot_)) {
		if (!Achievements::HardcoreModeActive()) {
			loadStateButton_ = buttons->Add(new Button(pa->T("Load State"), new LinearLayoutParams(0.0, Gravity::G_VCENTER)));
			loadStateButton_->OnClick.Handle(this, &SaveSlotView::OnLoadState);
		}

		std::string dateStr = SaveState::GetSlotDateAsString(saveStatePrefix_, slot_);

		if (slot_ == g_Config.iAutoLoadSaveState - 3) {
			dateStr += " (" + std::string(sy->T("Auto load savestate")) + ")";
		}

		if (!dateStr.empty()) {
			TextView *dateView = new TextView(dateStr, new LinearLayoutParams(0.0, Gravity::G_VCENTER));
			dateView->SetSmall(true);
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

void SaveSlotView::OnLoadState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::LoadSlot(saveStatePrefix_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateLoaded.Trigger(e2);
	}
}

void SaveSlotView::OnSaveState(UI::EventParams &e) {
	if (!NetworkWarnUserIfOnlineAndCantSavestate()) {
		g_Config.iCurrentStateSlot = slot_;
		SaveState::SaveSlot(saveStatePrefix_, slot_, &AfterSaveStateAction);
		UI::EventParams e2{};
		e2.v = this;
		OnStateSaved.Trigger(e2);
	}
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

	if (playButton_) {
		const bool mustRunBehind = MustRunBehind();
		playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);
	}

	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

GamePauseScreen::GamePauseScreen(const Path &filename, bool bootPending)
	: UIBaseDialogScreen(filename), bootPending_(bootPending) {
	// So we can tell if something blew up while on the pause screen.
	std::string assertStr = "PauseScreen: " + filename.GetFilename();
	SetExtraAssertInfo(assertStr.c_str());
	saveStatePrefix_ = SaveState::GetGamePrefix(g_paramSFO);
	SaveState::Rescan(saveStatePrefix_);
}

GamePauseScreen::~GamePauseScreen() {
	__DisplaySetWasPaused();
}

bool GamePauseScreen::key(const KeyInput &key) {
	bool handled = UIDialogScreen::key(key);

	if (!handled && (key.flags & KeyInputFlags::DOWN)) {
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
	return handled;
}

void GamePauseScreen::CreateSavestateControls(UI::LinearLayout *leftColumnItems) {
	auto pa = GetI18NCategory(I18NCat::PAUSE);

	using namespace UI;

	leftColumnItems->SetSpacing(10.0);
	for (int i = 0; i < g_Config.iSaveStateSlotCount; i++) {
		SaveSlotView *slot = leftColumnItems->Add(new SaveSlotView(saveStatePrefix_, i, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Gravity::G_HCENTER, Margins(0,0,0,0))));
		slot->OnStateLoaded.Handle(this, &GamePauseScreen::OnState);
		slot->OnStateSaved.Handle(this, &GamePauseScreen::OnState);
		slot->OnScreenshotClicked.Add([this](UI::EventParams &e) {
			SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
			int slot = v->GetSlot();
			g_Config.iCurrentStateSlot = v->GetSlot();
			if (SaveState::HasSaveInSlot(saveStatePrefix_, slot)) {
				Path fn = v->GetScreenshotFilename();
				std::string title = v->GetScreenshotTitle();
				Screen *screen = new ScreenshotViewScreen(fn, saveStatePrefix_, title, v->GetSlot(), gamePath_);
				screenManager()->push(screen);
			}
		});
		slot->OnSelected.Add([this](UI::EventParams &e) {
			SaveSlotView *v = static_cast<SaveSlotView *>(e.v);
			g_Config.iCurrentStateSlot = v->GetSlot();
			RecreateViews();
		});
	}
	leftColumnItems->Add(new Spacer(0.0));

	LinearLayout *buttonRow = leftColumnItems->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(Margins(10, 0, 0, 0))));
	if (g_Config.bEnableStateUndo && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
		UI::Choice *loadUndoButton = buttonRow->Add(new Choice(pa->T("Undo last load"), ImageID("I_NAVIGATE_BACK")));
		loadUndoButton->SetEnabled(SaveState::HasUndoLoad(saveStatePrefix_));
		loadUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLoadUndo);

		UI::Choice *saveUndoButton = buttonRow->Add(new Choice(pa->T("Undo last save"), ImageID("I_NAVIGATE_BACK")));
		saveUndoButton->SetEnabled(SaveState::HasUndoLastSave(saveStatePrefix_));
		saveUndoButton->OnClick.Handle(this, &GamePauseScreen::OnLastSaveUndo);
	}

	if (g_Config.iRewindSnapshotInterval > 0 && !Achievements::HardcoreModeActive() && NetworkAllowSaveState()) {
		UI::Choice *rewindButton = buttonRow->Add(new Choice(pa->T("Rewind"), ImageID("I_REWIND")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}
}

UI::Margins GamePauseScreen::RootMargins() const {
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE && GetDeviceOrientation() == DeviceOrientation::Landscape) {
		// Add some top margin on mobile so it isn't too close to the status bar, as we place buttons
		// very close to the top of the screen.
		return UI::Margins(0, 30, 0, 0);
	} else {
		return UI::Margins(0);
	}
}

void GamePauseScreen::CreateViews() {
	using namespace UI;

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	Margins scrollMargins(0, 10, 0, 0);
	Margins actionMenuMargins(0, 10, 15, 0);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto nw = GetI18NCategory(I18NCat::NETWORKING);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	root_ = new LinearLayout(portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL);

	if (portrait) {
		((LinearLayout *)root_)->SetSpacing(0);
	}

	if (portrait) {
		// We have room for a title bar. Use the game DB title if available.
		std::string title;
		std::vector<GameDBInfo> dbInfos;
		const bool inGameDB = g_gameDB.GetGameInfos(g_paramSFO.GetDiscID(), &dbInfos);
		if (inGameDB) {
			title = dbInfos[0].title;
		} else {
			title = g_paramSFO.GetValueString("TITLE");
		}
		TopBar *topBar = new TopBar(*screenManager()->getUIContext(), TopBarFlags::ContextMenuButton, title);
		root_->Add(topBar);

		topBar->OnContextMenuClick.Add([this, portrait](UI::EventParams &e) {
			UI::View *srcView = e.v;
			ShowContextMenu(srcView, portrait);
		});
	}

	ViewGroup *saveStateScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, scrollMargins));
	root_->Add(saveStateScroll);

	LinearLayout *saveDataScrollItems = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	saveStateScroll->Add(saveDataScrollItems);

	saveDataScrollItems->SetSpacing(5.0f);
	if (Achievements::IsActive()) {
		saveDataScrollItems->Add(new GameAchievementSummaryView());

		char buf[512];
		size_t sz = Achievements::GetRichPresenceMessage(buf, sizeof(buf));
		if (sz != (size_t)-1) {
			saveDataScrollItems->Add(new TextView(std::string_view(buf, sz), FLAG_WRAP_TEXT, true, new UI::LinearLayoutParams(Margins(5, 5))));
		}
	}

	if (IsNetworkConnected()) {
		saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Network connected"), ""));

		const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
		if (dnsConfig.loaded && __NetApctlConnected()) {
			saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, nw->T("Infrastructure"), ""));

			if (dnsConfig.state == InfraGameState::NotWorking) {
				saveDataScrollItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Some network functionality in this game is not working"), ""));
				if (!dnsConfig.workingIDs.empty()) {
					std::string str(nw->T("Other versions of this game that should work:"));
					for (auto &id : dnsConfig.workingIDs) {
						str.append("\n - ");
						str += id;
					}
					saveDataScrollItems->Add(new TextView(str));
				}
			} else if (dnsConfig.state == InfraGameState::Unknown) {
				saveDataScrollItems->Add(new NoticeView(NoticeLevel::WARN, nw->T("Network functionality in this game is not guaranteed"), ""));
			}
			if (!dnsConfig.revivalTeam.empty()) {
				saveDataScrollItems->Add(new TextView(std::string(nw->T("Infrastructure server provided by:"))));
				saveDataScrollItems->Add(new TextView(dnsConfig.revivalTeam));
				if (!dnsConfig.revivalTeamURL.empty()) {
					saveDataScrollItems->Add(new Button(dnsConfig.revivalTeamURL))->OnClick.Add([&dnsConfig](UI::EventParams &e) {
						if (!dnsConfig.revivalTeamURL.empty()) {
							System_LaunchUrl(LaunchUrlType::BROWSER_URL, dnsConfig.revivalTeamURL.c_str());
						}
					});
				}
			}
		}

		if (NetAdhocctl_GetState() >= ADHOCCTL_STATE_CONNECTED) {
			// Awkwardly re-using a string here
			saveDataScrollItems->Add(new TextView(std::string(nw->T("AdHoc server")) + ": " + std::string(nw->T("Connected"))));
		}
	}

	bool achievementsAllowSavestates = !Achievements::HardcoreModeActive() || g_Config.bAchievementsSaveStateInHardcoreMode;
	bool showSavestateControls = achievementsAllowSavestates;
	if (IsNetworkConnected() && !g_Config.bAllowSavestateWhileConnected) {
		showSavestateControls = false;
	}

	if (showSavestateControls) {
		if (PSP_CoreParameter().compat.flags().SaveStatesNotRecommended) {
			LinearLayout *horiz = new LinearLayout(ORIENT_HORIZONTAL);
			saveDataScrollItems->Add(horiz);
			horiz->Add(new NoticeView(NoticeLevel::WARN, pa->T("Using save states is not recommended in this game"), "", new LinearLayoutParams(1.0f)));
			horiz->Add(new Button(di->T("More info")))->OnClick.Add([](UI::EventParams &e) {
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/troubleshooting/save-state-time-warps");
			});
		}
		CreateSavestateControls(saveDataScrollItems);
	} else {
		// Let's show the active challenges.
		std::set<uint32_t> ids = Achievements::GetActiveChallengeIDs();
		if (!ids.empty()) {
			saveDataScrollItems->Add(new ItemHeader(ac->T("Active Challenges")));
			for (auto id : ids) {
				const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), id);
				if (!achievement)
					continue;
				saveDataScrollItems->Add(new AchievementView(achievement));
			}
		}

		// And tack on an explanation for why savestate options are not available.
		if (!achievementsAllowSavestates) {
			saveDataScrollItems->Add(new NoticeView(NoticeLevel::INFO, ac->T("Save states not available in Hardcore Mode"), ""));
		}
	}

	LinearLayout *middleColumn = nullptr;
	ViewGroup *buttonColumn = nullptr;
	if (portrait) {
		buttonColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

		middleColumn = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT, Margins(10, 10, 10, 10)));
		root_->Add(middleColumn);
		root_->Add(buttonColumn);
	} else {
		middleColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(ITEM_HEIGHT, FILL_PARENT, Margins(0, 10, 0, 15)));
		root_->Add(middleColumn);
		middleColumn->SetSpacing(0.0f);

		ViewGroup *buttonColumnScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(320, FILL_PARENT, actionMenuMargins));
		buttonColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		buttonColumnScroll->Add(buttonColumn);
		root_->Add(buttonColumnScroll);
	}

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	buttonColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(pa->T("Switch UMD"), ImageID("I_UMD")))->OnClick.Add([this](UI::EventParams &) {
			screenManager()->push(new UmdReplaceScreen());
		});
	}

	if (!portrait) {
		Choice *continueChoice = rightColumnItems->Add(new Choice(pa->T("Continue"), ImageID("I_PLAY")));
		root_->SetDefaultFocusView(continueChoice);
		continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		rightColumnItems->Add(new Spacer(20.0));
	}

	if (g_paramSFO.IsValid() && g_Config.HasGameConfig(g_paramSFO.GetDiscID())) {
		rightColumnItems->Add(new Choice(pa->T("Game Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		if (g_Config.HasGameConfig(g_paramSFO.GetValueString("DISC_ID"))) {
			Choice *delGameConfig = rightColumnItems->Add(new Choice(pa->T("Delete Game Config")));
			delGameConfig->OnClick.Handle(this, &GamePauseScreen::OnDeleteConfig);
			delGameConfig->SetEnabled(!bootPending_);
		}
	} else if (PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		rightColumnItems->Add(new Choice(pa->T("Settings"), ImageID("I_GEAR")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
		Choice *createGameConfig = rightColumnItems->Add(new Choice(pa->T("Create Game Config"), ImageID("I_GEAR_STAR")));
		createGameConfig->OnClick.Handle(this, &GamePauseScreen::OnCreateConfig);
		createGameConfig->SetEnabled(!bootPending_);
	}

	if (g_Config.bAchievementsEnable && Achievements::HasAchievementsOrLeaderboards()) {
		rightColumnItems->Add(new Choice(ac->T("Achievements"), ImageID("I_ACHIEVEMENT")))->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new RetroAchievementsListScreen(gamePath_));
		});
	}

	rightColumnItems->Add(new Choice(gr->T("Display layout & effects"), ImageID("I_DISPLAY")))->OnClick.Add([this](UI::EventParams &) -> void {
		screenManager()->push(new DisplayLayoutScreen(gamePath_));
	});
	if (g_Config.bShowTouchControls) {
		rightColumnItems->Add(new Choice(co->T("Edit touch control layout"), ImageID("I_CONTROLLER")))->OnClick.Add([this](UI::EventParams &) -> void {
			screenManager()->push(new TouchControlLayoutScreen(gamePath_));
		});
	}

	if (g_Config.bEnableCheats && PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		rightColumnItems->Add(new Choice(pa->T("Cheats"), ImageID("I_CHEAT")))->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new CwCheatScreen(gamePath_));
		});
	}

	// TODO, also might be nice to show overall compat rating here?
	// Based on their platform or even cpu/gpu/config.  Would add an API for it.
	if (!portrait) {
		AddExtraOptions(rightColumnItems);
	}
	rightColumnItems->Add(new Spacer(20.0));
	Choice *exit;
	if (g_Config.bPauseMenuExitsEmulator) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		exit = new Choice(mm->T("Exit"), ImageID("I_EXIT"));
	} else {
		exit = new Choice(pa->T("Exit to menu"), ImageID("I_EXIT"));
	}

	if (portrait) {
		UI::LinearLayout *exitRow = new UI::LinearLayout(ORIENT_HORIZONTAL, new UI::LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 0, 0, 0)));
		rightColumnItems->Add(exitRow);
		exitRow->Add(exit);
		exit->ReplaceLayoutParams(new UI::LinearLayoutParams(1.0f, Gravity::G_VCENTER));
		Choice *continueChoice = new Choice(pa->T("Continue"), ImageID("I_PLAY"));
		continueChoice->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root_->SetDefaultFocusView(continueChoice);
		exitRow->Add(continueChoice);
		continueChoice->ReplaceLayoutParams(new UI::LinearLayoutParams(1.0f, Gravity::G_VCENTER));
	} else {
		rightColumnItems->Add(exit);
	}

	exit->OnClick.Handle(this, &GamePauseScreen::OnExit);
	exit->SetEnabled(!bootPending_);

	if (middleColumn) {
		middleColumn->SetSpacing(portrait ? 8.0f : 0.0f);
		playButton_ = middleColumn->Add(new Choice(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"), new LinearLayoutParams(64, 64)));
		playButton_->OnClick.Add([this](UI::EventParams &e) {
			g_Config.bRunBehindPauseMenu = !g_Config.bRunBehindPauseMenu;
			playButton_->SetIconLeft(g_Config.bRunBehindPauseMenu ? ImageID("I_PAUSE") : ImageID("I_PLAY"));
		});

		bool mustRunBehind = MustRunBehind();
		playButton_->SetVisibility(mustRunBehind ? UI::V_GONE : UI::V_VISIBLE);

		if (!portrait) {
			middleColumn->Add(new Spacer(20.0f));
		}

		Choice *infoButton = middleColumn->Add(new Choice(ImageID("I_INFO"), new LinearLayoutParams(64, 64)));
		infoButton->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new GameScreen(gamePath_, true));
		});

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			AddRotationPicker(screenManager(), middleColumn, false);
		}

		if (!portrait) {
			Choice *menuButton = middleColumn->Add(new Choice("", ImageID("I_THREE_DOTS"), new LinearLayoutParams(64, 64)));
			menuButton->OnClick.Add([this, menuButton, portrait](UI::EventParams &e) {
				ShowContextMenu(menuButton, portrait);
			});
		}
	} else {
		playButton_ = nullptr;
	}
}

void GamePauseScreen::ShowContextMenu(UI::View *menuButton, bool portrait) {
	using namespace UI;
	PopupCallbackScreen *contextMenu = new UI::PopupCallbackScreen([this, portrait](UI::ViewGroup *parent) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		parent->Add(new Choice(di->T("Reset")))->OnClick.Add([this](UI::EventParams &e) {
			std::string confirmMessage = GetConfirmExitMessage();
			if (!confirmMessage.empty()) {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				screenManager()->push(new UI::MessagePopupScreen(di->T("Reset"), confirmMessage, di->T("Reset"), di->T("Cancel"), [](bool result) {
					if (result) {
						System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
					}
				}));
			} else {
				System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
			}
		});

		if (portrait) {
			AddExtraOptions(parent);
		}
	}, menuButton);
	screenManager()->push(contextMenu);
}

void GamePauseScreen::AddExtraOptions(UI::ViewGroup *parent) {
	using namespace UI;
	// Add some other options that are removed from the main screen in portrait mode.
	if (Reporting::IsSupported() && g_paramSFO.GetValueString("DISC_ID").size()) {
		auto rp = GetI18NCategory(I18NCat::REPORTING);
		parent->Add(new Choice(rp->T("ReportButton", "Report Feedback")))->OnClick.Handle(this, &GamePauseScreen::OnReportFeedback);
	}
}

void GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(gamePath_));
}

void GamePauseScreen::OnState(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
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

void GamePauseScreen::OnExit(UI::EventParams &e) {
	std::string confirmExitMessage = GetConfirmExitMessage();

	if (!confirmExitMessage.empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string_view title = di->T("Are you sure you want to exit?");
		screenManager()->push(new UI::MessagePopupScreen(title, confirmExitMessage, di->T("Exit"), di->T("Cancel"), [this](bool result) {
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
}

void GamePauseScreen::OnReportFeedback(UI::EventParams &e) {
	screenManager()->push(new ReportScreen(gamePath_));
}

void GamePauseScreen::OnRewind(UI::EventParams &e) {
	SaveState::Rewind(&AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
}

void GamePauseScreen::OnLoadUndo(UI::EventParams &e) {
	SaveState::UndoLoad(saveStatePrefix_, &AfterSaveStateAction);

	TriggerFinish(DR_CANCEL);
}

void GamePauseScreen::OnLastSaveUndo(UI::EventParams &e) {
	SaveState::UndoLastSave(saveStatePrefix_);

	RecreateViews();
}

void GamePauseScreen::OnCreateConfig(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info->Ready(GameInfoFlags::PARAM_SFO)) {
		std::string gameId = info->id;
		g_Config.CreateGameConfig(gameId);
		g_Config.SaveGameConfig(gameId, info->GetTitle());
		if (info) {
			info->hasConfig = true;
		}
		RecreateViews();
	}
}

void GamePauseScreen::OnDeleteConfig(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
	screenManager()->push(
		new UI::MessagePopupScreen(di->T("Delete"), di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"),
			trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"), [this](bool yes) {
		if (!yes) {
			return;
		}
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
		if (info->Ready(GameInfoFlags::PARAM_SFO)) {
			g_Config.UnloadGameConfig();
			g_Config.DeleteGameConfig(info->id);
			info->hasConfig = false;
			screenManager()->RecreateAllViews();
		}
	}));
}
