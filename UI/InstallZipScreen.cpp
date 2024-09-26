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
#include "Common/File/FileUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "Core/Loaders.h"
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
	auto ga = GetI18NCategory(I18NCat::GAME);

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
	existingSaveView_ = nullptr;
	destFolders_.clear();

	std::vector<Path> destOptions;

	if (z) {
		DetectZipFileContents(z, &zipFileInfo_);  // Even if this fails, it sets zipInfo->contents.
		if (zipFileInfo_.contents == ZipFileContents::ISO_FILE || zipFileInfo_.contents == ZipFileContents::PSP_GAME_DIR) {
			std::string_view question = iz->T("Install game from ZIP file?");

			leftColumn->Add(new TextView(question));
			leftColumn->Add(new TextView(shortFilename));
			if (!zipFileInfo_.contentName.empty()) {
				leftColumn->Add(new TextView(zipFileInfo_.contentName));
			}

			doneView_ = leftColumn->Add(new TextView(""));

			if (zipFileInfo_.contents == ZipFileContents::ISO_FILE) {
				const bool isInDownloads = File::IsProbablyInDownloadsFolder(zipPath_);
				Path parent;
				if (!isInDownloads && zipPath_.CanNavigateUp()) {
					parent = zipPath_.NavigateUp();
					destFolders_.push_back(parent);
				}
				if (g_Config.currentDirectory.IsLocalType() && File::Exists(g_Config.currentDirectory) && g_Config.currentDirectory != parent) {
					destFolders_.push_back(g_Config.currentDirectory);
				}
				destFolders_.push_back(g_Config.memStickDirectory);
			} else {
				destFolders_.push_back(GetSysDirectory(DIRECTORY_GAME));
			}

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
			std::string_view question = iz->T("Import savedata from ZIP file");
			leftColumn->Add(new TextView(question))->SetBig(true);
			leftColumn->Add(new TextView(zipFileInfo_.gameTitle + ": " + zipFileInfo_.savedataDir));

			Path savedataDir = GetSysDirectory(DIRECTORY_SAVEDATA);
			bool overwrite = !CanExtractWithoutOverwrite(z, savedataDir, 50);

			destFolders_.push_back(savedataDir);

			if (overwrite) {
				leftColumn->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"), ""));
			}

			int columnWidth = 300;

			LinearLayout *compareColumns = leftColumn->Add(new LinearLayout(UI::ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			LinearLayout *leftCompare = new LinearLayout(UI::ORIENT_VERTICAL);
			leftCompare->Add(new TextView(iz->T("Data to import")));
			compareColumns->Add(leftCompare);
			leftCompare->Add(new SavedataView(*screenManager()->getUIContext(), Path(), IdentifiedFileType::PSP_SAVEDATA_DIRECTORY,
				zipFileInfo_.gameTitle, zipFileInfo_.savedataTitle, zipFileInfo_.savedataDetails, NiceSizeFormat(zipFileInfo_.totalFileSize), zipFileInfo_.mTime, false, new LinearLayoutParams(columnWidth, WRAP_CONTENT)));

			// Check for potential overwrite at destination, and ask the user if it's OK to overwrite.
			if (overwrite) {
				savedataToOverwrite_ = savedataDir / zipFileInfo_.savedataDir;
				std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savedataToOverwrite_, GameInfoFlags::FILE_TYPE | GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);

				LinearLayout *rightCompare = new LinearLayout(UI::ORIENT_VERTICAL);
				rightCompare->Add(new TextView(iz->T("Existing data")));

				compareColumns->Add(rightCompare);
				existingSaveView_ = rightCompare->Add(new SavedataView(*screenManager()->getUIContext(), ginfo.get(), IdentifiedFileType::PSP_SAVEDATA_DIRECTORY, false, new LinearLayoutParams(columnWidth, WRAP_CONTENT)));
				if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
					rightCompare->Add(new Button(ga->T("Show In Folder")))->OnClick.Add([=](UI::EventParams &) {
						System_ShowFileInFolder(savedataToOverwrite_);
						return UI::EVENT_DONE;
					});
				}
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

	if (destFolders_.size() > 1) {
		leftColumn->Add(new TextView(iz->T("Install into folder")));
		for (int i = 0; i < (int)destFolders_.size(); i++) {
			leftColumn->Add(new RadioButton(&destFolderChoice_, i, destFolders_[i].ToVisualString()));
		}
	} else if (destFolders_.size() == 1 && zipFileInfo_.contents != ZipFileContents::SAVE_DATA) {
		leftColumn->Add(new TextView(StringFromFormat("%s %s", iz->T_cstr("Install into folder:"), destFolders_[0].ToVisualString().c_str())));
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
		return UIDialogScreen::key(key);
	}
	return false;
}

UI::EventReturn InstallZipScreen::OnInstall(UI::EventParams &params) {
	ZipFileTask task;
	task.url = zipPath_;
	task.fileName = zipPath_;
	task.deleteAfter = deleteZipFile_;
	task.zipFileInfo = zipFileInfo_;
	if (!destFolders_.empty() && destFolderChoice_ < destFolders_.size()) {
		task.destination = destFolders_[destFolderChoice_];
	}
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

	if (existingSaveView_) {
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savedataToOverwrite_, GameInfoFlags::FILE_TYPE | GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);
		existingSaveView_->UpdateGame(ginfo.get());
	}
	UIDialogScreenWithBackground::update();
}
