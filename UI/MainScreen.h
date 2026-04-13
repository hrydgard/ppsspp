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
#include <string_view>

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/PopupScreens.h"
#include "UI/BaseScreens.h"
#include "Common/File/PathBrowser.h"

#include "UI/GameBrowser.h"

class RemoteISOBrowseScreen;

struct HighlightedBackground {
	Path gamePath;
	double startTime;
	double endTime;
};

class MainScreen : public UIBaseScreen {
public:
	MainScreen();
	~MainScreen();

	bool isTopLevel() const override { return true; }

	const char *tag() const override { return "Main"; }

	// Horrible hack to show the demos & homebrew tab after having installed a game from a zip file.
	static bool showHomebrewTab;

	bool key(const KeyInput &touch) override;

protected:
	ViewLayoutMode LayoutMode() const override { return ViewLayoutMode::IgnoreBottomInset; }

	void CreateViews() override;
	void CreateRecentTab();
	GameBrowser *CreateBrowserTab(const Path &path, std::string_view title, std::string_view howToTitle, std::string_view howToUri, BrowseFlags browseFlags, bool *bGridView, float *scrollPos);
	void CreateMainButtons(UI::ViewGroup *parent, bool vertical);

	void DrawBackground(UIContext &dc) override;
	void update() override;
	void sendMessage(UIMessage message, const char *value) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	void DrawBackgroundFor(UIContext &dc, const Path &gamePath, float alpha);

	void OnGameSelected(UI::EventParams &e);
	void OnGameSelectedInstant(UI::EventParams &e);
	void OnGameHighlight(UI::EventParams &e);
	// Event handlers
	void OnLoadFile(UI::EventParams &e);
	void OnGameSettings(UI::EventParams &e);
	void OnCredits(UI::EventParams &e);
	void OnPPSSPPOrg(UI::EventParams &e);
	void OnForums(UI::EventParams &e);
	void OnDownloadUpgrade(UI::EventParams &e);
	void OnAllowStorage(UI::EventParams &e);

	UI::TabHolder *tabHolder_ = nullptr;

	Path restoreFocusGamePath_;
	std::vector<GameBrowser *> gameBrowsers_;

	std::vector<HighlightedBackground> highlightedBackgrounds_;
	Path highlightedGamePath_;

	bool backFromStore_ = false;
	bool lockBackgroundAudio_ = false;
	bool lastVertical_ = false;
	bool confirmedTemporary_ = false;
	bool searchKeyModifier_ = false;
	bool searchChanged_ = false;
	std::string searchFilter_;

	friend class RemoteISOBrowseScreen;
private:
	void InstantHighlight(const Path &path);
};

class UmdReplaceScreen : public UIBaseDialogScreen {
public:
	const char *tag() const override { return "UmdReplace"; }

protected:
	void CreateViews() override;
	void update() override;

private:
	void OnGameSelected(UI::EventParams &e);
	void OnGameSettings(UI::EventParams &e);
};

class GridSettingsPopupScreen : public UI::PopupScreen {
public:
	GridSettingsPopupScreen(std::string_view label) : PopupScreen(label) {}
	void CreatePopupContents(UI::ViewGroup *parent) override;
	UI::Event OnRecentChanged;

	const char *tag() const override { return "GridSettings"; }

private:
	void GridPlusClick(UI::EventParams &e);
	void GridMinusClick(UI::EventParams &e);
	void OnRecentClearClick(UI::EventParams &e);
	const float MAX_GAME_GRID_SCALE = 3.0f;
	const float MIN_GAME_GRID_SCALE = 0.8f;
};

void LaunchBuyGold(ScreenManager *screenManager);
