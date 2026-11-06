// Copyright (c) 2012- PPSSPP Project.

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

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/UI/Notice.h"
#include "Common/UI/ViewGroup.h"
#include "Core/Config.h"
#include "UI/StorageTestScreen.h"

// A developer tool, so the strings aren't translated.

static Path RememberedFolderFile() {
	return g_Config.internalDataDirectory / "storage_test_dir.txt";
}

StorageTestScreen::StorageTestScreen()
	: UITwoPaneBaseDialogScreen(Path(), TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll) {
	std::string remembered;
	if (!g_Config.internalDataDirectory.empty() && File::ReadTextFileToString(RememberedFolderFile(), &remembered)) {
		folder_ = Path(StripSpaces(remembered));
		// Started from a script, most likely, so get going (in update(), once the screen is up).
		autoStart_ = File::IsDirectory(folder_);
		if (!autoStart_) {
			WARN_LOG(Log::System, "StorageTest: Remembered folder '%s' is gone, choose another", folder_.c_str());
		}
	}
}

StorageTestScreen::~StorageTestScreen() {
	// The tests have no way to stop halfway, but they take seconds.
	if (thread_.joinable()) {
		thread_.join();
	}
	RestoreLogLevel();
}

void StorageTestScreen::RaiseLogLevel() {
	if (logRaised_) {
		return;
	}
	LogChannel *channel = g_logManager.GetLogChannel(Log::System);
	savedLogLevel_ = channel->level;
	savedLogEnabled_ = channel->enabled;
	channel->enabled = true;
	if (channel->level < LogLevel::LINFO) {
		channel->level = LogLevel::LINFO;
	}
	logRaised_ = true;
}

void StorageTestScreen::RestoreLogLevel() {
	if (!logRaised_) {
		return;
	}
	LogChannel *channel = g_logManager.GetLogChannel(Log::System);
	channel->level = savedLogLevel_;
	channel->enabled = savedLogEnabled_;
	logRaised_ = false;
}

void StorageTestScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;
	if (running_) {
		parent->Add(new TextView("Running...", ALIGN_LEFT, false));
		return;
	}
	parent->Add(new Choice("Choose folder", ImageID("I_FOLDER_OPEN")))->OnClick.Add([this](EventParams &) {
		ChooseFolder();
	});
	if (!folder_.empty()) {
		parent->Add(new Choice("Run tests"))->OnClick.Add([this](EventParams &) {
			StartTests();
		});
	}
}

void StorageTestScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	LinearLayout *list = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	if (folder_.empty()) {
		list->Add(new TextView("Create a new, empty folder for the tests in the folder picker, and choose it. The tests work in a folder of their own inside it and delete it again, but a folder of its own keeps them away from your data. It's remembered for later runs.", ALIGN_LEFT | FLAG_WRAP_TEXT, false));
		return;
	}
	list->Add(new TextView("Folder: " + folder_.ToVisualString(), ALIGN_LEFT | FLAG_WRAP_TEXT, false));
	const int failures = failures_;
	if (failures >= 0) {
		if (failures == 0) {
			list->Add(new NoticeView(NoticeLevel::SUCCESS, "All passed", ""));
		} else {
			list->Add(new NoticeView(NoticeLevel::ERROR, StringFromFormat("%d failed", failures), ""));
		}
	}

	std::lock_guard<std::mutex> guard(resultsLock_);
	for (const StorageTestResult &result : results_) {
		list->Add(new NoticeView(result.passed ? NoticeLevel::SUCCESS : NoticeLevel::ERROR, result.name, result.detail));
	}
	shownResults_ = results_.size();
}

void StorageTestScreen::update() {
	UITwoPaneBaseDialogScreen::update();
	if (autoStart_) {
		autoStart_ = false;
		StartTests();
	}

	bool changed;
	{
		std::lock_guard<std::mutex> guard(resultsLock_);
		changed = results_.size() != shownResults_;
	}
	if (!running_ && thread_.joinable()) {
		thread_.join();
		RestoreLogLevel();
		changed = true;
	}
	if (changed) {
		RecreateViews();
	}
}

void StorageTestScreen::ChooseFolder() {
	System_BrowseForFolder(GetRequesterToken(), "Choose a folder to test in", folder_, [this](std::string_view value, int) {
		const Path folder(value);
		if (folder.empty()) {
			return;
		}
		folder_ = folder;
		if (!g_Config.internalDataDirectory.empty()) {
			File::WriteStringToFile(true, folder_.ToString(), RememberedFolderFile());
		}
		StartTests();
	});
}

void StorageTestScreen::StartTests() {
	if (running_ || folder_.empty()) {
		return;
	}
	if (thread_.joinable()) {
		thread_.join();
	}
	{
		std::lock_guard<std::mutex> guard(resultsLock_);
		results_.clear();
	}
	shownResults_ = 0;
	failures_ = -1;
	running_ = true;
	RaiseLogLevel();
	RecreateViews();
	thread_ = std::thread([this]() {
		SetCurrentThreadName("StorageTest");
		// Content URIs go through Java.
		AndroidJNIThreadContext jniContext;
		const int failures = RunStorageTests(folder_, [this](const StorageTestResult &result) {
			std::lock_guard<std::mutex> guard(resultsLock_);
			results_.push_back(result);
		});
		failures_ = failures;
		running_ = false;
	});
}
