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

#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "UI/InstallZipScreen.h"
#include "UI/MainScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/SavedataScreen.h"

InstallZipScreen::InstallZipScreen(const Path &zipPath) : zipPath_(zipPath) {
	g_GameManager.ResetInstallError();
}

void InstallZipScreen::CreateViews() {
	using namespace UI;

	File::FileInfo fileInfo;
	bool success = File::GetFileInfo(zipPath_, &fileInfo);

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto er = GetI18NCategory(I18NCat::ERRORS);

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(12)));
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(leftColumn);
	root_->Add(rightColumnItems);

	std::string shortFilename = zipPath_.GetFilename();
	
	// TODO: Do in the background?
	struct zip *z = ZipOpenPath(zipPath_);

	bool showDeleteCheckbox = false;
	returnToHomebrew_ = false;
	installChoice_ = nullptr;
	doneView_ = nullptr;
	installChoice_ = nullptr;

	if (z) {
		DetectZipFileContents(z, &zipFileInfo_);  // Even if this fails, it sets zipInfo->contents.
		if (zipFileInfo_.contents == ZipFileContents::ISO_FILE || zipFileInfo_.contents == ZipFileContents::PSP_GAME_DIR) {
			std::string_view question = iz->T("Install game from ZIP file?");

			leftColumn->Add(new TextView(question));
			leftColumn->Add(new TextView(shortFilename));

			doneView_ = leftColumn->Add(new TextView(""));

			installChoice_ = rightColumnItems->Add(new Choice(iz->T("Install")));
			installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
			returnToHomebrew_ = true;
			showDeleteCheckbox = true;
		} else if (zipFileInfo_.contents == ZipFileContents::TEXTURE_PACK) {
			std::string_view question = iz->T("Install textures from ZIP file?");
			leftColumn->Add(new TextView(question));
			leftColumn->Add(new TextView(shortFilename));

			doneView_ = leftColumn->Add(new TextView(""));

			installChoice_ = rightColumnItems->Add(new Choice(iz->T("Install")));
			installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
			backChoice_ = rightColumnItems->Add(new Choice(di->T("Back")));

			showDeleteCheckbox = true;
		} else if (zipFileInfo_.contents == ZipFileContents::SAVE_DATA) {
			std::string_view question = iz->T("Install savedata?");
			leftColumn->Add(new TextView(question, ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
			leftColumn->Add(new TextView(zipFileInfo_.contentName));

			// Check for potential overwrite at destination, and ask the user if it's OK to overwrite.
			Path saveDir = GetSysDirectory(DIRECTORY_SAVEDATA);
			if (!CanExtractWithoutOverwrite(z, saveDir, 50)) {
				leftColumn->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"), "", new AnchorLayoutParams(10, 60, NONE, NONE)));
				leftColumn->Add(new SavedataButton(GetSysDirectory(DIRECTORY_SAVEDATA) / zipFileInfo_.savedataDir));
			}

			installChoice_ = rightColumnItems->Add(new Choice(iz->T("Install")));
			installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);

			doneView_ = leftColumn->Add(new TextView(""));
			showDeleteCheckbox = true;
		} else {
			leftColumn->Add(new TextView(iz->T("Zip file does not contain PSP software"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		}
	} else {
		leftColumn->Add(new TextView(er->T("Error reading file"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
	}

	// OK so that EmuScreen will handle it right.
	backChoice_ = rightColumnItems->Add(new Choice(di->T("Back")));
	backChoice_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);

	if (showDeleteCheckbox) {
		rightColumnItems->Add(new CheckBox(&deleteZipFile_, iz->T("Delete ZIP file")));
	}
}

bool InstallZipScreen::key(const KeyInput &key) {
	// Ignore all key presses during download and installation to avoid user escape
	if (g_GameManager.GetState() == GameManagerState::IDLE) {
		return UIScreen::key(key);
	}
	return false;
}

UI::EventReturn InstallZipScreen::OnInstall(UI::EventParams &params) {
	ZipFileTask task;
	task.url = zipPath_;
	task.fileName = zipPath_;
	task.deleteAfter = deleteZipFile_;
	task.zipFileInfo = zipFileInfo_;
	if (g_GameManager.InstallZipOnThread(task)) {
		installStarted_ = true;
		if (installChoice_) {
			installChoice_->SetEnabled(false);
		}
	}
	return UI::EVENT_DONE;
}

void InstallZipScreen::update() {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	using namespace UI;
	if (g_GameManager.GetState() != GameManagerState::IDLE) {
		if (backChoice_) {
			backChoice_->SetEnabled(false);
		}
	} else {
		if (backChoice_) {
			backChoice_->SetEnabled(true);
		}
		std::string err = g_GameManager.GetInstallError();
		if (!err.empty()) {
			if (doneView_)
				doneView_->SetText(iz->T(err.c_str()));
		} else if (installStarted_) {
			if (doneView_)
				doneView_->SetText(iz->T("Installed!"));
			MainScreen::showHomebrewTab = returnToHomebrew_;
		}
	}
	UIScreen::update();
}
