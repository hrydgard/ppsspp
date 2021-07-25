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

#include "Common/File/AndroidStorage.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

#include "Core/Util/GameManager.h"
#include "Core/System.h"
#include "Core/Config.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"

MemStickScreen::~MemStickScreen() {
	pendingMemStickFolder_ = g_Config.memStickDirectory;
}

void MemStickScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto iz = GetI18NCategory("MemStick");

	Margins actionMenuMargins(15, 15, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));

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

#if !PPSSPP_PLATFORM(WINDOWS)
	if (!pendingMemStickFolder_.empty()) {
		int64_t freeSpaceAtMemStick = -1;
		if (Android_IsContentUri(pendingMemStickFolder_.ToString())) {
			freeSpaceAtMemStick = Android_GetFreeSpaceByContentUri(pendingMemStickFolder_.ToString());
		} else {
			freeSpaceAtMemStick = Android_GetFreeSpaceByFilePath(pendingMemStickFolder_.ToString());
		}

		leftColumn->Add(new TextView(pendingMemStickFolder_.ToVisualString(), ALIGN_LEFT, false));
		std::string freeSpaceText = "Free space: N/A";
		if (freeSpaceAtMemStick >= 0) {
			freeSpaceText = StringFromFormat("free space: %lld MB", freeSpaceAtMemStick / (1024 * 1024));
			leftColumn->Add(new TextView(freeSpaceText, ALIGN_LEFT, false));
		}
	}

	if (!g_Config.memStickDirectory.empty()) {
		TextView *view = leftColumn->Add(new TextView(g_Config.memStickDirectory.ToVisualString(), ALIGN_LEFT, false));
		view->SetShadow(true);
	}
#endif

	leftColumn->Add(new Choice(iz->T("Create or Choose a PSP folder")))->OnClick.Handle(this, &MemStickScreen::OnBrowse);
	leftColumn->Add(new TextView(iz->T("ChooseFolderDesc", "* Data will stay even if you uninstall PPSSPP.\n* Data can be shared with PPSSPP Gold\n* Easy USB access"), ALIGN_LEFT, false));

	leftColumn->Add(new Choice(iz->T("Use App Private Directory")))->OnClick.Handle(this, &MemStickScreen::OnUseInternalStorage);
	leftColumn->Add(new TextView(iz->T("InternalStorageDesc", "* Warning! Data will be deleted if you uninstall PPSSPP!\n* Data cannot be shared with PPSSPP Gold\n* USB access through Android/data/org.ppsspp.ppsspp/files"), ALIGN_LEFT, false));

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	Choice *confirmButton = rightColumnItems->Add(new Choice(iz->T("Confirm")));
	confirmButton->OnClick.Handle(this, &MemStickScreen::OnConfirm);
	confirmButton->SetEnabled(!pendingMemStickFolder_.empty());

	if (!initialSetup_) {
		rightColumnItems->Add(new CheckBox(&moveData_, iz->T("Move Data")));
		rightColumnItems->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
	}

	INFO_LOG(SYSTEM, "MemStickScreen: initialSetup=%d", (int)initialSetup_);
}

UI::EventReturn MemStickScreen::OnUseInternalStorage(UI::EventParams &params) {
	pendingMemStickFolder_ = Path(g_extFilesDir);
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
			pendingMemStickFolder_ = Path(filename);
			RecreateViews();
		}
	}
}

UI::EventReturn MemStickScreen::OnConfirm(UI::EventParams &params) {
	auto sy = GetI18NCategory("System");

	Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
	Path testWriteFile = pendingMemStickFolder_ / ".write_verify_file";

	// Doesn't already exist, create.
	// Should this ever happen?
	if (pendingMemStickFolder_.Type() == PathType::NATIVE) {
		if (!File::Exists(pendingMemStickFolder_)) {
			File::CreateFullPath(pendingMemStickFolder_);
		}
		if (!File::WriteDataToFile(true, "1", 1, testWriteFile)) {
			// settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
			return UI::EVENT_DONE;
		}
		File::Delete(testWriteFile);
	} else {
		// TODO: Do the same but with scoped storage? Not really necessary, right? If it came from a browse
		// for folder, we can assume it exists and is writable, barring wacky race conditions like the user
		// being connected by USB and deleting it.
	}

	// This doesn't need the storage API - this path is accessible the normal way.
	std::string str = pendingMemStickFolder_.ToString();
	if (!File::WriteDataToFile(true, str.c_str(), (unsigned int)str.size(), memStickDirFile)) {
		ERROR_LOG(SYSTEM, "Failed to write memstick path '%s' to '%s'", pendingMemStickFolder_.c_str(), memStickDirFile.c_str());
		// Not sure what to do if this file.
	}

	// Save so the settings, at least, are transferred.
	g_Config.memStickDirectory = pendingMemStickFolder_;
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.UpdateIniLocation();

	if (g_Config.Save("MemstickPathChanged")) {
		TriggerFinish(DialogResult::DR_OK);
	} else {
		error_ = sy->T("Failed to save config");
		RecreateViews();
	}

	return UI::EVENT_DONE;
}
