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
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/File/AndroidStorage.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DiskFree.h"

#include "Common/Thread/ThreadManager.h"

#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/MemStick.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"
#include "UI/OnScreenDisplay.h"

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
#if PPSSPP_PLATFORM(ANDROID)
	// Let's only offer the browse-for-folder choice on Android 10 or later.
	// Earlier versions often don't really have working folder browsers.
	storageBrowserWorking_ = System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 29;
#else
	// For testing UI only
	storageBrowserWorking_ = true;
#endif

	if (initialSetup_) {
		// Preselect current choice.
		if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
			choice_ = CHOICE_BROWSE_FOLDER;
		} else {
			WARN_LOG_REPORT(SYSTEM, "Scoped storage not enabled - shouldn't be in MemStickScreen at initial setup");
			choice_ = CHOICE_STORAGE_ROOT;
			// Shouldn't really be here in initial setup.
		}
	} else {
		// Detect the current choice, so it's preselected in the UI.
#if PPSSPP_PLATFORM(UWP)
		if (g_Config.memStickDirectory == g_Config.internalDataDirectory) {
#else
		if (g_Config.memStickDirectory == Path(g_extFilesDir)) {
#endif
			choice_ = CHOICE_PRIVATE_DIRECTORY;
		} else if (g_Config.memStickDirectory == Path(g_externalDir)) {
			choice_ = CHOICE_STORAGE_ROOT;
		} else if (storageBrowserWorking_) {
			choice_ = CHOICE_BROWSE_FOLDER;
		} else {
			choice_ = CHOICE_SET_MANUAL;
		}
	}
}

static void AddExplanation(UI::ViewGroup *viewGroup, MemStickScreen::Choice choice, UI::View *extraView = nullptr) {
	auto iz = GetI18NCategory(I18NCat::MEMSTICK);
	using namespace UI;

	int flags = FLAG_WRAP_TEXT;

	UI::ViewGroup *holder = new UI::LinearLayout(ORIENT_VERTICAL);

	UI::ViewGroup *indentHolder = new UI::LinearLayout(ORIENT_HORIZONTAL);
	indentHolder->Add(new Spacer(20.0));
	indentHolder->Add(holder);

	viewGroup->Add(indentHolder);

	if (extraView) {
		holder->Add(extraView);
	}

	switch (choice) {
	case MemStickScreen::CHOICE_STORAGE_ROOT:
		// Old school choice
		holder->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP"), flags, false))->SetBullet(true);
		holder->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold"), flags, false))->SetBullet(true);
		holder->Add(new TextView(iz->T("EasyUSBAccess", "Easy USB access"), flags, false))->SetBullet(true);
		break;
	case MemStickScreen::CHOICE_BROWSE_FOLDER:
		holder->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP"), flags, false))->SetBullet(true);
		holder->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold"), flags, false))->SetBullet(true);
#if !PPSSPP_PLATFORM(UWP)
		holder->Add(new TextView(iz->T("EasyUSBAccess", "Easy USB access"), flags, false))->SetBullet(true);
#endif
		break;
	case MemStickScreen::CHOICE_PRIVATE_DIRECTORY:
		// Consider https://www.compart.com/en/unicode/U+26A0 (unicode warning sign?)? or a graphic?
		holder->Add(new TextView(iz->T("DataWillBeLostOnUninstall", "Warning! Data will be lost when you uninstall PPSSPP!"), flags, false))->SetBullet(true);
		holder->Add(new TextView(iz->T("DataCannotBeShared", "Data CANNOT be shared between PPSSPP regular/Gold!"), flags, false))->SetBullet(true);
#if !PPSSPP_PLATFORM(UWP)
#if GOLD
		holder->Add(new TextView(iz->T("USBAccessThroughGold", "USB access through Android/data/org.ppsspp.ppssppgold/files"), flags, false))->SetBullet(true);
#else
		holder->Add(new TextView(iz->T("USBAccessThrough", "USB access through Android/data/org.ppsspp.ppsspp/files"), flags, false))->SetBullet(true);
#endif
#endif
		break;
	case MemStickScreen::CHOICE_SET_MANUAL:
	default:
		holder->Add(new TextView(iz->T("EasyUSBAccess", "Easy USB access"), flags, false))->SetBullet(true);
		// What more?

		// Should we have a special text here? It'll popup a text window for editing.
		break;
	}
}

void MemStickScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::MEMSTICK);

	Margins actionMenuMargins(15, 0, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	Spacer *spacerColumn = new Spacer(new LinearLayoutParams(20.0, FILL_PARENT, 0.0f));
	ScrollView *mainColumnScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));

	ViewGroup *mainColumn = new LinearLayoutList(ORIENT_VERTICAL);
	mainColumnScroll->Add(mainColumn);

	root_->Add(spacerColumn);
	root_->Add(mainColumnScroll);

	if (initialSetup_) {
		mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));
		mainColumn->Add(new TextView(iz->T("Welcome to PPSSPP!"), ALIGN_LEFT, false));
	}

	mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 18.0f, 0.0f)));

	mainColumn->Add(new TextView(iz->T("MemoryStickDescription", "Choose where to keep PSP data (Memory Stick)"), ALIGN_LEFT, false));
	mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 18.0f, 0.0f)));

	ViewGroup *subColumns = new LinearLayoutList(ORIENT_HORIZONTAL);
	mainColumn->Add(subColumns);

	ViewGroup *leftColumn = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	subColumns->Add(leftColumn);

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(220, FILL_PARENT, actionMenuMargins));
	subColumns->Add(rightColumnItems);

	// For legacy Android systems, so you can switch back to the old ways if you move to SD or something.
	// Trying to avoid needing a scroll view, so only showing the explanation for one option at a time.
#if !PPSSPP_PLATFORM(UWP)
	if (!System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
		leftColumn->Add(new RadioButton(&choice_, CHOICE_STORAGE_ROOT, iz->T("Use PSP folder at root of storage")))->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
		if (choice_ == CHOICE_STORAGE_ROOT) {
			AddExplanation(leftColumn, (MemStickScreen::Choice)choice_);
		}
	}
#endif

	if (storageBrowserWorking_) {
		leftColumn->Add(new RadioButton(&choice_, CHOICE_BROWSE_FOLDER, iz->T("Create or Choose a PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);

		// TODO: Show current folder here if we have one set.
	} else {
		leftColumn->Add(new RadioButton(&choice_, CHOICE_SET_MANUAL, iz->T("Manually specify PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
		leftColumn->Add(new TextView(iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP.")))->SetBullet(true);
		leftColumn->Add(new TextView(iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold.")))->SetBullet(true);
		// TODO: Show current folder here if we have one set.
	}
	errorNoticeView_ = leftColumn->Add(new NoticeView(NoticeLevel::WARN, iz->T("Cancelled - try again"), ""));
	errorNoticeView_->SetVisibility(UI::V_GONE);

	if (choice_ == CHOICE_BROWSE_FOLDER || choice_ == CHOICE_SET_MANUAL) {
		UI::View *extraView = nullptr;
		if (!g_Config.memStickDirectory.empty()) {
			extraView = new TextView(StringFromFormat("    %s: %s", iz->T_cstr("Current"), g_Config.memStickDirectory.ToVisualString().c_str()), ALIGN_LEFT, false);
		}
		AddExplanation(leftColumn, (MemStickScreen::Choice)choice_, extraView);
	}

	std::string privateString(iz->T("Use App Private Data"));

	if (initialSetup_) {
		privateString = StringFromFormat("%s (%s)", iz->T_cstr("Skip for now"), privateString.c_str());
	}

	leftColumn->Add(new RadioButton(&choice_, CHOICE_PRIVATE_DIRECTORY, privateString))->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
	if (choice_ == CHOICE_PRIVATE_DIRECTORY) {
		AddExplanation(leftColumn, (MemStickScreen::Choice)choice_);
	}

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	std::string_view confirmButtonText = "";
	ImageID confirmButtonImage = ImageID::invalid();
	switch (choice_) {
	case CHOICE_BROWSE_FOLDER:
		confirmButtonText = di->T("OK");
		confirmButtonImage = ImageID("I_FOLDER_OPEN");
		break;
	case CHOICE_PRIVATE_DIRECTORY:
		if (initialSetup_) {
			confirmButtonText = di->T("Skip");
			confirmButtonImage = ImageID("I_WARNING");
		} else {
			confirmButtonText = di->T("OK");
		}
		break;
	case CHOICE_STORAGE_ROOT:
	case CHOICE_SET_MANUAL:
	default:
		confirmButtonText = di->T("OK");
		break;
	}

	rightColumnItems->Add(new UI::Choice(confirmButtonText, confirmButtonImage))->OnClick.Handle<MemStickScreen>(this, &MemStickScreen::OnConfirmClick);
	rightColumnItems->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	if (!initialSetup_) {
		rightColumnItems->Add(new UI::Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	}
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		rightColumnItems->Add(new UI::Choice(iz->T("WhatsThis", "What's this?")))->OnClick.Handle<MemStickScreen>(this, &MemStickScreen::OnHelp);
	}

	INFO_LOG(SYSTEM, "MemStickScreen: initialSetup=%d", (int)initialSetup_);
}

UI::EventReturn MemStickScreen::OnHelp(UI::EventParams &params) {
	// I'm letting the old redirect handle this one, as the target is within /docs on the website,
	// and that structure may change a bit.
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/guide_storage.html");

	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::OnChoiceClick(UI::EventParams &params) {
	// Change the confirm button to match the choice,
	// and change the text that we show.
	RecreateViews();
	return UI::EVENT_DONE;
}


UI::EventReturn MemStickScreen::OnConfirmClick(UI::EventParams &params) {
	switch (choice_) {
	case CHOICE_SET_MANUAL:
		return SetFolderManually(params);
	case CHOICE_STORAGE_ROOT:
		return UseStorageRoot(params);
	case CHOICE_PRIVATE_DIRECTORY:
		return UseInternalStorage(params);
	case CHOICE_BROWSE_FOLDER:
		return Browse(params);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::SetFolderManually(UI::EventParams &params) {
	// The old way, from before scoped storage.
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH)
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	System_InputBoxGetString(GetRequesterToken(), sy->T("Memory Stick Folder"), g_Config.memStickDirectory.ToString(), [&](const std::string &value, int) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto di = GetI18NCategory(I18NCat::DIALOG);

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
			System_Toast(sy->T("Path does not exist!"));
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
			System_Toast(sy->T("Path does not exist!"));
			return;
		}

		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::UseInternalStorage(UI::EventParams &params) {
#if PPSSPP_PLATFORM(UWP)
	Path pendingMemStickFolder = g_Config.internalDataDirectory;
#else
	Path pendingMemStickFolder = Path(g_extFilesDir);
#endif

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
			ERROR_LOG_REPORT(SYSTEM, "Could not switch memstick path in setup (internal)");
		}
		// Don't have a confirmation dialog that would otherwise do it for us, need to just switch directly to the main screen.
		screenManager()->switchScreen(new MainScreen());
	} else if (pendingMemStickFolder != g_Config.memStickDirectory) {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	} else {
		// User chose the same directory it's already in. Let's just bail.
		TriggerFinish(DialogResult::DR_OK);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MemStickScreen::UseStorageRoot(UI::EventParams &params) {
	Path pendingMemStickFolder = Path(g_externalDir);

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
			ERROR_LOG_REPORT(SYSTEM, "Could not switch memstick path in setup");
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

UI::EventReturn MemStickScreen::Browse(UI::EventParams &params) {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	System_BrowseForFolder(GetRequesterToken(), mm->T("Choose folder"), g_Config.memStickDirectory, [=](const std::string &value, int) {
		Path pendingMemStickFolder = Path(value);
		INFO_LOG(SYSTEM, "Got folder: '%s'", pendingMemStickFolder.c_str());
		// Browse finished. Let's pop up the confirmation dialog.
		if (!pendingMemStickFolder.empty() && pendingMemStickFolder == g_Config.memStickDirectory && File::IsDirectory(pendingMemStickFolder)) {
			auto iz = GetI18NCategory(I18NCat::MEMSTICK);
			// Not sure how this could happen, but let's go with it.
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, iz->T("Done!"));
			done_ = true;
			return;
		}
		errorNoticeView_->SetVisibility(UI::V_GONE);
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, initialSetup_));
	}, [=]() {
		errorNoticeView_->SetVisibility(UI::V_VISIBLE);
	});
	return UI::EVENT_DONE;
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
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto iz = GetI18NCategory(I18NCat::MEMSTICK);

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

	leftColumn->Add(new TextView(iz->T("Selected PSP Data Folder"), ALIGN_LEFT, false));
	if (!initialSetup_) {
		leftColumn->Add(new NoticeView(NoticeLevel::WARN, iz->T("PPSSPP will restart after the change"), ""));
	}
	leftColumn->Add(new TextView(newMemstickFolder_.ToVisualString(), ALIGN_LEFT, false));
	std::string newFreeSpaceText = std::string(iz->T("Free space")) + ": " + FormatSpaceString(freeSpaceNew);
	leftColumn->Add(new TextView(newFreeSpaceText, ALIGN_LEFT, false));
	if (existingFilesInNewFolder_) {
		leftColumn->Add(new NoticeView(NoticeLevel::SUCCESS, iz->T("Already contains PSP data"), ""));
		if (!moveData_) {
			leftColumn->Add(new NoticeView(NoticeLevel::INFO, iz->T("No data will be changed"), ""));
		}
	}
	if (!error_.empty()) {
		leftColumn->Add(new TextView(error_, ALIGN_LEFT, false));
	}

	if (!oldMemstickFolder.empty()) {
		std::string oldFreeSpaceText = std::string(iz->T("Free space")) + ": " + FormatSpaceString(freeSpaceOld);

		rightColumn->Add(new TextView(std::string(iz->T("Current")) + ":", ALIGN_LEFT, false));
		rightColumn->Add(new TextView(oldMemstickFolder.ToVisualString(), ALIGN_LEFT, false));
		rightColumn->Add(new TextView(oldFreeSpaceText, ALIGN_LEFT, false));
	}

	if (moveDataTask_) {
		progressView_ = leftColumn->Add(new TextView(progressReporter_.Format()));
	} else {
		progressView_ = nullptr;
	}

	if (!moveDataTask_) {
		if (!initialSetup_) {
			leftColumn->Add(new CheckBox(&moveData_, iz->T("Move Data")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnMoveDataClick);
		}

		auto di = GetI18NCategory(I18NCat::DIALOG);
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
	auto iz = GetI18NCategory(I18NCat::MEMSTICK);

	if (moveDataTask_) {
		if (progressView_) {
			progressView_->SetText(progressReporter_.Format());
		}

		MoveResult *result = moveDataTask_->Poll();

		if (result) {
			if (result->success) {
				progressReporter_.SetProgress(iz->T("Done!"));
				INFO_LOG(SYSTEM, "Move data task finished successfully!");
				// Succeeded!
				FinishFolderMove();
			} else {
				progressReporter_.SetProgress(iz->T("Failed to move some files!"));
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
	// Transfer all the files in /PSP from the original directory.
	// Should probably be done on a background thread so we can show some UI.
	// So we probably need another screen for this with a progress bar..
	// If the directory itself is called PSP, don't go below.

	if (moveData_) {
		progressReporter_.SetProgress(T(I18NCat::MEMSTICK, "Starting move..."));

		moveDataTask_ = Promise<MoveResult *>::Spawn(&g_threadManager, [&]() -> MoveResult * {
			Path moveSrc = g_Config.memStickDirectory;
			Path moveDest = newMemstickFolder_;
			return MoveDirectoryContentsSafe(moveSrc, moveDest, progressReporter_);
		}, TaskType::IO_BLOCKING, TaskPriority::HIGH);

		RecreateViews();
	} else {
		FinishFolderMove();
	}

	return UI::EVENT_DONE;
}

void ConfirmMemstickMoveScreen::FinishFolderMove() {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);

	Path oldMemstickFolder = g_Config.memStickDirectory;

	// Successful so far, switch the memstick folder.
	if (!SwitchMemstickFolderTo(newMemstickFolder_)) {
		// TODO: More precise errors.
		error_ = ms->T("That folder doesn't work as a memstick folder.");
		return;
	}

	// If the chosen folder already had a config, reload it!
	g_Config.Load();

	// If the current browser directory is the old memstick folder, drop it.
	if (g_Config.currentDirectory == oldMemstickFolder) {
		g_Config.currentDirectory = g_Config.defaultCurrentDirectory;
	}

	PostLoadConfig();

	if (!initialSetup_) {
		// We restart the app here, to get the new settings.
		System_RestartApp("");
	} else {
		// This is initial setup, we now switch to the main screen, if we were successful
		// (which we better have been...)
		if (g_Config.Save("MemstickPathChanged")) {
			// TriggerFinish(DialogResult::DR_OK);
			screenManager()->switchScreen(new MainScreen());
		} else {
			error_ = ms->T("Failed to save config");
			RecreateViews();
		}
	}
}
