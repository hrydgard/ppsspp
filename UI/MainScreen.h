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

#pragma once

#include <functional>

#include "file/path.h"
#include "ui/ui_screen.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(std::string path, bool allowBrowsing, bool *gridStyle_, std::string lastText, std::string lastLink, int flags = 0, UI::LayoutParams *layoutParams = 0);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;
	UI::Event OnHighlight;

	UI::Choice *HomebrewStoreButton() { return homebrewStoreButton_; }

	void FocusGame(std::string gamePath);

protected:
	virtual bool DisplayTopBar();
	virtual bool HasSpecialFiles(std::vector<std::string> &filenames);

	void Refresh();

private:
	bool IsCurrentPathPinned();
	const std::vector<std::string> GetPinnedPaths();
	const std::string GetBaseName(const std::string &path);

	UI::EventReturn GameButtonClick(UI::EventParams &e);
	UI::EventReturn GameButtonHoldClick(UI::EventParams &e);
	UI::EventReturn GameButtonHighlight(UI::EventParams &e);
	UI::EventReturn NavigateClick(UI::EventParams &e);
	UI::EventReturn LayoutChange(UI::EventParams &e);
	UI::EventReturn LastClick(UI::EventParams &e);
	UI::EventReturn HomeClick(UI::EventParams &e);
	UI::EventReturn PinToggleClick(UI::EventParams &e);

	UI::ViewGroup *gameList_;
	PathBrowser path_;
	bool *gridStyle_;
	bool allowBrowsing_;
	std::string lastText_;
	std::string lastLink_;
	int flags_;
	UI::Choice *homebrewStoreButton_;
	std::string focusGamePath_;
};

class RemoteISOBrowseScreen;

class MainScreen : public UIScreenWithBackground {
public:
	MainScreen();
	~MainScreen();

	bool isTopLevel() const override { return true; }

	// Horrible hack to show the demos & homebrew tab after having installed a game from a zip file.
	static bool showHomebrewTab;

protected:
	void CreateViews() override;
	void DrawBackground(UIContext &dc) override;
	void update() override;
	void sendMessage(const char *message, const char *value) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	bool UseVerticalLayout() const;
	bool DrawBackgroundFor(UIContext &dc, const std::string &gamePath, float progress);

	UI::EventReturn OnGameSelected(UI::EventParams &e);
	UI::EventReturn OnGameSelectedInstant(UI::EventParams &e);
	UI::EventReturn OnGameHighlight(UI::EventParams &e);
	// Event handlers
	UI::EventReturn OnLoadFile(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnRecentChange(UI::EventParams &e);
	UI::EventReturn OnCredits(UI::EventParams &e);
	UI::EventReturn OnSupport(UI::EventParams &e);
	UI::EventReturn OnPPSSPPOrg(UI::EventParams &e);
	UI::EventReturn OnForums(UI::EventParams &e);
	UI::EventReturn OnExit(UI::EventParams &e);
	UI::EventReturn OnDownloadUpgrade(UI::EventParams &e);
	UI::EventReturn OnDismissUpgrade(UI::EventParams &e);
	UI::EventReturn OnHomebrewStore(UI::EventParams &e);
	UI::EventReturn OnAllowStorage(UI::EventParams &e);

	UI::LinearLayout *upgradeBar_;
	UI::TabHolder *tabHolder_;

	std::string restoreFocusGamePath_;
	std::vector<GameBrowser *> gameBrowsers_;

	std::string highlightedGamePath_;
	std::string prevHighlightedGamePath_;
	float highlightProgress_;
	float prevHighlightProgress_;
	bool backFromStore_;
	bool lockBackgroundAudio_;
	bool lastVertical_;
	bool confirmedTemporary_ = false;

	friend class RemoteISOBrowseScreen;
};

class UmdReplaceScreen : public UIDialogScreenWithBackground {
public:
	UmdReplaceScreen() {}

protected:
	void CreateViews() override;
	void update() override;
	//virtual void sendMessage(const char *message, const char *value);

private:
	UI::EventReturn OnGameSelected(UI::EventParams &e);
	UI::EventReturn OnGameSelectedInstant(UI::EventParams &e);

	UI::EventReturn OnCancel(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
};
