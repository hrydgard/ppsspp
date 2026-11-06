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
#include <string>
#include <atomic>

#include "ppsspp_config.h"

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/Thread/Promise.h"

#include "Core/Util/MemStick.h"

#include "UI/BaseScreens.h"
#include "UI/MiscViews.h"
#include "UI/SimpleDialogScreen.h"

class NoticeView;
class WrappedTextItem;

// A memory stick folder as shown to the user: "Internal storage / PSP" rather than a content URI or a
// /storage/emulated/0 path, and the app's own folder by name.
std::string GetMemstickFolderDisplayName(const Path &folder);

// MemStickScreen - let's you configure your memory stick directory.
// Currently only useful for Android.
class MemStickScreen : public UITwoPaneBaseDialogScreen {
public:
	MemStickScreen(bool initialSetup);
	~MemStickScreen() = default;

	const char *tag() const override { return "MemStick"; }

	enum Choice {
		CHOICE_BROWSE_FOLDER,
		CHOICE_PRIVATE_DIRECTORY,
		CHOICE_STORAGE_ROOT,
		CHOICE_SET_MANUAL,
	};

protected:
	void CreateContentViews(UI::ViewGroup *parent) override;
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	std::string_view GetTitle() const override;

	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	// Event handlers
	void OnHelp(UI::EventParams &e);

	// Confirm button sub handlers
	void Browse(UI::EventParams &e);
	void UseInternalStorage(UI::EventParams &params);
	void UseStorageRoot(UI::EventParams &params);
	void SetFolderManually(UI::EventParams &params);

	// Button handlers.
	void OnConfirmClick(UI::EventParams &params);
	void OnChoiceClick(UI::EventParams &params);

	bool initialSetup_;
	bool storageBrowserWorking_;

#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	int choice_ = CHOICE_PRIVATE_DIRECTORY;
#else
	int choice_ = 0;
#endif
};

struct SpaceResult {
	int64_t bytesFree;
};

class ConfirmMemstickMoveScreen : public UITwoPaneBaseDialogScreen {
public:
	ConfirmMemstickMoveScreen(const Path &newMemstickFolder, bool initialSetup);
	~ConfirmMemstickMoveScreen();

	const char *tag() const override { return "ConfirmMemstickMove"; }

	bool key(const KeyInput &key) override;
	void TriggerFinish(DialogResult result) override;
	void onFinish(DialogResult result) override;

protected:
	void update() override;
	void CreateContentViews(UI::ViewGroup *parent) override;
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	std::string_view GetTitle() const override;

private:
	void OnMoveDataClick(UI::EventParams &params);
	void FinishFolderMove();

	void OnConfirm(UI::EventParams &params);

	bool IsBlocked() const {
		return check_ == MemstickFolderCheck::INSIDE_CURRENT || check_ == MemstickFolderCheck::INSIDE_OTHER || check_ == MemstickFolderCheck::LOOKS_LIKE_PSP_FOLDER;
	}
	bool CanMoveData() const {
		return check_ == MemstickFolderCheck::OK;
	}

	Path newMemstickFolder_;
	bool existingFilesInNewFolder_;
	MemstickFolderCheck check_ = MemstickFolderCheck::OK;

#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	bool moveData_ = false;
#else
	bool moveData_ = true;
#endif
	bool initialSetup_;
	// The data is already in the new folder.
	bool dataMoved_ = false;
	std::string RestartCountdownText() const;

	// Set when everything worked: restart then (time_now_d()), after showing that.
	static constexpr double RESTART_DELAY_SECONDS = 5.0;
	double restartTime_ = 0.0;
	NoticeView *doneNotice_ = nullptr;
	static constexpr int64_t FREE_SPACE_PENDING = -2;
	int64_t freeSpace_ = FREE_SPACE_PENDING;

	MoveProgressReporter progressReporter_;
	WrappedTextItem *progressView_ = nullptr;
	UI::ProgressBar *progressBar_ = nullptr;
	UI::InfoItem *newFreeSpaceView_ = nullptr;

	Promise<MoveResult *> *moveDataTask_ = nullptr;
	Promise<SpaceResult *> *newSpaceTask_ = nullptr;

	std::string error_;
};
