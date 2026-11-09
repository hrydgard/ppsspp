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

#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Core/Util/StorageTest.h"
#include "UI/SimpleDialogScreen.h"

// Runs RunStorageTests() in a folder the user picks, which on Android is a content URI: the storage code
// the unit tests can't reach. Opened with --start-screen=storagetest (see Tools/android_storage_test.py).
// The folder is remembered, so later runs start by themselves.
class StorageTestScreen : public UITwoPaneBaseDialogScreen {
public:
	StorageTestScreen();
	~StorageTestScreen();

	const char *tag() const override { return "StorageTest"; }

protected:
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	void CreateContentViews(UI::ViewGroup *parent) override;
	std::string_view GetTitle() const override { return "Storage tests"; }
	void update() override;

private:
	void ChooseFolder();
	void StartTests();
	// The results are logged at INFO for Tools/android_storage_test.py, so they have to get through
	// whatever level the user has. Restored when the run ends, since Config::Save() stores the levels.
	void RaiseLogLevel();
	void RestoreLogLevel();

	Path folder_;
	bool autoStart_ = false;

	std::thread thread_;
	std::atomic<bool> running_{ false };
	std::mutex resultsLock_;
	std::vector<StorageTestResult> results_;
	size_t shownResults_ = 0;
	std::atomic<int> failures_{ -1 };  // -1 until a run has finished.

	bool logRaised_ = false;
	LogLevel savedLogLevel_ = LogLevel::LINFO;
	bool savedLogEnabled_ = true;
};
