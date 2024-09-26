// Copyright (c) 2015- PPSSPP Project.

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
#include <string>

#include "Common/File/Path.h"

#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "UI/MiscScreens.h"
#include "UI/GameInfoCache.h"

enum class SavedataSortOption {
	FILENAME,
	SIZE,
	DATE,
};

class SavedataBrowser : public UI::LinearLayout {
public:
	SavedataBrowser(const Path &path, UI::LayoutParams *layoutParams = 0);

	void Update() override;

	void SetSortOption(SavedataSortOption opt);
	void SetSearchFilter(const std::string &filter);

	UI::Event OnChoice;

private:
	static void PrepFilename(UI::View *);
	static void PrepSize(UI::View *);
	static void PrepDate(UI::View *);
	static bool ByFilename(const UI::View *, const UI::View *);
	static bool BySize(const UI::View *, const UI::View *);
	static bool ByDate(const UI::View *, const UI::View *);

	void Refresh();
	UI::EventReturn SavedataButtonClick(UI::EventParams &e);

	SavedataSortOption sortOption_ = SavedataSortOption::FILENAME;
	UI::ViewGroup *gameList_ = nullptr;
	UI::TextView *noMatchView_ = nullptr;
	UI::TextView *searchingView_ = nullptr;
	Path path_;
	std::string searchFilter_;
	bool searchPending_ = false;
};

class SavedataScreen : public UIDialogScreenWithGameBackground {
public:
	// gamePath can be empty, in that case this screen will show all savedata in the save directory.
	SavedataScreen(const Path &gamePath);
	~SavedataScreen();

	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void sendMessage(UIMessage message, const char *value) override;

	const char *tag() const override { return "Savedata"; }

protected:
	UI::EventReturn OnSavedataButtonClick(UI::EventParams &e);
	UI::EventReturn OnSortClick(UI::EventParams &e);
	UI::EventReturn OnSearch(UI::EventParams &e);
	void CreateViews() override;

	bool gridStyle_;
	SavedataSortOption sortOption_ = SavedataSortOption::FILENAME;
	SavedataBrowser *dataBrowser_;
	SavedataBrowser *stateBrowser_;
	std::string searchFilter_;
};

class GameIconView : public UI::InertView {
public:
	GameIconView(const Path &gamePath, float scale, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), gamePath_(gamePath), scale_(scale) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return ""; }

private:
	Path gamePath_;
	float scale_ = 1.0f;
	int textureWidth_ = 0;
	int textureHeight_ = 0;
};

class SavedataButton : public UI::Clickable {
public:
	SavedataButton(const Path &gamePath, UI::LayoutParams *layoutParams = 0)
		: UI::Clickable(layoutParams), savePath_(gamePath) {
		SetTag(gamePath.ToString());
	}

	void Draw(UIContext &dc) override;
	bool UpdateText();
	std::string DescribeText() const override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500;
		h = 74;
	}

	const Path &GamePath() const { return savePath_; }

	uint64_t GetTotalSize() const {
		return totalSize_;
	}
	int64_t GetDateSeconds() const {
		return dateSeconds_;
	}

	void UpdateTotalSize();
	void UpdateDateSeconds();

private:
	void UpdateText(const std::shared_ptr<GameInfo> &ginfo);

	Path savePath_;
	std::string title_;
	std::string subtitle_;
	uint64_t totalSize_ = 0;
	int64_t dateSeconds_ = 0;
	bool hasTotalSize_ = false;
	bool hasDateSeconds_ = false;
};

// View used for the detailed popup, and also in the import savedata comparison.
// It doesn't do its own data loading for that reason.
class SavedataView : public UI::LinearLayout {
public:
	SavedataView(UIContext &dc, GameInfo *ginfo, IdentifiedFileType type, bool showIcon, UI::LayoutParams *layoutParams = nullptr);
	SavedataView(UIContext &dc, const Path &savePath, IdentifiedFileType type, std::string_view title, std::string_view savedataTitle, std::string_view savedataDetail, std::string_view fileSize, std::string_view mtime, bool showIcon, UI::LayoutParams *layoutParams = nullptr);

	void UpdateGame(GameInfo *ginfo);
private:
	UI::TextView *savedataTitle_ = nullptr;
	UI::TextView *detail_ = nullptr;
	UI::TextView *mTime_ = nullptr;
	UI::TextView *fileSize_ = nullptr;
};
