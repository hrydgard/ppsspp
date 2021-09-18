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

#include "Common/Thread/ThreadManager.h"

#include "Core/Util/GameManager.h"
#include "Core/System.h"
#include "Core/Config.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"

static bool FolderSeemsToBeUsed(Path newMemstickFolder) {
	// Inspect the potential new folder.
	if (File::Exists(newMemstickFolder / "PSP/SAVEDATA") || File::Exists(newMemstickFolder / "SAVEDATA")) {
		// Does seem likely. We could add more criteria like checking for actual savegames or something.
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
}

void MemStickScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto iz = GetI18NCategory("MemStick");

	Margins actionMenuMargins(15, 15, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	Spacer *spacerColumn = new Spacer(new LinearLayoutParams(20.0, FILL_PARENT, 0.0f));
	ScrollView *leftColumnScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));

	ViewGroup *leftColumn = new LinearLayoutList(ORIENT_VERTICAL);
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, actionMenuMargins));
	root_->Add(spacerColumn);
	root_->Add(leftColumnScroll);
	root_->Add(rightColumnItems);

	leftColumnScroll->Add(leftColumn);

	if (initialSetup_) {
		leftColumn->Add(new TextView(iz->T("Welcome to PPSSPP!"), ALIGN_LEFT, false));
		leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));
	}

	leftColumn->Add(new TextView(iz->T("MemoryStickDescription", "Choose PSP data storage (Memory Stick):"), ALIGN_LEFT, false));

	// For legacy Android systems, so you can switch back to the old ways if you move to SD or something.
	// TODO: Gonna need a scroll view.
#if PPSSPP_PLATFORM(ANDROID)
	if (!System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
		leftColumn->Add(new Choice(iz->T("Use PSP folder at root of storage")))->OnClick.Handle(this, &MemStickScreen::OnUseStorageRoot);
		leftColumn->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("EasyUSBAccess", "Easy USB access")))->SetBullet(true);
	}
#endif

	// Let's only offer the browse-for-folder choice on Android 10 or later.
	// Earlier versions often don't really have working folder browsers.
	bool storageBrowserWorking = System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 29;

	if (storageBrowserWorking) {
		leftColumn->Add(new Choice(iz->T("Create or Choose a PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnBrowse);
		leftColumn->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("EasyUSBAccess", "Easy USB access")))->SetBullet(true);
	} else {
		leftColumn->Add(new Choice(iz->T("Manually specify PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnSetFolderManually);
		leftColumn->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold.")))->SetBullet(true);
	}

	leftColumn->Add(new Choice(iz->T("Use App Private Directory")))->OnClick.Handle(this, &MemStickScreen::OnUseInternalStorage);
	// Consider https://www.compart.com/en/unicode/U+26A0 (unicode warning sign?)? or a graphic?
	leftColumn->Add(new TextView(iz->T("DataWillBeLostOnUninstall", "Warning! Data will be lost when you uninstall PPSSPP!")))->SetBullet(true);
	leftColumn->Add(new TextView(iz->T("DataCannotBeShared", "Data CANNOT be shared between PPSSPP regular/Gold!")))->SetBullet(true);
#if GOLD
	leftColumn->Add(new TextView(iz->T("USBAccessThroughGold", "USB access through Android/data/org.ppsspp.ppssppgold/files")))->SetBullet(true);
#else
	leftColumn->Add(new TextView(iz->T("USBAccessThrough", "USB access through Android/data/org.ppsspp.ppsspp/files")))->SetBullet(true);
#endif

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	if (!initialSetup_) {
		rightColumnItems->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		rightColumnItems->Add(new Choice(di->T("Help")))->OnClick.Handle<MemStickScreen>(this, &MemStickScreen::OnHelp);
	}

	INFO_LOG(SYSTEM, "MemStickScreen: initialSetup=%d", (int)initialSetup_);
}

UI::EventReturn MemStickScreen::OnHelp(UI::EventParams &params) {
	LaunchBrowser("https://ppsspp.org/guide_storage.html");

	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnSetFolderManually(UI::EventParams &params) {
	// The old way, from before scoped storage.
#if PPSSPP_PLATFORM(ANDROID)
	auto sy = GetI18NCategory("System");
	System_InputBoxGetString(sy->T("Memory Stick Folder"), g_Config.memStickDirectory.ToString(), [&](bool result, const std::string &value) {
		auto sy = GetI18NCategory("System");
		auto di = GetI18NCategory("Dialog");

		if (result) {
			std::string newPath = value;
			size_t pos = newPath.find_last_not_of("/");
			// Gotta have at least something but a /, and also needs to start with a /.
			if (newPath.empty() || pos == newPath.npos || newPath[0] != '/') {
				settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
				return;
			}
			if (pos != newPath.size() - 1) {
				newPath = newPath.substr(0, pos + 1);
			}

			if (newPath.empty()) {
				// Reuse below message instead of adding yet another string.
				SystemToast(sy->T("Path does not exist!"));
				return;
			}

			Path pendingMemStickFolder(newPath);

			if (!File::Exists(pendingMemStickFolder)) {
				// Try to fix the path string, apparently some users got used to leaving out the /.
				if (newPath[0] != '/') {
					newPath = "/" + newPath;
				}

				pendingMemStickFolder = Path(newPath);
			}

			if (!File::Exists(pendingMemStickFolder) && pendingMemStickFolder.Type() == PathType::NATIVE) {
				// Still no path? Try to automatically fix the case.
				std::string oldNewPath = newPath;
				FixPathCase(Path(""), newPath, FixPathCaseBehavior::FPC_FILE_MUST_EXIST);
				if (oldNewPath != newPath) {
					NOTICE_LOG(IO, "Fixed path case: %s -> %s", oldNewPath.c_str(), newPath.c_str());
					pendingMemStickFolder = Path(newPath);
				} else {
					NOTICE_LOG(IO, "Failed to fix case of path %s (result: %s)", newPath.c_str(), oldNewPath.c_str());
				}
			}

			if (pendingMemStickFolder == g_Config.memStickDirectory) {
				// Same directory as before - all good. Nothing to do.
				TriggerFinish(DialogResult::DR_OK);
				return;
			}

			if (!File::Exists(pendingMemStickFolder)) {
				SystemToast(sy->T("Path does not exist!"));
				return;
			}

			screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
		}
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnUseInternalStorage(UI::EventParams &params) {
	Path pendingMemStickFolder = Path(g_extFilesDir);

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
		}
	} else if (pendingMemStickFolder != g_Config.memStickDirectory) {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	} else {
		// User chose the same directory it's already in. Let's just bail.
		TriggerFinish(DialogResult::DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnUseStorageRoot(UI::EventParams &params) {
	Path pendingMemStickFolder = Path(g_externalDir);

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
		}
	} else if (pendingMemStickFolder != g_Config.memStickDirectory) {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	} else {
		// User chose the same directory it's already in. Let's just bail.
		TriggerFinish(DialogResult::DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnBrowse(UI::EventParams &params) {
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
			Path pendingMemStickFolder = Path(filename);

			if (pendingMemStickFolder == g_Config.memStickDirectory) {
				auto iz = GetI18NCategory("MemStick");
				return;
			}

			bool existingFiles = FolderSeemsToBeUsed(pendingMemStickFolder);
			screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, initialSetup_));
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

// Keep the size with the file, so we can skip overly large ones in the move.
// The user will have to take care of them afterwards, it'll just take too long probably.
struct FileSuffix {
	std::string suffix;
	u64 fileSize;
};

static bool ListFileSuffixesRecursively(const Path &root, Path folder, std::vector<std::string> &dirSuffixes, std::vector<FileSuffix> &fileSuffixes) {
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
				fileSuffixes.push_back(FileSuffix{ fileSuffix, file.size });
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

ConfirmMemstickMoveScreen::~ConfirmMemstickMoveScreen() {
	if (moveDataTask_) {
		INFO_LOG(SYSTEM, "Move Data task still running, blocking on it");
		moveDataTask_->BlockUntilReady();
		delete moveDataTask_;
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
	if (!initialSetup_) {
		leftColumn->Add(new TextView(iz->T("PPSSPP will restart after the change."), ALIGN_LEFT, false));
	}
	leftColumn->Add(new TextView(newMemstickFolder_.ToVisualString(), ALIGN_LEFT, false));
	std::string newFreeSpaceText = std::string(iz->T("Free space")) + ": " + FormatSpaceString(freeSpaceNew);
	leftColumn->Add(new TextView(newFreeSpaceText, ALIGN_LEFT, false));
	if (existingFilesInNewFolder_) {
		leftColumn->Add(new TextView(iz->T("Already contains data."), ALIGN_LEFT, false));
		if (!moveData_) {
			leftColumn->Add(new TextView(iz->T("No data will be changed."), ALIGN_LEFT, false));
		}
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

	if (moveDataTask_) {
		progressView_ = leftColumn->Add(new TextView(progressReporter_.Get()));
	} else {
		progressView_ = nullptr;
	}

	if (!moveDataTask_) {
		if (!initialSetup_) {
			leftColumn->Add(new CheckBox(&moveData_, iz->T("Move Data")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnMoveDataClick);
		}

		leftColumn->Add(new Choice(di->T("OK")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnConfirm);
		leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	}
}

UI::EventReturn ConfirmMemstickMoveScreen::OnMoveDataClick(UI::EventParams &params) {
	RecreateViews();
	return UI::EVENT_DONE;
}

void ConfirmMemstickMoveScreen::update() {
	UIDialogScreenWithBackground::update();
	auto iz = GetI18NCategory("MemStick");

	if (moveDataTask_) {
		if (progressView_) {
			progressView_->SetText(progressReporter_.Get());
		}

		MoveResult *result = moveDataTask_->Poll();

		if (result) {
			if (result->success) {
				progressReporter_.Set(iz->T("Done!"));
				INFO_LOG(SYSTEM, "Move data task finished successfully!");
				// Succeeded!
				FinishFolderMove();
			} else {
				progressReporter_.Set(iz->T("Failed to move some files!"));
				INFO_LOG(SYSTEM, "Move data task failed!");
				// What do we do here? We might be in the middle of a move... Bad.
				RecreateViews();
			}
			delete moveDataTask_;
			moveDataTask_ = nullptr;
		}
	}
}

UI::EventReturn ConfirmMemstickMoveScreen::OnConfirm(UI::EventParams &params) {
	auto sy = GetI18NCategory("System");
	auto iz = GetI18NCategory("MemStick");

	// Transfer all the files in /PSP from the original directory.
	// Should probably be done on a background thread so we can show some UI.
	// So we probably need another screen for this with a progress bar..
	// If the directory itself is called PSP, don't go below.

	if (moveData_) {
		progressReporter_.Set(iz->T("Starting move..."));

		moveDataTask_ = Promise<MoveResult>::Spawn(&g_threadManager, [&]() -> MoveResult * {
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
			std::vector<FileSuffix> fileSuffixesToMove;
			std::vector<std::string> directorySuffixesToCreate;

			// NOTE: It's correct to pass moveSrc twice here, it's to keep the root in the recursion.
			if (!ListFileSuffixesRecursively(moveSrc, moveSrc, directorySuffixesToCreate, fileSuffixesToMove)) {
				// TODO: Handle failure listing files.
				std::string error = "Failed to read old directory";
				INFO_LOG(SYSTEM, "%s", error.c_str());
				progressReporter_.Set(iz->T(error.c_str()));
				return new MoveResult{ false, error };
			}

			bool dryRun = false;  // Useful for debugging.

			size_t failedFiles = 0;
			size_t skippedFiles = 0;

			// We're not moving huge files like ISOs during this process, unless
			// they can be directly moved, without rewriting the file.
			const uint64_t BIG_FILE_THRESHOLD = 24 * 1024 * 1024;

			if (!moveSrc.empty()) {
				// Better not interrupt the app while this is happening!

				// Create all the necessary directories.
				for (auto &dirSuffix : directorySuffixesToCreate) {
					Path dir = moveDest / dirSuffix;
					if (dryRun) {
						INFO_LOG(SYSTEM, "dry run: Would have created dir '%s'", dir.c_str());
					} else {
						INFO_LOG(SYSTEM, "Creating dir '%s'", dir.c_str());
						// Just ignore already-exists errors.
						File::CreateDir(dir);
					}
				}

				for (auto &fileSuffix : fileSuffixesToMove) {
					progressReporter_.Set(StringFromFormat("%s (%s)", fileSuffix.suffix.c_str(), NiceSizeFormat(fileSuffix.fileSize).c_str()));

					Path from = moveSrc / fileSuffix.suffix;
					Path to = moveDest / fileSuffix.suffix;

					if (fileSuffix.fileSize > BIG_FILE_THRESHOLD) {
						// We only move big files if it's fast to do so.
						if (dryRun) {
							INFO_LOG(SYSTEM, "dry run: Would have moved '%s' to '%s' (%d bytes) if fast", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
						} else {
							if (!File::MoveIfFast(from, to)) {
								INFO_LOG(SYSTEM, "Skipped moving file '%s' to '%s' (%s)", from.c_str(), to.c_str(), NiceSizeFormat(fileSuffix.fileSize).c_str());
								skippedFiles++;
							} else {
								INFO_LOG(SYSTEM, "Moved file '%s' to '%s'", from.c_str(), to.c_str());
							}
						}
					} else {
						if (dryRun) {
							INFO_LOG(SYSTEM, "dry run: Would have moved '%s' to '%s' (%d bytes)", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
						} else {
							// Remove the "from" prefix from the path.
							// We have to drop down to string operations for this.
							if (!File::Move(from, to)) {
								ERROR_LOG(SYSTEM, "Failed to move file '%s' to '%s'", from.c_str(), to.c_str());
								failedFiles++;
								// Should probably just bail?
							} else {
								INFO_LOG(SYSTEM, "Moved file '%s' to '%s'", from.c_str(), to.c_str());
							}
						}
					}
				}

				// Delete all the old, now hopefully empty, directories.
				for (auto &dirSuffix : directorySuffixesToCreate) {
					Path dir = moveSrc / dirSuffix;
					if (dryRun) {
						INFO_LOG(SYSTEM, "dry run: Would have deleted dir '%s'", dir.c_str());
					} else {
						INFO_LOG(SYSTEM, "Deleting dir '%s'", dir.c_str());
						if (!File::Exists(dir)) {
							File::DeleteDir(dir);
						}
					}
				}
			}

			return new MoveResult{ true, "", failedFiles };
		}, TaskType::IO_BLOCKING);

		RecreateViews();
	} else {
		FinishFolderMove();
	}

	return UI::EVENT_DONE;
}

void ConfirmMemstickMoveScreen::FinishFolderMove() {
	auto iz = GetI18NCategory("MemStick");

	// Successful so far, switch the memstick folder.
	if (!SwitchMemstickFolderTo(newMemstickFolder_)) {
		// TODO: More precise errors.
		error_ = iz->T("That folder doesn't work as a memstick folder.");
		return;
	}

	// If the chosen folder already had a config, reload it!
	g_Config.Load();

	if (!initialSetup_) {
		// We restart the app here, to get the new settings.
		System_SendMessage("graphics_restart", "");
	}

	if (g_Config.Save("MemstickPathChanged")) {
		TriggerFinish(DialogResult::DR_OK);
	} else {
		error_ = iz->T("Failed to save config");
		RecreateViews();
	}
}
