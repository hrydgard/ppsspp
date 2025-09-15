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

#include "UI/MiscScreens.h"

class NoticeView;

// MemStickScreen - let's you configure your memory stick directory.
// Currently only useful for Android.
class MemStickScreen : public UIDialogScreenWithBackground {
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
	void CreateViews() override;

	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void update() override;
	ScreenRenderFlags render(ScreenRenderMode mode) override {
		// Simple anti-flicker due to delayed finish.
		if (!done_) {
			// render as usual.
			return UIDialogScreenWithBackground::render(mode);
		} else {
			// no render. black frame insertion is better than flicker.
		}
		return ScreenRenderFlags::NONE;
	}

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

	SettingInfoMessage *settingInfo_ = nullptr;
	NoticeView *errorNoticeView_ = nullptr;

	bool initialSetup_;
	bool storageBrowserWorking_;
	bool done_ = false;

#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	int choice_ = CHOICE_PRIVATE_DIRECTORY;
#else
	int choice_ = 0;
#endif
};

struct SpaceResult {
	int64_t bytesFree;
};

class ConfirmMemstickMoveScreen : public UIDialogScreenWithBackground {
public:
	ConfirmMemstickMoveScreen(const Path &newMemstickFolder, bool initialSetup);
	~ConfirmMemstickMoveScreen();

	const char *tag() const override { return "ConfirmMemstickMove"; }

protected:
	void update() override;
	void CreateViews() override;

private:
	void OnMoveDataClick(UI::EventParams &params);
	void FinishFolderMove();

	void OnConfirm(UI::EventParams &params);

	Path newMemstickFolder_;
	bool existingFilesInNewFolder_;
	bool folderConflict_;

#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	bool moveData_ = false;
#else
	bool moveData_ = true;
#endif
	bool initialSetup_;

	MoveProgressReporter progressReporter_;
	UI::TextView *progressView_ = nullptr;
	UI::TextView *newFreeSpaceView_ = nullptr;

	Promise<MoveResult *> *moveDataTask_ = nullptr;
	Promise<SpaceResult *> *newSpaceTask_ = nullptr;

	std::string error_;
};
