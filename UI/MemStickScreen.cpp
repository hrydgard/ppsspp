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
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Display.h"

#include "Core/Util/GameManager.h"
#include "Core/System.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"

#include "Core/Config.h"

MemStickScreen::~MemStickScreen() { }

void MemStickScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto iz = GetI18NCategory("MemStick");

	Margins actionMenuMargins(15, 15, 15, 0);

	root_ = new AnchorLayout();

	ViewGroup *columns = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(columns);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0));
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	columns->Add(leftColumn);
	columns->Add(rightColumnItems);

	Path path = g_Config.memStickDirectory;
	int64_t freeSpaceAtMemStick = -1;
#if PPSSPP_PLATFORM(ANDROID)
	if (Android_IsContentUri(path.ToString())) {
		freeSpaceAtMemStick = Android_GetFreeSpaceByContentUri(path.ToString());
	} else {
		freeSpaceAtMemStick = Android_GetFreeSpaceByFilePath(path.ToString());
	}
#endif

	int leftSide = 100;
	settingInfo_ = new SettingInfoMessage(ALIGN_CENTER | FLAG_WRAP_TEXT, new AnchorLayoutParams(dp_xres - leftSide - 40.0f, WRAP_CONTENT, leftSide, dp_yres - 80.0f - 40.0f, NONE, NONE));
	settingInfo_->SetBottomCutoff(dp_yres - 200.0f);

	root_->Add(settingInfo_);

	leftColumn->Add(new TextView(iz->T("Memory Stick Storage"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
	leftColumn->Add(new TextView(iz->T("MemoryStickDescription", "Choose where your PSP memory stick data (savegames, etc) is stored"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 50, NONE, NONE)));
	leftColumn->Add(new TextView(g_Config.memStickDirectory.ToVisualString(), ALIGN_LEFT, false, new AnchorLayoutParams(10, 140, NONE, NONE)));

	std::string freeSpaceText = "Free space: N/A";
	if (freeSpaceAtMemStick >= 0) {
		freeSpaceText = StringFromFormat("free space: %lld MB", freeSpaceAtMemStick / (1024 * 1024));
	}

	leftColumn->Add(new TextView(freeSpaceText, ALIGN_LEFT, false, new AnchorLayoutParams(10, 240, NONE, NONE)));

	rightColumnItems->Add(new Choice(iz->T("Browse")))->OnClick.Handle(this, &MemStickScreen::OnBrowse);
	rightColumnItems->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
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
			CallbackMemStickFolder(true);
		}
	}
}

void MemStickScreen::CallbackMemStickFolder(bool yes) {
	auto sy = GetI18NCategory("System");

	if (yes) {
		Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
		Path testWriteFile = pendingMemStickFolder_ / ".write_verify_file";

		// Doesn't already exist, create.
		// Should this ever happen?
		if (pendingMemStickFolder_.Type() == PathType::NATIVE) {
			if (!File::Exists(pendingMemStickFolder_)) {
				File::CreateFullPath(pendingMemStickFolder_);
			}
			if (!File::WriteDataToFile(true, "1", 1, testWriteFile)) {
				settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
				return;
			}
			File::Delete(testWriteFile);
		} else {
			// TODO: Do the same but with scoped storage? Not really necessary, right? If it came from a browse
			// for folder, we can assume it exists, barring wacky race conditions like the user being connected
			// by USB and deleting it.
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

		g_Config.Save("MemstickPathChanged");
		screenManager()->RecreateAllViews();
	}
}
