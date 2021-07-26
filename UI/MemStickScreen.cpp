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

#include "ppsspp_config.h"

#include "android/jni/app-android.h"

#include "Common/Log.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/File/AndroidStorage.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DiskFree.h"

#include "Core/Util/GameManager.h"
#include "Core/System.h"
#include "Core/Config.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"

static bool FolderSeemsToBeUsed(Path newMemstickFolder) {
	// Inspect the potential new folder.
	if (File::Exists(newMemstickFolder / "PSP") || File::Exists(newMemstickFolder / "SYSTEM")) {
		// Does seem likely. We could add more critera like checking for actual savegames or something.
		return true;
	} else {
		return false;
	}
}

static bool SwitchMemstickFolderTo(Path newMemstickFolder) {
	Path testWriteFile = newMemstickFolder / ".write_verify_file";

	// Doesn't already exist, create.
	// Should this ever happen?
	if (newMemstickFolder.Type() == PathType::NATIVE) {
		if (!File::Exists(newMemstickFolder)) {
			File::CreateFullPath(newMemstickFolder);
		}
		if (!File::WriteDataToFile(true, "1", 1, testWriteFile)) {
			return false;
		}
		File::Delete(testWriteFile);
	} else {
		// TODO: Do the same but with scoped storage? Not really necessary, right? If it came from a browse
		// for folder, we can assume it exists and is writable, barring wacky race conditions like the user
		// being connected by USB and deleting it.
	}

	Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
	std::string str = newMemstickFolder.ToString();
	if (!File::WriteDataToFile(true, str.c_str(), (unsigned int)str.size(), memStickDirFile)) {
		ERROR_LOG(SYSTEM, "Failed to write memstick path '%s' to '%s'", newMemstickFolder.c_str(), memStickDirFile.c_str());
		// Not sure what to do if this file.
	}

	// Save so the settings, at least, are transferred.
	g_Config.memStickDirectory = newMemstickFolder;
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.UpdateIniLocation();

	return true;
}

static std::string FormatSpaceString(int64_t space) {
	if (space >= 0) {
		char buffer[50];
		NiceSizeFormat(space, buffer, sizeof(buffer));
		return buffer;
	} else {
		return "N/A";
	}
}

MemStickScreen::MemStickScreen(bool initialSetup)
	: initialSetup_(initialSetup) {
	pendingMemStickFolder_ = g_Config.memStickDirectory;
}

void MemStickScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto iz = GetI18NCategory("MemStick");

	Margins actionMenuMargins(15, 15, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	Spacer *spacerColumn = new Spacer(new LinearLayoutParams(20.0, FILL_PARENT, 0.0f));
	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(spacerColumn);
	root_->Add(leftColumn);
	root_->Add(rightColumnItems);

	if (initialSetup_) {
		leftColumn->Add(new TextView(iz->T("Welcome to PPSSPP!"), ALIGN_LEFT, false));
		leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));
	}

	leftColumn->Add(new TextView(iz->T("MemoryStickDescription", "Choose PSP data storage (Memory Stick)"), ALIGN_LEFT, false));

	leftColumn->Add(new Choice(iz->T("Create or Choose a PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnBrowse);
	leftColumn->Add(new TextView(iz->T("ChooseFolderDesc", "* Data will stay even if you uninstall PPSSPP.\n* Data can be shared with PPSSPP Gold\n* Easy USB access"), ALIGN_LEFT, false));

	leftColumn->Add(new Choice(iz->T("Use App Private Directory")))->OnClick.Handle(this, &MemStickScreen::OnUseInternalStorage);
	leftColumn->Add(new TextView(iz->T("InternalStorageDesc", "* Warning! Data will be deleted if you uninstall PPSSPP!\n* Data cannot be shared with PPSSPP Gold\n* USB access through Android/data/org.ppsspp.ppsspp/files"), ALIGN_LEFT, false));

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	if (!initialSetup_) {
		leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	}

	INFO_LOG(SYSTEM, "MemStickScreen: initialSetup=%d", (int)initialSetup_);
}

UI::EventReturn MemStickScreen::OnUseInternalStorage(UI::EventParams &params) {
	pendingMemStickFolder_ = Path(g_extFilesDir);

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder_)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
		}
	} else {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder_, false));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnBrowse(UI::EventParams &params) {
	auto sy = GetI18NCategory("System");
	System_SendMessage("browse_folder", "");
	return UI::EVENT_DONE;
}

void MemStickScreen::sendMessage(const char *message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIDialogScreenWithBackground::sendMessage(message, value);

	if (screenManager()->topScreen() == this) {
		if (!strcmp(message, "browse_folderSelect")) {
			std::string filename;
			filename = value;
			INFO_LOG(SYSTEM, "Got folder: '%s'", filename.c_str());

			// Browse finished. Let's pop up the confirmation dialog.
			pendingMemStickFolder_ = Path(filename);
			bool existingFiles = FolderSeemsToBeUsed(pendingMemStickFolder_);
			screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder_, initialSetup_));
		}
	}
}

void MemStickScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DialogResult::DR_OK) {
		INFO_LOG(SYSTEM, "Confirmation screen done - moving on.");
		// There's a screen manager bug if we call TriggerFinish directly.
		// Can't be bothered right now, so we pick this up in update().
		done_ = true;
	}
	// otherwise, we just keep going.
}

void MemStickScreen::update() {
	UIDialogScreenWithBackground::update();
	if (done_) {
		TriggerFinish(DialogResult::DR_OK);
		done_ = false;
	}
}

static bool ListFileSuffixesRecursively(const Path &root, Path folder, std::vector<std::string> &dirSuffixes, std::vector<std::string> &fileSuffixes) {
	std::vector<File::FileInfo> files;
	if (!File::GetFilesInDir(folder, &files)) {
		return false;
	}

	for (auto &file : files) {
		if (file.isDirectory) {
			std::string dirSuffix;
			if (root.ComputePathTo(file.fullName, dirSuffix)) {
				dirSuffixes.push_back(dirSuffix);
				ListFileSuffixesRecursively(root, folder / file.name, dirSuffixes, fileSuffixes);
			} else {
				ERROR_LOG(SYSTEM, "Failed to compute PathTo from '%s' to '%s'", root.c_str(), folder.c_str());
			}
		} else {
			std::string fileSuffix;
			if (root.ComputePathTo(file.fullName, fileSuffix)) {
				fileSuffixes.push_back(fileSuffix);
			}
		}
	}

	return true;
}

ConfirmMemstickMoveScreen::ConfirmMemstickMoveScreen(Path newMemstickFolder, bool initialSetup)
	: newMemstickFolder_(newMemstickFolder), initialSetup_(initialSetup) {
	existingFilesInNewFolder_ = FolderSeemsToBeUsed(newMemstickFolder);
	if (initialSetup_) {
		moveData_ = false;
	}
}

void ConfirmMemstickMoveScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory("Dialog");
	auto sy = GetI18NCategory("System");
	auto iz = GetI18NCategory("MemStick");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	Path oldMemstickFolder = g_Config.memStickDirectory;

	Spacer *spacerColumn = new Spacer(new LinearLayoutParams(20.0, FILL_PARENT, 0.0f));
	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	root_->Add(spacerColumn);
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	int64_t freeSpaceNew;
	int64_t freeSpaceOld;
	free_disk_space(newMemstickFolder_, freeSpaceNew);
	free_disk_space(oldMemstickFolder, freeSpaceOld);

	leftColumn->Add(new TextView(iz->T("New PSP Data Folder"), ALIGN_LEFT, false));
	leftColumn->Add(new TextView(newMemstickFolder_.ToVisualString(), ALIGN_LEFT, false));
	std::string newFreeSpaceText = std::string(iz->T("Free space")) + ": " + FormatSpaceString(freeSpaceNew);
	leftColumn->Add(new TextView(newFreeSpaceText, ALIGN_LEFT, false));
	if (existingFilesInNewFolder_) {
		leftColumn->Add(new TextView(iz->T("Warning: Already contains data"), ALIGN_LEFT, false));
	}
	if (!error_.empty()) {
		leftColumn->Add(new TextView(error_, ALIGN_LEFT, false));
	}

	if (!oldMemstickFolder.empty()) {
		std::string oldFreeSpaceText = std::string(iz->T("Free space")) + ": " + FormatSpaceString(freeSpaceOld);
		rightColumn->Add(new TextView(iz->T("Old PSP Data Folder"), ALIGN_LEFT, false));
		rightColumn->Add(new TextView(oldMemstickFolder.ToVisualString(), ALIGN_LEFT, false));
		rightColumn->Add(new TextView(oldFreeSpaceText, ALIGN_LEFT, false));
	}

	if (!initialSetup_) {
		leftColumn->Add(new CheckBox(&moveData_, iz->T("Move Data")));
	}

	leftColumn->Add(new Choice(di->T("OK")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnConfirm);
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

UI::EventReturn ConfirmMemstickMoveScreen::OnConfirm(UI::EventParams &params) {
	auto sy = GetI18NCategory("System");
	auto iz = GetI18NCategory("MemStick");

	// Transfer all the files in /PSP from the original directory.
	// Should probably be done on a background thread so we can show some UI.
	// So we probably need another screen for this with a progress bar..
	// If the directory itself is called PSP, don't go below.

	if (moveData_) {
		Path moveSrc = g_Config.memStickDirectory;
		Path moveDest = newMemstickFolder_;
		if (moveSrc.GetFilename() != "PSP") {
			moveSrc = moveSrc / "PSP";
		}
		if (moveDest.GetFilename() != "PSP") {
			moveDest = moveDest / "PSP";
			File::CreateDir(moveDest);
		}

		INFO_LOG(SYSTEM, "About to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());

		// Search through recursively, listing the files to move and also summing their sizes.
		std::vector<std::string> fileSuffixesToMove;
		std::vector<std::string> directorySuffixesToCreate;

		// NOTE: It's correct to pass moveSrc twice here, it's to keep the root in the recursion.
		if (!ListFileSuffixesRecursively(moveSrc, moveSrc, directorySuffixesToCreate, fileSuffixesToMove)) {
			// TODO: Handle failure listing files.
			error_ = "Failed to read old directory";
			INFO_LOG(SYSTEM, "%s", error_.c_str());
			return UI::EVENT_DONE;
		}

		bool dryRun = false;  // Useful for debugging.

		size_t moveFailures = 0;

		if (!moveSrc.empty()) {
			// Better not interrupt the app while this is happening!

			// Create all the necessary directories.
			for (auto &dirSuffix : directorySuffixesToCreate) {
				Path dir = moveDest / dirSuffix;
				if (dryRun) {
					INFO_LOG(SYSTEM, "dry run: Would have created dir '%s'", dir.c_str());
				} else {
					INFO_LOG(SYSTEM, "Creating dir '%s'", dir.c_str());
					if (!File::Exists(dir)) {
						File::CreateDir(dir);
					}
				}
			}

			for (auto &fileSuffix : fileSuffixesToMove) {
				Path from = moveSrc / fileSuffix;
				Path to = moveDest / fileSuffix;
				if (dryRun) {
					INFO_LOG(SYSTEM, "dry run: Would have moved '%s' to '%s'", from.c_str(), to.c_str());
				} else {
					// Remove the "from" prefix from the path.
					// We have to drop down to string operations for this.
					if (!File::Move(from, to)) {
						ERROR_LOG(SYSTEM, "Failed to move file '%s' to '%s'", from.c_str(), to.c_str());
						moveFailures++;
						// Should probably just bail?
					} else {
						INFO_LOG(SYSTEM, "Moved file '%s' to '%s'", from.c_str(), to.c_str());
					}
				}
			}
		}

		if (moveFailures > 0) {
			error_ = "Failed to move some files!";
			RecreateViews();
			return UI::EVENT_DONE;
		}
	}

	// Successful so far, switch the memstick folder.
	if (!SwitchMemstickFolderTo(newMemstickFolder_)) {
		// TODO: More precise errors.
		error_ = iz->T("That folder doesn't work as a memstick folder.");
		return UI::EVENT_DONE;
	}

	// If the chosen folder already had a config, reload it!
	g_Config.Load();

	if (g_Config.Save("MemstickPathChanged")) {
		TriggerFinish(DialogResult::DR_OK);
	} else {
		error_ = iz->T("Failed to save config");
		RecreateViews();
	}

	return UI::EVENT_DONE;
}
