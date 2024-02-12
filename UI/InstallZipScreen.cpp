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
#include "Core/Util/GameManager.h"
#include "UI/InstallZipScreen.h"
#include "UI/MainScreen.h"

InstallZipScreen::InstallZipScreen(const Path &zipPath) : zipPath_(zipPath) {
	g_GameManager.ResetInstallError();
}

void InstallZipScreen::CreateViews() {
	using namespace UI;

	File::FileInfo fileInfo;
	bool success = File::GetFileInfo(zipPath_, &fileInfo);

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(leftColumn);
	root_->Add(rightColumnItems);

	std::string shortFilename = zipPath_.GetFilename();
	
	// TODO: Do in the background?
	ZipFileInfo zipInfo;
	ZipFileContents contents = DetectZipFileContents(zipPath_, &zipInfo);

	if (contents == ZipFileContents::ISO_FILE || contents == ZipFileContents::PSP_GAME_DIR) {
		std::string_view question = iz->T("Install game from ZIP file?");
		leftColumn->Add(new TextView(question, ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		leftColumn->Add(new TextView(shortFilename, ALIGN_LEFT, false, new AnchorLayoutParams(10, 60, NONE, NONE)));

		doneView_ = leftColumn->Add(new TextView("", new AnchorLayoutParams(10, 120, NONE, NONE)));
		progressBar_ = leftColumn->Add(new ProgressBar(new AnchorLayoutParams(10, 200, 200, NONE)));

		installChoice_ = rightColumnItems->Add(new Choice(iz->T("Install")));
		installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
		backChoice_ = rightColumnItems->Add(new Choice(di->T("Back")));
		rightColumnItems->Add(new CheckBox(&deleteZipFile_, iz->T("Delete ZIP file")));

		returnToHomebrew_ = true;
	} else if (contents == ZipFileContents::TEXTURE_PACK) {
		std::string_view question = iz->T("Install textures from ZIP file?");
		leftColumn->Add(new TextView(question, ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		leftColumn->Add(new TextView(shortFilename, ALIGN_LEFT, false, new AnchorLayoutParams(10, 60, NONE, NONE)));

		doneView_ = leftColumn->Add(new TextView("", new AnchorLayoutParams(10, 120, NONE, NONE)));
		progressBar_ = leftColumn->Add(new ProgressBar(new AnchorLayoutParams(10, 200, 200, NONE)));

		installChoice_ = rightColumnItems->Add(new Choice(iz->T("Install")));
		installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
		backChoice_ = rightColumnItems->Add(new Choice(di->T("Back")));
		rightColumnItems->Add(new CheckBox(&deleteZipFile_, iz->T("Delete ZIP file")));

		returnToHomebrew_ = false;
	} else {
		leftColumn->Add(new TextView(iz->T("Zip file does not contain PSP software"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		doneView_ = nullptr;
		progressBar_ = nullptr;
		installChoice_ = nullptr;
		backChoice_ = rightColumnItems->Add(new Choice(di->T("Back")));
	}

	// OK so that EmuScreen will handle it right.
	backChoice_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
}

bool InstallZipScreen::key(const KeyInput &key) {
	// Ignore all key presses during download and installation to avoid user escape
	if (g_GameManager.GetState() == GameManagerState::IDLE) {
		return UIScreen::key(key);
	}
	return false;
}

UI::EventReturn InstallZipScreen::OnInstall(UI::EventParams &params) {
	if (g_GameManager.InstallGameOnThread(zipPath_, zipPath_, deleteZipFile_)) {
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
		if (progressBar_) {
			progressBar_->SetVisibility(V_VISIBLE);
			progressBar_->SetProgress(g_GameManager.GetCurrentInstallProgressPercentage());
		}
		if (backChoice_) {
			backChoice_->SetEnabled(false);
		}
	} else {
		if (progressBar_) {
			progressBar_->SetVisibility(V_GONE);
		}
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
