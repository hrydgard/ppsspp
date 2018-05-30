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

#include "ppsspp_config.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "math/curves.h"
#include "util/text/utf8.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "Common/FileUtil.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "UI/CwCheatScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/MainScreen.h"
#include "UI/BackgroundAudio.h"

GameScreen::GameScreen(const std::string &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
	SetBackgroundAudioGame(gamePath);
}

GameScreen::~GameScreen() {
}

void GameScreen::CreateViews() {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);

	if (info && !info->id.empty())
		saveDirs = info->GetSaveDataDirectories(); // Get's very heavy, let's not do it in update()

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	I18NCategory *pa = GetI18NCategory("Pause");

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle(this, &GameScreen::OnSwitchBack);
	if (info) {
		texvGameIcon_ = leftColumn->Add(new TextureView(0, IS_DEFAULT, new AnchorLayoutParams(144 * 2, 80 * 2, 10, 10, NONE, NONE)));

		LinearLayout *infoLayout = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(10, 200, NONE, NONE));
		leftColumn->Add(infoLayout);

		tvTitle_ = infoLayout->Add(new TextView(info->GetTitle(), ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvTitle_->SetShadow(true);
		infoLayout->Add(new Spacer(12));
		// This one doesn't need to be updated.
		infoLayout->Add(new TextView(gamePath_, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetShadow(true);
		tvGameSize_ = infoLayout->Add(new TextView("...", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvGameSize_->SetShadow(true);
		tvSaveDataSize_ = infoLayout->Add(new TextView("...", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvSaveDataSize_->SetShadow(true);
		tvInstallDataSize_ = infoLayout->Add(new TextView("", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvInstallDataSize_->SetShadow(true);
		tvInstallDataSize_->SetVisibility(V_GONE);
		tvRegion_ = infoLayout->Add(new TextView("", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvRegion_->SetShadow(true);
	} else {
		texvGameIcon_ = nullptr;
		tvTitle_ = nullptr;
		tvGameSize_ = nullptr;
		tvSaveDataSize_ = nullptr;
		tvInstallDataSize_ = nullptr;
		tvRegion_ = nullptr;
	}

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);
	
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->Add(new Choice(ga->T("Play")))->OnClick.Handle(this, &GameScreen::OnPlay);

	btnGameSettings_ = rightColumnItems->Add(new Choice(ga->T("Game Settings")));
	btnGameSettings_->OnClick.Handle(this, &GameScreen::OnGameSettings);
	btnDeleteGameConfig_ = rightColumnItems->Add(new Choice(ga->T("Delete Game Config")));
	btnDeleteGameConfig_->OnClick.Handle(this, &GameScreen::OnDeleteConfig);
	btnCreateGameConfig_ = rightColumnItems->Add(new Choice(ga->T("Create Game Config")));
	btnCreateGameConfig_->OnClick.Handle(this, &GameScreen::OnCreateConfig);

	btnGameSettings_->SetVisibility(V_GONE);
	btnDeleteGameConfig_->SetVisibility(V_GONE);
	btnCreateGameConfig_->SetVisibility(V_GONE);

	btnDeleteSaveData_ = new Choice(ga->T("Delete Save Data"));
	rightColumnItems->Add(btnDeleteSaveData_)->OnClick.Handle(this, &GameScreen::OnDeleteSaveData);
	btnDeleteSaveData_->SetVisibility(V_GONE);

	if (info && !info->pending) {
		otherChoices_.clear();
	}

	rightColumnItems->Add(AddOtherChoice(new Choice(ga->T("Delete Game"))))->OnClick.Handle(this, &GameScreen::OnDeleteGame);
	if (host->CanCreateShortcut()) {
		rightColumnItems->Add(AddOtherChoice(new Choice(ga->T("Create Shortcut"))))->OnClick.Handle(this, &GameScreen::OnCreateShortcut);
	}
	if (isRecentGame(gamePath_)) {
		rightColumnItems->Add(AddOtherChoice(new Choice(ga->T("Remove From Recent"))))->OnClick.Handle(this, &GameScreen::OnRemoveFromRecent);
	}
#ifdef _WIN32
	rightColumnItems->Add(AddOtherChoice(new Choice(ga->T("Show In Folder"))))->OnClick.Handle(this, &GameScreen::OnShowInFolder);
#endif
	if (g_Config.bEnableCheats) {
		rightColumnItems->Add(AddOtherChoice(new Choice(pa->T("Cheats"))))->OnClick.Handle(this, &GameScreen::OnCwCheat);
	}

	btnSetBackground_ = rightColumnItems->Add(new Choice(ga->T("Use UI background")));
	btnSetBackground_->OnClick.Handle(this, &GameScreen::OnSetBackground);
	btnSetBackground_->SetVisibility(V_GONE);
}

UI::Choice *GameScreen::AddOtherChoice(UI::Choice *choice) {
	otherChoices_.push_back(choice);
	// While loading.
	choice->SetVisibility(UI::V_GONE);
	return choice;
}

UI::EventReturn GameScreen::OnCreateConfig(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
	if (!info) {
		return UI::EVENT_SKIPPED;
	}
	g_Config.createGameConfig(info->id);
	g_Config.saveGameConfig(info->id);
	info->hasConfig = true;

	screenManager()->topScreen()->RecreateViews();
	return UI::EVENT_DONE;
}

void GameScreen::CallbackDeleteConfig(bool yes) {
	if (yes) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
		if (!info) {
			return;
		}
		g_Config.deleteGameConfig(info->id);
		info->hasConfig = false;
		screenManager()->RecreateAllViews();
	}
}

UI::EventReturn GameScreen::OnDeleteConfig(UI::EventParams &e)
{
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	screenManager()->push(
		new PromptScreen(di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"), ga->T("ConfirmDelete"), di->T("Cancel"),
		std::bind(&GameScreen::CallbackDeleteConfig, this, std::placeholders::_1)));

	return UI::EVENT_DONE;
}

void GameScreen::update() {
	UIScreen::update();

	I18NCategory *ga = GetI18NCategory("Game");

	Draw::DrawContext *thin3d = screenManager()->getDrawContext();

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(thin3d, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);

	if (tvTitle_)
		tvTitle_->SetText(info->GetTitle() + " (" + info->id + ")");
	if (info->icon.texture && texvGameIcon_) {
		texvGameIcon_->SetTexture(info->icon.texture->GetTexture());
		// Fade the icon with the background.
		double loadTime = info->icon.timeLoaded;
		if (info->pic1.texture) {
			loadTime = std::max(loadTime, info->pic1.timeLoaded);
		}
		if (info->pic0.texture) {
			loadTime = std::max(loadTime, info->pic0.timeLoaded);
		}
		uint32_t color = whiteAlpha(ease((time_now_d() - loadTime) * 3));
		texvGameIcon_->SetColor(color);
	}

	if (info->gameSize) {
		char temp[256];
		snprintf(temp, sizeof(temp), "%s: %1.1f %s", ga->T("Game"), (float) (info->gameSize) / 1024.f / 1024.f, ga->T("MB"));
		tvGameSize_->SetText(temp);
		snprintf(temp, sizeof(temp), "%s: %1.2f %s", ga->T("SaveData"), (float) (info->saveDataSize) / 1024.f / 1024.f, ga->T("MB"));
		tvSaveDataSize_->SetText(temp);
		if (info->installDataSize > 0) {
			snprintf(temp, sizeof(temp), "%s: %1.2f %s", ga->T("InstallData"), (float) (info->installDataSize) / 1024.f / 1024.f, ga->T("MB"));
			tvInstallDataSize_->SetText(temp);
			tvInstallDataSize_->SetVisibility(UI::V_VISIBLE);
		}
	}

	if (info->region >= 0 && info->region < GAMEREGION_MAX && info->region != GAMEREGION_OTHER) {
		static const char *regionNames[GAMEREGION_MAX] = {
			"Japan",
			"USA",
			"Europe",
			"Hong Kong",
			"Asia"
		};
		tvRegion_->SetText(ga->T(regionNames[info->region]));
	} else if (info->region > GAMEREGION_MAX){
		tvRegion_->SetText(ga->T("Homebrew"));
	}

	if (!info->id.empty()) {
		btnGameSettings_->SetVisibility(info->hasConfig ? UI::V_VISIBLE : UI::V_GONE);
		btnDeleteGameConfig_->SetVisibility(info->hasConfig ? UI::V_VISIBLE : UI::V_GONE);
		btnCreateGameConfig_->SetVisibility(info->hasConfig ? UI::V_GONE : UI::V_VISIBLE);

		if (saveDirs.size()) {
			btnDeleteSaveData_->SetVisibility(UI::V_VISIBLE);
		}
		if (info->pic0.texture || info->pic1.texture) {
			btnSetBackground_->SetVisibility(UI::V_VISIBLE);
		}
	}
	if (!info->pending) {
		// At this point, the above buttons won't become visible.  We can show these now.
		for (UI::Choice *choice : otherChoices_) {
			choice->SetVisibility(UI::V_VISIBLE);
		}
	}
}

UI::EventReturn GameScreen::OnShowInFolder(UI::EventParams &e) {
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	std::string str = std::string("explorer.exe /select,\"") + ReplaceAll(gamePath_, "/", "\\") + "\"";
	_wsystem(ConvertUTF8ToWString(str).c_str());
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnSwitchBack(UI::EventParams &e) {
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnPlay(UI::EventParams &e) {
	screenManager()->switchScreen(new EmuScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnGameSettings(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);
	if (info && info->paramSFOLoaded) {
		std::string discID = info->paramSFO.GetValueString("DISC_ID");
		if ((discID.empty() || !info->disc_total) && gamePath_.find("/PSP/GAME/") != std::string::npos)
			discID = g_paramSFO.GenerateFakeID(gamePath_);
		screenManager()->push(new GameSettingsScreen(gamePath_, discID, true));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameScreen::OnDeleteSaveData(UI::EventParams &e) {
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);
	if (info) {
		// Check that there's any savedata to delete
		if (saveDirs.size()) {
			screenManager()->push(
				new PromptScreen(di->T("DeleteConfirmAll", "Do you really want to delete all\nyour save data for this game?"), ga->T("ConfirmDelete"), di->T("Cancel"),
				std::bind(&GameScreen::CallbackDeleteSaveData, this, std::placeholders::_1)));
		}
	}

	RecreateViews();
	return UI::EVENT_DONE;
}

void GameScreen::CallbackDeleteSaveData(bool yes) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	if (yes) {
		info->DeleteAllSaveData();
		info->saveDataSize = 0;
		info->installDataSize = 0;
	}
}

UI::EventReturn GameScreen::OnDeleteGame(UI::EventParams &e) {
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ga = GetI18NCategory("Game");
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);
	if (info) {
		screenManager()->push(
			new PromptScreen(di->T("DeleteConfirmGame", "Do you really want to delete this game\nfrom your device? You can't undo this."), ga->T("ConfirmDelete"), di->T("Cancel"),
			std::bind(&GameScreen::CallbackDeleteGame, this, std::placeholders::_1)));
	}

	return UI::EVENT_DONE;
}

void GameScreen::CallbackDeleteGame(bool yes) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	if (yes) {
		info->Delete();
		g_gameInfoCache->Clear();
		screenManager()->switchScreen(new MainScreen());
	}
}

UI::EventReturn GameScreen::OnCreateShortcut(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	if (info) {
		host->CreateDesktopShortcut(gamePath_, info->GetTitle());
	}
	return UI::EVENT_DONE;
}

bool GameScreen::isRecentGame(const std::string &gamePath) {
	if (g_Config.iMaxRecent <= 0)
		return false;

	for (auto it = g_Config.recentIsos.begin(); it != g_Config.recentIsos.end(); ++it) {
#ifdef _WIN32
		if (!strcmpIgnore((*it).c_str(), gamePath.c_str(), "\\","/"))
#else
		if (!strcmp((*it).c_str(), gamePath.c_str()))
#endif
			return true;
	}
	return false;
}

UI::EventReturn GameScreen::OnRemoveFromRecent(UI::EventParams &e) {
	if (g_Config.iMaxRecent <= 0)
		return UI::EVENT_DONE;
	for (auto it = g_Config.recentIsos.begin(); it != g_Config.recentIsos.end(); ++it) {
#ifdef _WIN32
		if (!strcmpIgnore((*it).c_str(), gamePath_.c_str(), "\\","/")) {
#else
		if (!strcmp((*it).c_str(), gamePath_.c_str())) {
#endif
			g_Config.recentIsos.erase(it);
			screenManager()->switchScreen(new MainScreen());
			return UI::EVENT_DONE;
		}
	}
	return UI::EVENT_DONE;
}

class SetBackgroundPopupScreen : public PopupScreen {
public:
	SetBackgroundPopupScreen(const std::string &title, const std::string &gamePath);

protected:
	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return false; }
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;

private:
	std::string gamePath_;
	double timeStart_;
	double timeDone_ = 0.0;

	enum class Status {
		PENDING,
		DELAY,
		DONE,
	};
	Status status_ = Status::PENDING;
};

SetBackgroundPopupScreen::SetBackgroundPopupScreen(const std::string &title, const std::string &gamePath)
	: PopupScreen(title), gamePath_(gamePath) {
	timeStart_ = time_now_d();
}

void SetBackgroundPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	I18NCategory *ga = GetI18NCategory("Game");
	parent->Add(new UI::TextView(ga->T("One moment please..."), ALIGN_LEFT | ALIGN_VCENTER, false, new UI::LinearLayoutParams(UI::Margins(10, 0, 10, 10))));
}

void SetBackgroundPopupScreen::update() {
	PopupScreen::update();

	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTBGDATA);
	if (status_ == Status::PENDING && info && !info->pending) {
		GameInfoTex *pic = nullptr;
		if (info->pic1.dataLoaded && info->pic1.data.size()) {
			pic = &info->pic1;
		} else if (info->pic0.dataLoaded && info->pic0.data.size()) {
			pic = &info->pic0;
		}

		if (pic) {
			const std::string bgPng = GetSysDirectory(DIRECTORY_SYSTEM) + "background.png";
			writeStringToFile(false, pic->data, bgPng.c_str());
		}

		NativeMessageReceived("bgImage_updated", "");

		// It's worse if it flickers, stay open for at least 1s.
		timeDone_ = timeStart_ + 1.0;
		status_ = Status::DELAY;
	}

	if (status_ == Status::DELAY && timeDone_ <= time_now_d()) {
		TriggerFinish(DR_OK);
		status_ = Status::DONE;
	}
}

UI::EventReturn GameScreen::OnSetBackground(UI::EventParams &e) {
	I18NCategory *ga = GetI18NCategory("Game");
	// This popup is used to prevent any race condition:
	// g_gameInfoCache may take time to load the data, and a crash could happen if they exit before then.
	SetBackgroundPopupScreen *pop = new SetBackgroundPopupScreen(ga->T("Setting Background"), gamePath_);
	if (e.v)
		pop->SetPopupOrigin(e.v);
	screenManager()->push(pop);
	return UI::EVENT_DONE;
}
