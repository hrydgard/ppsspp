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

enum GameBrowserFlags {
	FLAG_HOMEBREWSTOREBUTTON = 1
};

enum class BrowseFlags {
	NONE = 0,
	NAVIGATE = 1,
	BROWSE = 2,
	ARCHIVES = 4,
	PIN = 8,
	HOMEBREW_STORE = 16,
	UPLOAD_BUTTON = 32,
	STANDARD = 1 | 2 | 4 | 8 | 32,
};
ENUM_CLASS_BITOPS(BrowseFlags);

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(int token, const Path &path, BrowseFlags browseFlags, bool portrait, bool *gridStyle, ScreenManager *screenManager, std::string_view lastText, std::string_view lastLink, UI::LayoutParams *layoutParams = nullptr);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;
	UI::Event OnHighlight;

	void FocusGame(const Path &gamePath);
	void SetPath(const Path &path);
	void ApplySearchFilter(const std::string &filter);
	void Draw(UIContext &dc) override;
	void Update() override;
	void RequestRefresh() {
		refreshPending_ = true;
	}

	void SetHomePath(const Path &path) {
		homePath_ = path;
	}

protected:
	virtual bool DisplayTopBar();
	virtual bool HasSpecialFiles(std::vector<Path> &filenames);
	virtual Path HomePath();
	void ApplySearchFilter();

	void Refresh();

	Path homePath_;

private:
	bool IsCurrentPathPinned();
	std::vector<Path> GetPinnedPaths() const;

	void GameButtonClick(UI::EventParams &e);
	void GameButtonHoldClick(UI::EventParams &e);
	void GameButtonHighlight(UI::EventParams &e);
	void NavigateClick(UI::EventParams &e);
	void LayoutChange(UI::EventParams &e);
	void LastClick(UI::EventParams &e);
	void BrowseClick(UI::EventParams &e);
	void StorageClick(UI::EventParams &e);
	void OnHomeClick(UI::EventParams &e);
	void PinToggleClick(UI::EventParams &e);
	void GridSettingsClick(UI::EventParams &e);
	void OnRecentClear(UI::EventParams &e);
	void OnHomebrewStore(UI::EventParams &e);

	enum class SearchState {
		MATCH,
		MISMATCH,
		PENDING,
	};

	UI::ViewGroup *gameList_ = nullptr;
	PathBrowser path_;
	bool *gridStyle_ = nullptr;
	BrowseFlags browseFlags_;
	std::string lastText_;
	std::string lastLink_;
	std::string searchFilter_;
	std::vector<SearchState> searchStates_;
	Path focusGamePath_;
	bool listingPending_ = false;
	bool searchPending_ = false;
	bool refreshPending_ = false;
	float lastScale_ = 1.0f;
	bool lastLayoutWasGrid_ = true;
	ScreenManager *screenManager_;
	int token_ = -1;
	bool portrait_ = false;
	Path aliasMatch_;
	std::string aliasDisplay_;
};

class RemoteISOBrowseScreen;

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
	void CreateViews() override;
	void CreateRecentTab();
	GameBrowser *CreateBrowserTab(const Path &path, std::string_view title, std::string_view howToTitle, std::string_view howToUri, BrowseFlags browseFlags, bool *bGridView, float *scrollPos);
	void CreateMainButtons(UI::ViewGroup *parent, bool vertical);

	void DrawBackground(UIContext &dc) override;
	void update() override;
	void sendMessage(UIMessage message, const char *value) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	bool DrawBackgroundFor(UIContext &dc, const Path &gamePath, float progress);

	void OnGameSelected(UI::EventParams &e);
	void OnGameSelectedInstant(UI::EventParams &e);
	void OnGameHighlight(UI::EventParams &e);
	// Event handlers
	void OnLoadFile(UI::EventParams &e);
	void OnGameSettings(UI::EventParams &e);
	void OnCredits(UI::EventParams &e);
	void OnPPSSPPOrg(UI::EventParams &e);
	void OnForums(UI::EventParams &e);
	void OnExit(UI::EventParams &e);
	void OnAllowStorage(UI::EventParams &e);

	UI::TabHolder *tabHolder_ = nullptr;
	UI::Button *fullscreenButton_ = nullptr;

	Path restoreFocusGamePath_;
	std::vector<GameBrowser *> gameBrowsers_;

	Path highlightedGamePath_;
	Path prevHighlightedGamePath_;
	float highlightProgress_ = 0.0f;
	float prevHighlightProgress_ = 0.0f;
	bool backFromStore_ = false;
	bool lockBackgroundAudio_ = false;
	bool lastVertical_ = false;
	bool confirmedTemporary_ = false;
	bool searchKeyModifier_ = false;
	bool searchChanged_ = false;
	std::string searchFilter_;

	friend class RemoteISOBrowseScreen;
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
