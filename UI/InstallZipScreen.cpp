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
#include "Common/UI/Notice.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/PathUtil.h"
#include "Core/Loaders.h"

#include "UI/InstallZipScreen.h"
#include "UI/MainScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/SavedataScreen.h"
#include "UI/EmuScreen.h"

InstallZipScreen::InstallZipScreen(const Path &zipPath) : UITwoPaneBaseDialogScreen(Path(), TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll), zipPath_(zipPath) {
	g_GameManager.ResetInstallError();
	ZipContainer zipFile = ZipOpenPath(zipPath_);
	if (zipFile) {
		DetectZipFileContents(zipFile, &zipFileInfo_);  // Even if this fails, it sets zipInfo->contents.
		ZipClose(zipFile);
	}
}

std::string_view InstallZipScreen::GetTitle() const {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	return iz->T("ZIP file");
}

void InstallZipScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto er = GetI18NCategory(I18NCat::ERRORS);
	auto ga = GetI18NCategory(I18NCat::GAME);

	std::string shortFilename = zipPath_.GetFilename();

	bool showDeleteCheckbox = false;
	returnToHomebrew_ = false;
	installChoice_ = nullptr;
	playChoice_ = nullptr;
	doneView_ = nullptr;
	existingSaveView_ = nullptr;

	std::vector<Path> destOptions;

	switch (zipFileInfo_.contents) {
	case ZipFileContents::ISO_FILE:
	case ZipFileContents::PSP_GAME_DIR:
		installChoice_ = parent->Add(new Choice(iz->T("Install"), ImageID("I_FOLDER_UPLOAD")));
		installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);

		// NOTE: We detect PBP isos (like demos) as game dirs currently. Can't play them directly.
		if (zipFileInfo_.contents == ZipFileContents::ISO_FILE) {
			playChoice_ = parent->Add(new Choice(ga->T("Play"), ImageID("I_PLAY")));
			playChoice_->OnClick.Handle(this, &InstallZipScreen::OnPlay);
		}

		returnToHomebrew_ = true;
		showDeleteCheckbox = true;
		break;
	case ZipFileContents::TEXTURE_PACK:
	case ZipFileContents::SAVE_DATA:
	case ZipFileContents::SAVE_STATES:
		installChoice_ = parent->Add(new Choice(iz->T("Install"), ImageID("I_FOLDER_UPLOAD")));
		installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
		showDeleteCheckbox = true;
		break;
	case ZipFileContents::FRAME_DUMP:
		// It's a frame dump, add a play button!
		playChoice_ = parent->Add(new Choice(ga->T("Play"), ImageID("I_PLAY")));
		playChoice_->OnClick.Handle(this, &InstallZipScreen::OnPlay);
		break;
	default:
		// Nothing to do!
		break;
	}	

	if (showDeleteCheckbox) {
		parent->Add(new Spacer(12.0f));
		parent->Add(new CheckBox(&deleteZipFile_, iz->T("Delete ZIP file")));
	}
}

void InstallZipScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	LinearLayout *leftColumn = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8))));

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto er = GetI18NCategory(I18NCat::ERRORS);
	auto ga = GetI18NCategory(I18NCat::GAME);

	std::string shortFilename = zipPath_.GetFilename();

	bool showDeleteCheckbox = false;
	returnToHomebrew_ = false;
	installChoice_ = nullptr;
	playChoice_ = nullptr;
	doneView_ = nullptr;
	existingSaveView_ = nullptr;
	destFolders_.clear();

	std::vector<Path> destOptions;
	bool overwrite = false;
	switch (zipFileInfo_.contents) {
	case ZipFileContents::ISO_FILE:
	case ZipFileContents::PSP_GAME_DIR:
	{
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

		returnToHomebrew_ = true;
		showDeleteCheckbox = true;
		break;
	}
	case ZipFileContents::TEXTURE_PACK:
	{
		std::string_view question = iz->T("Install textures from ZIP file?");
		leftColumn->Add(new TextView(question));
		leftColumn->Add(new TextView(shortFilename));

		doneView_ = leftColumn->Add(new TextView(""));

		showDeleteCheckbox = true;
		break;
	}
	case ZipFileContents::SAVE_STATES:
	{
		std::string_view question = iz->T("Import savestates from ZIP file");
		leftColumn->Add(new TextView(question))->SetBig(true);
		leftColumn->Add(new TextView(GetFriendlyPath(zipPath_)));
		leftColumn->Add(new TextView(zipFileInfo_.gameTitle));

		Path savestateDir = GetSysDirectory(DIRECTORY_SAVESTATE);
		ZipContainer zipFile = ZipOpenPath(zipPath_);
		overwrite = !CanExtractWithoutOverwrite(zipFile, savestateDir, 50);
		ZipClose(zipFile);

		destFolders_.push_back(savestateDir);

		// TODO: Use the GameInfoCache to display data about the game if available.
		doneView_ = leftColumn->Add(new TextView(""));
		showDeleteCheckbox = true;
		break;
	}
	case ZipFileContents::SAVE_DATA:
	{
		std::string_view question = iz->T("Import savedata from ZIP file");
		leftColumn->Add(new TextView(question))->SetBig(true);
		leftColumn->Add(new TextView(zipFileInfo_.gameTitle + ": " + zipFileInfo_.savedataDir));

		Path savedataDir = GetSysDirectory(DIRECTORY_SAVEDATA);
		ZipContainer zipFile = ZipOpenPath(zipPath_);
		overwrite = !CanExtractWithoutOverwrite(zipFile, savedataDir, 50);
		ZipClose(zipFile);

		destFolders_.push_back(savedataDir);
		int columnWidth = 300;

		LinearLayout *compareColumns = leftColumn->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		LinearLayout *leftCompare = new LinearLayout(ORIENT_VERTICAL);
		leftCompare->Add(new TextView(iz->T("Data to import")));
		compareColumns->Add(leftCompare);
		leftCompare->Add(new SavedataView(*screenManager()->getUIContext(), Path(), IdentifiedFileType::PSP_SAVEDATA_DIRECTORY,
			zipFileInfo_.gameTitle, zipFileInfo_.savedataTitle, zipFileInfo_.savedataDetails, NiceSizeFormat(zipFileInfo_.totalFileSize), zipFileInfo_.mTime, false, new LinearLayoutParams(columnWidth, WRAP_CONTENT)));

		// Check for potential overwrite at destination, and ask the user if it's OK to overwrite.
		if (overwrite) {
			savedataToOverwrite_ = savedataDir / zipFileInfo_.savedataDir;
			std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savedataToOverwrite_, GameInfoFlags::FILE_TYPE | GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);

			LinearLayout *rightCompare = new LinearLayout(ORIENT_VERTICAL);
			rightCompare->Add(new TextView(iz->T("Existing data")));

			compareColumns->Add(rightCompare);
			existingSaveView_ = rightCompare->Add(new SavedataView(*screenManager()->getUIContext(), ginfo.get(), IdentifiedFileType::PSP_SAVEDATA_DIRECTORY, false, new LinearLayoutParams(columnWidth, WRAP_CONTENT)));
			if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
				rightCompare->Add(new Button(di->T("Show in folder")))->OnClick.Add([=](UI::EventParams &) {
					System_ShowFileInFolder(savedataToOverwrite_);
				});
			}
		}

		doneView_ = leftColumn->Add(new TextView(""));
		showDeleteCheckbox = true;
		break;
	}
	case ZipFileContents::FRAME_DUMP:
		leftColumn->Add(new TextView(zipFileInfo_.contentName));
		// It's a frame dump, add a play button!
		break;
	case ZipFileContents::EXTRACTED_GAME:
		// We can do something smarter here later.
		leftColumn->Add(new TextView(GetFriendlyPath(zipPath_)));
		leftColumn->Add(new TextView(er->T("File format not supported")));
		break;
	case ZipFileContents::UNKNOWN:
		leftColumn->Add(new TextView(iz->T("Zip file does not contain PSP software"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		break;
	default:
		leftColumn->Add(new TextView(er->T("The file is not a valid zip file"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
		break;
	}

	if (destFolders_.size() > 1) {
		leftColumn->Add(new TextView(iz->T("Install into folder")));
		for (int i = 0; i < (int)destFolders_.size(); i++) {
			leftColumn->Add(new RadioButton(&destFolderChoice_, i, GetFriendlyPath(destFolders_[i])));
		}
	} else if (destFolders_.size() == 1 && zipFileInfo_.contents != ZipFileContents::SAVE_DATA) {
		leftColumn->Add(new TextView(iz->T("Install into folder")));
		leftColumn->Add(new TextView(GetFriendlyPath(destFolders_[0])))->SetAlign(FLAG_WRAP_TEXT);
	}

	if (overwrite) {
		leftColumn->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"), ""));
	}
}

void InstallZipScreen::BeforeCreateViews() {
	File::FileInfo fileInfo;
	File::GetFileInfo(zipPath_, &fileInfo);
}

bool InstallZipScreen::key(const KeyInput &key) {
	// Ignore all key presses during download and installation to avoid user escape
	if (g_GameManager.GetState() == GameManagerState::IDLE) {
		return UIDialogScreen::key(key);
	}
	return false;
}

void InstallZipScreen::OnInstall(UI::EventParams &params) {
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
		if (playChoice_) {
			playChoice_->SetEnabled(false);  // need to exit this screen to played the installed one. We could make this smarter.
		}
		if (installChoice_) {
			installChoice_->SetEnabled(false);
		}
	}
}

void InstallZipScreen::OnPlay(UI::EventParams &params) {
	screenManager()->switchScreen(new EmuScreen(zipPath_));
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
				doneView_->SetText(iz->T(err));
		} else if (installStarted_) {
			if (doneView_) {
				doneView_->SetText(iz->T("Installed!"));
			}
			MainScreen::showHomebrewTab = returnToHomebrew_;
		}
	}

	if (existingSaveView_) {
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savedataToOverwrite_, GameInfoFlags::FILE_TYPE | GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);
		existingSaveView_->UpdateGame(ginfo.get());
	}
	UIBaseDialogScreen::update();
}
